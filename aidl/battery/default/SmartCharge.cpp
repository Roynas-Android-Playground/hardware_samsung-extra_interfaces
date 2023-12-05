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

#include <chrono>
#include <functional>
#include <sstream>
#include <type_traits>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

using ::android::base::GetProperty;
using ::android::base::ReadFileToString;
using ::android::base::SetProperty;
using ::android::base::WaitForPropertyCreation;
using ::android::base::WriteStringToFile;

using namespace std::chrono_literals;

static const char kSmartChargeConfigProp[] = "persist.ext.smartcharge.config";
static const char kSmartChargeEnabledProp[] = "persist.ext.smartcharge.enabled";
static const char kComma = ',';
static const char kChargeCtlSysfs[] =
    "/sys/class/power_supply/battery/batt_slate_mode";
static const char kBatteryPercentSysfs[] =
    "/sys/class/power_supply/battery/capacity";

template <typename T>
using is_integral_or_bool =
    std::enable_if_t<std::is_integral_v<T> || std::is_same_v<T, bool>, bool>;

static inline bool isValidBool(const int val) {
  return val == true || val == false;
}
static inline bool verifyConfig(const int lower, const int upper) {
  return !(upper <= lower || upper > 95 || (0 <= lower && lower < 50));
}

template <typename T, is_integral_or_bool<T> = true>
struct ConfigPair {
  T first, second;
  std::string toString(void) {
    return std::to_string(first) + kComma + std::to_string(second);
  }
};

template <typename U>
bool fromString(const std::string &v, ConfigPair<U> *pair) {
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

template <>
bool fromString(const std::string &v, ConfigPair<bool> *pair) {
  ConfigPair<int> tmp{};
  if (fromString<int>(v, &tmp) && isValidBool(tmp.first) &&
      isValidBool(tmp.second)) {
    pair->first = tmp.first;
    pair->second = tmp.second;
    return true;
  }
  return false;
}

template <typename U>
bool getAndParse(const char *prop, ConfigPair<U> *pair) {
  if (WaitForPropertyCreation(prop, 500ms)) {
    std::string propval = GetProperty(prop, "");
    if (!propval.empty()) {
      return fromString(propval, pair);
    }
  }
  return false;
}

const static auto kDisabledCfgStr = ConfigPair<bool>{0, 0}.toString();

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

SmartCharge::SmartCharge(void) {
  {
    ConfigPair<int> ret{};
    if (getAndParse(kSmartChargeConfigProp, &ret) &&
        verifyConfig(ret.first, ret.second)) {
      upper = ret.second;
      lower = ret.first;
      ALOGD("%s: upper: %d, lower: %d", __func__, upper, lower);
    } else {
      upper = -1;
      lower = -1;
      ALOGW("%s: Parsing config failed", __func__);
      return;
    }
  }
  {
    ConfigPair<bool> ret{};
    if (getAndParse(kSmartChargeEnabledProp, &ret)) {
      if (ret.first) {
        ALOGD("%s: Starting loop, withrestart: %d", __func__, ret.second);
        kRun.store(true);
        createLoopThread(ret.second);
      } else
        ALOGD("%s: Not starting loop", __func__);
    } else {
      ALOGE("%s: Enabled prop value invaild, resetting to valid one...",
            __func__);
      SetProperty(kSmartChargeEnabledProp, kDisabledCfgStr);
    }
  }
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
  std::unique_lock<std::mutex> lock(kCVLock);
  while (kRun.load()) {
    auto per = BatteryHelper::getPercent();
    if (per < 0) {
      kRun.store(false);
      SetProperty(kSmartChargeEnabledProp, kDisabledCfgStr);
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
    if (cv.wait_for(lock, 5s) == std::cv_status::no_timeout) {
      // cv signaled, exit now
      break;
    }
  }
  lock.unlock();
  ALOGD("%s: --", __func__);
}

void SmartCharge::createLoopThread(bool restart) {
  const std::lock_guard<std::mutex> _(thread_lock);
  ALOGD("%s: create thread", __func__);
  kLoopThread = std::make_shared<std::thread>(&SmartCharge::startLoop, this, restart);
}

ndk::ScopedAStatus SmartCharge::setChargeLimit(int32_t upper_, int32_t lower_) {
  ALOGD("%s: upper: %d, lower: %d, kRun: %d", __func__, upper_, lower_,
        kRun.load());
  if (!verifyConfig(lower_, upper_))
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  if (kRun.load())
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  auto pair = ConfigPair<int>{lower_ < 0 ? -1 : lower_, upper_};
  SetProperty(kSmartChargeConfigProp, pair.toString());
  {
    std::unique_lock<std::mutex> _(config_lock);
    lower = lower_ < 0 ? -1 : lower_;
    upper = upper_;
  }
  ALOGD("%s: Exit", __func__);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SmartCharge::activate(bool enable, bool restart) {
  auto pair = ConfigPair<bool>{enable, restart};
  {
    std::unique_lock<std::mutex> _(config_lock);
    ALOGD("%s: upper: %d, lower: %d, enable: %d, restart: %d, kRun: %d", __func__,
          upper, lower, enable, restart, kRun.load());
    if (!verifyConfig(lower, upper))
      return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    if (lower == -1 && restart)
      return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  if (kRun.load() == enable)
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  SetProperty(kSmartChargeEnabledProp, pair.toString());
  if (enable) {
    kRun.store(true);
    if (kLoopThread) {
      ALOGW("Thread is running?");
    } else {
      createLoopThread(restart);
    }
  } else {
    kRun.store(false);
    BatteryHelper::setChargable(true);
    if (kLoopThread) {
      const std::lock_guard<std::mutex> _(thread_lock);
      if (kLoopThread->joinable()) {
        cv.notify_one();
        kLoopThread->join();
      }
      kLoopThread.reset();
    } else {
      ALOGW("No threads to stop?");
    }
  }
  ALOGD("%s: Exit", __func__);
  return ndk::ScopedAStatus::ok();
}

} // namespace battery
} // namespace framework
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
