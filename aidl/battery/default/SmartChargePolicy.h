#pragma once

#include <optional>
#include <string_view>

namespace aidl::vendor::samsung_ext::framework::battery {

constexpr int kInvalidLowerLimit = -1;
constexpr int kMinimumChargeLimit = 50;
constexpr int kMaximumChargeLimit = 95;

enum class ChargeDecision {
  KEEP_CURRENT,
  ALLOW_CHARGING,
  DISALLOW_CHARGING,
};

bool IsValidChargeConfig(int upperPercent, int lowerPercent);
bool IsValidChargeConfigForMode(int upperPercent, int lowerPercent, bool restartEnabled);
ChargeDecision EvaluateChargePolicy(int batteryPercent, int upperPercent, int lowerPercent,
                                    bool restartEnabled);
bool ParseIntegerPair(std::string_view value, int *first, int *second);

}  // namespace aidl::vendor::samsung_ext::framework::battery
