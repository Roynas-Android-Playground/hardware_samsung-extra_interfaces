/*
 * Copyright (C) 2023 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "SmartCharge.h"

#include <GetServiceSupport.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <hidl/HidlTransportSupport.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <utility>

namespace aidl::vendor::samsung_ext::framework::battery {
namespace {
using namespace std::chrono_literals;
constexpr char kConfigProp[] = "persist.ext.smartcharge.config";
constexpr char kEnabledProp[] = "persist.ext.smartcharge.enabled";
constexpr char kConfigPath[] = "/system_ext/etc/smartcharge_nodes.json";
constexpr char kDisabled[] = "0,0";
std::string Pair(int a, int b) { return std::to_string(a) + "," + std::to_string(b); }
ndk::ScopedAStatus PropertyError(const char *message) {
  return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
      errno == 0 ? EIO : errno, message);
}
void OnAidlHealthDied(void *cookie) {
  if (auto *service = static_cast<SmartCharge *>(cookie)) service->reloadHealthService();
}
}  // namespace

SmartCharge::SmartCharge() {
  loadConfiguration();
  connectHealthService();
  loadPersistedState();
}

SmartCharge::~SmartCharge() {
  std::thread worker;
  {
    std::unique_lock lock(stateLock_);
    enabled_ = false;
    worker = stopWorkerLocked(&lock);
  }
  if (worker.joinable()) worker.join();
  if (backendSupported_) (void)applyChargingPermission(true);
}

void SmartCharge::loadConfiguration() {
  ConfigParser parser(kConfigPath);
  setChargingAllowed_ = parser.findEntry({
      android::base::GetProperty("ro.product.device", ""),
      android::base::GetProperty("ro.product.manufacturer", "")});
  backendSupported_ = static_cast<bool>(setChargingAllowed_);
  if (!backendSupported_) LOG(ERROR) << "SmartCharge is unsupported on this device";
}

void SmartCharge::connectHealthService() {
  std::lock_guard lock(healthLock_);
  healthAidl_.reset();
  healthHidl_.clear();
  hidlDeathRecipient_.clear();
  aidlDeathRecipient_ = {};
  healthBackend_ = HealthBackend::NONE;

  healthAidl_ = getServiceDefault<IHealthAIDL>();
  if (healthAidl_) {
    healthBackend_ = HealthBackend::AIDL;
    aidlDeathRecipient_ = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(OnAidlHealthDied));
    (void)AIBinder_linkToDeath(healthAidl_->asBinder().get(),
                              aidlDeathRecipient_.get(), this);
    return;
  }

  healthHidl_ = ::android::hardware::health::V2_0::get_health_service();
  if (healthHidl_) {
    healthBackend_ = HealthBackend::HIDL;
    hidlDeathRecipient_ = new hidl_health_death_recipient(healthHidl_, this);
    (void)healthHidl_->linkToDeath(hidlDeathRecipient_, 0);
  }
}

void SmartCharge::reloadHealthService() {
  connectHealthService();
  wakeWorker();
}

std::optional<int> SmartCharge::readBatteryPercent(std::string *error) {
  std::lock_guard lock(healthLock_);
  int percent = -1;
  if (healthBackend_ == HealthBackend::AIDL && healthAidl_) {
    auto status = healthAidl_->getCapacity(&percent);
    if (!status.isOk()) {
      if (error) *error = status.getDescription();
      return std::nullopt;
    }
  } else if (healthBackend_ == HealthBackend::HIDL && healthHidl_) {
    using ::android::hardware::health::V2_0::Result;
    Result result = Result::UNKNOWN;
    healthHidl_->getCapacity([&](Result returned, int32_t value) {
      result = returned;
      percent = value;
    });
    if (result != Result::SUCCESS) {
      if (error) *error = "HIDL Health getCapacity failed";
      return std::nullopt;
    }
  } else {
    if (error) *error = "No Health HAL is connected";
    return std::nullopt;
  }
  if (percent < 0 || percent > 100) {
    if (error) *error = "Health HAL returned an invalid percentage";
    return std::nullopt;
  }
  return percent;
}

bool SmartCharge::applyChargingPermission(bool allow) {
  return backendSupported_ && setChargingAllowed_ && setChargingAllowed_(allow);
}

void SmartCharge::workerLoop() {
  std::unique_lock lock(stateLock_);
  while (!stopRequested_ && enabled_) {
    const auto generation = generation_;
    const int upper = upper_;
    const int lower = lower_;
    const bool restart = restartEnabled_;
    const auto lastApplied = lastAppliedPermission_;
    lock.unlock();

    std::string error;
    const auto percent = readBatteryPercent(&error);
    if (!percent) {
      const bool restored = lastApplied == true || applyChargingPermission(true);
      lock.lock();
      lastBatteryPercent_ = -1;
      lastAppliedPermission_ = restored ? std::optional<bool>{true} : std::nullopt;
      lastError_ = error + (restored ? "" : "; failed to restore charging");
      lock.unlock();
      connectHealthService();
      lock.lock();
    } else {
      const auto decision = EvaluateChargePolicy(*percent, upper, lower, restart);
      std::optional<bool> desired;
      if (decision == ChargeDecision::ALLOW_CHARGING ||
          (decision == ChargeDecision::KEEP_CURRENT && !lastApplied)) {
        desired = true;
      } else if (decision == ChargeDecision::DISALLOW_CHARGING) {
        desired = false;
      }

      lock.lock();
      if (stopRequested_ || !enabled_ || generation_ != generation) continue;
      lock.unlock();
      bool applied = true;
      bool failOpen = false;
      if (desired && desired != lastApplied) {
        applied = applyChargingPermission(*desired);
        if (!applied && !*desired) failOpen = applyChargingPermission(true);
      }
      lock.lock();
      lastBatteryPercent_ = *percent;
      if (!applied) {
        lastAppliedPermission_ = failOpen ? std::optional<bool>{true} : std::nullopt;
        lastError_ = failOpen ? "Policy apply failed; charging restored"
                              : "Policy and fail-open actions failed";
      } else {
        if (desired) lastAppliedPermission_ = desired;
        lastError_.clear();
      }
    }

    stateCv_.wait_for(lock, 5s, [&] {
      return stopRequested_ || !enabled_ || generation_ != generation;
    });
  }
}

void SmartCharge::startWorkerLocked() {
  if (worker_.joinable()) return;
  stopRequested_ = false;
  worker_ = std::thread(&SmartCharge::workerLoop, this);
}

std::thread SmartCharge::stopWorkerLocked(std::unique_lock<std::mutex> *lock) {
  stopRequested_ = true;
  ++generation_;
  stateCv_.notify_all();
  std::thread worker = std::move(worker_);
  if (lock && lock->owns_lock()) lock->unlock();
  return worker;
}

void SmartCharge::wakeWorker() {
  std::lock_guard lock(stateLock_);
  ++generation_;
  stateCv_.notify_all();
}

void SmartCharge::loadPersistedState() {
  int lower = kInvalidLowerLimit, upper = kInvalidLowerLimit;
  const auto config = android::base::GetProperty(kConfigProp, "");
  if (ParseIntegerPair(config, &lower, &upper) &&
      IsValidChargeConfig(upper, lower)) {
    upper_ = upper;
    lower_ = lower;
  }

  int enabled = 0, restart = 0;
  const auto state = android::base::GetProperty(kEnabledProp, kDisabled);
  if (!ParseIntegerPair(state, &enabled, &restart) ||
      (enabled != 0 && enabled != 1) || (restart != 0 && restart != 1)) {
    (void)android::base::SetProperty(kEnabledProp, kDisabled);
    return;
  }
  if (!enabled) return;
  if (!backendSupported_ || !IsValidChargeConfig(upper_, lower_) ||
      (restart && lower_ == kInvalidLowerLimit)) {
    (void)android::base::SetProperty(kEnabledProp, kDisabled);
    if (backendSupported_) (void)applyChargingPermission(true);
    return;
  }
  std::lock_guard lock(stateLock_);
  enabled_ = true;
  restartEnabled_ = restart != 0;
  ++generation_;
  startWorkerLocked();
}

ndk::ScopedAStatus SmartCharge::setChargeLimit(int32_t upper, int32_t lower) {
  std::lock_guard apiGuard(apiLock_);
  if (lower < 0) lower = kInvalidLowerLimit;
  if (!IsValidChargeConfig(upper, lower)) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
        EX_ILLEGAL_ARGUMENT,
        "upper must be 50-95 and lower must be -1 or 50..upper-1");
  }
  errno = 0;
  if (!android::base::SetProperty(kConfigProp, Pair(lower, upper)))
    return PropertyError("Failed to persist SmartCharge limits");
  {
    std::lock_guard lock(stateLock_);
    upper_ = upper;
    lower_ = lower;
    ++generation_;
  }
  stateCv_.notify_all();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SmartCharge::activate(bool enable, bool restart) {
  std::lock_guard apiGuard(apiLock_);
  if (enable && !backendSupported_) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
        EX_UNSUPPORTED_OPERATION, "SmartCharge is unsupported on this device");
  }
  if (enable) {
    {
      std::lock_guard lock(stateLock_);
      if (!IsValidChargeConfig(upper_, lower_))
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
      if (restart && lower_ == kInvalidLowerLimit)
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    errno = 0;
    if (!android::base::SetProperty(kEnabledProp, Pair(1, restart ? 1 : 0)))
      return PropertyError("Failed to persist SmartCharge enabled state");
    {
      std::lock_guard lock(stateLock_);
      enabled_ = true;
      restartEnabled_ = restart;
      ++generation_;
      startWorkerLocked();
    }
    stateCv_.notify_all();
    return ndk::ScopedAStatus::ok();
  }

  errno = 0;
  const bool propertyUpdated = android::base::SetProperty(kEnabledProp, kDisabled);
  std::thread worker;
  {
    std::unique_lock lock(stateLock_);
    enabled_ = false;
    restartEnabled_ = false;
    worker = stopWorkerLocked(&lock);
  }
  if (worker.joinable()) worker.join();
  const bool restored = !backendSupported_ || applyChargingPermission(true);
  {
    std::lock_guard lock(stateLock_);
    lastAppliedPermission_ = restored ? std::optional<bool>{true} : std::nullopt;
    lastError_ = restored ? "" : "Failed to restore charging permission";
  }
  if (!propertyUpdated) return PropertyError("Failed to persist disabled state");
  if (!restored)
    return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
        EIO, "Failed to restore charging permission");
  return ndk::ScopedAStatus::ok();
}

binder_status_t SmartCharge::dump(int fd, const char **, uint32_t) {
  std::scoped_lock lock(stateLock_, healthLock_);
  dprintf(fd, "Supported: %s\n", backendSupported_ ? "yes" : "no");
  dprintf(fd, "Enabled: %s\n", enabled_ ? "yes" : "no");
  dprintf(fd, "Mode: %s\n", restartEnabled_ ? "hysteresis" : "stop-only");
  dprintf(fd, "Configuration (upper/lower): %d %d\n", upper_, lower_);
  dprintf(fd, "Last battery percent: %d\n", lastBatteryPercent_);
  dprintf(fd, "Charging permission: %s\n",
          !lastAppliedPermission_ ? "unknown"
                                 : (*lastAppliedPermission_ ? "allowed" : "blocked"));
  dprintf(fd, "Health backend: %s\n",
          healthBackend_ == HealthBackend::AIDL
              ? "AIDL"
              : (healthBackend_ == HealthBackend::HIDL ? "HIDL" : "none"));
  dprintf(fd, "Last error: %s\n", lastError_.empty() ? "none" : lastError_.c_str());
  return STATUS_OK;
}

using ::android::hardware::interfacesEqual;
void hidl_health_death_recipient::serviceDied(
    uint64_t, const wp<::android::hidl::base::V1_0::IBase> &who) {
  if (owner_ && health_ && interfacesEqual(health_, who.promote()))
    owner_->reloadHealthService();
}

}  // namespace aidl::vendor::samsung_ext::framework::battery
