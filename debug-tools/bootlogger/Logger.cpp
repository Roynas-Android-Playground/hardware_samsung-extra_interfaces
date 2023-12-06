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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <functional>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LoggerInternal.h"

using android::base::GetProperty;
using android::base::GetBoolProperty;
using android::base::WaitForProperty;
using android::base::WriteStringToFile;
using std::chrono_literals::operator""s; // NOLINT (misc-unused-using-decls)

namespace fs = std::filesystem;

// TODO is there anything better than global variable?
static fs::path kLogDir;

// Base context for outputs with file
struct OutputContext {
  // File path (absolute)  of this context.
  // Note that .txt suffix is auto appended in constructor.
  std::string kFilePath;
  // Just the filename only
  std::string kFileName;

  // Takes one argument 'filename' without file extension
  OutputContext(const std::string &filename) : kFileName(filename) {
    auto localLogDir = kLogDir; // Copy-construct
    kFilePath = localLogDir.append(kFileName + ".txt").string();
  }

  // Takes two arguments 'filename' and is_filter
  OutputContext(const std::string &filename, const bool isFilter)
      : OutputContext(filename) {
    is_filter = isFilter;
  }

  // No default constructor
  OutputContext() = delete;

  /**
   * Open outfilestream.
   */
  bool openOutput(void) {
    const char *kFilePathStr = kFilePath.c_str();
    ALOGI("%s: Opening '%s'%s", __func__, kFilePathStr, is_filter ? " (filter)" : "");
    fd = open(kFilePathStr, O_RDWR | O_CREAT, 0644);
    if (fd < 0)
      PLOGE("Failed to open '%s'", kFilePathStr);
    return fd >= 0;
  }

  /**
   * Writes the string to this context's file
   *
   * @param string data
   */
  void writeToOutput(const std::string &data) {
    len += write(fd, data.c_str(), data.size());
    len += write(fd, "\n", 1);
    if (len > BUF_SIZE) {
      fsync(fd);
      len = 0;
    }
  }

  operator bool() const { return fd >= 0; }

  /**
   * To be called on ~OutputContext Sub-classes
   */
  void cleanupOutputCtx() {
    struct stat buf {};
    int rc = fstat(fd, &buf);
    if (rc == 0 && buf.st_size == 0) {
      ALOGD("Deleting '%s' because it is empty", kFilePath.c_str());
      std::remove(kFilePath.c_str());
    }
    close(fd);
    fd = -1;
  }

  virtual ~OutputContext() {}

private:
  int fd = -1;
  int len = 0;
  bool is_filter = false;
};

/**
 * Filter support to LoggerContext's stream and outputting to a file.
 */
struct LogFilterContext {
  // Function to be invoked to filter
  // Note: The line passed to this function is modifiable.
  virtual bool filter(const std::string &line) const = 0;
  // Filter name, must be a vaild file name itself.
  std::string kFilterName;
  // Provide a single constant for regEX usage
  const std::regex_constants::match_flag_type kRegexMatchflags =
      std::regex_constants::format_sed;
  // Constructor accepting filtername
  LogFilterContext(const std::string &name) : kFilterName(name) {}
  // No default one
  LogFilterContext() = delete;
  // Virtual dtor
  virtual ~LogFilterContext() {}
};

struct LoggerContext : OutputContext {
  /**
   * Opens the log file stream handle
   *
   * @return FILE* handle
   */
  virtual FILE *openSource(void) = 0;

  /**
   * Closes log file stream handle and does cleanup
   *
   * @param fp The file stream to close and cleanup. NonNull.
   */
  virtual void closeSource(FILE *fp) = 0;

  /**
   * Register a LogFilterContext to this stream.
   *
   * @param ctx The context to register
   */
  void registerLogFilter(std::shared_ptr<LogFilterContext> ctx) {
    if (ctx) {
      ALOGD("%s: registered filter '%s' to '%s' logger", __func__,
            ctx->kFilterName.c_str(), name.c_str());
      filters.emplace(
          ctx, std::make_unique<OutputContext>(ctx->kFilterName + '.' + name,
                                               /*isFilter*/ true));
    }
  }

  /**
   * Start the associated logger
   *
   * @param run Pointer to run/stop control variable
   */
  void startLogger(std::atomic_bool *run) {
    char buf[BUF_SIZE];
    auto fp = openSource();
    if (fp) {
      memset(buf, 0, BUF_SIZE);
      if (openOutput()) {
        for (auto &f : filters) {
          f.second->openOutput();
        }
        // Erase failed-to-open contexts
        for (auto it = filters.begin(), last = filters.end(); it != last;) {
          if (!it->second.get())
            it = filters.erase(it);
          else
            ++it;
        }
        while (*run) {
          auto ret = fgets(buf, sizeof(buf), fp);
          std::istringstream ss(buf);
          std::string line;
          if (ret) {
            while (std::getline(ss, line)) {
              for (auto &f : filters) {
                std::string fline = line;
                if (f.first->filter(fline))
                  f.second->writeToOutput(fline);
              }
              writeToOutput(line);
            }
          }
        }
        for (auto &f : filters) {
          f.second->cleanupOutputCtx();
        }
        // ofstream will auto close
      } else {
        PLOGE("[Context %s] Opening output '%s'", name.c_str(),
              kFilePath.c_str());
      }
      closeSource(fp);
    } else {
      PLOGE("[Context %s] Opening source", name.c_str());
    }
  }
  LoggerContext(const std::string &fname) : OutputContext(fname), name(fname) {
    ALOGD("%s: Logger context '%s' created", __func__, fname.c_str());
  }
  virtual ~LoggerContext(){};

private:
  std::string name;
  std::unordered_map<std::shared_ptr<LogFilterContext>,
                     std::unique_ptr<OutputContext>>
      filters;
};

// DMESG
struct DmesgContext : LoggerContext {
  FILE *openSource(void) override { return fopen("/proc/kmsg", "r"); }
  void closeSource(FILE *fp) override { fclose(fp); }
  DmesgContext() : LoggerContext("kmsg") {}
  ~DmesgContext() override { cleanupOutputCtx(); }
};

// Logcat
struct LogcatContext : LoggerContext {
  FILE *openSource(void) override { return popen("/system/bin/logcat", "r"); }
  void closeSource(FILE *fp) override { pclose(fp); }
  LogcatContext() : LoggerContext("logcat") {}
  ~LogcatContext() override { cleanupOutputCtx(); }
};

// Filters - AVC
struct AvcFilterContext : LogFilterContext {
  bool filter(const std::string &line) const override {
    // Matches "avc: denied { ioctl } for comm=..." for example
    const static auto kAvcMessageRegEX =
        std::regex(R"(avc:\s+denied\s+\{\s\w+\s\}\sfor\s)");
    bool match = std::regex_search(line, kAvcMessageRegEX, kRegexMatchflags);
    if (match && _ctx) {
      const std::lock_guard<std::mutex> _(_lock);
      parseOneAvcContext(line, *_ctx);
    }
    return match;
  }
  std::shared_ptr<AvcContexts> _ctx;
  std::mutex& _lock;
  AvcFilterContext(std::shared_ptr<AvcContexts> ctx, std::mutex& lock) :
    LogFilterContext("avc"), _ctx(ctx), _lock(lock) {}
  AvcFilterContext() = delete;
  ~AvcFilterContext() override = default;
};

// Filters - libc property
struct libcPropFilterContext : LogFilterContext {
  bool filter(const std::string &line) const override {
    // libc : Access denied finding property "
    const static auto kPropertyAccessRegEX =
        std::regex(R"(libc\s+:\s+\w+\s\w+\s\w+\s\w+\s\")");
    static std::vector<std::string> propsDenied;
    std::smatch kPropMatch;

    // Matches "libc : Access denied finding property ..."
    if (std::regex_search(line, kPropMatch, kPropertyAccessRegEX,
                          kRegexMatchflags)) {
      // Trim property name from "property: \"ro.a.b\""
      // line: property "{prop name}"
      std::string prop = kPropMatch.suffix();
      // line: {prop name}"
      prop = prop.substr(0, prop.find_first_of('"'));
      // Starts with ctl. ?
      if (prop.find("ctl.") == 0)
        return true;
      // Cache the properties
      if (std::find(propsDenied.begin(), propsDenied.end(), prop) == propsDenied.end()) {
        propsDenied.emplace_back(prop);
        return true;
      }
    }
    return false;
  }
  libcPropFilterContext() : LogFilterContext("libc_props") {}
  ~libcPropFilterContext() override = default;
};

using std::chrono::duration_cast;

static void recordBootTime() {
  struct sysinfo x;
  std::chrono::seconds uptime;
  std::string logbuf;

  if ((sysinfo(&x) == 0)) {
     uptime = std::chrono::seconds(x.uptime);
     logbuf = LOG_TAG ": Boot completed in ";
     auto mins = duration_cast<std::chrono::minutes>(uptime);
     if (mins.count() > 0)
       logbuf += std::to_string(mins.count()) + 'm' + ' ';
     logbuf += std::to_string((uptime - duration_cast<std::chrono::seconds>(mins)).count()) + 's';
     WriteStringToFile(logbuf, "/dev/kmsg");
  }
}

int main(int argc, const char** argv) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  std::error_code ec;
  std::string kLogRoot;
  KernelConfig_t kConfig;
  bool system_log = false;
  int rc;
  std::mutex lock;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s [log directory]\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogRoot = argv[1];
  if (kLogRoot.empty()) {
    fprintf(stderr, "%s: Invaild empty string for log directory\n", argv[0]);
    return EXIT_FAILURE;
  }
  kLogDir = fs::path(kLogRoot);

  if (GetProperty("sys.boot_completed", "") == "1") {
     ALOGI("Running in system log mode");
     system_log = true;
  }
  if (system_log)
     kLogDir.append("system");
  else
     kLogDir.append("boot");

  DmesgContext kDmesgCtx;
  LogcatContext kLogcatCtx;
  auto kAvcCtx = std::make_shared<std::vector<AvcContext>>();
  auto kAvcFilter = std::make_shared<AvcFilterContext>(kAvcCtx, lock);
  auto kLibcPropsFilter = std::make_shared<libcPropFilterContext>();
  bool ever_removed = false;

  ALOGI("Logger starting with logdir '%s' ...", kLogDir.c_str());

  for (auto const& ent : fs::directory_iterator(kLogRoot, ec)) {
    if (fs::is_directory(ent, ec))
      fs::remove_all(ent, ec);
    else
      fs::remove(ent, ec);
    if (ec) {
      ALOGW("Cannot remove '%s': %s", ent.path().string().c_str(), ec.message().c_str());
      ec.clear();
    } else {
      ever_removed = true;
    }
  }
  // If error_code is set here, it means from the directory_iterator,
  // as error code is always cleared if failure inside the loop.
  if (ec || !ever_removed) {
    ALOGE("Failed to remove files in log directory: %s", ec.message().c_str());
    ec.clear();
  } else {
    ALOGI("Cleared log directory files");
  }

  // Determine audit support
  rc = ReadKernelConfig(kConfig);
  if (rc == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      ALOGD("Detected CONFIG_AUDIT=y in kernel configuration");
    } else {
      ALOGI("Kernel configuration does not have CONFIG_AUDIT=y, disabling avc filters.");
      kAvcFilter.reset();
      kAvcCtx.reset();
    }
  }

  // Create log dir again
  fs::create_directory(kLogDir, ec);
  if (ec) {
     ALOGE("Failed to create directory '%s': %s", kLogDir.c_str(), ec.message().c_str());
     return EXIT_FAILURE;
  }
  run = true;

  // If this prop is true, logd logs kernel message to logcat
  // Don't make duplicate (Also it will race against kernel logs)
  if (!GetBoolProperty("ro.logd.kernel", false)) {
    kDmesgCtx.registerLogFilter(kAvcFilter);
    threads.emplace_back(std::thread([&] { kDmesgCtx.startLogger(&run); }));
  }
  kLogcatCtx.registerLogFilter(kAvcFilter);
  kLogcatCtx.registerLogFilter(kLibcPropsFilter);
  threads.emplace_back(std::thread([&] { kLogcatCtx.startLogger(&run); }));

  if (system_log) {
    WaitForProperty("persist.ext.logdump.enabled", "false");
  } else {
    WaitForProperty("sys.boot_completed", "1");
    recordBootTime();

    // Delay a bit to finish
    std::this_thread::sleep_for(3s);
  }
  run = false;
  for (auto &i : threads)
    i.join();

  if (kAvcCtx) {
    std::vector<std::string> allowrules;
    std::stringstream iss;
    for (const auto& e : *kAvcCtx) {
      std::string line;
      writeAllowRules(e, line);
      allowrules.emplace_back(line);
    }
    std::sort(allowrules.begin(), allowrules.end());
    allowrules.erase(std::unique(allowrules.begin(), allowrules.end()), allowrules.end());
    for (const auto& l : allowrules)
      iss << l;
    auto rules = kLogDir;
    rules.append("rules.autogen.te");
    WriteStringToFile(iss.str(), rules);
  }
  return 0;
}
