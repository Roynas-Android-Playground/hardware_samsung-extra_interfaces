#include <array>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "LoggerInternal.h"

// Import class-permissions mapping from linux kernel header
struct security_class_mapping {
  std::string name;
  std::array<std::string, sizeof(unsigned) * 8 + 1> perms;
};

#pragma push_macro("NULL")
#define NULL "__unused__" // Kill null terminator
// Linux 6.7-rc4
#include "linux/classmap.h"
#pragma pop_macro("NULL")

#define COMMON_ANDROID_IPC_PERMS  "add", "find", "list"

struct security_class_mapping secclass_map_ext[] {
    { "service_manager",
	  { COMMON_ANDROID_IPC_PERMS }
    },
    { "hwservice_manager",
	  { COMMON_ANDROID_IPC_PERMS }
    },
    { "property_service",
	  { "set" }
    },
};

enum class MapVerifyResult {
    OK = 0,
    INAPPROPRIATE_PERMISSION,
    TCLASS_NOT_FOUND,
};

static MapVerifyResult verifyOneMappingTable(const security_class_mapping map[], const size_t size,
                                  const std::string& tclass, const OperationVec& perms) {
    auto ret = MapVerifyResult::TCLASS_NOT_FOUND;
    for (int i = 0; i < size; ++i) {
        auto it = map[i];
        if (it.name == tclass) {
            for (const auto& perm : perms) {
                if (std::find(it.perms.begin(), it.perms.end(), perm) == it.perms.end()) {
                    ALOGE("Invaild permission '%s' for tclass '%s'", perm.c_str(), tclass.c_str());
                    ret = MapVerifyResult::INAPPROPRIATE_PERMISSION;
                    return ret;
                }
            }
            ret = MapVerifyResult::OK;
            break;
        }
    }
    return ret;
}
static bool isVaildPermission(const std::string& tclass, const std::vector<std::string>& perms) {
    bool ret = false;
    // If ret is smaller than INAPPROPRIATE_PERMISSION, means tclass existed on the map
    // One of two maps should return true to pass the check.
    ret |= verifyOneMappingTable(secclass_map, sizeof(secclass_map) / sizeof(security_class_mapping),
                                 tclass, perms) <= MapVerifyResult::INAPPROPRIATE_PERMISSION;
    ret |= verifyOneMappingTable(secclass_map_ext, sizeof(secclass_map_ext) / sizeof(security_class_mapping),
                                 tclass, perms) <= MapVerifyResult::INAPPROPRIATE_PERMISSION;
    if (!ret) {
        ALOGE("Invaild tclass: '%s'", tclass.c_str());
    }
    return ret;
}

static inline std::string TrimDoubleQuote(const std::string &str) {
  if (str.size() > 2) { // At least one character inside quotes
    if (str.front() == '"' && str.back() == '"') {
      return str.substr(1, str.size() - 2);
    }
  }
  return str;
}

// Trim u:object_r, :s0...
static inline std::string TrimSEContext(const std::string &str) {
  const static std::regex kSEContextRegex(R"(^u:(object_)?r:\w+:s0(.+)?$)");
  if (std::regex_match(str, kSEContextRegex,
                       std::regex_constants::format_sed)) {
    auto begin_pos = str.find_first_of('r') + 2; // Skip 'r:' so add 2
    auto end_pos = str.find_first_of(':', begin_pos + 1);
    return str.substr(begin_pos, end_pos - begin_pos);
  }
  return str;
}

static bool findOrDie(std::string &dest, AttributeMap &map,
                      const std::string &key) {
  auto it = map.find(key);
  bool ret = it != map.end();

  if (ret) {
    dest = it->second;
    map.erase(it);
  } else {
    ALOGE("Empty value for key: '%s'", key.c_str());
  }
  return ret;
}

bool parseOneAvcContext(const std::string &str, AvcContexts &outvec) {
  std::string line, sub_str = str.substr(str.find("avc:"));
  std::istringstream iss(sub_str);
  std::vector<std::string> lines;
  decltype(lines)::iterator it;
  AttributeMap attributes;
  AvcContext ctx;
  bool ret = true;

  while ((iss >> line)) {
    lines.emplace_back(line);
  }
  it = lines.begin();
  ++it; // Skip avc:
  if (*it == "granted") {
    ctx.granted = true;
  } else if (*it == "denied") {
    ctx.granted = false;
  } else {
    ALOGW("Unknown value for ACL status: '%s'", it->c_str());
    return false;
  }
  ++it; // Now move onto next
  ++it; // Skip opening bracelet
  do {
    ctx.operation.emplace_back(*it);
  } while (*(++it) != "}");
  ++it; // Skip ending bracelet
  ++it; // Skip 'for'
  if (it == lines.end()) {
    ALOGE("Invaild input: '%s'", str.c_str());
    return false;
  }
  do {
    auto idx = it->find('=');
    if (idx == std::string::npos) {
      ALOGW("Unparsable attribute: '%s'", it->c_str());
      continue;
    }
    attributes.emplace(it->substr(0, idx),
                       TrimDoubleQuote(it->substr(idx + 1)));
  } while (++it != lines.end());

  // Bitwise AND, ret will be set to 0 if any of the calls return false(0)
  decltype(attributes)::iterator pit = attributes.find("permissive");
  ret &= findOrDie(ctx.scontext, attributes, "scontext");
  ret &= findOrDie(ctx.tcontext, attributes, "tcontext");
  ret &= findOrDie(ctx.tclass, attributes, "tclass");
  ret &= pit != attributes.end();
  // If still vaild
  if (ret) {
    auto permissive = pit->second.empty() ? '\0' : pit->second[0];
    ret &= (permissive == '0' || permissive == '1');
    if (ret) {
      ctx.permissive = permissive - '0';
      attributes.erase(pit);
    } else {
      ALOGE("Invaild permissive status: '%c'", permissive);
    }
  }
  if (!ctx.tclass.empty())
      ret &= isVaildPermission(ctx.tclass, ctx.operation);
  if (!ret) {
    ALOGE("Failed to parse '%s'", sub_str.c_str());
    return false;
  }
  ctx.misc_attributes = attributes;
  outvec.emplace_back(ctx);
  return true;
}

bool writeAllowRules(const AvcContext &ctx, std::string &out) {
  std::stringstream ss;

  if (!ctx.stale) {
    ss << "allow " << TrimSEContext(ctx.scontext) << ' '
       << TrimSEContext(ctx.tcontext) << ':' << ctx.tclass << ' ';
    switch (ctx.operation.size()) {
      case 0: {
        return false;
      }
      case 1: {
        ss << ctx.operation.front();
      } break;
      default: {
        ss << '{' << ' ';
        for (const auto &op : ctx.operation)
          ss << op << ' ';
        ss << '}';
      } break;
    };
    ss << ';' << std::endl;
    out.append(ss.str());
    return true;
  }
  return false;
}
