#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>

#include <GetServiceSupport.h>
#include <TestLogSupport.h>
#include <android-base/parseint.h>

using aidl::vendor::samsung_ext::framework::battery::ISmartCharge;

int main(int argc, const char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s [cmd num] [arg1] [arg2]\n", argv[0]);
    fprintf(stderr, "           cmd_num -> 1: setChargeLimit, 2: activate\n");
    return 1;
  }

  int command = 0;
  int arg1 = 0;
  int arg2 = 0;
  if (!android::base::ParseInt(argv[1], &command) ||
      !android::base::ParseInt(argv[2], &arg1) ||
      !android::base::ParseInt(argv[3], &arg2)) {
    fprintf(stderr, "Failed to parse arguments as strict integers\n");
    return 1;
  }

  auto service = getServiceDefault<ISmartCharge>();
  if (!service) {
    fprintf(stderr, "getService returned null\n");
    return 1;
  }

  switch (command) {
    case 1:
      TEST_LOG2(service, setChargeLimit, arg1, arg2);
      break;
    case 2:
      if ((arg1 != 0 && arg1 != 1) || (arg2 != 0 && arg2 != 1)) {
        fprintf(stderr, "activate arguments must be 0 or 1\n");
        return 1;
      }
      TEST_LOG2(service, activate, arg1 != 0, arg2 != 0);
      break;
    default:
      fprintf(stderr, "Unsupported cmd: %d\n", command);
      return 1;
  }
  return 0;
}
