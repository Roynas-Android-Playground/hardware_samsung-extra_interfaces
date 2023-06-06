/*
 * Copyright (C) 2023 Royna (@roynatech2544 on GH)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>

#include <atomic>

#include "ThreadPool.h"

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace framework {
namespace battery {

class SmartCharge : public BnSmartCharge {
  std::shared_ptr<ThreadPool> kPoolPtr;
  int upper, lower;
  void startLoop(bool withrestart);
  std::atomic_bool kRun;

 public:
  SmartCharge();
  ndk::ScopedAStatus setChargeLimit(int32_t upper, int32_t lower) override;
  ndk::ScopedAStatus activate(bool enable, bool restart) override;
};

}  // namespace battery
}  // namespace framework
}  // namespace samsung_ext
}  // namespace vendor
}  // namespace aidl
