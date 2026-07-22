#include "SmartChargePolicy.h"

#include <charconv>
#include <string_view>

namespace aidl::vendor::samsung_ext::framework::battery {
namespace {

bool ParseInteger(std::string_view value, int *result) {
  if (result == nullptr || value.empty()) {
    return false;
  }
  const char *begin = value.data();
  const char *end = begin + value.size();
  const auto parsed = std::from_chars(begin, end, *result);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

}  // namespace

bool IsValidChargeConfig(int upperPercent, int lowerPercent) {
  if (upperPercent < kMinimumChargeLimit ||
      upperPercent > kMaximumChargeLimit) {
    return false;
  }
  return lowerPercent == kInvalidLowerLimit ||
         (lowerPercent >= kMinimumChargeLimit && lowerPercent < upperPercent);
}

ChargeDecision EvaluateChargePolicy(int batteryPercent, int upperPercent,
                                    int lowerPercent, bool restartEnabled) {
  if (!IsValidChargeConfig(upperPercent, lowerPercent) || batteryPercent < 0 ||
      batteryPercent > 100) {
    return ChargeDecision::ALLOW_CHARGING;
  }
  if (batteryPercent >= upperPercent) {
    return ChargeDecision::DISALLOW_CHARGING;
  }
  if (!restartEnabled) {
    return ChargeDecision::ALLOW_CHARGING;
  }
  if (lowerPercent != kInvalidLowerLimit && batteryPercent <= lowerPercent) {
    return ChargeDecision::ALLOW_CHARGING;
  }
  return ChargeDecision::KEEP_CURRENT;
}

bool ParseIntegerPair(std::string_view value, int *first, int *second) {
  if (first == nullptr || second == nullptr) {
    return false;
  }
  const auto comma = value.find(',');
  if (comma == std::string_view::npos || comma == 0 ||
      comma + 1 >= value.size() ||
      value.find(',', comma + 1) != std::string_view::npos) {
    return false;
  }
  return ParseInteger(value.substr(0, comma), first) &&
         ParseInteger(value.substr(comma + 1), second);
}

}  // namespace aidl::vendor::samsung_ext::framework::battery
