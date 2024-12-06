/*
 * Copyright (C) 2022 The LineageOS Project
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

package com.royna.flashcontrol

import android.content.Context
import android.content.ContentResolver
import android.content.SharedPreferences
import android.database.ContentObserver
import android.os.Bundle
import android.os.Looper
import android.os.Handler
import android.os.ServiceManager
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import android.widget.CompoundButton
import android.widget.CompoundButton.OnCheckedChangeListener

import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager

import com.android.settingslib.widget.MainSwitchPreference
import com.android.settingslib.widget.SelectorWithWidgetPreference

import java.lang.IllegalStateException

import com.royna.flashcontrol.R

import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashFragment : PreferenceFragmentCompat(), OnCheckedChangeListener {

    private lateinit var switchBar: MainSwitchPreference
    private val mService : IFlashlight? = IFlashlight.Stub.asInterface(ServiceManager.waitForDeclaredService("vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"))
    private lateinit var mSharedPreferences : SharedPreferences
    private lateinit var mCurrentIntensity : Preference
    private lateinit var mCurrentOn: Preference

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        addPreferencesFromResource(R.xml.flash_settings)

        mSharedPreferences = PreferenceManager.getDefaultSharedPreferences(requireContext())

        switchBar = findPreference<MainSwitchPreference>(PREF_FLASH_ENABLE)!!
        switchBar.addOnSwitchChangeListener(this)
        val mBrightness = try {
            mService!!.getCurrentBrightness()
        } catch (e : Exception) {
            if (e is NullPointerException) {
                 0
            } else if (e is IllegalStateException) {
                 Log.e(TAG, "IllegalStateException on getCurrentBrightness", e)
                 0
            } else {
                 throw e // rethrow if it's not one of the expected exceptions
            }
	}
        val mSettingBrightness = Settings.Secure.getInt(requireContext().contentResolver, Settings.Secure.FLASHLIGHT_ENABLED, 0)
        switchBar.isChecked = mBrightness != 0
        switchBar.isEnabled = mSettingBrightness == 0

        val mSavedIntensity = mSharedPreferences.getInt(PREF_FLASH_INTENSITY, 1)

        for ((key, value) in PREF_FLASH_MODES) {
            val preference = findPreference<SelectorWithWidgetPreference>(key)!!
            preference.isChecked = value == mSavedIntensity
            preference.isEnabled = switchBar.isChecked
            preference.setOnPreferenceClickListener {
                setIntensity(value)
                mSharedPreferences.edit().putInt(PREF_FLASH_INTENSITY, value).apply()
                true
            }
        }
        mCurrentOn = findPreference<Preference>(PREF_FLASH_CURRENT_ON)!!
        mCurrentIntensity = findPreference<Preference>(PREF_FLASH_CURRENT_INTENSITY)!!
        requireContext().contentResolver.registerContentObserver(mFlashUrl, false, mSettingsObserver)
    }

    private fun changeIntensityView(b: Int) { mCurrentIntensity.title = String.format(requireContext().getString(R.string.flash_current_intensity), b) }
    private fun changeOnOffView(b: Boolean) { mCurrentOn.title = String.format(requireContext().getString(R.string.flash_current_on), requireContext().getString(if (b) R.string.on else R.string.off)) }
    private fun getSettingFlash() = Settings.Secure.getInt(requireContext().contentResolver, Settings.Secure.FLASHLIGHT_ENABLED)

    override fun onResume() {
        super.onResume()
        val mBrightness = mService?.getCurrentBrightness() ?: 0
	changeIntensityView(mBrightness)
	changeOnOffView(mBrightness != 0)
	val isSettingOn = getSettingFlash() != 0
	if (!isSettingOn) {
	    switchBar.apply {
		setChecked(mBrightness != 0)
		isEnabled = mBrightness == 0
	    }
	}
    }

    private val mSettingsObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) {
            super.onChange(selfChange)
	    if (context == null) return
            try {
		val mMainHandler = Handler(Looper.getMainLooper())
		val mEnabled = getSettingFlash()
                when (mEnabled) {
                    0 -> mMainHandler.post {
		        switchBar.setChecked(false)
			switchBar.isEnabled = true
			changeOnOffView(false)
                    }
                    1 -> mMainHandler.post {
		        switchBar.setChecked(true)
			switchBar.isEnabled = false
		        Toast.makeText(requireContext(), R.string.disabled_qs, Toast.LENGTH_SHORT).show()
		    }
                    else -> return@onChange
		}
		changeRadioButtons(mEnabled == 1)
            } catch (e: Settings.SettingNotFoundException) {
                e.printStackTrace()
            }
        }
    }

    override fun onCheckedChanged(buttonView: CompoundButton, isChecked: Boolean) {
        if (mService == null) {
            Log.e(TAG, "mService is null...")
            buttonView.setChecked(false)
            return
        }
        try {
            mService.enableFlash(isChecked)
        } catch (e : IllegalStateException) {
            Log.w(TAG, "enableFlash() failed")
            buttonView.setChecked(false)
            return
        }
	val kBright = mService.getCurrentBrightness()
        changeOnOffView(isChecked)
        changeIntensityView(kBright)
	setIntensity(kBright)
        changeRadioButtons(isChecked)
    }

    private fun changeRadioButtons(enable: Boolean) {
        for ((key, _) in PREF_FLASH_MODES) {
            val mPreference = findPreference<SelectorWithWidgetPreference>(key)!!
            mPreference.isEnabled = enable
        }
    }
            
    private fun setIntensity(intensity: Int) {
        if (intensity < 1 || intensity > 5) {
           Log.e(TAG, "Invalid intensity $intensity")
           return
        }
        if (mService == null) {
           Log.e(TAG, "mService is null...")
           return
        }
        mService.setBrightness(intensity)
        for ((key, value) in PREF_FLASH_MODES) {
            val preference = findPreference<SelectorWithWidgetPreference>(key)!!
            preference.isChecked = value == intensity
        }
        mSharedPreferences.edit().putInt(PREF_FLASH_INTENSITY, intensity).apply()
        changeIntensityView(mService.getCurrentBrightness())
    }

    override fun onPause() {
        super.onPause()
	beGoneFlash()
    }

    override fun onStop() {
        super.onStop()
        beGoneFlash()
    }

    private fun beGoneFlash() {
	if (getSettingFlash() == 0 && mService?.getCurrentBrightness() ?: 0 != 0) {
	   mService?.enableFlash(false)
	}
    }

    override fun onDestroy() {
	super.onDestroy()
	requireContext().contentResolver.unregisterContentObserver(mSettingsObserver)
    }

    companion object {
        private const val PREF_FLASH_ENABLE = "flash_enable"
        const val PREF_FLASH_INTENSITY = "flash_intensity"
        private const val PREF_FLASH_CURRENT_ON = "flash_current_on"
        private const val PREF_FLASH_CURRENT_INTENSITY = "flash_current_intensity"
        val PREF_FLASH_MODES = mapOf(
                "flash_intensity_1" to 1,
                "flash_intensity_2" to 2,
                "flash_intensity_3" to 3,
                "flash_intensity_4" to 4,
                "flash_intensity_5" to 5,
        )
        private const val TAG = "FlashCtrl"
        val mFlashUrl = Settings.Secure.getUriFor(Settings.Secure.FLASHLIGHT_ENABLED)
    }
}
