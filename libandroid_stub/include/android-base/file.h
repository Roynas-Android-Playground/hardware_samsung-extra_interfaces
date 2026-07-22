#pragma once

#include <fstream>
#include <string>

namespace android::base {

inline bool WriteStringToFile(const std::string &content, const std::string &path) {
  std::ofstream file(path);
  return static_cast<bool>(file << content);
}

} // namespace android::base