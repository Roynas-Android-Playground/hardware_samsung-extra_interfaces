/*
 * Copyright (C) 2023 Royna
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Flashlight.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

using ::aidl::vendor::samsung_ext::hardware::camera::flashlight::Flashlight;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Flashlight> flashlight = ndk::SharedRefBase::make<Flashlight>();

    const std::string instance = std::string() + Flashlight::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(flashlight->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // should not reach
}
