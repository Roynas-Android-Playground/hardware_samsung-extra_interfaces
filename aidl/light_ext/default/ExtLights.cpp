/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.samsung_ext.hardware.lights-service"

#include <android-base/logging.h>
#include "ExtLights.h"

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace light {

ndk::ScopedAStatus ExtLights::onPropsChanged(void) {
  if (svc) {
    svc->handleBacklight_brightness(true, /*unused*/ 0);
    return ndk::ScopedAStatus::ok();
  } else {
    LOG(ERROR) << __func__ << "svc is NULL";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
}

} // namespace light
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
