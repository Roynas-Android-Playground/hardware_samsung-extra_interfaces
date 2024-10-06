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

#include <android-base/file.h>
#include <android-base/properties.h>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <system_error>
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

// Base context for outputs with file
struct OutputContext {
  // File path (absolute)  of this context.
  // Note that .txt suffix is auto appended in constructor.
  std::filesystem::path kFilePath;

  // Takes one argument 'filename' without file extension
  OutputContext(const fs::path &logDir, const std::string_view filename,
                const std::string_view filtername = "") {

    std::string craftedFilename;
    if (!filtername.empty()) {
      craftedFilename = std::string(filtername).append(".").append(filename);
    } else {
      craftedFilename = filename;
    }
    kFilePath = logDir / craftedFilename.append(".txt");

    ALOGI("%s: Opening '%s'%s", __func__, kFilePath.c_str(),
          !filtername.empty() ? " (filter)" : "");
    ofs.open(kFilePath);

    if (!ofs) {
      PLOGE("Failed to open '%s'", kFilePath.c_str());
    }
  }

  // No default constructor
  OutputContext() = delete;

  /**
   * Writes the string to this context's file
   *
   * @param string data
   */
  OutputContext &operator<<(const std::string_view &data) {
    len += data.size();
    if (len > BUF_SIZE) {
      ofs.flush();
      len = 0;
    }
    ofs << data << "\n";
    return *this;
  }

  operator bool() const { return static_cast<bool>(ofs); }

  /**
   * Cleanup
   */
  ~OutputContext() {
    std::error_code ec;
    const auto rc = std::filesystem::file_size(kFilePath, ec);
    if (!ec && rc == 0) {
      ALOGD("Deleting '%s' because it is empty", kFilePath.c_str());
      std::filesystem::remove(kFilePath);
    }
  }

private:
  std::ofstream ofs;
  size_t len = 0;
};

/**
 * Filter support to LoggerContext's stream and outputting to a file.
 */
struct LogFilterContext {
  // Function to be invoked to filter
  virtual bool filter(const std::string &line) const = 0;
  // Constructor accepting filtername
  explicit LogFilterContext(std::string name) : kFilterName(std::move(name)) {}
  // No default one
  LogFilterContext() = delete;
  // Virtual dtor
  virtual ~LogFilterContext() = default;
  // Log filter name
  [[nodiscard]] std::string_view name() const { return kFilterName; }

protected:
  // Provide a single constant for regEX usage
  constexpr static std::regex_constants::match_flag_type kRegexMatchflags =
      std::regex_constants::format_sed;
  // Filter name, must be a vaild file name itself.
  std::string kFilterName;
};

struct LoggerContext : OutputContext {

  /**
   * Register a LogFilterContext to this stream.
   *
   * @param ctx The context to register
   */
  void registerLogFilter(const fs::path &logDir,
                         const std::shared_ptr<LogFilterContext> &ctx) {
    if (ctx) {
      filters[ctx] = std::make_unique<OutputContext>(logDir, name, ctx->name());
    }
  }

  /**
   * Start the associated logger
   *
   * @param run Pointer to run/stop control variable
   */
  void startLogger(std::atomic_bool *run) {
    std::array<char, 512> buf = {0};
    if (_fp != nullptr) {
      // Erase failed-to-open contexts
      for (auto it = filters.begin(), last = filters.end(); it != last;) {
        if (!it->second) {
          it = filters.erase(it);
        } else {
          ++it;
        }
      }
      while (*run) {
        const auto *ret = fgets(buf.data(), sizeof(buf), _fp.get());
        std::istringstream ss(buf.data());
        std::string line;
        if (ret != nullptr) {
          while (std::getline(ss, line)) {
            for (auto &f : filters) {
              if (f.first->filter(line)) {
                *f.second << line;
              }
            }
            *this << line;
          }
        }
      }
      // ofstream will auto close
    } else {
      ALOGE("[Context %s] Opening output '%s'", name.c_str(),
            kFilePath.c_str());
    }
  }

private:
  std::string name;
  std::unordered_map<std::shared_ptr<LogFilterContext>,
                     std::unique_ptr<OutputContext>>
      filters;
  std::unique_ptr<FILE, std::function<void(FILE *)>> _fp;

public:
  LoggerContext(decltype(_fp) fp, const fs::path &logDir, std::string name)
      : OutputContext(logDir, name), name(std::move(name)), _fp(std::move(fp)) {
  }
  using FileHandle = decltype(_fp);
};

// Filters - AVC
struct AvcFilterContext : LogFilterContext {
  bool filter(const std::string &line) const override {
    // Matches "avc: denied { ioctl } for comm=..." for example
    const static auto kAvcMessageRegEX =
        std::regex(R"(avc:\s+denied\s+\{(\s\w+)+\s\}\sfor\s)");
    bool match = std::regex_search(line, kAvcMessageRegEX, kRegexMatchflags);
    match &= line.find("untrusted_app") == std::string::npos;
    if (match && _ctx) {
      const std::lock_guard<std::mutex> _(_lock);
      _ctx->emplace_back(line);
    }
    return match;
  }
  std::shared_ptr<AvcContexts> _ctx;
  std::mutex &_lock;
  AvcFilterContext(std::shared_ptr<AvcContexts> ctx, std::mutex &lock)
      : LogFilterContext("avc"), _ctx(std::move(ctx)), _lock(lock) {}
  AvcFilterContext() = delete;
  ~AvcFilterContext() override = default;
};

// Filters - libc property
struct libcPropFilterContext : LogFilterContext {
  bool filter(const std::string &line) const override {
    // libc : Access denied finding property "
    const static auto kPropertyAccessRegEX = std::regex(
        R"(libc\s+:\s+\w+\s\w+\s\w+\s\w+\s(\"[a-zA-z.]+\")( to \"([a-zA-z0-9.@:\/]+)\")?)");
    static std::set<std::string> propsDenied;
    std::smatch kPropMatch;

    // Matches "libc : Access denied finding property ..."
    if (std::regex_search(line, kPropMatch, kPropertyAccessRegEX,
                          kRegexMatchflags)) {
      if (kPropMatch.size() == 3) {
        ALOGI("Control message %s was unable to be set for %s",
              kPropMatch.str(1).c_str(), kPropMatch.str(3).c_str());
        return true;
      } else if (kPropMatch.size() == 1) {
        const auto propString = kPropMatch.str(1);
        ALOGI("Couldn't set prop %s", propString.c_str());
        if (propsDenied.find(propString) != propsDenied.end()) {
          return false;
        }
        propsDenied.insert(propString);
        return true;
      }
    }
    return false;
  }
  libcPropFilterContext() : LogFilterContext("libc_props") {}
  ~libcPropFilterContext() override = default;
};

namespace {
// Logcat
constexpr std::string_view LOGCAT_EXE = "/system/bin/logcat";

void recordBootTime() {
  struct sysinfo x {};
  std::chrono::seconds uptime;
  std::string logbuf;
  using std::chrono::duration_cast;
  using std::chrono::minutes;
  using std::chrono::seconds;

  if ((sysinfo(&x) == 0)) {
    uptime = std::chrono::seconds(x.uptime);
    logbuf = LOG_TAG ": Boot completed in ";
    auto mins = duration_cast<minutes>(uptime);
    if (mins.count() > 0) {
      logbuf += std::to_string(mins.count()) + 'm' + ' ';
    }
    logbuf +=
        std::to_string((uptime - duration_cast<seconds>(mins)).count()) + 's';
    WriteStringToFile(logbuf, "/dev/kmsg");
  }
}

bool delAllAndRecreate(const std::filesystem::path &path) {
  std::error_code ec;

  ALOGI("Deleting everything in %s", path.string().c_str());
  if (fs::is_directory(path)) {
    fs::remove_all(path, ec);
    if (ec) {
      PLOGE("Failed to remove directory '%s': %s", path.string().c_str(),
            ec.message().c_str());
      return false;
    }
  }
  ALOGI("Recreating directory...");
  fs::create_directories(path, ec);
  if (ec) {
    PLOGE("Failed to create directory '%s': %s", path.string().c_str(),
          ec.message().c_str());
    return false;
  }
  return true;
}
} // namespace

int main(int argc, const char **argv) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  std::error_code ec;
  KernelConfigType kConfig;
  bool system_log = false;
  int rc;
  std::mutex lock;
  fs::path kLogDir;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s [log directory]\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogDir = argv[1];
  if (kLogDir.empty()) {
    fprintf(stderr, "%s: Invalid empty string for log directory\n", argv[0]);
    return EXIT_FAILURE;
  }
  umask(022);

  if (getenv("LOGGER_MODE_SYSTEM") != nullptr) {
    ALOGI("Running in system log mode");
    system_log = true;
  }
  if (system_log) {
    kLogDir /= "system";
  } else {
    kLogDir /= "boot";
  }

  auto kAvcCtx = std::make_shared<std::vector<AvcContext>>();
  auto kAvcFilter = std::make_shared<AvcFilterContext>(kAvcCtx, lock);
  auto kLibcPropsFilter = std::make_shared<libcPropFilterContext>();
  ALOGI("Logger starting with logdir '%s' ...", kLogDir.c_str());

  // Determine audit support
  rc = ReadKernelConfig(kConfig);
  if (rc == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      ALOGD("Detected CONFIG_AUDIT=y in kernel configuration");
    } else {
      ALOGI("Kernel configuration does not have CONFIG_AUDIT=y, disabling avc "
            "filters.");
      kAvcFilter.reset();
      kAvcCtx.reset();
    }
  }

  if (!delAllAndRecreate(kLogDir)) {
    return EXIT_FAILURE;
  }

  run = true;
  LoggerContext kDmesgCtx = {
      LoggerContext::FileHandle(fopen("/proc/kmsg", "r"), fclose), kLogDir,
      "dmesg"};
  LoggerContext kLogcatCtx = {
      LoggerContext::FileHandle(popen(LOGCAT_EXE.data(), "r"), pclose), kLogDir,
      "logcat"};

  // If this prop is true, logd logs kernel message to logcat
  // Don't make duplicate (Also it will race against kernel logs)
  if (!GetBoolProperty("ro.logd.kernel", false)) {
    kDmesgCtx.registerLogFilter(kLogDir, kAvcFilter);
    threads.emplace_back([&] { kDmesgCtx.startLogger(&run); });
  }
  kLogcatCtx.registerLogFilter(kLogDir, kAvcFilter);
  kLogcatCtx.registerLogFilter(kLogDir, kLibcPropsFilter);
  threads.emplace_back([&] { kLogcatCtx.startLogger(&run); });

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

  if (kAvcCtx) {
    std::vector<std::string> allowrules;
    OutputContext seGenCtx(kLogDir, "sepolicy.gen");

    if (!seGenCtx) {
      ALOGE("Failed to create sepolicy.gen");
      return EXIT_FAILURE;
    }

    for (auto &e1 : *kAvcCtx) {
      for (auto &e2 : *kAvcCtx) {
        if (&e1 == &e2) {
          continue;
        }
        e1 += e2;
      }
    }
    std::stringstream ss;
    ss << *kAvcCtx;
    seGenCtx << ss.str();
  }
  return 0;
}
