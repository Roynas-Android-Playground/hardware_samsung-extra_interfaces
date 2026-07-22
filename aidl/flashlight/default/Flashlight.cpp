/*
 * Copyright (C) 2023 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Flashlight.h"

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <cerrno>
#include <string>

namespace aidl::vendor::samsung_ext::hardware::camera::flashlight {
namespace {

constexpr const char *kFlashNode = "/sys/class/camera/flash/rear_flash";
constexpr const char *kBrightnessProperty =
    "persist.ext.flashlight.last_brightness";

ndk::ScopedAStatus IoError(const char *message) {
  const int error = errno == 0 ? EIO : errno;
  return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(error,
                                                                  message);
}

}  // namespace

Flashlight::Flashlight()
    : desiredBrightness_(android::base::GetIntProperty(
          kBrightnessProperty, 1, 1, 5)) {}

ndk::ScopedAStatus Flashlight::readStateLocked(DecodedFlashlightState *state,
                                               int *rawValue) {
  std::string value;
  errno = 0;
  if (!android::base::ReadFileToString(kFlashNode, &value)) {
    return IoError("Failed to read flashlight sysfs node");
  }

  int parsed = 0;
  if (!android::base::ParseInt(android::base::Trim(value), &parsed)) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
        EX_ILLEGAL_STATE, "Flashlight sysfs node contains an invalid integer");
  }

  const auto decoded = DecodeFlashlightValue(parsed, desiredBrightness_);
  if (!decoded) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
        EX_ILLEGAL_STATE, "Flashlight sysfs node contains an unknown value");
  }

  *state = *decoded;
  if (rawValue != nullptr) {
    *rawValue = parsed;
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::writeRawValueLocked(int rawValue) {
  errno = 0;
  if (!android::base::WriteStringToFile(std::to_string(rawValue), kFlashNode)) {
    return IoError("Failed to write flashlight sysfs node");
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::persistBrightnessLocked(int level) {
  errno = 0;
  if (!android::base::SetProperty(kBrightnessProperty, std::to_string(level))) {
    return IoError("Failed to persist flashlight brightness");
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::getState(FlashlightState *_aidl_return) {
  if (_aidl_return == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }

  std::lock_guard<std::mutex> guard(lock_);
  DecodedFlashlightState state{};
  auto status = readStateLocked(&state, nullptr);
  if (!status.isOk()) {
    return status;
  }
  _aidl_return->enabled = state.enabled;
  _aidl_return->brightnessLevel = state.brightnessLevel;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::getCurrentBrightness(int32_t *_aidl_return) {
  if (_aidl_return == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }

  std::lock_guard<std::mutex> guard(lock_);
  DecodedFlashlightState state{};
  auto status = readStateLocked(&state, nullptr);
  if (!status.isOk()) {
    return status;
  }
  *_aidl_return = state.enabled ? state.brightnessLevel : 0;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::setBrightness(int32_t level) {
  if (!IsValidBrightnessLevel(level)) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
        EX_ILLEGAL_ARGUMENT, "Brightness level must be between 1 and 5");
  }

  std::lock_guard<std::mutex> guard(lock_);
  DecodedFlashlightState state{};
  int rawValue = 0;
  auto status = readStateLocked(&state, &rawValue);
  if (!status.isOk()) {
    return status;
  }

  const bool alreadyApplied =
      state.enabled && rawValue == EncodeBrightnessLevel(level);
  if (desiredBrightness_ == level && (!state.enabled || alreadyApplied)) {
    return ndk::ScopedAStatus::ok();
  }

  status = persistBrightnessLocked(level);
  if (!status.isOk()) {
    return status;
  }
  desiredBrightness_ = level;

  if (state.enabled && !alreadyApplied) {
    return writeRawValueLocked(EncodeBrightnessLevel(level));
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::enableFlash(bool enable) {
  std::lock_guard<std::mutex> guard(lock_);
  DecodedFlashlightState state{};
  int rawValue = 0;
  auto status = readStateLocked(&state, &rawValue);
  if (!status.isOk()) {
    return status;
  }

  if (!enable) {
    return rawValue == 0 ? ndk::ScopedAStatus::ok()
                         : writeRawValueLocked(0);
  }

  const int desiredRawValue = EncodeBrightnessLevel(desiredBrightness_);
  return rawValue == desiredRawValue ? ndk::ScopedAStatus::ok()
                                     : writeRawValueLocked(desiredRawValue);
}

}  // namespace aidl::vendor::samsung_ext::hardware::camera::flashlight
