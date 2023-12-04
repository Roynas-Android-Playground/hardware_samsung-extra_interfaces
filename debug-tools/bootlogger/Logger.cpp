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

#include <android-base/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LoggerInternal.h"

using android::base::GetBoolProperty;
using android::base::WaitForProperty;

// Base context for outputs with file
struct OutputContext {
  // File path (absolute)  of this context.
  // Note that .txt suffix is auto appended in constructor.
  std::string kFilePath;
  // Just the filename only
  std::string kFileName;

  // Takes one argument 'filename' without file extension
  OutputContext(const std::string &filename) : kFileName(filename) {
    static std::string kLogDir = "/data/debug/";
    kFilePath = kLogDir + kFileName + ".txt";
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
    ALOGI("%s: Opening '%s'%s", __func__, kFilePathStr,
          is_filter ? " (filter)" : "");
    std::remove(kFilePathStr);
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
        std::regex(R"(avc:\s+denied\s+\{\s\w+\s\})");
    return std::regex_search(line, kAvcMessageRegEX, kRegexMatchflags);
  }
  AvcFilterContext() : LogFilterContext("avc") {}
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

int main(void) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  KernelConfig_t kConfig;
  int rc;

  DmesgContext kDmesgCtx;
  LogcatContext kLogcatCtx;
  auto kAvcFilter = std::make_shared<AvcFilterContext>();
  auto kLibcPropsFilter = std::make_shared<libcPropFilterContext>();

  ALOGI("Logger starting...");

  rc = ReadKernelConfig(kConfig);
  if (rc == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      ALOGD("Detected CONFIG_AUDIT=y in kernel configuration");
    } else {
      ALOGI("Kernel configuration does not have CONFIG_AUDIT=y,"
            " disabling avc filters.");
      kAvcFilter.reset();
    }
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

  WaitForProperty("sys.boot_completed", "1");
  run = false;
  for (auto &i : threads)
    i.join();
  return 0;
}
