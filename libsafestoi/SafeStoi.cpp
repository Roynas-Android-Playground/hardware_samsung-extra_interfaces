#include "SafeStoi.h"

#include <sstream>

int stoi_safe(const std::string& str, const int fallback) {
  std::istringstream iss(str);
  int value;
  if (!(iss >> value)) {
    return fallback;
  }
  return value;
}
