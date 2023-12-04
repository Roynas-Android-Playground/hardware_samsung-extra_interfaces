/*
 * Copyright (C) 2023 Royna (@roynatech2544 on GH)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

class SmartCharge : public BnSmartCharge {
  std::shared_ptr<std::thread> kLoopThread;
  int upper, lower;
  // Worker function
  void startLoop(bool withrestart);
  // Starter function
  void createLoopThread(bool restart);
  std::atomic_bool kRun;
  std::mutex config_lock;

  std::condition_variable cv;
  // Used by above condition_variable
  std::mutex kCVLock;

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
