package vendor.samsung_ext.hardware.camera.flashlight;

@VintfStability
interface IFlashlight {
    int getCurrentBrightness();
    void setBrightness(in int level);
    void enableFlash(in boolean enable);
}
