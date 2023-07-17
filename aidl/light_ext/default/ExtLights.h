/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/samsung_ext/hardware/light/BnExtLights.h>
#include "Lights.h"

using ::aidl::android::hardware::light::Lights;

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace light {

struct ExtLights : public BnExtLights {
    ndk::ScopedAStatus onPropsChanged() override;
};

} // namespace light
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
