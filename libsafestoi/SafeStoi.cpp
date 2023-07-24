#include "SafeStoi.h"

#include <iostream>
#include <sstream>

std::optional<int> stoi_safe(const std::string& str) {
  std::istringstream iss(str);
  int value;
  if (!(iss >> value)) {
    return std::nullopt;
  }
  return std::optional(value);
}
