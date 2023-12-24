# ✔ Hardware Samsung Extra Interfaces
Set of packages for common Samsung devices, and some extras

## ➡ Subdirectories
Sub directory  | Description
------------------------------:|:-----------
`aidl/battery/default`         | AIDL Smartcharge HAL Implementation, frozen as vendor.samsung_ext.framework.battery-V1
`aidl/battery/test_client`     | Client to the AIDL Smartcharge HAL, for testing and debugging
`aidl/flashlight/default`      | AIDL Flashlight Brightness Controller HAL Implementation, frozen as vendor.samsung_ext.hardware.camera.flashlight-V1
`aidl/battery/test_client`     | Client to the AIDL Flashlight Brightness Controller HAL, for testing and debugging
`aidl/light_ext/default`       | AIDL Light HAL Implementation with 'Sunlight' mode vendor extension, frozen as vendor.samsung_ext.hardware.light-V1, based on android.hardware.light-service.samsung (BROKEN)
`aidl/light_ext/test_client`   | Client to the AIDL Light HAL with vendor extension, for testing and debugging
`app/FlashControl`             | Client to the AIDL Flashlight Brightness Controller HAL, actual user frontend app providing the UI for configuring flashlight brightness scale
`app/SmartCharge`              | Client to the AIDL Smartcharge HAL, actual user frontend app providing the UI for configuring 'Smartcharge' settings
`debug-tools/bootlogger`       | A boot time logger binary used to collect dmesg, logcat logs while system boot, or at system runtime. Supports AVC (Access Vector Control) denial message filtering and even generating allow rules for those denials.
`debug-tools/dlopener`         | A little program to try dlopen(3) on a given ELF file. Prints whether dlopening succeeded or failed. installed as 32/64 system/vendor variants.
`keymaster`                    | Shim for Samsung's keymaster implementations on VNDK 34. Common VNDK33 package so sorted out to here
`libextsupport`                | Support headers used by test_clients and AIDL impls
`libsafestoi`                  | Shared common string to int safe version function. as std::stoi does throw exceptions.
`sepolicy`                     | SEPolicy rules for executable binaries and apps to function, some parts need to be added to device tree side as well.
`touch`                        | LineageOS HIDL Touch HAL Implementation for Single tap. Device supports single_tap if `/sys/class/sec/tsp/cmd_list` contains 'singletap_enable'

## ❓ Design goals
These are currently used with my device trees, as they are common on Samsung devices which i have device trees on. It will be updated as long as I work on those devices. I won't mind if other persons want to use any of these components on their ROM's
