#include <android/binder_manager.h>

#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/BnFlashlight.h>
#include <cstdio>
#include <unistd.h>

using SpB = ndk::SpAIBinder;
using aidl::vendor::samsung_ext::hardware::camera::flashlight::IFlashlight;

#define TEST_LOG_EXT(name, arg, exttext, ...)                                      \
  {                                                                            \
    auto rc = svc->name(arg);                                                  \
    printf("%s: ok: %s" exttext "\n", #name, rc.isOk() ? "true" : "false",     \
           ##__VA_ARGS__);                                                     \
    sleep(1);                                                                  \
  }

#define TEST_LOG(name, arg) TEST_LOG_EXT(name, arg, "")

int main(void) {
  auto binder = AServiceManager_waitForService(
      "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default");
  if (!binder) {
    fprintf(stderr, "waitForService returned NULL\n");
    return 1;
  }
  auto svc = IFlashlight::fromBinder(SpB(binder));
  int ret = 0;
  TEST_LOG(enableFlash, 1);
  TEST_LOG_EXT(getCurrentBrightness, &ret, "ret: %d", ret);
  TEST_LOG(setBrightness, 5);
  TEST_LOG(setBrightness, 1);
  TEST_LOG(enableFlash, 0);
}
