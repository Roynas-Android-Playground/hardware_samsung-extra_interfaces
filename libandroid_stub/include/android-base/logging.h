#pragma once

#include <absl/log/initialize.h>
#include <absl/log/log.h>

namespace android::base {
inline void InitLogging(char * /*argv*/[]) { absl::InitializeLog(); }
} // namespace android::base