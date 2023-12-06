#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#define LOG_TAG "bootlogger"

#include <log/log.h>

// Similar to perror(3)
#define PLOGE(fmt, ...) \
  ALOGE("%s: " fmt ": %s", __func__, ##__VA_ARGS__, strerror(errno))

// Caches sysconf(_SC_PAGESIZE) and returns it
std::size_t getPageSize();
// Alias
#define BUF_SIZE getPageSize()

// KernelConfig.cpp
enum ConfigValue {
  UNKNOWN,   // Should be first for default-initialization
  BUILT_IN,  // =y
  STRING,    // =""
  INT,       // =1
  MODULE,    // =m
  UNSET,     // =n
};

using KernelConfig_t = std::unordered_map<std::string, ConfigValue>;

/**
 * Read KernelConfig (/proc/config.gz)
 * And serializes it to KernelConfig_t object
 *
 * @param out buffer to store
 * @return 0 on success, else non-zero value
 */
int ReadKernelConfig(KernelConfig_t& out);

// AuditToAllow.cpp
#include <algorithm>
#include <map>
#include <vector>

struct AvcContext;

using AttributeMap = std::map<std::string, std::string>;
using OperationVec = std::vector<std::string>;
using AvcContexts = std::vector<AvcContext>;

struct AvcContext {
  bool granted;                       // granted or denied?
  std::vector<std::string> operation; // find, ioctl, open...
  std::string scontext, tcontext;     // untrusted_app, init... Always enclosed with u:object_r: and :s0
  std::string tclass;                 // file, lnk_file, sock_file...
  AttributeMap misc_attributes;       // ino, dev, name, app...
  bool permissive;                    // enforced or not
  bool stale = false;                 // Whether this is used, used for merging contexts
  AvcContext &operator+=(AvcContext &other) {
    if (!stale && !other.stale) {
      bool mergable = true;
      mergable &= granted == other.granted;
      mergable &= scontext == other.scontext;
      mergable &= tcontext == other.tcontext;
      mergable &= tclass == other.tclass;
      // TODO: Check for misc_attributes?
      if (mergable) {
        other.stale = true;
        operation.insert(operation.end(), other.operation.begin(),
                         other.operation.end());
        std::sort(operation.begin(), operation.end());
        operation.erase(std::unique(operation.begin(), operation.end()),
                        operation.end());
      }
    }
    return *this;
  }
};

/**
 * parseOneAvcContext - parse a std::string to AvcContext object
 *
 * @param str input string, in the format avc: denied { ... } for ...
 * @param outvec out buffer of AvcContext
 * @return true on success, else false, and outvec is not modified.
 */
bool parseOneAvcContext(const std::string &str, AvcContexts &outvec);

/**
 * writeAllowRules - generate a selinux allowlist from AvcContext
 * Note - new line terminated
 *
 * @param ctx context to generate rules from
 * @param out std::string buffer containing the rules
 * @return true on success
 */
bool writeAllowRules(const AvcContext &ctx, std::string &out);
