#include <json/json.h>
#include <functional>

class ConfigParser {
public:
  struct SearchEntry;

private:
  Json::Value root;
  struct Handler {
    std::function<void(const std::string &, const std::string &)> handler;
    std::string name;
  };

  static void handler_OpenFile(const std::string &node,
                               const std::string &data);

  static void handler_WriteFile(const std::string &node,
                                const std::string &data);

  // Takes a node path and data
  using HandlerFunction =
      std::function<void(const std::string &, const std::string &)>;
  // Takes name and handler function
  using HandlerType = std::pair<std::string, HandlerFunction>;

  std::vector<HandlerType> m_handlers = {
      {"OpenFile", handler_OpenFile},
      {"WriteFile", handler_WriteFile},
  };

  enum class MatchQuality { EXACT, MATCHES_VENDOR, NO_MATCH };

  // Returns a pair with the matching device and its match quality.
  std::pair<Json::Value, MatchQuality> lookupEntry(const SearchEntry &search);

public:
  ConfigParser(const std::string &path);

  struct SearchEntry {
    std::string codename;
    std::string vendor;
  };

  std::function<void(bool)> findEntry(const SearchEntry &search);
};
