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
using android::sp;
using IHealthAIDL = aidl::android::hardware::health::IHealth;

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

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

  // Thread controller
  std::atomic_bool kRun;

  std::condition_variable cv;
  // Used by above condition_variable
  std::mutex kCVLock;

  void* handle;
  std::function<void(const bool)> setChargableFunc;

  sp<IHealth> health_hidl;
  std::shared_ptr<IHealthAIDL> health_aidl;
  void loadHealthImpl();
  enum {
      USE_HEALTH_AIDL,
      USE_HEALTH_HIDL,
  } healthState;
public:
  SmartCharge();
  ndk::ScopedAStatus setChargeLimit(int32_t upper, int32_t lower) override;
  ndk::ScopedAStatus activate(bool enable, bool restart) override;
};

} // namespace battery
} // namespace framework
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
