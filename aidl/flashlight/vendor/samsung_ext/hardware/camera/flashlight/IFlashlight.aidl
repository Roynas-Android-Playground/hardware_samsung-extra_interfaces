package vendor.samsung_ext.hardware.camera.flashlight;

@VintfStability
interface IFlashlight {
    /** Returns 0 while disabled, otherwise the current logical level (1-5). */
    int getCurrentBrightness();

    /**
     * Updates the HAL-owned desired brightness.
     * If the flashlight is enabled, the new level is applied immediately.
     * If it is disabled, the level is remembered without enabling it.
     */
    void setBrightness(in int level);

    /** Idempotently enables or disables the flashlight. */
    void enableFlash(in boolean enable);

    /** Returns both the current enabled state and remembered brightness. */
    FlashlightState getState();
}
