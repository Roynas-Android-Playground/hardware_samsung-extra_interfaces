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

using android::base::GetProperty;
using android::base::SetProperty;

int main(void) {
  static const std::string kInstance = std::string() + ILights::descriptor + "/default",
                           false_s = std::to_string(false);
  std::string propval;
  android::status_t status;
  std::shared_ptr<IExtLights> extsvc;
  static const char SUNLIGHT_ENABLED_PROP[] = "persist.ext.light.sunlight_on";
  ndk::SpAIBinder BExtLights;
  bool enable_todo = false;

  auto svc = getService<ILights>(kInstance);
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
  propval = GetProperty(SUNLIGHT_ENABLED_PROP, false_s);
  switch (*propval.c_str()) {
  case '1':
    enable_todo = false;
    break;
  case '0':
    enable_todo = true;
    break;
  default:
    printf("In GetProperty: Property value %s invalid.\n", propval.c_str());
    goto exit;
  }
  SetProperty(SUNLIGHT_ENABLED_PROP, std::to_string(enable_todo));
  extsvc->onPropsChanged();
  return 0;
exit:
  return 1;
}
