/*
 * Copyright (C) 2023 Royna
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/BnFlashlight.h>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace camera {
namespace flashlight {

struct Flashlight : public BnFlashlight {
    int level_saved = 1; /* 1 - 5 */
    ndk::ScopedAStatus getCurrentBrightness(int32_t* _aidl_return) override;
    ndk::ScopedAStatus setBrightness(int32_t level) override;
    ndk::ScopedAStatus enableFlash(bool enable) override;
};

} // namespace flashlight
} // namespace camera
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
