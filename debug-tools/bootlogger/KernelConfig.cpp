#include <sys/stat.h>
#include <zlib.h>

#include <cstdio>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "LoggerInternal.h"

size_t getPageSize() {
  static size_t pagesize = sysconf(_SC_PAGESIZE);
  return pagesize;
}

static constexpr char kProcConfigGz[] = "/proc/config.gz";

static int ReadConfigGz(std::string &out) {
  char buf[BUF_SIZE];
  size_t len;
  gzFile f = gzopen(kProcConfigGz, "rb");
  if (f == nullptr) {
    PLOGE("gzopen");
    return -errno;
  }
  while ((len = gzread(f, buf, sizeof(buf)))) {
    out.append(buf, len);
  }
  if (len < 0) {
    int errnum;
    const char *errmsg = gzerror(f, &errnum);
    ALOGE("Could not read %s, %s", kProcConfigGz, errmsg);
    return (errnum == Z_ERRNO ? -errno : errnum);
  }
  gzclose(f);
  return 0;
}

static bool parseOneConfigLine(
    const std::string &line,
    std::unordered_map<std::string, ConfigValue> &outvec) {
  static const std::regex kDisabledConfig(R"(^#\sCONFIG_\w+ is not set$)");
  static const std::regex kEnabledConfig(R"(^CONFIG_\w+=(y|m|(")?(.+)?(")?)$)");
  static const auto flags = std::regex_constants::format_sed;
  std::string config;
  bool ret = false;
  ConfigValue value = ConfigValue::UNKNOWN;

  ret = std::regex_match(line, kEnabledConfig, flags);
  if (ret) {
    char c = line[line.find('=') + 1];
    switch (c) {
      case 'y':
        value = ConfigValue::BUILT_IN;
        break;
      case 'm':
        value = ConfigValue::MODULE;
        break;
      case '"':
        value = ConfigValue::STRING;
        break;
      case '-':  // Minus
      case '0' ... '9':
        value = ConfigValue::INT;
        break;
      default:
        ALOGW("Unknown config value: %c", c);
        return ret;
    };
  } else {
    ret = std::regex_match(line, kDisabledConfig, flags);
    if (ret) {
      value = ConfigValue::UNSET;
    } else {
      // Is it a comment or newline?
      // Assume OK first
      ret = true;
      if (!line.empty()) {
        ret = line.front() == '#';
        if (!ret) {
          ALOGW("Unparsable line: '%s'", line.c_str());
        }
      }
      return ret;
    }
  }
  // Trim out CONFIG_* part
  switch (value) {
    case BUILT_IN:
    case MODULE:
    case STRING:
    case INT:
      // CONFIG_AAA=y
      // = symbol being the delimiter
      config = line.substr(0, line.find_first_of('='));
      break;
    case UNSET:
      // # CONFIG_AAA is not set
      // Space after AAA being the delimiter.
      // '# ', size 2
      config = line.substr(2).substr(0, config.find_first_of(' '));
      break;
    case UNKNOWN:
      __builtin_unreachable();
      break;
  }

  outvec.emplace(config, value);
  return ret;
}

int ReadKernelConfig(KernelConfig_t &out) {
  struct stat statbuf {};
  std::string buf, line;
  std::stringstream ss;
  int rc = 0, lines = 0;

  // Determine config.gz size
  rc = stat(kProcConfigGz, &statbuf);
  if (rc < 0) {
    PLOGE("stat");
    return -errno;
  }
  // Linux uses gzip -9 ratio to compress, which has average ratio of 21%
  // Reserve string buffer size to avoid realloc's
  buf.reserve(statbuf.st_size * 5);
  rc = ReadConfigGz(buf);
  if (rc < 0) {
    return rc;
  }
  // Clear if there was anything
  out.clear();
  // Determine map size by newlines
  for (const char c : buf)
    if (c == '\n') lines++;
  // Avoid unnessary reallocs (Kernel configurations are a lot)
  out.reserve(lines);
  // Parse line by line
  ss = std::stringstream(buf);
  while (std::getline(ss, line)) {
    // Returns true (1) on success, so invert it to
    // make use of bitwise OR
    rc |= !parseOneConfigLine(line, out);
  }
  // If any of them returned false, rc would be 1
  if (rc) {
    ALOGW("Error(s) were found parsing '%s'", kProcConfigGz);
  }
  return rc;
}
