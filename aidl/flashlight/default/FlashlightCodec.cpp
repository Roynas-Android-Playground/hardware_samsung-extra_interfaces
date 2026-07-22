#include "FlashlightCodec.h"

namespace aidl::vendor::samsung_ext::hardware::camera::flashlight {

bool IsValidBrightnessLevel(int level) { return level >= 1 && level <= 5; }

int EncodeBrightnessLevel(int level) {
  switch (level) {
    case 1:
      return 1001;
    case 2:
      return 1002;
    case 3:
      return 1004;
    case 4:
      return 1006;
    case 5:
      return 1009;
    default:
      return -1;
  }
}

std::optional<DecodedFlashlightState> DecodeFlashlightValue(
    int rawValue, int rememberedBrightnessLevel) {
  if (!IsValidBrightnessLevel(rememberedBrightnessLevel)) {
    return std::nullopt;
  }

  switch (rawValue) {
    case 0:
      return DecodedFlashlightState{false, rememberedBrightnessLevel};
    case 1:
      return DecodedFlashlightState{true, rememberedBrightnessLevel};
    case 1001:
      return DecodedFlashlightState{true, 1};
    case 1002:
      return DecodedFlashlightState{true, 2};
    case 1003:
    case 1004:
      return DecodedFlashlightState{true, 3};
    case 1005:
    case 1006:
      return DecodedFlashlightState{true, 4};
    case 1007:
    case 1008:
    case 1009:
      return DecodedFlashlightState{true, 5};
    default:
      return std::nullopt;
  }
}

}  // namespace aidl::vendor::samsung_ext::hardware::camera::flashlight
