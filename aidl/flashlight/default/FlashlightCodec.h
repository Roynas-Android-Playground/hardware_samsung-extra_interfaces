#pragma once

#include <optional>

namespace aidl::vendor::samsung_ext::hardware::camera::flashlight {

struct DecodedFlashlightState {
  bool enabled;
  int brightnessLevel;
};

bool IsValidBrightnessLevel(int level);
int EncodeBrightnessLevel(int level);
std::optional<DecodedFlashlightState> DecodeFlashlightValue(
    int rawValue, int rememberedBrightnessLevel);

}  // namespace aidl::vendor::samsung_ext::hardware::camera::flashlight
