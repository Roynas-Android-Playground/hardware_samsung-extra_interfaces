/*
 * Copyright (C) 2022 The LineageOS Project
 * Licensed under the Apache License, Version 2.0
 */
package com.royna.flashcontrol

import android.database.ContentObserver
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ServiceManager
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import com.android.settingslib.widget.MainSwitchPreference
import com.android.settingslib.widget.SelectorWithWidgetPreference
import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashFragment : PreferenceFragmentCompat() {
  private lateinit var switchBar: MainSwitchPreference
  private val service: IFlashlight? =
    IFlashlight.Stub.asInterface(
      ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"
      )
    )
  private lateinit var currentIntensity: Preference
  private lateinit var currentOn: Preference

  override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
    addPreferencesFromResource(R.xml.flash_settings)

    switchBar = findPreference<MainSwitchPreference>(PREF_FLASH_ENABLE)!!
    switchBar.setOnPreferenceChangeListener { _, value -> setFlashEnabled(value as Boolean) }

    val state = runCatching { service?.state }.getOrNull()
    val frameworkFlashEnabled =
      Settings.Secure.getInt(
        requireContext().contentResolver,
        Settings.Secure.FLASHLIGHT_ENABLED,
        0,
      ) != 0

    switchBar.isChecked = state?.enabled ?: false
    switchBar.isEnabled = !frameworkFlashEnabled
    val rememberedBrightness = state?.brightnessLevel ?: 1

    for ((key, value) in PREF_FLASH_MODES) {
      val preference = findPreference<SelectorWithWidgetPreference>(key)!!
      preference.isChecked = value == rememberedBrightness
      preference.isEnabled = state?.enabled == true
      preference.setOnPreferenceClickListener {
        setIntensity(value)
        true
      }
    }

    currentOn = findPreference(PREF_FLASH_CURRENT_ON)!!
    currentIntensity = findPreference(PREF_FLASH_CURRENT_INTENSITY)!!
    updateStateView(state?.enabled ?: false, rememberedBrightness)
    requireContext().contentResolver.registerContentObserver(flashUrl, false, settingsObserver)
  }

  private fun changeIntensityView(level: Int) {
    currentIntensity.title = getString(R.string.flash_current_intensity, level)
  }

  private fun changeOnOffView(enabled: Boolean) {
    currentOn.title =
      getString(R.string.flash_current_on, getString(if (enabled) R.string.on else R.string.off))
  }

  private fun updateStateView(enabled: Boolean, brightness: Int) {
    changeOnOffView(enabled)
    changeIntensityView(brightness)
    changeRadioButtons(enabled)
    for ((key, value) in PREF_FLASH_MODES) {
      findPreference<SelectorWithWidgetPreference>(key)!!.isChecked = value == brightness
    }
  }

  private fun frameworkFlashEnabled(): Boolean =
    Settings.Secure.getInt(
      requireContext().contentResolver,
      Settings.Secure.FLASHLIGHT_ENABLED,
      0,
    ) != 0

  override fun onResume() {
    super.onResume()
    val state =
      runCatching { service?.state }
        .onFailure { Log.e(TAG, "Failed to query flashlight state", it) }
        .getOrNull()
    val enabled = state?.enabled ?: false
    val brightness = state?.brightnessLevel ?: 1
    updateStateView(enabled, brightness)
    switchBar.setChecked(enabled)
    switchBar.isEnabled = !frameworkFlashEnabled()
  }

  private val settingsObserver =
    object : ContentObserver(Handler(Looper.getMainLooper())) {
      override fun onChange(selfChange: Boolean) {
        super.onChange(selfChange)
        if (context == null) return

        val frameworkEnabled = runCatching { frameworkFlashEnabled() }.getOrDefault(false)
        val state = runCatching { service?.state }.getOrNull()
        val enabled = state?.enabled ?: frameworkEnabled
        val brightness = state?.brightnessLevel ?: 1
        switchBar.setChecked(enabled)
        switchBar.isEnabled = !frameworkEnabled
        updateStateView(enabled, brightness)
        if (frameworkEnabled) {
          Toast.makeText(requireContext(), R.string.disabled_qs, Toast.LENGTH_SHORT).show()
        }
      }
    }

  private fun setFlashEnabled(isChecked: Boolean): Boolean {
    val activeService = service
    if (activeService == null) {
      Log.e(TAG, "Flashlight service is unavailable")
      return false
    }

    return runCatching {
        activeService.enableFlash(isChecked)
        activeService.state
      }
      .fold(
        onSuccess = { state ->
          updateStateView(state.enabled, state.brightnessLevel)
          true
        },
        onFailure = {
          Log.w(TAG, "enableFlash($isChecked) failed", it)
          false
        },
      )
  }

  private fun changeRadioButtons(enable: Boolean) {
    for ((key, _) in PREF_FLASH_MODES) {
      findPreference<SelectorWithWidgetPreference>(key)!!.isEnabled = enable
    }
  }

  private fun setIntensity(intensity: Int) {
    val activeService = service
    if (intensity !in 1..5 || activeService == null) {
      Log.e(TAG, "Invalid intensity or unavailable service: $intensity")
      return
    }

    runCatching {
        activeService.setBrightness(intensity)
        activeService.state
      }
      .onSuccess { state -> updateStateView(state.enabled, state.brightnessLevel) }
      .onFailure { Log.e(TAG, "Failed to set flashlight intensity $intensity", it) }
  }

  override fun onPause() {
    super.onPause()
    disableAppOwnedFlash()
  }

  override fun onStop() {
    super.onStop()
    disableAppOwnedFlash()
  }

  private fun disableAppOwnedFlash() {
    val state = runCatching { service?.state }.getOrNull() ?: return
    if (!frameworkFlashEnabled() && state.enabled) {
      runCatching { service?.enableFlash(false) }
        .onFailure { Log.w(TAG, "Failed to disable flashlight", it) }
    }
  }

  override fun onDestroy() {
    requireContext().contentResolver.unregisterContentObserver(settingsObserver)
    super.onDestroy()
  }

  companion object {
    private const val PREF_FLASH_ENABLE = "flash_enable"
    private const val PREF_FLASH_CURRENT_ON = "flash_current_on"
    private const val PREF_FLASH_CURRENT_INTENSITY = "flash_current_intensity"
    private const val TAG = "FlashCtrl"
    val PREF_FLASH_MODES =
      mapOf(
        "flash_intensity_1" to 1,
        "flash_intensity_2" to 2,
        "flash_intensity_3" to 3,
        "flash_intensity_4" to 4,
        "flash_intensity_5" to 5,
      )
    val flashUrl = Settings.Secure.getUriFor(Settings.Secure.FLASHLIGHT_ENABLED)
  }
}
