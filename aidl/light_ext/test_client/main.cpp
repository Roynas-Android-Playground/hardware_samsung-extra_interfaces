#include <android-base/properties.h>
#include <android/binder_manager.h>

#include <aidl/android/hardware/light/BnLights.h>
#include <aidl/vendor/samsung_ext/hardware/light/BnExtLights.h>

#include <cstdio>
#include <unistd.h>

#include <GetServiceSupport.h>
#include <TestLogSupport.h>

using aidl::android::hardware::light::ILights;
using aidl::vendor::samsung_ext::hardware::light::IExtLights;

using android::base::GetBoolProperty;
using android::base::SetProperty;

int main(void) {
  android::status_t status;
  std::shared_ptr<IExtLights> extsvc;
  static const char SUNLIGHT_ENABLED_PROP[] = "persist.vendor.ext.sunlight.on";
  ndk::SpAIBinder BExtLights;
  bool enable_todo = false;

  auto svc = getServiceDefault<ILights>();
  if (!svc) {
    printf("getService returned null\n");
    goto exit;
  }
  status = AIBinder_getExtension(svc->asBinder().get(), BExtLights.getR());
  if (status != STATUS_OK) {
    printf("In AIBinder_getExtension: status %d invalid.\n", status);
    goto exit;
  }
  extsvc = IExtLights::fromBinder(BExtLights);
  if (!extsvc) {
    printf("In IFlashlight::fromBinder: IExtLights object is NULL\n");
    goto exit;
  }
  enable_todo = !GetBoolProperty(SUNLIGHT_ENABLED_PROP, false);
  SetProperty(SUNLIGHT_ENABLED_PROP, std::to_string(enable_todo));
  extsvc->onPropsChanged();
  return 0;
exit:
  return 1;
}
