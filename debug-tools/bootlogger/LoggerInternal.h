#pragma once

#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

constexpr int BUF_SIZE = 4096;

// KernelConfig.cpp
enum ConfigValue {
  UNKNOWN,  // Should be first for default-initialization
  BUILT_IN, // =y
  STRING,   // =""
  INT,      // =1
  MODULE,   // =m
  UNSET,    // =n
};

using KernelConfigType = std::unordered_map<std::string, ConfigValue>;

/**
 * Read KernelConfig (/proc/config.gz)
 * And serializes it to KernelConfig_t object
 *
 * @param out buffer to store
 * @return 0 on success, else non-zero value
 */
int ReadKernelConfig(KernelConfigType &out);

// AuditToAllow.cpp
#include <map>
#include <vector>

struct AvcContext;

using AttributeMap = std::map<std::string, std::string>;
using OperationVec = std::vector<std::string>;
using AvcContexts = std::vector<AvcContext>;

struct SEContext {
  explicit SEContext(std::string context);
  SEContext() = default;

  explicit operator std::string() const { return m_context; }
  bool operator==(const SEContext &other) const {
    return m_context == other.m_context;
  }

private:
  std::string m_context;
};

struct AvcContext {
  bool granted;                    // granted or denied?
  std::set<std::string> operation; // find, ioctl, open...
  SEContext scontext, tcontext; // untrusted_app, init... Always enclosed with
                                // u:object_r: and :s0
  std::string tclass;           // file, lnk_file, sock_file...
  AttributeMap misc_attributes; // ino, dev, name, app...
  bool permissive;              // enforced or not
  bool stale = false; // Whether this is used, used for merging contexts

  explicit AvcContext(const std::string_view string);
  AvcContext &operator+=(AvcContext &other);

private:
  bool findOrDie(std::string &dest, const std::string &key);
  bool findOrDie(SEContext &dest, const std::string &key);
};

extern std::ostream &operator<<(std::ostream &self, const AvcContext &context);
extern std::ostream &operator<<(std::ostream &self, const AvcContexts &context);