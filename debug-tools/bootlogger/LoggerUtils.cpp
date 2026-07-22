#include "LoggerInternal.h"

#include <cctype>
#include <system_error>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace fs = std::filesystem;

bool IsSafeCaptureName(std::string_view name) {
  if (name.empty() || name == "." || name == ".." ||
      !std::isalnum(static_cast<unsigned char>(name.front()))) {
    return false;
  }
  for (const char c : name) {
    const auto ch = static_cast<unsigned char>(c);
    if (!std::isalnum(ch) && c != '-' && c != '_' && c != '.') {
      return false;
    }
  }
  return name.find('/') == std::string_view::npos &&
         name.find('\\') == std::string_view::npos;
}

bool PrepareCaptureDirectory(const fs::path &root, std::string_view name,
                             std::size_t retainedCaptures, fs::path *current,
                             std::string *error) {
  if (current == nullptr || retainedCaptures == 0 ||
      !IsSafeCaptureName(name)) {
    if (error != nullptr) {
      *error = "invalid capture name or retention count";
    }
    return false;
  }

  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create log root: " + ec.message();
    }
    return false;
  }
  if (::chmod(root.c_str(), 0700) != 0) {
    if (error != nullptr) {
      *error = "failed to set log root permissions";
    }
    return false;
  }

  const fs::path base = root / std::string(name);
  const auto rotated = [&base](std::size_t index) {
    return fs::path(base.string() + "." + std::to_string(index));
  };

  if (retainedCaptures > 1) {
    fs::remove_all(rotated(retainedCaptures - 1), ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to remove oldest capture: " + ec.message();
      }
      return false;
    }

    for (std::size_t index = retainedCaptures - 1; index > 1; --index) {
      const fs::path source = rotated(index - 1);
      if (!fs::exists(source, ec)) {
        ec.clear();
        continue;
      }
      fs::rename(source, rotated(index), ec);
      if (ec) {
        if (error != nullptr) {
          *error = "failed to rotate capture: " + ec.message();
        }
        return false;
      }
    }

    if (fs::exists(base, ec)) {
      fs::rename(base, rotated(1), ec);
      if (ec) {
        if (error != nullptr) {
          *error = "failed to rotate current capture: " + ec.message();
        }
        return false;
      }
    }
  } else {
    fs::remove_all(base, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to remove prior capture: " + ec.message();
      }
      return false;
    }
  }

  fs::create_directories(base, ec);
  if (ec || ::chmod(base.c_str(), 0700) != 0) {
    if (error != nullptr) {
      *error = ec ? "failed to create capture directory: " + ec.message()
                  : "failed to set capture directory permissions";
    }
    return false;
  }

  *current = base;
  return true;
}

bool HasMinimumFreeSpace(const fs::path &path, std::uintmax_t minimumBytes) {
  struct statvfs info {};
  if (::statvfs(path.c_str(), &info) != 0) {
    return false;
  }
  const auto available = static_cast<std::uintmax_t>(info.f_bavail) *
                         static_cast<std::uintmax_t>(info.f_frsize);
  return available >= minimumBytes;
}
