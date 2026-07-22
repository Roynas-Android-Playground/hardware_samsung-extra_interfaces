/*
 * Copyright (C) 2023 Royna
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "FlashlightCodec.h"

#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/BnFlashlight.h>
#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/FlashlightState.h>

#include <mutex>

namespace aidl::vendor::samsung_ext::hardware::camera::flashlight {

class Flashlight final : public BnFlashlight {
 public:
  Flashlight();

  ndk::ScopedAStatus getCurrentBrightness(int32_t *_aidl_return) override;
  ndk::ScopedAStatus setBrightness(int32_t level) override;
  ndk::ScopedAStatus enableFlash(bool enable) override;
  ndk::ScopedAStatus getState(FlashlightState *_aidl_return) override;

 private:
  ndk::ScopedAStatus readStateLocked(DecodedFlashlightState *state,
                                     int *rawValue);
  ndk::ScopedAStatus writeRawValueLocked(int rawValue);
  ndk::ScopedAStatus persistBrightnessLocked(int level);

  std::mutex lock_;
  int desiredBrightness_ = 1;
};

}  // namespace aidl::vendor::samsung_ext::hardware::camera::flashlight
