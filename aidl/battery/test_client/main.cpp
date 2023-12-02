#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>

#include <GetServiceSupport.h>
#include <TestLogSupport.h>
#include <SafeStoi.h>

using aidl::vendor::samsung_ext::framework::battery::ISmartCharge;

int main(int argc, const char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s [cmd num] [arg1] [arg2]\n", argv[0]);
    fprintf(stderr, "           cmd_num -> 1: setChargeLimit, 2: activate\n");
    return 1;
  }
  auto svc = getServiceDefault<ISmartCharge>();
  if (!svc) {
    fprintf(stderr, "getService returned null\n");
    return 1;
  }
  int arg1, arg2, arg3;
  arg1 = stoi_safe(argv[1]);
  arg2 = stoi_safe(argv[2]);
  arg3 = stoi_safe(argv[3]);
  if (arg1 < 0 || arg2 < 0 || arg3 < 0) {
    fprintf(stderr, "Failed to parse arguments to int or is invalid input\n");
    return 1;
  }
  switch (arg1) {
  case 1: {
    TEST_LOG2(svc, setChargeLimit, arg2, arg3);
    break;
  }
  case 2: {
    TEST_LOG2(svc, activate, !!arg2, !!arg3);
    break;
  }
  default: {
    fprintf(stderr, "Unsupported cmd: %d\n", arg1);
    break;
  }
  }
}
