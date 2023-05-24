package com.royna.flashcontrol

import android.app.Service
import android.content.Intent
import android.database.ContentObserver
import android.os.Looper
import android.os.Handler
import android.os.ServiceManager
import android.util.Log
import android.provider.Settings

import androidx.preference.PreferenceManager

import vendor.samsung_ext.hardware.camera.flash.IFlashlight

class FlashService : Service() {
     private val mService : IFlashlight? = IFlashlight.Stub.asInterface(ServiceManager.waitForDeclaredService("vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"))
     private val mMainHandler = Handler(Looper.getMainLooper())
     private var mBrightSetting = 0
     override fun onStartCommand(i: Intent?, flags: Int, startid : Int) : Int {
          mBrightSetting = PreferenceManager.getDefaultSharedPreferences(this).getInt(FlashFragment.PREF_FLASH_INTESITY, 1)
     }

     private val mFlashObserver = object : ContentObserver(mMainHandler) {
         override fun onChange(s: Boolean) {
	      super.onChange(s)
	      val isOn = Settings.Secure.getInt(requireContext().contentResolver, Settings.Secure.FLASHLIGHT_ENABLED, 0)
	      if (isOn == 1) {
	          mMainHandler.postDelayed({
		       Log.d("FlashControlSVC", Setting $mBrightSetting")
                       mService?.setBrightness(mBrightSetting)
                  }, 500)
	      }
          }
      }
}
