#include <android/binder_manager.h>

#include <aidl/vendor/samsung_ext/hardware/camera/flashlight/BnFlashlight.h>

#include <cstdio>
#include <unistd.h>

#include <GetServiceSupport.h>
#include <TestLogSupport.h>

using aidl::vendor::samsung_ext::hardware::camera::flashlight::IFlashlight;

int main(void) {
  auto svc = getService<IFlashlight>("vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default");
  if (!svc) {
    printf("getService returned null\n");
    return 1;
  }
  int ret = 0;
  TEST_LOG(svc, enableFlash, 1);
  TEST_LOG_EXT(svc, getCurrentBrightness, &ret, " ret: %d", ret);
  TEST_LOG(svc, setBrightness, 5);
  TEST_LOG(svc, setBrightness, 1);
  TEST_LOG(svc, enableFlash, 0);
}
