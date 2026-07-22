# ✔ Hardware Samsung Extra Interfaces
Set of packages for common Samsung devices, and some extras.

## ➡ Subdirectories
Sub directory  | Description
------------------------------:|:-----------
`aidl/battery/default`         | AIDL SmartCharge policy service, frozen as vendor.samsung_ext.framework.battery-V1
`aidl/battery/test_client`     | Client to the AIDL SmartCharge service for testing and debugging
`aidl/flashlight/default`      | AIDL flashlight brightness controller; frozen V1 plus the current state-query extension
`aidl/flashlight/test_client`  | Client to the AIDL flashlight controller for testing and debugging
`aidl/light_ext/default`       | AIDL Light HAL with a sunlight-mode vendor extension (BROKEN)
`aidl/light_ext/test_client`   | Client to the AIDL Light HAL vendor extension
`app/FlashControl`             | UI for the HAL-owned flashlight brightness setting
`app/SmartCharge`              | UI for configuring and live-updating SmartCharge policy
`debug-tools/bootlogger`       | Bounded boot/runtime log capture with AVC filtering and review-only SELinux rule suggestions
`debug-tools/dlopener`         | Small program that tests `dlopen(3)` on a supplied ELF file
`libextsupport`                | Support headers used by test clients and AIDL implementations
`sepolicy`                     | SELinux policy required by the executables and apps
`touch`                        | LineageOS HIDL Touch HAL for single tap support

## Host tests
This tree includes a CMake/CTest harness for bootlogger parsing and
rotation, flashlight raw-value mapping, and SmartCharge policy decisions.
AddressSanitizer and UndefinedBehaviorSanitizer can be enabled with
`-DSAMSUNG_EXT_ENABLE_SANITIZERS=ON`.

## ❓ Design goals
These components are used by device trees maintained in this organization and
are intended to remain reusable across devices with compatible kernel nodes.
Contributions and reuse in other ROMs are welcome.
