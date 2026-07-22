/*
 * Copyright (C) 2023 Royna
 * Licensed under the Apache License, Version 2.0
 */
package com.royna.smartcharge

import android.content.SharedPreferences
import android.content.res.Resources.NotFoundException
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ServiceManager
import android.util.Log
import android.widget.CompoundButton
import android.widget.Toast
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager
import androidx.preference.SeekBarPreference
import androidx.preference.SwitchPreference
import com.android.settingslib.widget.MainSwitchPreference
import vendor.samsung_ext.framework.battery.ISmartCharge

class SmartChargeFragment : PreferenceFragmentCompat(), CompoundButton.OnCheckedChangeListener {
  private lateinit var mainSwitch: MainSwitchPreference
  private lateinit var stopBar: SeekBarPreference
  private lateinit var restartBar: SeekBarPreference
  private lateinit var restartSwitch: SwitchPreference
  private val mainHandler = Handler(Looper.getMainLooper())
  private val service: ISmartCharge? =
    ISmartCharge.Stub.asInterface(
      ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.framework.battery.ISmartCharge/default",
      ),
    )
  private lateinit var preferences: SharedPreferences

  override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
    addPreferencesFromResource(R.xml.smartcharge_settings)
    preferences = PreferenceManager.getDefaultSharedPreferences(requireContext())
    mainSwitch = findPreference(PREF_ENABLE)!!
    stopBar = findPreference(PREF_STOP)!!
    restartBar = findPreference(PREF_RESTART)!!
    restartSwitch = findPreference(PREF_RESTART_ENABLED)!!

    mainSwitch.setChecked(preferences.getBoolean(PREF_ENABLE, false))
    restartSwitch.isChecked = preferences.getBoolean(PREF_RESTART_ENABLED, false)
    stopBar.min = MINIMUM_LIMIT
    stopBar.value = preferences.getInt(PREF_STOP, DEFAULT_STOP)
    restartBar.min = MINIMUM_LIMIT
    restartBar.value = preferences.getInt(PREF_RESTART, DEFAULT_RESTART)
    restartBar.isEnabled = restartSwitch.isChecked

    mainSwitch.addOnSwitchChangeListener(this)
    restartSwitch.setOnPreferenceChangeListener { _, value ->
      val enabled = value as Boolean
      val previousRestart = restartBar.value
      val previousRestartEnabled = restartBar.isEnabled
      if (enabled && stopBar.value <= restartBar.value) {
        restartBar.value = (stopBar.value - MINIMUM_LIMIT) / 2 + MINIMUM_LIMIT
      }
      restartBar.isEnabled = enabled
      val success =
        if (mainSwitch.isChecked) applyRunningConfiguration(enabled) else true
      if (success) {
        updateSeekbarTitles(mapOf(PREF_RESTART to restartBar.value))
      } else {
        restartBar.value = previousRestart
        restartBar.isEnabled = previousRestartEnabled
      }
      success
    }

    val seekListener = Preference.OnPreferenceChangeListener { preference, value ->
      val candidate = value as Int
      val newStop = if (preference.key == PREF_STOP) candidate else stopBar.value
      val newRestart = if (preference.key == PREF_RESTART) candidate else restartBar.value
      if (restartSwitch.isChecked && newRestart >= newStop) {
        showInvalidConfig()
        return@OnPreferenceChangeListener false
      }

      val success =
        if (mainSwitch.isChecked) {
          applyRunningConfiguration(restartSwitch.isChecked, newStop, newRestart)
        } else {
          true
        }
      if (success) updateSeekbarTitles(mapOf(preference.key to candidate))
      success
    }
    stopBar.onPreferenceChangeListener = seekListener
    restartBar.onPreferenceChangeListener = seekListener
    updateSeekbarTitles()
  }

  private fun SharedPreferences.requireInt(key: String): Int =
    getInt(key, -1).also {
      if (it == -1) throw NotFoundException("SharedPreference with key $key not found")
    }

  private fun updateSeekbarTitles(updates: Map<String, Int> = emptyMap()) {
    for ((key, resource) in SEEK_TITLES) {
      val value = updates[key] ?: try {
        preferences.requireInt(key)
      } catch (error: NotFoundException) {
        Log.w(TAG, error.message ?: "Missing SmartCharge preference")
        continue
      }
      findPreference<SeekBarPreference>(key)!!.title = getString(resource, value)
    }
  }

  private fun applyRunningConfiguration(
    restartEnabled: Boolean,
    stop: Int = stopBar.value,
    restart: Int = restartBar.value,
  ): Boolean =
    runCatching {
      val activeService = checkNotNull(service) { "SmartCharge service unavailable" }
      activeService.setChargeLimit(stop, if (restartEnabled) restart else -1)
      activeService.activate(true, restartEnabled)
    }.fold(
      onSuccess = { true },
      onFailure = {
        Log.e(TAG, "Failed to update running SmartCharge policy", it)
        showInvalidConfig()
        false
      },
    )

  override fun onCheckedChanged(buttonView: CompoundButton, isChecked: Boolean) {
    val success =
      runCatching {
        val activeService = checkNotNull(service) { "SmartCharge service unavailable" }
        if (isChecked) {
          val restartEnabled = restartSwitch.isChecked
          activeService.setChargeLimit(
            stopBar.value,
            if (restartEnabled) restartBar.value else -1,
          )
          activeService.activate(true, restartEnabled)
        } else {
          activeService.activate(false, false)
        }
      }.onFailure {
        Log.e(TAG, "Failed to change SmartCharge state", it)
      }.isSuccess

    if (!success) {
      mainHandler.post {
        mainSwitch.setChecked(!isChecked)
        Toast.makeText(requireContext(), R.string.smart_charge_internal_error, Toast.LENGTH_SHORT).show()
      }
      return
    }
    preferences.edit().putBoolean(PREF_ENABLE, isChecked).apply()
  }

  private fun showInvalidConfig() {
    mainHandler.post {
      Toast.makeText(requireContext(), R.string.smart_charge_invalid_config, Toast.LENGTH_SHORT).show()
    }
  }

  companion object {
    private const val PREF_ENABLE = "smart_charge_enable"
    private const val PREF_STOP = "smart_charge_stop_cfg"
    private const val PREF_RESTART = "smart_charge_restart_cfg"
    private const val PREF_RESTART_ENABLED = "smart_charge_restart_enabled"
    private const val TAG = "SmartChargeApp"
    private const val MINIMUM_LIMIT = 50
    private const val DEFAULT_STOP = 80
    private const val DEFAULT_RESTART = 70
    private val SEEK_TITLES =
      mapOf(
        PREF_RESTART to R.string.smart_charge_restart,
        PREF_STOP to R.string.smart_charge_stop,
      )
  }
}
