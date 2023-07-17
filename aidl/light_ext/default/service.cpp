/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.samsung_ext.hardware.light-service"

#include "Lights.h"
#include "ExtLights.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

using ::aidl::android::hardware::light::Lights;
using ::aidl::vendor::samsung_ext::hardware::light::ExtLights;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Lights> lights = ndk::SharedRefBase::make<Lights>();
    std::shared_ptr<ExtLights> extlights = ndk::SharedRefBase::make<ExtLights>();

    const std::string instance = std::string() + Lights::descriptor + "/default";
    CHECK(AIBinder_setExtension(lights->asBinder().get(), extlights->asBinder().get()) == STATUS_OK);
    binder_status_t status = AServiceManager_addService(lights->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // should not reach
}
