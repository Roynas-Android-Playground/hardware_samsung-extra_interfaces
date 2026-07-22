#define LOG_TAG "SmartChargeSvc::JSONParser"

#include "JSONParser.hpp"

#include <android-base/logging.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

bool ConfigParser::handler_OpenFile(const std::string &node,
                                    const std::string & /*data*/) {
  std::ifstream file(node);
  if (!file.is_open()) {
    PLOG(ERROR) << "Failed to open " << node;
    return false;
  }
  std::string ignored;
  file >> ignored;
  return file.good() || file.eof();
}

bool ConfigParser::handler_WriteFile(const std::string &node,
                                     const std::string &data) {
  std::ofstream file(node);
  if (!file.is_open()) {
    PLOG(ERROR) << "Failed to open " << node;
    return false;
  }
  file << data;
  file.flush();
  if (!file.good()) {
    PLOG(ERROR) << "Failed to write " << node;
    return false;
  }
  return true;
}

std::pair<Json::Value, ConfigParser::MatchQuality>
ConfigParser::lookupEntry(const SearchEntry &search) {
  std::pair<Json::Value, MatchQuality> current = {
      Json::Value{}, MatchQuality::NO_MATCH};
  for (const auto &device : root_) {
    if (device["codename"].asString() == search.codename) {
      return {device, MatchQuality::EXACT};
    }
    if (device["vendor"].asString() == search.vendor) {
      current = {device, MatchQuality::MATCHES_VENDOR};
    }
  }
  return current;
}

ConfigParser::ConfigParser(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    PLOG(ERROR) << "Failed to open " << path;
    return;
  }
  file >> root_;
  if (!file.good() && !file.eof()) {
    LOG(ERROR) << "Failed to parse " << path;
    root_.clear();
  }
}

ConfigParser::ActionFunction ConfigParser::findEntry(
    const SearchEntry &search) {
  constexpr std::size_t kEnable = 0;
  constexpr std::size_t kDisable = 1;
  std::array<std::function<bool()>, 2> actions;

  const auto current = lookupEntry(search);
  if (current.second == MatchQuality::NO_MATCH) {
    LOG(ERROR) << "No matching SmartCharge device configuration";
    return {};
  }

  for (const auto &action : current.first["actions"]) {
    if (!action["action"].isString() || !action["node"].isString() ||
        !action["handler"].isString()) {
      LOG(ERROR) << "Invalid SmartCharge action entry";
      return {};
    }

    const std::string actionType = action["action"].asString();
    const std::string node = action["node"].asString();
    const std::string handlerName = action["handler"].asString();
    const std::string handlerData = action["handler_data"].asString();

    const auto handler = std::find_if(
        handlers_.begin(), handlers_.end(), [&](const auto &candidate) {
          return candidate.first == handlerName;
        });
    if (handler == handlers_.end()) {
      LOG(ERROR) << "Unknown SmartCharge handler " << handlerName;
      return {};
    }

    const auto callback = [function = handler->second, node, handlerData] {
      return function(node, handlerData);
    };
    if (actionType == "enable") {
      actions[kEnable] = callback;
    } else if (actionType == "disable") {
      actions[kDisable] = callback;
    } else {
      LOG(ERROR) << "Unknown SmartCharge action type " << actionType;
      return {};
    }
  }

  if (!actions[kEnable] || !actions[kDisable]) {
    LOG(ERROR) << "SmartCharge configuration must define enable and disable";
    return {};
  }

  return [actions = std::move(actions)](bool allowCharging) {
    return actions[allowCharging ? kEnable : kDisable]();
  };
}
