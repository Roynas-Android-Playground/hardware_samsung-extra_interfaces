/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.samsung_ext.hardware.lights-service"

#include "ExtLights.h"

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace light {

ndk::ScopedAStatus ExtLights::onPropsChanged(void) {
	Lights::handleBacklight_brightness(0);
	return ndk::ScopedAStatus::ok();
}

} // namespace light
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
