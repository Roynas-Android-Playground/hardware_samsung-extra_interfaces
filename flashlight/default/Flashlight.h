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

class Flashlight : public BnFlashlight {
    int level_saved = 3; /* 1 - 5 */
public:
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
