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
import android.widget.Switch

import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager

import com.android.settingslib.widget.MainSwitchPreference
import com.android.settingslib.widget.OnMainSwitchChangeListener
import com.android.settingslib.widget.RadioButtonPreference

import java.lang.IllegalStateException

import com.royna.flashcontrol.R

import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashFragment : PreferenceFragmentCompat(), OnMainSwitchChangeListener {

    private lateinit var switchBar: MainSwitchPreference
    private val mService : IFlashlight? = IFlashlight.Stub.asInterface(ServiceManager.waitForDeclaredService("vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"))
    private lateinit var mSharedPreferences : SharedPreferences
    private lateinit var mCurrentIntesity : Preference
    private lateinit var mCurrentOn: Preference

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        addPreferencesFromResource(R.xml.flash_settings)

        mSharedPreferences = PreferenceManager.getDefaultSharedPreferences(requireContext())

        switchBar = findPreference<MainSwitchPreference>(PREF_FLASH_ENABLE)!!
        switchBar.addOnSwitchChangeListener(this)
        val mBrightness = mService?.getCurrentBrightness() ?: 0
        val mSettingBrightness = Settings.Secure.getInt(requireContext().contentResolver, Settings.Secure.FLASHLIGHT_ENABLED, 0)
        switchBar.isChecked = mBrightness != 0
        switchBar.isEnabled = mSettingBrightness == 0

        val mSavedIntesity = mSharedPreferences.getInt(PREF_FLASH_INTESITY, 1)

        for ((key, value) in PREF_FLASH_MODES) {
            val preference = findPreference<RadioButtonPreference>(key)!!
            preference.isChecked = value == mSavedIntesity
            preference.isEnabled = switchBar.isChecked
            preference.setOnPreferenceClickListener {
                setIntesity(value)
                mSharedPreferences.edit().putInt(PREF_FLASH_INTESITY, value).apply()
                true
            }
        }
        mCurrentOn = findPreference<Preference>(PREF_FLASH_CURRENT_ON)!!
        mCurrentIntesity = findPreference<Preference>(PREF_FLASH_CURRENT_INTESITY)!!
        requireContext().contentResolver.registerContentObserver(mFlashUrl, false, mSettingsObserver)
    }

    override fun onResume() {
        super.onResume()
        val mBrightness = mService?.getCurrentBrightness() ?: 0
	mCurrentIntesity.title = String.format(requireContext().getString(R.string.flash_current_intesity), mBrightness)
	mCurrentOn.title = String.format(requireContext().getString(R.string.flash_current_on), requireContext().getString(if (mBrightness != 0) R.string.on else R.string.off))
    }

    private val mSettingsObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) {
            super.onChange(selfChange)
	    if (context == null) return
            try {
	        val mEnabled = Settings.Secure.getInt(requireContext().contentResolver, Settings.Secure.FLASHLIGHT_ENABLED)
		val mMainHandler = Handler(Looper.getMainLooper())
                when (mEnabled) {
                    0 -> mMainHandler.post {
		        switchBar.setChecked(false)
			switchBar.isEnabled = true
			mCurrentOn.title = String.format(requireContext().getString(R.string.flash_current_on), requireContext().getString(R.string.off))
                    }
                    1 -> mMainHandler.post {
		        switchBar.setChecked(true)
			switchBar.isEnabled = false
		        Toast.makeText(requireContext(), R.string.disabled_qs, Toast.LENGTH_SHORT).show()
		    }
                    else -> {}
		}
		changeRadioButtons(mEnabled == 1)
            } catch (e: Settings.SettingNotFoundException) {
                e.printStackTrace()
            }
        }
    }

    override fun onSwitchChanged(switchView: Switch, isChecked: Boolean) {
        if (mService == null) {
            Log.e(TAG, "mService is null...")
            switchView.setChecked(false)
            return
        }
        try {
            mService.enableFlash(isChecked)
        } catch (e : IllegalStateException) {
            Log.w(TAG, "enableFlash() failed")
            switchView.setChecked(false)
            return
        }
        mCurrentOn.title = String.format(requireContext().getString(R.string.flash_current_on), requireContext().getString(if (isChecked) R.string.on else R.string.off))
        if (isChecked) mCurrentIntesity.title = String.format(requireContext().getString(R.string.flash_current_intesity), mService.getCurrentBrightness() ?: -1)
        requireContext().contentResolver.notifyChange(mFlashUrl, mSettingsObserver, ContentResolver.NOTIFY_UPDATE)
        changeRadioButtons(isChecked)
    }

    private fun changeRadioButtons(enable: Boolean) {
        for ((key, value) in PREF_FLASH_MODES) {
            val mPreference = findPreference<RadioButtonPreference>(key)!!
            mPreference.isEnabled = enable
        }
    }
            
    private fun setIntesity(intesity: Int) {
        if (intesity < 0 || intesity > 5) {
           Log.e(TAG, "Invalid intesity $intesity")
           return
        }
        if (mService == null) {
           Log.e(TAG, "mService is null...")
           return
        }
        mService.setBrightness(intesity)
        for ((key, value) in PREF_FLASH_MODES) {
            val preference = findPreference<RadioButtonPreference>(key)!!
            preference.isChecked = value == intesity
        }
        mSharedPreferences.edit().putInt(PREF_FLASH_INTESITY, intesity).apply()
        mCurrentIntesity.title = String.format(requireContext().getString(R.string.flash_current_intesity), mService.getCurrentBrightness() ?: -1)
    }

    companion object {
        private const val PREF_FLASH_ENABLE = "flash_enable"
        const val PREF_FLASH_INTESITY = "flash_intesity"
        private const val PREF_FLASH_CURRENT_ON = "flash_current_on"
        private const val PREF_FLASH_CURRENT_INTESITY = "flash_current_intesity"
        val PREF_FLASH_MODES = mapOf(
                "flash_intesity_1" to 1,
                "flash_intesity_2" to 2,
                "flash_intesity_3" to 3,
                "flash_intesity_4" to 4,
                "flash_intesity_5" to 5,
        )
        private const val TAG = "FlashCtrl"
        val mFlashUrl = Settings.Secure.getUriFor(Settings.Secure.FLASHLIGHT_ENABLED)
    }
}
