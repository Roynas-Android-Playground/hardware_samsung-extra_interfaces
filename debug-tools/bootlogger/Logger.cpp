/*
 * Copyright 2021 Soo Hwan Na "Royna"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <functional>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "LoggerInternal.h"

using android::base::GetBoolProperty;
using android::base::GetProperty;
using android::base::WaitForProperty;
using android::base::WriteStringToFile;
using std::chrono_literals::operator""s; // NOLINT (misc-unused-using-decls)

namespace fs = std::filesystem;

#define MAKE_LOGGER_PROP(prop) "persist.ext.logdump." prop

struct Logcat {
  constexpr static std::string_view NAME = "logcat";
  constexpr static std::string_view LOGC = "/system/bin/logcat";
  using HANDLE = std::unique_ptr<FILE, int (*)(FILE *)>;

  static HANDLE open() { return {popen(LOGC.data(), "r"), &pclose}; }
  static void close(HANDLE fp) {
    // No-op
  }
};

struct Dmesg {
  constexpr static std::string_view NAME = "dmesg";
  constexpr static std::string_view FILEC = "/proc/kmsg";
  using HANDLE = std::unique_ptr<FILE, int (*)(FILE *)>;

  static HANDLE open() { return {fopen(FILEC.data(), "r"), &fclose}; }
  static void close(HANDLE fp) {
    // No-op
  }
};

struct Filter {
  static bool write(const std::filesystem::path &file,
                    const std::vector<std::string> &results) {
    if (results.empty()) {
      return true;
    }
    std::ofstream fileStream(file);
    if (!fileStream.is_open()) {
      fmt::print("Failed to open file: {}\n", file.string());
      return false;
    }
    for (const auto &result : results) {
      fileStream << result << '\n';
    }
    fileStream.close();
    return true;
  }
};

struct FilterAvc : Filter {
  constexpr static std::string_view NAME = "avc";

  static bool filter(const std::string &line) {
    // Matches "avc: denied { ioctl } for comm=..." for example
    const static auto kAvcMessageRegEX =
        std::regex(R"(avc:\s+denied\s+\{(\s\w+)+\s\}\sfor\s)");
    bool match = std::regex_search(line, kAvcMessageRegEX,
                                   std::regex_constants::format_sed);
    match &= line.find("untrusted_app") == std::string::npos;
    return match;
  }
};

struct FilterAvcGen : Filter {
  constexpr static std::string_view NAME = "sepolicy.gen";

  static bool filter(const std::string &line) {
    AvcContext ctx(line);
    return !ctx.stale;
  }
  static bool write(const std::filesystem::path &file,
                    const std::vector<std::string> &results) {
    if (results.empty()) {
      return true;
    }

    // Translate to AVCContexts vector
    AvcContexts contexts;
    std::transform(results.begin(), results.end(), std::back_inserter(contexts),
                   [](const std::string &file) { return AvcContext(file); });

    // Combine AVC contexts
    for (auto &e1 : contexts) {
      for (auto &e2 : contexts) {
        if (&e1 == &e2) {
          continue;
        }
        e1 += e2;
      }
    }

    // Write to file
    std::ofstream fileStream(file);
    if (!fileStream.is_open()) {
      fmt::print("Failed to open file: {}\n", file.string());
      return false;
    }
    fileStream << contexts;
    fileStream.close();
    return true;
  }
};

struct FilterLibc : Filter {
  constexpr static std::string_view NAME = "libc_properties";

  static bool filter(const std::string &line) {
    // libc : Access denied finding property "
    const static auto kPropertyAccessRegEX = std::regex(
        R"(libc\s+:\s+\w+\s\w+\s\w+\s\w+\s(\"[a-zA-z.]+\")( to \"([a-zA-z0-9.@:\/]+)\")?)");
    static std::set<std::string> propsDenied;
    std::smatch kPropMatch;

    // Matches "libc : Access denied finding property ..."
    if (std::regex_search(line, kPropMatch, kPropertyAccessRegEX,
                          std::regex_constants::format_sed)) {
      if (kPropMatch.size() == 3) {
        fmt::print("Control message {} was unable to be set for {}\n",
                   kPropMatch.str(1), kPropMatch.str(3));
        return true;
      } else if (kPropMatch.size() == 1) {
        const auto propString = kPropMatch.str(1);
        fmt::print("Couldn't set prop {}\n", propString);
        if (propsDenied.find(propString) != propsDenied.end()) {
          return false;
        }
        propsDenied.insert(propString);
        return true;
      }
    }
    return false;
  }
};

/**
 * Start the associated logger
 *
 * @param run Pointer to run/stop control variable
 */
template <typename Logger, typename... Filters>
void start(const std::filesystem::path &directory, std::atomic_bool *run) {
  std::array<char, 512> buf = {0};

  // Open log source
  auto _fp = Logger::open();
  if (_fp == nullptr) {
    fmt::print("Failed to open file for logger {}: {}\n", Logger::NAME,
               strerror(errno));
    Logger::close(std::move(_fp));
    return;
  }

  // Open log destination
  std::filesystem::path logPath(directory /
                                fmt::format("{}-{:%F-%T}.log", Logger::NAME,
                                            std::chrono::system_clock::now()));
  std::ofstream logFile(logPath);
  if (!logFile.is_open()) {
    fmt::print("Failed to open file for logging: {}\n", logPath.string());
    Logger::close(std::move(_fp));
    return;
  }

  std::tuple<std::pair<Filters, std::vector<std::string>>...> filters{};
  while (*run) {
    const char *ret = fgets(buf.data(), sizeof(buf), _fp.get());
    std::istringstream ss(buf.data());
    std::string line;
    if (ret == nullptr) {
      continue;
    }
    while (std::getline(ss, line)) {
      std::apply(
          [&line](auto &...filter) {
            (
                [&] {
                  if (filter.first.filter(line)) {
                    filter.second.emplace_back(line);
                  }
                }(),
                ...);
          },
          filters);
      logFile << line << '\n';
    }
  }
  Logger::close(std::move(_fp));
  logFile.close();

  std::error_code ec;
  if (std::filesystem::file_size(logPath, ec) == 0) {
    std::filesystem::remove(logPath, ec);
    fmt::print("No log entries found for logger {}\n", Logger::NAME);
    return;
  }

  std::apply(
      [&directory](auto &...filter) {
        (
            [&] {
              if (filter.second.empty()) {
                return;
              }
              using FilterType = std::decay_t<decltype(filter.first)>;
              FilterType::write(
                  directory / fmt::format("{}.{}-{:%F-%T}.log", Logger::NAME,
                                          FilterType::NAME,
                                          std::chrono::system_clock::now()),
                  filter.second);
            }(),
            ...);
      },
      filters);
}

namespace {
constexpr std::string_view DEV_KMSG = "/dev/kmsg";

void recordBootTime() {
  struct sysinfo x {};
  std::string logbuf;
  using std::chrono::seconds;

  if ((sysinfo(&x) == 0)) {
    logbuf = fmt::format("bootlogger: Boot completed in {:%Mm%Ss}",
                         seconds(x.uptime));
    WriteStringToFile(logbuf, DEV_KMSG.data());
  }
}

bool delAllAndRecreate(const std::filesystem::path &path) {
  std::error_code ec;

  fmt::print("Deleting everything in {}\n", path.string());
  if (fs::is_directory(path, ec)) {
    fs::remove_all(path, ec);
    if (ec) {
      fmt::print("Failed to remove directory '{}': {}\n", path.string(),
                 ec.message());
      return false;
    }
  }
  puts("Recreating directory...");
  fs::create_directories(path, ec);
  if (ec) {
    fmt::print("Failed to create directory '{}': {}\n", path.string(),
               ec.message());
    return false;
  }
  return true;
}
} // namespace

int main(int argc, const char **argv) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  bool system_log = false;
  std::mutex lock;
  fs::path kLogDir;

  if (argc != 2) {
    fmt::print(stderr, "Usage: {} [log directory]\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogDir = argv[1];
  if (kLogDir.empty()) {
    fmt::print(stderr, "{}: Invalid empty string for log directory\n", argv[0]);
    return EXIT_FAILURE;
  }
  umask(022);

  if (getenv("LOGGER_MODE_SYSTEM") != nullptr) {
    fmt::print("Running in system log mode\n");
    system_log = true;
  }
  if (system_log) {
    kLogDir /= "system";
  } else {
    kLogDir /= "boot";
  }

  fmt::print("Logger starting with logdir '{}' ...\n", kLogDir.string());

  // Determine audit support
  bool has_audit = false;
  if (KernelConfigType kConfig; ReadKernelConfig(kConfig) == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      puts("Detected CONFIG_AUDIT=y in kernel configuration");
      has_audit = true;
    }
  }

  if (!delAllAndRecreate(kLogDir)) {
    return EXIT_FAILURE;
  }

  run = true;
  // If this prop is true, logd logs kernel message to logcat
  // Don't make duplicate (Also it will race against kernel logs)
  if (!GetBoolProperty("ro.logd.kernel", false)) {
    threads.emplace_back([&] {
      if (has_audit) {
        start<Dmesg, FilterAvc, FilterAvcGen>(kLogDir, &run);
      } else {
        start<Dmesg>(kLogDir, &run);
      }
    });
  }
  threads.emplace_back([&] {
    start<Logcat, FilterAvc, FilterAvcGen, FilterLibc>(kLogDir, &run);
  });

  if (system_log) {
    WaitForProperty(MAKE_LOGGER_PROP("enabled"), "false");
  } else {
    WaitForProperty("sys.boot_completed", "1");
    recordBootTime();

    // Delay a bit to finish
    std::this_thread::sleep_for(3s);
  }
  run = false;
  for (auto &i : threads) {
    i.join();
  }
  return 0;
}
