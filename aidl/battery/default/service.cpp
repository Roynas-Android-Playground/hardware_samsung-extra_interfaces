/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SmartCharge.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using ::aidl::vendor::samsung_ext::framework::battery::SmartCharge;

int main(int argc, char **argv) {
  android::base::InitLogging(argv);
 
  ABinderProcess_setThreadPoolMaxThreadCount(8);
  ABinderProcess_startThreadPool();
  std::shared_ptr<SmartCharge> smartcharge =
      ndk::SharedRefBase::make<SmartCharge>();

  const std::string instance =
      std::string() + SmartCharge::descriptor + "/default";
  binder_status_t status = AServiceManager_addService(
      smartcharge->asBinder().get(), instance.c_str());
  CHECK(status == STATUS_OK);

  ABinderProcess_joinThreadPool();
  return EXIT_FAILURE; // should not reach
}
