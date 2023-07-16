#include <aidl/vendor/samsung_ext/framework/battery/BnSmartCharge.h>

#include <GetServiceSupport.h>
#include <TestLogSupport.h>

using aidl::vendor::samsung_ext::framework::battery::ISmartCharge;

int main(int argc, const char **argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s [cmd num] [arg1] [arg2]\n", argv[0]);
    fprintf(stderr, "           cmd_num -> 1: setChargeLimit, 2: activate\n");
    return 1;
  }
  auto svc = getService<ISmartCharge>(
      "vendor.samsung_ext.framework.battery.ISmartCharge/default");
  if (!svc) {
    fprintf(stderr, "getService returned null\n");
    return 1;
  }
  int arg1, arg2, arg3;
  try {
    arg1 = std::stoi(argv[1]);
    arg2 = std::stoi(argv[2]);
    arg3 = std::stoi(argv[3]);
  } catch (const std::exception &e) {
    fprintf(stderr, "Failed to parse args: %s\n", e.what());
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
