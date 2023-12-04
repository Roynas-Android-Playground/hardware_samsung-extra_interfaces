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

struct LoggerContext;

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
    ALOGI("%s: Opening '%s'%s", __func__, kFilePath.c_str(),
          is_filter ? " (filter)" : "");
    std::remove(kFilePath.c_str());
    ofs = std::ofstream(kFilePath);
    bool ok = ofs.good();
    if (!ok)
      PLOGE("Failed to open '%s'", kFilePath.c_str());
    return ok;
  }

  /**
   * Writes the string to this context's file
   *
   * @param string data
   */
  void writeToOutput(const std::string &data) { ofs << data << std::endl; }

  operator bool() const { return ofs.good(); }

  /**
   * To be called on ~OutputContext Sub-classes
   */
  void cleanupOutputCtx() {
    struct stat buf {};
    auto &path = kFilePath;
    int rc = stat(path.c_str(), &buf);
    if (rc == 0 && buf.st_size == 0) {
      ALOGD("Deleting '%s' because it is empty", path.c_str());
      std::remove(path.c_str());
    }
  }

  virtual ~OutputContext() {}

private:
  std::ofstream ofs;
  bool is_filter;
};

/**
 * Filter support to LoggerContext's stream and outputting to a file.
 */
struct LogFilterContext {
  // Function to be invoked to filter
  // Note: The line passed to this function is modifiable.
  virtual bool filter(std::string &line) const = 0;
  // Filter name, must be a vaild file name itself.
  std::string kFilterName;
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
  std::map<std::shared_ptr<LogFilterContext>, std::unique_ptr<OutputContext>>
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
  bool filter(std::string &line) const override {
    const static auto kAvcMessageRegEX =
        std::regex(R"(avc:\s+denied\s+\{\s\w+\s\})");
    // libc : Access denied finding property "
    const static auto kPropertyAccessRegEX =
        std::regex(R"(libc\s+:\s+\w+\s\w+\s\w+\s\w+\s\")");
    static std::vector<std::string> propsDenied;
    std::smatch kPropMatch;
    bool kMatch = false;
    const static auto flags = std::regex_constants::format_sed;

    // Matches "avc: denied { ioctl } for comm=..." for example
    kMatch |= std::regex_search(line, kAvcMessageRegEX, flags);
    // Matches "libc : Access denied finding property ..."
    if (std::regex_search(line, kPropMatch, kPropertyAccessRegEX, flags)) {
      // Trim property name
      // line: property "{prop name}"
      line = line.substr(kPropMatch.length());
      // line: {prop name}"
      line = line.substr(0, line.find_first_of('"'));
      if (std::find(propsDenied.begin(), propsDenied.end(), line) ==
          propsDenied.end()) {
        propsDenied.emplace_back(line);
        kMatch = true;
      }
    }
    return kMatch;
  }
  AvcFilterContext() : LogFilterContext("avc") {}
  ~AvcFilterContext() override = default;
};

int main(void) {
  std::vector<std::thread> threads;
  std::atomic_bool run;
  KernelConfig_t kConfig;
  int rc;

  DmesgContext kDmesgCtx;
  LogcatContext kLogcatCtx;
  std::shared_ptr<AvcFilterContext> kAvcFilter(nullptr);

  ALOGI("Logger starting...");

  rc = ReadKernelConfig(kConfig);
  if (rc == 0) {
    if (kConfig["CONFIG_AUDIT"] == ConfigValue::BUILT_IN) {
      ALOGD("Detected CONFIG_AUDIT=y in kernel configuration");
      kAvcFilter = std::make_shared<AvcFilterContext>();
    } else {
      ALOGI("Kernel configuration does not have CONFIG_AUDIT=y,"
            " disabling avc filters.");
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
  threads.emplace_back(std::thread([&] { kLogcatCtx.startLogger(&run); }));

  WaitForProperty("sys.boot_completed", "1");
  run = false;
  for (auto &i : threads)
    i.join();
  return 0;
}
