/*
 * Copyright (C) 2023 Royna (@roynatech2544 on GH)
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "JSONParser.hpp"
#include "SmartChargePolicy.h"

#include <aidl/android/hardware/health/IHealth.h>
#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>
#include <healthhalutils/HealthHalUtils.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using android::hardware::health::V2_0::IHealth;
using android::hardware::hidl_death_recipient;
using android::sp;
using android::wp;
using IHealthAIDL = aidl::android::hardware::health::IHealth;

namespace aidl::vendor::samsung_ext::framework::battery {

class SmartCharge;

class hidl_health_death_recipient final : public hidl_death_recipient {
 public:
  hidl_health_death_recipient(const sp<IHealth> &health, SmartCharge *owner)
      : health_(health), owner_(owner) {}
  void serviceDied(
      uint64_t cookie,
      const wp<::android::hidl::base::V1_0::IBase> &who) override;

 private:
  sp<IHealth> health_;
  SmartCharge *owner_;
};

class SmartCharge final : public BnSmartCharge {
 public:
  SmartCharge();
  ~SmartCharge() override;

  ndk::ScopedAStatus setChargeLimit(int32_t upper, int32_t lower) override;
  ndk::ScopedAStatus activate(bool enable, bool restart) override;
  binder_status_t dump(int fd, const char **args, uint32_t numArgs) override;

  void reloadHealthService();

 private:
  enum class HealthBackend {
    NONE,
    AIDL,
    HIDL,
  };

  void loadConfiguration();
  void loadPersistedState();
  void connectHealthService();
  std::optional<int> readBatteryPercent(std::string *error);

  void startWorkerLocked();
  std::thread stopWorkerLocked(std::unique_lock<std::mutex> *lock);
  void workerLoop();
  void wakeWorker();
  bool applyChargingPermission(bool allowCharging);

  std::mutex apiLock_;
  ConfigParser::ActionFunction setChargingAllowed_;
  bool backendSupported_ = false;

  std::mutex stateLock_;
  std::condition_variable stateCv_;
  std::thread worker_;
  bool stopRequested_ = false;
  bool enabled_ = false;
  bool restartEnabled_ = false;
  int upper_ = kInvalidLowerLimit;
  int lower_ = kInvalidLowerLimit;
  std::optional<bool> lastAppliedPermission_;
  int lastBatteryPercent_ = -1;
  std::string lastError_;
  std::uint64_t generation_ = 0;

  std::mutex healthLock_;
  HealthBackend healthBackend_ = HealthBackend::NONE;
  sp<IHealth> healthHidl_;
  sp<hidl_death_recipient> hidlDeathRecipient_;
  std::shared_ptr<IHealthAIDL> healthAidl_;
  ndk::ScopedAIBinder_DeathRecipient aidlDeathRecipient_;
};

}  // namespace aidl::vendor::samsung_ext::framework::battery
