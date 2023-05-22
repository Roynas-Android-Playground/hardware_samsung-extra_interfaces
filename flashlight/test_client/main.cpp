#include <android/binder_manager.h>

#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/BnFlashlight.h>
#include <cstdio>
#include <unistd.h>

using SpB = ndk::SpAIBinder;
using aidl::vendor::samsung_ext::hardware::camera::flashlight::IFlashlight;

int main(const int, const char**) {
  auto binder = AServiceManager_waitForService(
      "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default");
  if (!binder) {
    fprintf(stderr, "waitForService returned NULL\n");
    return 1;
  }
  auto svc = IFlashlight::fromBinder(SpB(binder));
  int rc = 0;
  svc->enableFlash(1);
  sleep(2);
  auto ret = svc->getCurrentBrightness(&rc);
  printf("getCurrentBrightness: ok: %d, %d\n", ret.isOk(), rc);
  sleep(2);
  svc->setBrightness(5);
  sleep(2);
  svc->setBrightness(1);
  sleep(2);
  svc->enableFlash(0);
}
