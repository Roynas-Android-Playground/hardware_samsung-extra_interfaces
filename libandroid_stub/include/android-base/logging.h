#pragma once

#include <absl/base/config.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>
#include <absl/log/log_sink_registry.h>
#include <mutex>

namespace absl {

ABSL_NAMESPACE_BEGIN

namespace log_internal {

template LogMessage &LogMessage::operator<<(const char &v);
template LogMessage &LogMessage::operator<<(const signed char &v);
template LogMessage &LogMessage::operator<<(const unsigned char &v);
template LogMessage &LogMessage::operator<<(const short &v);          // NOLINT
template LogMessage &LogMessage::operator<<(const unsigned short &v); // NOLINT
template LogMessage &LogMessage::operator<<(const int &v);
template LogMessage &LogMessage::operator<<(const unsigned int &v);
template LogMessage &LogMessage::operator<<(const long &v);          // NOLINT
template LogMessage &LogMessage::operator<<(const unsigned long &v); // NOLINT
template LogMessage &LogMessage::operator<<(const long long &v);     // NOLINT
template LogMessage &
LogMessage::operator<<(const unsigned long long &v); // NOLINT
template LogMessage &LogMessage::operator<<(void *const &v);
template LogMessage &LogMessage::operator<<(const void *const &v);
template LogMessage &LogMessage::operator<<(const float &v);
template LogMessage &LogMessage::operator<<(const double &v);
template LogMessage &LogMessage::operator<<(const bool &v);

} // namespace log_internal

ABSL_NAMESPACE_END

} // namespace absl

struct FileSinkBase : absl::LogSink {
  void Send(const absl::LogEntry &entry) override {
    const std::lock_guard<std::mutex> lock(m);
    if (entry.log_severity() < absl::LogSeverity::kError) {
      printf("%s", entry.text_message_with_prefix().data());
    }
  }
  FileSinkBase() = default;
  ~FileSinkBase() override = default;

protected:
  std::mutex m;
};

namespace android::base {
inline void InitLogging(char * /*argv*/[]) {
  absl::InitializeLog();
  static FileSinkBase file_sink;
  absl::AddLogSink(&file_sink);
}
} // namespace android::base