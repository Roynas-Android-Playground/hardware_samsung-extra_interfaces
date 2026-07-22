#include "SmartChargePolicy.h"

#include <iostream>

using namespace aidl::vendor::samsung_ext::framework::battery;

int main() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };

  expect(IsValidChargeConfig(80, 70), "80/70 should be valid");
  expect(IsValidChargeConfig(80, -1), "80/no-restart should be valid");
  expect(!IsValidChargeConfigForMode(80, -1, true), "restart mode must require a lower threshold");
  expect(IsValidChargeConfigForMode(80, -1, false),
         "stop-only mode should allow no lower threshold");
  expect(!IsValidChargeConfig(0, -1), "upper=0 must be rejected");
  expect(!IsValidChargeConfig(49, -1), "upper below 50 must be rejected");
  expect(!IsValidChargeConfig(96, 70), "upper above 95 must be rejected");
  expect(!IsValidChargeConfig(80, 80), "equal thresholds must be rejected");

  expect(EvaluateChargePolicy(80, 80, 70, true) == ChargeDecision::DISALLOW_CHARGING,
         "upper edge should disable charging");
  expect(EvaluateChargePolicy(70, 80, 70, true) == ChargeDecision::ALLOW_CHARGING,
         "lower edge should enable charging");
  expect(EvaluateChargePolicy(75, 80, 70, true) == ChargeDecision::KEEP_CURRENT,
         "hysteresis middle should preserve state");
  expect(EvaluateChargePolicy(79, 80, -1, false) == ChargeDecision::ALLOW_CHARGING,
         "stop-only mode should allow below upper");
  expect(EvaluateChargePolicy(-1, 80, 70, true) == ChargeDecision::ALLOW_CHARGING,
         "invalid health data should fail open");

  int first = 0;
  int second = 0;
  expect(ParseIntegerPair("70,80", &first, &second) && first == 70 && second == 80,
         "valid pair parse failed");
  expect(!ParseIntegerPair("70garbage,80", &first, &second), "trailing garbage must be rejected");
  expect(!ParseIntegerPair("70,80,90", &first, &second), "multiple commas must be rejected");

  return failures == 0 ? 0 : 1;
}
