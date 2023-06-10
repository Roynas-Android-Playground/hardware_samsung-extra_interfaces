/*
 * Copyright (C) 2023 Royna (@roynatech2544 on Gh)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.royna.smartcharge

import android.content.SharedPreferences
import android.content.res.Resources.NotFoundException
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ServiceManager
import android.util.Log
import android.widget.Switch
import android.widget.Toast

import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager
import androidx.preference.SeekBarPreference
import androidx.preference.SwitchPreference

import com.android.settingslib.widget.MainSwitchPreference
import com.android.settingslib.widget.OnMainSwitchChangeListener

import com.royna.smartcharge.R

import vendor.samsung_ext.framework.battery.ISmartCharge

import java.lang.IllegalArgumentException
import java.lang.IllegalStateException

class SmartChargeFragment : PreferenceFragmentCompat(), OnMainSwitchChangeListener {
    private lateinit var mMainSwitch : MainSwitchPreference
    private lateinit var mStopBar : SeekBarPreference
    private lateinit var mRestartBar : SeekBarPreference
    private lateinit var mRestartEnableSwitch : SwitchPreference
    private val mMainHandler = Handler(Looper.getMainLooper())
    enum class Config {
        STOP_RESTART,
        STOP
    }
    private var mConfig = Config.STOP
    private var mService: ISmartCharge? = ISmartCharge.Stub.asInterface(
        ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.framework.battery.ISmartCharge/default"))
    private lateinit var mSharedPreferences : SharedPreferences

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        addPreferencesFromResource(R.xml.smartcharge_settings)
        mSharedPreferences = PreferenceManager.getDefaultSharedPreferences(requireContext())
        mMainSwitch = findPreference<MainSwitchPreference>(PREF_SMTCHG_ENABLE)!!
        mStopBar = findPreference(PREF_STOP_CFG)!!
        mRestartBar = findPreference(PREF_RESTART_CFG)!!
        mRestartEnableSwitch = findPreference(PREF_ENABLE_RESTART)!!

        mMainSwitch.setChecked(mSharedPreferences.getBoolean(PREF_SMTCHG_ENABLE, false))
        mRestartEnableSwitch.isChecked = mSharedPreferences.getBoolean(PREF_ENABLE_RESTART, false)
        mStopBar.value = mSharedPreferences.getInt(PREF_STOP_CFG, 80)
        mRestartBar.value = mSharedPreferences.getInt(PREF_RESTART_CFG, 70)
        mRestartBar.isEnabled = mRestartEnableSwitch.isChecked
        mConfig = if (mRestartEnableSwitch.isChecked) {Config.STOP_RESTART} else {Config.STOP}

        mMainSwitch.addOnSwitchChangeListener(this)
        mRestartEnableSwitch.setOnPreferenceChangeListener { _, new ->
            mConfig = if (new as Boolean) {
                Config.STOP_RESTART
            } else {
                Config.STOP
            }
            mRestartBar.isEnabled = new
            mSharedPreferences.edit().putBoolean(PREF_ENABLE_RESTART, new).apply()
            runCatching {
                mService?.activate(false, /*unused*/false)
            }.onSuccess {
                mService?.activate(true, new)
            }
            true
        }
        val kSeekBarListener = Preference.OnPreferenceChangeListener { preference, newValue ->
            when (preference.key) {
                PREF_RESTART_CFG, PREF_STOP_CFG -> {
                    mSharedPreferences.edit().putInt(preference.key, newValue as Int).apply()
                    updateSeekbarTitles(mapOf(preference.key to newValue))
                }
                else -> return@OnPreferenceChangeListener false
            }
            true
        }
        mStopBar.onPreferenceChangeListener = kSeekBarListener
        mRestartBar.onPreferenceChangeListener = kSeekBarListener
        updateSeekbarTitles()
    }

    private fun SharedPreferences.getIntZ(value: String): Int {
        getInt(value, -1).apply {
            if (this == -1)
                throw NotFoundException("SharedPreference with key $value not found")
            return this
        }
    }

    private fun updateSeekbarTitles(updates : Map<String, Int> = emptyMap()) {
        if (updates.isEmpty()) {
            for (mPref in kSeekPrefMap) {
                try {
                    findPreference<SeekBarPreference>(mPref.key)!!.title = requireContext()
                        .getString(mPref.value, mSharedPreferences.getIntZ(mPref.key))
                } catch (e: NotFoundException) {
                    e.message?.let { Log.w(TAG, it) }
                    continue
                }
            }
        } else {
            for (update in updates) {
                findPreference<SeekBarPreference>(update.key)!!.title = requireContext()
                    .getString(kSeekPrefMap.getValue(update.key), update.value)
            }
        }
    }

    override fun onSwitchChanged(switchView: Switch, isChecked: Boolean) {
        runCatching {
            when (mConfig) {
                Config.STOP_RESTART -> {
                    mService?.setChargeLimit(mSharedPreferences.getIntZ(PREF_STOP_CFG),
                        mSharedPreferences.getIntZ(PREF_RESTART_CFG))
                }
                Config.STOP -> {
                    mService?.setChargeLimit(mSharedPreferences.getIntZ(PREF_STOP_CFG),-1)
                }
            }
            mService?.activate(isChecked, mSharedPreferences.getBoolean(PREF_ENABLE_RESTART, false))
        }.onFailure {
            when (it) {
                is IllegalArgumentException -> {
                    // Switch not synced... Hence do not reset switch
                    mMainHandler.post {
                        Toast.makeText(requireContext(),
                            R.string.smart_charge_internal_error, Toast.LENGTH_SHORT).show()
                    }
                }
                is IllegalStateException -> {
                    // Config error...
                    mMainHandler.post {
                        switchView.isChecked = false
                        Toast.makeText(requireContext(),
                            R.string.smart_charge_invalid_config, Toast.LENGTH_SHORT).show()
                    }
                }
                else -> {
                    Log.w(TAG, it)
                }
            }
        }.onSuccess {
            mSharedPreferences.edit().putBoolean(PREF_SMTCHG_ENABLE, isChecked).apply()
        }
    }

    companion object {
        private const val PREF_SMTCHG_ENABLE = "smart_charge_enable"
        private const val PREF_STOP_CFG = "smart_charge_stop_cfg"
        private const val PREF_RESTART_CFG = "smart_charge_restart_cfg"
        private const val PREF_ENABLE_RESTART = "smart_charge_restart_enabled"
        private const val TAG = "SmartChargeApp"
        private val kSeekPrefMap = mapOf (
            PREF_RESTART_CFG to R.string.smart_charge_restart,
            PREF_STOP_CFG to R.string.smart_charge_stop
        )
    }
}