#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

constexpr std::size_t BUF_SIZE = 4096;

enum class ConfigValue {
  UNKNOWN,
  BUILT_IN,
  STRING,
  INT,
  MODULE,
  UNSET,
};

using KernelConfigType = std::unordered_map<std::string, ConfigValue>;

bool ParseKernelConfigLine(std::string_view line, std::string *name, ConfigValue *value);
int ReadKernelConfig(KernelConfigType &out);

using AttributeMap = std::map<std::string, std::string>;

struct SEContext {
  explicit SEContext(std::string context);
  SEContext() = default;

  [[nodiscard]] const std::string &name() const { return m_context; }
  bool operator==(const SEContext &other) const { return m_context == other.m_context; }

 private:
  std::string m_context;
};

struct AvcContext {
  bool granted = false;
  std::set<std::string> operations;
  SEContext scontext;
  SEContext tcontext;
  std::string tclass;
  AttributeMap misc_attributes;
  bool permissive = false;
  bool valid = false;
  bool consumed = false;

  explicit AvcContext(std::string_view line);
  AvcContext() = default;

  [[nodiscard]] bool isDenied() const { return valid && !granted; }
  [[nodiscard]] bool isUntrustedApp() const;
  [[nodiscard]] bool mergeFrom(AvcContext &other);
  [[nodiscard]] std::string toAllowRule() const;
};

using AvcContexts = std::vector<AvcContext>;
std::string FormatAllowSuggestions(AvcContexts contexts);
bool ShouldCollectAvcFromSource(std::string_view sourceName, bool requested);

bool IsSafeCaptureName(std::string_view name);
bool PrepareCaptureDirectory(const std::filesystem::path &root, std::string_view name,
                             std::size_t retainedCaptures, std::filesystem::path *current,
                             std::string *error);
bool HasMinimumFreeSpace(const std::filesystem::path &path, std::uintmax_t minimumBytes);
