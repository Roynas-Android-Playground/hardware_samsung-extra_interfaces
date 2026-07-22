package vendor.samsung_ext.hardware.camera.flashlight;

/** Current hardware state plus the HAL-owned desired brightness. */
@VintfStability
parcelable FlashlightState {
    /** Whether the flashlight hardware is currently enabled. */
    boolean enabled;
    /** Desired brightness level in the inclusive range 1 through 5. */
    int brightnessLevel;
}
