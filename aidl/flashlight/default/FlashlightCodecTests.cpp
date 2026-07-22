#include "FlashlightCodec.h"

#include <iostream>

using namespace aidl::vendor::samsung_ext::hardware::camera::flashlight;

int main() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };

  for (int level = 1; level <= 5; ++level) {
    const int raw = EncodeBrightnessLevel(level);
    const auto decoded = DecodeFlashlightValue(raw, 1);
    expect(decoded.has_value(), "encoded value did not decode");
    expect(decoded && decoded->enabled, "encoded value should be enabled");
    expect(decoded && decoded->brightnessLevel == level,
           "brightness round trip failed");
  }

  const auto off = DecodeFlashlightValue(0, 4);
  expect(off && !off->enabled && off->brightnessLevel == 4,
         "off state should preserve remembered brightness");
  const auto genericOn = DecodeFlashlightValue(1, 5);
  expect(genericOn && genericOn->enabled && genericOn->brightnessLevel == 5,
         "generic framework activation should use remembered brightness");
  expect(!DecodeFlashlightValue(99, 1), "unknown raw value must fail");
  expect(EncodeBrightnessLevel(0) == -1, "invalid level must not encode");

  return failures == 0 ? 0 : 1;
}
