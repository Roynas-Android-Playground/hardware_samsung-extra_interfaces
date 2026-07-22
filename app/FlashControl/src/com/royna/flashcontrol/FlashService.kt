package com.royna.flashcontrol

import android.app.Service
import android.content.Intent
import android.database.ContentObserver
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ServiceManager
import android.provider.Settings
import android.util.Log
import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashService : Service() {
  private val service: IFlashlight? =
    IFlashlight.Stub.asInterface(
      ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default",
      ),
    )
  private val mainHandler = Handler(Looper.getMainLooper())
  private var observerRegistered = false

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (!observerRegistered) {
      contentResolver.registerContentObserver(FlashFragment.flashUrl, false, flashObserver)
      observerRegistered = true
    }
    return START_STICKY
  }

  override fun onBind(intent: Intent): IBinder? = null

  override fun onDestroy() {
    if (observerRegistered) {
      contentResolver.unregisterContentObserver(flashObserver)
      observerRegistered = false
    }
    super.onDestroy()
  }

  private fun rememberedBrightness(): Int =
    runCatching { service?.state?.brightnessLevel ?: 1 }
      .onFailure { Log.e(TAG, "Failed to query HAL-owned brightness", it) }
      .getOrDefault(1)

  private val flashObserver =
    object : ContentObserver(mainHandler) {
      override fun onChange(selfChange: Boolean) {
        super.onChange(selfChange)
        val isOn =
          Settings.Secure.getInt(
            contentResolver,
            Settings.Secure.FLASHLIGHT_ENABLED,
            0,
          )
        if (isOn != 1) return

        val brightness = rememberedBrightness()
        // QS and other framework clients can enable the Samsung node without
        // going through this HAL. Re-apply the HAL-owned level afterwards.
        mainHandler.postDelayed(
          {
            runCatching { service?.setBrightness(brightness) }
              .onFailure { Log.e(TAG, "Failed to apply brightness $brightness", it) }
          },
          25,
        )
      }
    }

  companion object {
    private const val TAG = "FlashControlSVC"
  }
}
