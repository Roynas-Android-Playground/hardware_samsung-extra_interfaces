/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SmartCharge.h"

#include <android-base/file.h>
#include <android-base/properties.h>

#include <sstream>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

using ::android::base::GetProperty;
using ::android::base::SetProperty;
using ::android::base::WaitForPropertyCreation;

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

static constexpr const char *kSmartChargeConfigProp =
    "persist.ext.smartcharge.config";
static constexpr const char *kSmartChargeEnabledProp =
    "persist.ext.smartcharge.enabled";
static constexpr const char kComma = ',';
static constexpr const char *kChargeCtlSysfs =
    "/sys/devices/platform/battery/power_supply/battery/batt_slate_mode";
static constexpr const char *kBatteryPercentSysfs =
    "/sys/devices/platform/battery/power_supply/battery/capacity";

struct ConfigPair {
  int first;
  int second;
  std::string fromPair(void) {
    return std::to_string(first) + kComma + std::to_string(second);
  }
  static std::optional<ConfigPair> fromString(const std::string &v) {
    int first, second;
    std::stringstream ss(v);
    std::string res;

    if (v.find(kComma) == std::string::npos) return std::nullopt;
    getline(ss, res, kComma);
    first = std::stoi(res);
    getline(ss, res, kComma);
    second = std::stoi(res);
    return std::optional<ConfigPair>({first, second});
  }
};

struct BatteryHelper {
  static bool canCharge(void) {
    std::string data;
    ReadFileToString(kChargeCtlSysfs, &data);
    return !stoi(data);
  }
  static void setChargable(bool enable) {
    WriteStringToFile(std::to_string(!enable), kChargeCtlSysfs);
  }
  static int getPercent(void) {
    std::string data;
    ReadFileToString(kBatteryPercentSysfs, &data);
    return stoi(data);
  }
};

static std::optional<ConfigPair> getAndParseIfPossible(const char *prop) {
  if (WaitForPropertyCreation(prop, std::chrono::milliseconds(500))) {
    std::string propval = GetProperty(prop, "");
    if (!propval.empty()) {
      return ConfigPair::fromString(propval);
    }
  }
  return std::nullopt;
}

static inline bool verifyConfig(const int lower, const int upper) {
  return !(upper <= lower || upper > 95 || lower < 50);
}

SmartCharge::SmartCharge(void) {
  kPoolPtr = std::make_shared<ThreadPool>(3);
  kPoolPtr->Enqueue([this] {
    auto ret = getAndParseIfPossible(kSmartChargeConfigProp);
    if (ret.has_value() && verifyConfig(ret->first, ret->second)) {
      upper = ret->second;
      lower = ret->first;
    } else {
      upper = -1;
      lower = -1;
      kPoolPtr->Shutdown();
    }
  });
  kPoolPtr->Enqueue([this] {
    if (upper == -1 && lower == -1) return;
    auto ret = getAndParseIfPossible(kSmartChargeEnabledProp);
    if (ret.has_value() && !!ret->first) {
      startLoop(!!ret->second);
    }
    kPoolPtr->Shutdown();
  });
}

void SmartCharge::startLoop(bool withrestart) {
  while (true) {
    if (BatteryHelper::getPercent() > upper)
      BatteryHelper::setChargable(false);
    else if (withrestart && BatteryHelper::getPercent() < lower)
      BatteryHelper::setChargable(true);
    else if (!withrestart && BatteryHelper::getPercent() == upper - 1)
      BatteryHelper::setChargable(true);
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

ndk::ScopedAStatus SmartCharge::setChargeLimit(int32_t upper_, int32_t lower_) {
  if (!verifyConfig(lower_, upper_))
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  if (kPoolPtr && kPoolPtr->isRunning())
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  auto pair = ConfigPair{lower_ < 0 ? -1 : lower_, upper_};
  SetProperty(kSmartChargeConfigProp, pair.fromPair());
  lower = lower_;
  upper = upper_;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SmartCharge::activate(bool enable, bool restart) {
  if (!verifyConfig(lower, upper))
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  if (lower == -1 && restart)
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  if (kPoolPtr && !kPoolPtr->isRunning()) {
    // Dead pointer, reset it.
    kPoolPtr.reset();
  }
  if (!!kPoolPtr == enable)
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  auto pair = ConfigPair{static_cast<int>(enable), static_cast<int>(restart)};
  SetProperty(kSmartChargeEnabledProp, pair.fromPair());
  if (enable) {
    kPoolPtr = std::make_shared<ThreadPool>(3);
    kPoolPtr->Enqueue([this](bool withrestart) { startLoop(withrestart); },
                      restart);
  } else {
    kPoolPtr->Shutdown();
  }
  return ndk::ScopedAStatus::ok();
}

}  // namespace battery
}  // namespace framework
}  // namespace samsung_ext
}  // namespace vendor
}  // namespace aidl
