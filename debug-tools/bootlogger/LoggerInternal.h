#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#define LOG_TAG "bootlogger"

#include <log/log.h>

// Similar to perror(3)
#define PLOGE(fmt, ...) \
  ALOGE("%s: " fmt ": %s", __func__, ##__VA_ARGS__, strerror(errno))

// Caches sysconf(_SC_PAGESIZE) and returns it
std::size_t getPageSize();
// Alias
#define BUF_SIZE getPageSize()

// KernelConfig.cpp
enum ConfigValue {
  UNKNOWN,   // Should be first for default-initialization
  BUILT_IN,  // =y
  STRING,    // =""
  INT,       // =1
  MODULE,    // =m
  UNSET,     // =n
};

using KernelConfig_t = std::unordered_map<std::string, ConfigValue>;

/**
 * Read KernelConfig (/proc/config.gz)
 * And serializes it to KernelConfig_t object
 *
 * @param out buffer to store
 * @return 0 on success, else non-zero value
 */
int ReadKernelConfig(KernelConfig_t& out);
