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

template <> struct fmt::formatter<SEContext> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  auto format(const SEContext &context,
              format_context &ctx) const -> format_context::iterator {
    return formatter<string_view>::format(static_cast<std::string>(context), ctx);
  }
};

AvcContext::AvcContext(const std::string_view string) : stale(true) {
  std::string line;
  std::vector<std::string> lines;
  bool ret = true;

  const std::string sub_str = std::string(string.substr(string.find("avc:")));
  std::istringstream iss(sub_str);
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
    fmt::print("Unknown value for ACL status: '{}'\n", *it);
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
    fmt::print("Invalid input: '{}'\n", string);
    return;
  }
  do {
    auto idx = it->find('=');
    if (idx == std::string::npos) {
      fmt::print("Unparsable attribute: '{}'\n", *it);
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
        permissive = x;
        misc_attributes.erase(pit);
        found = true;
      }
    }
    if (!found) {
      fmt::print("Invalid permissive status: '{}'\n", pit->second);
      ret = false;
    }
  }
  if (ret) {
    stale = false;
  } else {
    fmt::print("Failed to parse '{}'\n", sub_str.c_str());
  }
}

bool AvcContext::findOrDie(std::string &dest, const std::string &key) {
  auto it = misc_attributes.find(key);
  bool ret = it != misc_attributes.end();

  if (ret) {
    dest = it->second;
    misc_attributes.erase(it);
  } else {
    fmt::print("Empty value for key: '{}'\n", key);
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

std::ostream &operator<<(std::ostream &self, const AvcContext &context) {
  if (context.stale || context.operation.size() == 0) {
    return self;
  }
  // BEGIN HOOKS (Some hardcoded blockings), TODO: Init script?
  if (context.operation.find("sys_admin") != context.operation.end()) {
    return self;
  }
  // END HOOKS
  self << fmt::format("allow {} {}:{} ", context.scontext, context.tcontext,
                      context.tclass);
  switch (context.operation.size()) {
  case 1: {
    self << *context.operation.begin();
  } break;
  default: {
    self << fmt::format("{{ {} }}", fmt::join(context.operation, " "));
  } break;
  };
  self << ';';
  return self;
}

std::ostream &operator<<(std::ostream &self, const AvcContexts &context) {
  std::stringstream ss;
  std::set<std::string> rules;
  for (const auto &entry : context) {
    ss << entry << std::endl;
    rules.insert(ss.str());
    std::stringstream ss2;
    ss.swap(ss2);
  }
  for (const auto &entry : rules) {
    self << entry;
  }
  return self;
}
