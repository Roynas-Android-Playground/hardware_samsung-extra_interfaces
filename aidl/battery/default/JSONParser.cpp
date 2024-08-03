#define LOG_TAG "SmartChargeSvc::JSONParser"

#include "JSONParser.hpp"
#include <android-base/logging.h>

#include <initializer_list>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

void ConfigParser::handler_OpenFile(const std::string &node,
                                    const std::string &data) {
  LOG(DEBUG) << "Opening file: " << data;
  std::ifstream file(node);
  std::string line;

  if (file) {
    file >> line;
  } else {
    PLOG(ERROR) << "Failed to read file";
  }
}

void ConfigParser::handler_WriteFile(const std::string &node,
                                     const std::string &data) {
  LOG(DEBUG) << "Writing to file: " << data;
  std::ofstream file(node);
  if (file) {
    file << data;
  } else {
    PLOG(ERROR) << "Failed to write to file";
  }
}

// Returns a pair with the matching device and its match quality.
std::pair<Json::Value, ConfigParser::MatchQuality>
ConfigParser::lookupEntry(const SearchEntry &search) {
  std::pair<Json::Value, MatchQuality> current = {root, MatchQuality::NO_MATCH};
  for (const auto &devices : root) {
    if (devices["codename"].asString() == search.codename) {
      current = {devices, MatchQuality::EXACT};
      break;
    }
    if (devices["vendor"].asString() == search.vendor) {
      current = {devices, MatchQuality::MATCHES_VENDOR};
    }
  }
  if (current.second == MatchQuality::NO_MATCH) {
    LOG(ERROR) << "No matching device found";
    return current;
  }
  LOG(DEBUG) << "Found a match with quality: ";
  switch (current.second) {
  case MatchQuality::EXACT:
    LOG(DEBUG) << "EXACT";
    break;
  case MatchQuality::MATCHES_VENDOR:
    LOG(DEBUG) << "MATCHES_VENDOR";
    break;
  case MatchQuality::NO_MATCH:
    break;
  };
  return current;
}

ConfigParser::ConfigParser(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    PLOG(ERROR) << "Failed to open file";
    return;
  }
  file >> root;
}

std::function<void(bool)> ConfigParser::findEntry(const SearchEntry &search) {
  constexpr int ENABLE_FN_INDEX = 0;
  constexpr int DISABLE_FN_INDEX = 1;
  std::array<std::function<void(void)>, 2> handlers;

  const auto current = lookupEntry(search);
  if (current.second == MatchQuality::NO_MATCH) {
    return [](bool) {};
  }

  for (const auto &action : current.first["actions"]) {
    if (!action["action"].isString()) {
      LOG(ERROR) << "Invalid action type";
      return [](bool) {};
    }
    const std::string actionType = action["action"].asString();
    const std::string node = action["node"].asString();
    const std::string handlerName = action["handler"].asString();
    const std::string handlerData = action["handler_data"].asString();

    if (actionType == "enable") {
      for (const auto &handler : m_handlers) {
        if (handler.first == handlerName) {
          handlers[ENABLE_FN_INDEX] = [callback = handler.second, node,
                                       handlerData]() {
            callback(node, handlerData);
          };
          break;
        }
      }
      if (!handlers[ENABLE_FN_INDEX]) {
        LOG(ERROR) << "No handlers found for enable action";
        return [](bool) {};
      }
    } else if (actionType == "disable") {
      for (const auto &handler : m_handlers) {
        if (handler.first == handlerName) {
          handlers[DISABLE_FN_INDEX] = [callback = handler.second, node,
                                        handlerData]() {
            callback(node, handlerData);
          };
          break;
        }
      }
      if (!handlers[DISABLE_FN_INDEX]) {
        LOG(ERROR) << "No handlers found for disable action";
        return [](bool) {};
      }
    } else {
      LOG(ERROR) << "Invalid action type";
      return [](bool) {};
    }
  }
  return [handlers](bool enable) {
    if (enable) {
      handlers[ENABLE_FN_INDEX]();
    } else {
      handlers[DISABLE_FN_INDEX]();
    }
  };
}

