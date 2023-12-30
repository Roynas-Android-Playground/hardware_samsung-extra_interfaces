/*
 * Copyright (C) 2023 Royna (@roynatech2544 on GH)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>
#include <aidl/android/hardware/health/BnHealth.h>
#include <healthhalutils/HealthHalUtils.h>

#include <dlfcn.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

using android::hardware::health::V2_0::IHealth;
using android::hardware::hidl_death_recipient;
using android::sp;
using android::wp;
using IHealthAIDL = aidl::android::hardware::health::IHealth;

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

class hidl_health_death_recipient : public hidl_death_recipient {
  public:
    hidl_health_death_recipient(const sp<IHealth>& health)
        : mHealth(health) {}
    void serviceDied(uint64_t cookie, const wp<::android::hidl::base::V1_0::IBase>& who);

  private:
    sp<IHealth> mHealth;
};

class SmartCharge : public BnSmartCharge {
  std::shared_ptr<std::thread> kLoopThread;
  // Protect above thread pointer
  std::mutex thread_lock;

  int upper, lower;
  // Protect above variables
  std::mutex config_lock;

  // Worker function
  void startLoop(bool withrestart);
  // Starter function
  void createLoopThread(bool restart);

  // Thread status indicator
  std::atomic_bool kRunning;

  std::condition_variable cv;
  // Used by above condition_variable
  std::mutex kCVLock;

  void* handle;
  std::function<void(const bool)> setChargableFunc;

  sp<IHealth> health_hidl;
  sp<hidl_death_recipient> hidl_death_recp;
  std::shared_ptr<IHealthAIDL> health_aidl;
  ndk::ScopedAIBinder_DeathRecipient aidl_death_recp;
  // Protect health_hal pointers
  std::mutex hal_health_lock;

  enum {
      USE_HEALTH_AIDL,
      USE_HEALTH_HIDL,
  } healthState;

  bool loadAndParseConfigProp();
  void loadImplLibrary();
  void loadEnabledAndStart();

public:
  void loadHealthImpl();
  SmartCharge();
  ndk::ScopedAStatus setChargeLimit(int32_t upper, int32_t lower) override;
  ndk::ScopedAStatus activate(bool enable, bool restart) override;
};

} // namespace battery
} // namespace framework
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
