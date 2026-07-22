#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string_view>

namespace android::base {

class HostLogMessage {
 public:
  HostLogMessage(std::string_view severity, bool includeErrno)
      : severity_(severity), includeErrno_(includeErrno), savedErrno_(errno) {}

  ~HostLogMessage() {
    std::cerr << severity_ << ": " << stream_.str();
    if (includeErrno_) {
      std::cerr << ": " << std::strerror(savedErrno_);
    }
    std::cerr << '\n';
    if (severity_ == "FATAL") {
      std::abort();
    }
  }

  std::ostream &stream() { return stream_; }

 private:
  std::string_view severity_;
  bool includeErrno_;
  int savedErrno_;
  std::ostringstream stream_;
};

inline void InitLogging(char * /*argv*/[]) {}

}  // namespace android::base

#define LOG(severity) \
  ::android::base::HostLogMessage(#severity, false).stream()
#define PLOG(severity) \
  ::android::base::HostLogMessage(#severity, true).stream()
#define CHECK(condition)                                                     \
  if (condition) {                                                           \
  } else                                                                     \
    ::android::base::HostLogMessage("FATAL", false).stream()                \
        << "Check failed: " #condition " "
