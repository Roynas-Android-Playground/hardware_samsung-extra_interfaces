#include <battery.h>
#include <fstream>

static const char kChargeCtlSysfs[] =
    "/sys/class/power_supply/battery/batt_slate_mode";

extern "C" void setChargable(bool enable) {
  std::ofstream p(kChargeCtlSysfs);
  if (p)
    p << !enable;
}
