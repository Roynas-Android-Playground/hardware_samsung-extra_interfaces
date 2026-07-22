#pragma once

#include <functional>
#include <json/json.h>
#include <string>
#include <utility>
#include <vector>

class ConfigParser {
 public:
  struct SearchEntry {
    std::string codename;
    std::string vendor;
  };
  using ActionFunction = std::function<bool(bool)>;

  explicit ConfigParser(const std::string &path);
  ActionFunction findEntry(const SearchEntry &search);

 private:
  using HandlerFunction =
      std::function<bool(const std::string &, const std::string &)>;
  using HandlerType = std::pair<std::string, HandlerFunction>;
  enum class MatchQuality { EXACT, MATCHES_VENDOR, NO_MATCH };

  static bool handler_OpenFile(const std::string &node,
                               const std::string &data);
  static bool handler_WriteFile(const std::string &node,
                                const std::string &data);
  std::pair<Json::Value, MatchQuality> lookupEntry(const SearchEntry &search);

  Json::Value root_;
  std::vector<HandlerType> handlers_ = {
      {"OpenFile", handler_OpenFile},
      {"WriteFile", handler_WriteFile},
  };
};
