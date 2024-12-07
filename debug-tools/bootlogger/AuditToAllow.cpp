#include <fmt/core.h>
#include <fmt/format.h>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "LoggerInternal.h"

namespace {

inline std::string TrimDoubleQuote(const std::string &str) {
  if (str.size() > 2) { // At least one character inside quotes
    if (str.front() == '"' && str.back() == '"') {
      return str.substr(1, str.size() - 2);
    }
  }
  return str;
}

} // namespace

SEContext::SEContext(std::string context) : m_context(std::move(context)) {
  const static std::regex kSEContextRegex(
      R"(^u:(object_)?r:([\w-]+):s0(.+)?$)");

  std::smatch match;
  if (std::regex_match(m_context, match, kSEContextRegex,
                       std::regex_constants::format_sed)) {
    m_context = match.str(2);
  }
}

AvcContext::AvcContext(const std::string_view string) {
  std::string line;
  std::vector<std::string> lines;
  bool ret = true;

  auto pos = string.find("avc:");
  if (pos == std::string::npos) {
    return;
  }

  std::istringstream iss(std::string(string.substr(pos)));
  while ((iss >> line)) {
    lines.emplace_back(line);
  }
  auto it = lines.begin();
  ++it; // Skip avc:
  if (*it == "granted") {
    granted = true;
  } else if (*it == "denied") {
    granted = false;
  } else {
    LOG(WARNING) << "Unknown value for ACL status: " << *it;
    return;
  }
  ++it; // Now move onto next
  ++it; // Skip opening bracelet
  do {
    operation.insert(*it);
  } while (*(++it) != "}");
  ++it; // Skip ending bracelet
  ++it; // Skip 'for'
  if (it == lines.end()) {
    LOG(WARNING) << "Invalid input: " << string;
    return;
  }
  do {
    auto idx = it->find('=');
    if (idx == std::string::npos) {
      LOG(WARNING) << "Unparsable attribute: " << *it;
      continue;
    }
    misc_attributes.emplace(it->substr(0, idx),
                            TrimDoubleQuote(it->substr(idx + 1)));
  } while (++it != lines.end());

  // Bitwise AND, ret will be set to 0 if any of the calls return false(0)
  auto pit = misc_attributes.find("permissive");
  ret &= findOrDie(scontext, "scontext");
  ret &= findOrDie(tcontext, "tcontext");
  ret &= findOrDie(tclass, "tclass");
  ret &= pit != misc_attributes.end();
  // If still vaild
  if (ret) {
    bool found = false;
    int x = 0;
    if (std::stringstream(pit->second) >> x) {
      if (x == 0 || x == 1) {
        permissive = (x != 0);
        misc_attributes.erase(pit);
        found = true;
      }
    }
    if (!found) {
      LOG(WARNING) << "Invalid permissive status: " << pit->second;
      ret = false;
    }
  }
  if (ret) {
    stale = false;
  } else {
    LOG(ERROR) << "Failed to parse: " << string;
  }
}

bool AvcContext::findOrDie(std::string &dest, const std::string &key) {
  auto it = misc_attributes.find(key);
  bool ret = it != misc_attributes.end();

  if (ret) {
    dest = it->second;
    misc_attributes.erase(it);
  } else {
    LOG(WARNING) << "Empty value for key: " << key;
  }
  return ret;
}

bool AvcContext::findOrDie(SEContext &dest, const std::string &key) {
  std::string value;
  if (findOrDie(value, key)) {
    dest = SEContext(value);
    return true;
  }
  return false;
}

AvcContext &AvcContext::operator+=(AvcContext &other) {
  if (!stale && !other.stale) {
    bool mergable = true;
    mergable &= granted == other.granted;
    mergable &= scontext == other.scontext;
    mergable &= tcontext == other.tcontext;
    mergable &= tclass == other.tclass;
    // TODO: Check for misc_attributes?
    if (mergable) {
      other.stale = true;
      operation.insert(other.operation.begin(), other.operation.end());
    }
  }
  return *this;
}