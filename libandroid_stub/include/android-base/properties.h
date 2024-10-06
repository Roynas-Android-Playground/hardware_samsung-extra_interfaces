#include <chrono>
#include <map>
#include <string>
#include <thread>

namespace android::base {

inline std::map<std::string_view, std::string_view> kProperties{
    {"ro.build.version.release", "9"},
    {"ro.product.model", "Pixel 4"},
    {"ro.build.version.codename", "REL"},
    {"ro.build.version.incremental", "OP7T10_190720"},
    {"ro.build.id", "OP7T10_190720"},
    {"ro.product.manufacturer", "Google"},
    {"ro.product.name", "Pixel_4"},
    {"ro.hardware", "qcom"},
    {"ro.build.fingerprint",
     "google/OP7T10/OP7T10:10/QPP2A.190710.007/7335553:user/release-keys"},
};

// Returns the current value of the system property `key`,
// or `default_value` if the property is empty or doesn't exist.
inline std::string GetProperty(const std::string &key,
                               const std::string &default_value) {
  auto it = kProperties.find(key);
  if (it != kProperties.end()) {
    return std::string(it->second);
  }
  return default_value;
}

// Returns true if the system property `key` has the value "1", "y", "yes",
// "on", or "true", false for "0", "n", "no", "off", or "false", or
// `default_value` otherwise.
inline bool GetBoolProperty(const std::string &key, bool default_value) {
  std::string value = GetProperty(key, "");
  if (value == "1" || value == "y" || value == "yes" || value == "on" ||
      value == "true") {
    return true;
  }
  if (value == "0" || value == "n" || value == "no" || value == "off" ||
      value == "false") {
    return false;
  }
  return default_value;
}

// This is a mock anyway, no need to get serious
inline bool WaitForProperty(const std::string &key, const std::string &expected_value,
                     std::chrono::milliseconds relative_timeout =
                         std::chrono::milliseconds::max()) {
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return true;
}

} // namespace android::base