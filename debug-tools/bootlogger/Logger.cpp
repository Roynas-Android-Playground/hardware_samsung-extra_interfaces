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
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <iomanip>
#include <limits>
#include <set>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
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
  constexpr static std::string_view LOGC = "logcat";
  struct Data {
    FILE *out_buf;
    FILE *err_buf;
    pid_t pid;
  };
  using HANDLE = std::unique_ptr<Data, void (*)(Data *)>;

  static HANDLE open() {
    HANDLE empty{nullptr, +[](Data * /*file*/) {}};
    std::array<int, 2> out_fds = {};
    std::array<int, 2> err_fds = {};

    if (pipe(out_fds.data()) || pipe(err_fds.data())) {
      PLOG(ERROR) << "Failed to create pipe";
      return empty;
    }

    pid_t pid = vfork();
    if (pid < 0) {
      PLOG(ERROR) << "Failed to fork";
      return empty;
    } else if (pid == 0) {
      dup2(out_fds[1], STDOUT_FILENO);
      dup2(err_fds[1], STDERR_FILENO);
      ::close(out_fds[0]);
      ::close(err_fds[0]);
      execlp(LOGC.data(), LOGC.data(), nullptr);
      _exit(std::numeric_limits<uint8_t>::max());
    } else {
      ::close(out_fds[1]);
      ::close(err_fds[1]);
      int status = 0;
      if (waitpid(pid, &status, WNOHANG) != 0) {
        LOG(ERROR) << "Proc early-exited with error code "
                   << WEXITSTATUS(status);
        return empty;
      }
      LOG(INFO) << "Forked exe " << std::quoted(LOGC) << " with pid: " << pid;
      return {new Data{fdopen(out_fds[0], "r"), fdopen(err_fds[0], "r"), pid},
              &close};
    }
  }
  static void close(Data *data) {
    ::kill(data->pid, SIGTERM);
    ::waitpid(data->pid, nullptr, 0);
    if (data->out_buf != nullptr) {
      ::fflush(data->out_buf);
      ::fclose(data->out_buf);
    }
    if (data->err_buf != nullptr) {
      std::array<char, 64> errbuf{};
      if (::fread(errbuf.data(), errbuf.size(), 1, data->err_buf) != 0) {
        LOG(ERROR) << "standard error output" << errbuf.data();
      }
      ::fflush(data->err_buf);
      ::fclose(data->err_buf);
    }
    delete data;
  }
  template <size_t size>
  static const char *fgets(std::array<char, size> &data, const HANDLE &handle) {
    return ::fgets(data.data(), data.size(), handle->out_buf);
  }
};

struct Dmesg {
  constexpr static std::string_view NAME = "dmesg";
  constexpr static std::string_view FILEC = "/proc/kmsg";
  using HANDLE = std::unique_ptr<FILE, int (*)(FILE *)>;

  static HANDLE open() { return {fopen(FILEC.data(), "r"), &fclose}; }
  template <size_t size>
  static const char *fgets(std::array<char, size> &data, const HANDLE &handle) {
    return ::fgets(data.data(), data.size(), handle.get());
  }
};

struct Filter {
  static bool write(const std::filesystem::path &file,
                    const std::set<std::string> &results) {
    if (results.empty()) {
      return true;
    }
    std::ofstream fileStream(file);
    if (!fileStream.is_open()) {
      PLOG(ERROR) << "Failed to open file: " << file;
      return false;
    }
    fileStream << fmt::format("{}", fmt::join(results, "\n"));
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
                    const std::set<std::string> &results) {
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
      PLOG(ERROR) << "Failed to open file: " << file;
      return false;
    }
    fileStream << fmt::format("{}\n", contexts);
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
        LOG(INFO) << fmt::format(
            "Control message {} was unable to be set for {}", kPropMatch.str(1),
            kPropMatch.str(3));
        return true;
      } else if (kPropMatch.size() == 1) {
        const auto propString = kPropMatch.str(1);
        LOG(INFO) << fmt::format("Couldn't set prop {}", propString);
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
  typename Logger::HANDLE _fp = Logger::open();
  if (_fp == nullptr) {
    LOG(ERROR) << "Failed to open source for logger " << Logger::NAME;
    return;
  }

  // Open log destination
  std::filesystem::path logPath(
      directory / fmt::format("{}-{:%F-%H_%M_%S}.log", Logger::NAME,
                              std::chrono::system_clock::now()));
  std::ofstream logFile(logPath);
  if (!logFile.is_open()) {
    PLOG(ERROR) << "Failed to open " << logPath << " for logging";
    return;
  }

  std::tuple<std::pair<Filters, std::set<std::string>>...> filters{};
  while (*run) {
    const char *ret = Logger::fgets(buf, _fp);
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
                    filter.second.insert(line);
                  }
                }(),
                ...);
          },
          filters);
      logFile << line << '\n';
    }
  }
  _fp.reset();
  logFile.close();

  std::error_code ec;
  if (std::filesystem::file_size(logPath, ec) == 0) {
    std::filesystem::remove(logPath, ec);
    LOG(INFO) << "No log entries found for logger " << Logger::NAME;
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
              bool wrote = FilterType::write(
                  directory / fmt::format("{}.{}-{:%F-%H_%M_%S}.log",
                                          Logger::NAME, FilterType::NAME,
                                          std::chrono::system_clock::now()),
                  filter.second);
              if (!wrote) {
                PLOG(ERROR) << "Failed to write to log file for logger "
                            << Logger::NAME;
              }
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
    logbuf = fmt::format("Boot completed in {:%Mm%Ss}", seconds(x.uptime));
    LOG(INFO) << logbuf;
    WriteStringToFile(logbuf, DEV_KMSG.data());
  }
}

bool delAllAndRecreate(const std::filesystem::path &path) {
  std::error_code ec;

  LOG(INFO) << "Deleting everything in " << path;
  if (fs::is_directory(path, ec)) {
    fs::remove_all(path, ec);
    if (ec) {
      LOG(ERROR) << fmt::format("Failed to remove directory '{}': {}",
                                path.string(), ec.message());
      return false;
    }
  }
  LOG(INFO) << "Recreating directory...";
  if (!fs::create_directories(path, ec) && ec) {
    LOG(ERROR) << fmt::format("Failed to create directory '{}': {}",
                              path.string(), ec.message());
    return false;
  }
  return true;
}
} // namespace

int main(int argc, char **argv) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  bool system_log = false;
  std::mutex lock;
  fs::path kLogDir;

  android::base::InitLogging(argv);

  umask(022);

  if (argc != 3) {
    fmt::print(stderr, "Usage: {} [log directory] [directory name]\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogDir = argv[1];
  if (kLogDir.empty()) {
    fmt::print(stderr, "{}: Invalid empty string for log directory\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogDir /= argv[2];

  if (getenv("LOGGER_MODE_SYSTEM") != nullptr) {
    LOG(INFO) << "Running in system log mode";
    system_log = true;
  }

  LOG(INFO) << fmt::format("Logger starting with logdir '{}'...",
                           kLogDir.string());

  // Determine audit support
  bool has_audit = false;
  if (KernelConfigType kConfig; ReadKernelConfig(kConfig) == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      LOG(INFO) << "Detected CONFIG_AUDIT=y in kernel configuration";
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
  }
  LOG(INFO) << "Woke up, waiting for threads to finish";
  run = false;
  for (auto &i : threads) {
    i.join();
  }
  LOG(INFO) << "Logger stopped";
  return 0;
}
