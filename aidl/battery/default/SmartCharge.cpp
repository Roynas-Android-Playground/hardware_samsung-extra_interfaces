/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SmartCharge.h"

#include <SafeStoi.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <log/log.h>

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
    "/sys/class/power_supply/battery/batt_slate_mode";
static constexpr const char *kBatteryPercentSysfs =
    "/sys/class/power_supply/battery/capacity";

struct ConfigPair {
  int first;
  int second;
  std::string fromPair(void) {
    return std::to_string(first) + kComma + std::to_string(second);
  }
  static bool fromString(const std::string &v, ConfigPair *pair) {
    std::stringstream ss(v);
    std::string res;

    if (v.find(kComma) != std::string::npos) {
      getline(ss, res, kComma);
      pair->first = stoi_safe(res);
      getline(ss, res, kComma);
      pair->second = stoi_safe(res);
      return true;
    }
    return false;
  }
};

class BatteryHelper {
  static int _readSysfs(const char *sysfs) {
    std::string data;
    ReadFileToString(sysfs, &data);
    return stoi_safe(data);
  }

 public:
  static void setChargable(bool enable) {
    WriteStringToFile(std::to_string(!enable), kChargeCtlSysfs);
  }
  static int getPercent(void) { return _readSysfs(kBatteryPercentSysfs); }
};

static bool getAndParse(const char *prop, ConfigPair *pair) {
  if (WaitForPropertyCreation(prop, std::chrono::milliseconds(500))) {
    std::string propval = GetProperty(prop, "");
    if (!propval.empty()) {
      return ConfigPair::fromString(propval, pair);
    }
  }
  return false;
}

static inline bool verifyConfig(const int lower, const int upper) {
  return !(upper <= lower || upper > 95 || (0 <= lower && lower < 50));
}

SmartCharge::SmartCharge(void) {
  kPoolPtr = std::make_shared<ThreadPool>(3);
#define func "SmartCharge()"
  kPoolPtr->Enqueue([this] {
    ConfigPair ret {};
    if (getAndParse(kSmartChargeConfigProp, &ret) && verifyConfig(ret.first, ret.second)) {
      upper = ret.second;
      lower = ret.first;
      ALOGD("%s: upper: %d, lower: %d", func, upper, lower);
    } else {
      upper = -1;
      lower = -1;
      ALOGW("%s: Parsing config failed", func);
      return;
    }
    if (getAndParse(kSmartChargeEnabledProp, &ret) && !!ret.first) {
      ALOGD("%s: Starting loop, withrestart: %d", func, !!ret.second);
      kRun.store(true);
      startLoop(!!ret.second);
    } else
      ALOGD("%s: Not starting loop", func);
  });
#undef func
}

enum ChargeStatus {
  ON,
  OFF,
  NOOP,
};

void SmartCharge::startLoop(bool withrestart) {
  bool initdone = false;
  ChargeStatus tmp, status = ChargeStatus::NOOP;
  ALOGD("%s: ++", __func__);
  while (kRun.load()) {
    auto per = BatteryHelper::getPercent();
    if (per < 0) {
      kRun.store(false);
      SetProperty(kSmartChargeEnabledProp, ConfigPair{0, 0}.fromPair());
      ALOGE("%s: exit loop: percent: %d", __func__, per);
      break;
    }
    if (per > upper)
      tmp = ChargeStatus::OFF;
    else if (withrestart && per < lower)
      tmp = ChargeStatus::ON;
    else if (!withrestart && per <= upper - 1)
      tmp = ChargeStatus::ON;
    else
      tmp = ChargeStatus::NOOP;
    if (tmp != status || !initdone) {
      switch (tmp) {
        case ChargeStatus::OFF:
          BatteryHelper::setChargable(false);
          break;
        case ChargeStatus::ON:
          BatteryHelper::setChargable(true);
          break;
        default:
          break;
      }
      status = tmp;
      initdone = true;
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  ALOGD("%s: --", __func__);
}

ndk::ScopedAStatus SmartCharge::setChargeLimit(int32_t upper_, int32_t lower_) {
  ALOGD("%s: upper: %d, lower: %d, kRun: %d", __func__, upper_, lower_,
        kRun.load());
  if (!verifyConfig(lower_, upper_))
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  if (kRun.load())
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  auto pair = ConfigPair{lower_ < 0 ? -1 : lower_, upper_};
  {
    std::unique_lock<std::mutex> _(config_lock);
    SetProperty(kSmartChargeConfigProp, pair.fromPair());
    lower = lower_ < 0 ? -1 : lower_;
    upper = upper_;
  }
  ALOGD("%s: Exit", __func__);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SmartCharge::activate(bool enable, bool restart) {
  ALOGD("%s: upper: %d, lower: %d, enable: %d, restart: %d, kRun: %d", __func__,
        upper, lower, enable, restart, kRun.load());
  if (!verifyConfig(lower, upper))
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  if (lower == -1 && restart)
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  if (kRun.load() == enable)
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  auto pair = ConfigPair{static_cast<int>(enable), static_cast<int>(restart)};
  {
    std::unique_lock<std::mutex> _(config_lock);
    SetProperty(kSmartChargeEnabledProp, pair.fromPair());
  }
  if (enable) {
    kRun.store(true);
    kPoolPtr->Enqueue([this](bool withrestart) { startLoop(withrestart); },
                      restart);
  } else {
    kRun.store(false);
    BatteryHelper::setChargable(true);
  }
  ALOGD("%s: Exit", __func__);
  return ndk::ScopedAStatus::ok();
}

}  // namespace battery
}  // namespace framework
}  // namespace samsung_ext
}  // namespace vendor
}  // namespace aidl
