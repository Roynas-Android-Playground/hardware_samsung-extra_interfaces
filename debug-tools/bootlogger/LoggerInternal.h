#pragma once

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#define LOG_TAG "bootlogger"

#include <android-base/logging.h>

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
#include <utility>
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
  bool stale = true; // Whether this is used, used for merging contexts

  explicit AvcContext(const std::string_view string);
  AvcContext() = default;
  AvcContext &operator+=(AvcContext &other);

private:
  bool findOrDie(std::string &dest, const std::string &key);
  bool findOrDie(SEContext &dest, const std::string &key);
};

template <> struct fmt::formatter<SEContext> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  static auto format(const SEContext &context,
              format_context &ctx)  -> format_context::iterator {
    return fmt::format_to(ctx.out(), "{}", static_cast<std::string>(context));
  }
};

template <> struct fmt::formatter<AvcContext> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  static auto format(const AvcContext &context,
                     format_context &ctx) -> format_context::iterator {

    auto prefix = fmt::format("allow {} {}:{}", context.scontext,
                              context.tcontext, context.tclass);
    switch (context.operation.size()) {
    case 1: {
      return fmt::format_to(ctx.out(), "{} {};", prefix,
                            *context.operation.begin());
    }
    default: {
      return fmt::format_to(ctx.out(), "{} {{ {} }};", prefix,
                            fmt::join(context.operation, " "));
    }
    };
  }
};

template <> struct fmt::formatter<AvcContexts> : formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  static auto format(AvcContexts context,
                     format_context &ctx) -> format_context::iterator {
    AvcContexts filtered_contexts = std::move(context);
    auto en =
        std::remove_if(filtered_contexts.begin(), filtered_contexts.end(),
                       [](const auto &context) {
                         return context.stale || context.operation.size() == 0;
                       });
    filtered_contexts.resize(std::distance(filtered_contexts.begin(), en));
    return fmt::format_to(ctx.out(), "{}", fmt::join(filtered_contexts, "\n"));
  }
};
