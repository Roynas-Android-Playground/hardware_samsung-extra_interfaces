#include "LoggerInternal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <zlib.h>

#include <array>
#include <sstream>

namespace {

constexpr std::string_view kProcConfigGz = "/proc/config.gz";

int ReadConfigGz(std::string &out) {
  std::array<char, BUF_SIZE> buffer{};
  gzFile file = gzopen(kProcConfigGz.data(), "rb");
  if (file == nullptr) {
    return -errno;
  }

  int length = 0;
  while ((length = gzread(file, buffer.data(),
                          static_cast<unsigned int>(buffer.size()))) > 0) {
    out.append(buffer.data(), static_cast<std::size_t>(length));
  }

  if (length < 0) {
    int zlibError = Z_OK;
    (void)gzerror(file, &zlibError);
    gzclose(file);
    return zlibError == Z_ERRNO ? -errno : zlibError;
  }

  const int closeResult = gzclose(file);
  return closeResult == Z_OK ? 0 : closeResult;
}

bool IsIntegerConfigValue(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  if (value.front() == '-') {
    value.remove_prefix(1);
  }
  if (value.empty()) {
    return false;
  }
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    if (value.empty()) {
      return false;
    }
    for (const char c : value) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        return false;
      }
    }
    return true;
  }
  for (const char c : value) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ParseKernelConfigLine(std::string_view line, std::string *name,
                           ConfigValue *value) {
  if (name == nullptr || value == nullptr) {
    return false;
  }
  name->clear();
  *value = ConfigValue::UNKNOWN;

  if (line.empty() || (line.front() == '#' &&
                       line.rfind("# CONFIG_", 0) != 0)) {
    return true;
  }

  constexpr std::string_view kDisabledPrefix = "# ";
  constexpr std::string_view kDisabledSuffix = " is not set";
  if (line.rfind("# CONFIG_", 0) == 0 &&
      line.size() > kDisabledPrefix.size() + kDisabledSuffix.size() &&
      line.substr(line.size() - kDisabledSuffix.size()) == kDisabledSuffix) {
    const auto config = line.substr(
        kDisabledPrefix.size(),
        line.size() - kDisabledPrefix.size() - kDisabledSuffix.size());
    if (config.empty()) {
      return false;
    }
    *name = std::string(config);
    *value = ConfigValue::UNSET;
    return true;
  }

  if (line.rfind("CONFIG_", 0) != 0) {
    return false;
  }

  const auto delimiter = line.find('=');
  if (delimiter == std::string_view::npos || delimiter == 0 ||
      delimiter + 1 >= line.size()) {
    return false;
  }

  *name = std::string(line.substr(0, delimiter));
  const auto configValue = line.substr(delimiter + 1);
  if (configValue == "y") {
    *value = ConfigValue::BUILT_IN;
  } else if (configValue == "m") {
    *value = ConfigValue::MODULE;
  } else if (configValue.size() >= 2 && configValue.front() == '"' &&
             configValue.back() == '"') {
    *value = ConfigValue::STRING;
  } else if (IsIntegerConfigValue(configValue)) {
    *value = ConfigValue::INT;
  } else {
    name->clear();
    *value = ConfigValue::UNKNOWN;
    return false;
  }
  return true;
}

int ReadKernelConfig(KernelConfigType &out) {
  struct stat statBuffer {};
  if (stat(kProcConfigGz.data(), &statBuffer) < 0) {
    return -errno;
  }

  std::string buffer;
  if (statBuffer.st_size > 0) {
    buffer.reserve(static_cast<std::size_t>(statBuffer.st_size) * 5U);
  }

  const int readResult = ReadConfigGz(buffer);
  if (readResult != 0) {
    return readResult;
  }

  out.clear();
  out.reserve(static_cast<std::size_t>(
      std::count(buffer.begin(), buffer.end(), '\n')));

  std::istringstream stream(buffer);
  std::string line;
  int parseErrors = 0;
  while (std::getline(stream, line)) {
    std::string name;
    ConfigValue value = ConfigValue::UNKNOWN;
    if (!ParseKernelConfigLine(line, &name, &value)) {
      parseErrors = 1;
      continue;
    }
    if (!name.empty()) {
      out.insert_or_assign(std::move(name), value);
    }
  }
  return parseErrors;
}
