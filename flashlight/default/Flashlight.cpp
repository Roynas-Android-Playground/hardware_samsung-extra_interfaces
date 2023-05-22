/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Flashlight.h"

#include <android-base/file.h>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace camera {
namespace flashlight {

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

static constexpr const char* FLASH_NODE = "/sys/class/camera/flash/rear_flash";

ndk::ScopedAStatus Flashlight::getCurrentBrightness(int32_t* _aidl_return) {
    std::string value;
    int intvalue = -1;

    ReadFileToString(FLASH_NODE, &value);
    intvalue = std::stoi(value);
    switch (intvalue) {
	    case 0:
		    *_aidl_return = 0;
		    break;
	    case 1:
		    // If last written is 1, we are really not so sure it is still level_saved.
		    // But QS flash writes 1 anyway.
		    *_aidl_return = level_saved;
		    break;
	    case 1001:
		    *_aidl_return = 1;
		    break;
	    case 1002:
		    *_aidl_return = 2;
		    break;
	    case 1003:
	    case 1004:
		    *_aidl_return = 3;
		    break;
	    case 1005:
	    case 1006:
		    *_aidl_return = 4;
		    break;
	    case 1007 ... 1009:
		    *_aidl_return = 5;
		    break;
	    default:
		    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::setBrightness(int32_t level) {
    if (level > 5 || level < 1)
       return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    int writeval = 0;
    switch (level) {
	case 1:
		writeval = 1001;
		break;
	case 2:
		writeval = 1002;
		break;
	case 3:
		writeval = 1004;
		break;
	case 4:
		writeval = 1006;
		break;
	case 5:
		writeval = 1009;
		break;
	default:
		break;
    }
    WriteStringToFile(FLASH_NODE, std::to_string(writeval));
    level_saved = level;
    // Disable it, it will turn on as it writes
    enableFlash(false);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::enableFlash(bool enable) {
    WriteStringToFile(FLASH_NODE, std::to_string(static_cast<int>(enable)));
    return ndk::ScopedAStatus::ok();
}

} // namespace flashlight
} // namespace camera
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
