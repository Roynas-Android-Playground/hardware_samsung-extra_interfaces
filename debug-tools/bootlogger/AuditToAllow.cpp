#include "LoggerInternal.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <tuple>

namespace {

std::string Unquote(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool ParseBoolean(const std::string &value, bool *out) {
  if (value == "0") {
    *out = false;
    return true;
  }
  if (value == "1") {
    *out = true;
    return true;
  }
  return false;
}

bool IsPolicyIdentifier(const std::string &value) {
  static const std::regex kIdentifierRegex(R"(^[A-Za-z_][A-Za-z0-9_]*$)", std::regex::ECMAScript);
  return std::regex_match(value, kIdentifierRegex);
}

}  // namespace

SEContext::SEContext(std::string context) : m_context(std::move(context)) {
  static const std::regex kSEContextRegex(
      R"(^u:(?:object_)?r:([A-Za-z_][A-Za-z0-9_]*):s[0-9]+(?::c[0-9]+(?:[,.]c[0-9]+)*)?(?:-s[0-9]+(?::c[0-9]+(?:[,.]c[0-9]+)*)?)?$)",
      std::regex::ECMAScript);
  std::smatch match;
  if (std::regex_match(m_context, match, kSEContextRegex)) {
    m_context = match.str(1);
  } else {
    m_context.clear();
  }
}

AvcContext::AvcContext(std::string_view input) {
  static const std::regex kAvcRegex(R"(avc:\s+(granted|denied)\s+\{\s*([^}]*)\}\s+for\s+(.*)$)",
                                    std::regex::ECMAScript);
  static const std::regex kAttributeRegex(R"(([A-Za-z_][A-Za-z0-9_]*)=("[^"]*"|\S+))",
                                          std::regex::ECMAScript);

  const std::string line(input);
  std::smatch match;
  if (!std::regex_search(line, match, kAvcRegex)) {
    return;
  }

  granted = match.str(1) == "granted";

  std::istringstream operationStream(match.str(2));
  for (std::string operation; operationStream >> operation;) {
    if (!IsPolicyIdentifier(operation)) {
      operations.clear();
      return;
    }
    operations.insert(std::move(operation));
  }
  if (operations.empty()) {
    return;
  }

  const std::string attributes = match.str(3);
  for (std::sregex_iterator it(attributes.begin(), attributes.end(), kAttributeRegex), end;
       it != end; ++it) {
    misc_attributes.emplace((*it).str(1), Unquote((*it).str(2)));
  }

  const auto source = misc_attributes.find("scontext");
  const auto target = misc_attributes.find("tcontext");
  const auto klass = misc_attributes.find("tclass");
  if (source == misc_attributes.end() || target == misc_attributes.end() ||
      klass == misc_attributes.end()) {
    return;
  }

  scontext = SEContext(source->second);
  tcontext = SEContext(target->second);
  tclass = klass->second;
  misc_attributes.erase(source);
  misc_attributes.erase(target);
  misc_attributes.erase(klass);

  if (const auto it = misc_attributes.find("permissive"); it != misc_attributes.end()) {
    if (!ParseBoolean(it->second, &permissive)) {
      return;
    }
    misc_attributes.erase(it);
  }

  valid = !scontext.name().empty() && !tcontext.name().empty() && IsPolicyIdentifier(tclass);
}

bool AvcContext::isUntrustedApp() const {
  return scontext.name().find("untrusted_app") != std::string::npos;
}

bool AvcContext::mergeFrom(AvcContext &other) {
  if (!valid || !other.valid || consumed || other.consumed || granted != other.granted ||
      !(scontext == other.scontext) || !(tcontext == other.tcontext) || tclass != other.tclass) {
    return false;
  }

  operations.insert(other.operations.begin(), other.operations.end());
  other.consumed = true;
  return true;
}

std::string AvcContext::toAllowRule() const {
  if (!isDenied() || consumed || operations.empty()) {
    return {};
  }

  std::ostringstream out;
  out << "allow " << scontext.name() << ' ' << tcontext.name() << ':' << tclass << ' ';
  if (operations.size() == 1) {
    out << *operations.begin();
  } else {
    out << "{ ";
    for (const auto &operation : operations) {
      out << operation << ' ';
    }
    out << '}';
  }
  out << ';';
  return out.str();
}

bool ShouldCollectAvcFromSource(std::string_view sourceName, bool requested) {
  return requested && sourceName == "dmesg";
}

std::string FormatAllowSuggestions(AvcContexts contexts) {
  for (std::size_t i = 0; i < contexts.size(); ++i) {
    if (!contexts[i].isDenied() || contexts[i].isUntrustedApp()) {
      contexts[i].consumed = true;
      continue;
    }
    for (std::size_t j = i + 1; j < contexts.size(); ++j) {
      (void)contexts[i].mergeFrom(contexts[j]);
    }
  }

  std::sort(contexts.begin(), contexts.end(), [](const auto &left, const auto &right) {
    return std::tie(left.scontext.name(), left.tcontext.name(), left.tclass) <
           std::tie(right.scontext.name(), right.tcontext.name(), right.tclass);
  });

  std::ostringstream out;
  out << "# Diagnostic suggestions generated from observed SELinux denials.\n"
      << "# Review labels, macros, neverallow rules, and access intent before "
         "using any rule.\n"
      << "# Do not copy these rules blindly into production policy.\n\n";

  for (const auto &context : contexts) {
    const auto rule = context.toAllowRule();
    if (!rule.empty()) {
      out << rule << '\n';
    }
  }
  return out.str();
}
