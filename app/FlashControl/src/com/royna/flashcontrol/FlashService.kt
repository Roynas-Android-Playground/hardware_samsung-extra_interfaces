package com.royna.flashcontrol

import android.app.Service
import android.content.Context
import android.content.Intent
import android.database.ContentObserver
import android.os.IBinder
import android.os.Looper
import android.os.Handler
import android.os.ServiceManager
import android.util.Log
import android.provider.Settings

import androidx.preference.PreferenceManager

import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashService : Service() {
     private val mService : IFlashlight? = IFlashlight.Stub.asInterface(ServiceManager.waitForDeclaredService("vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"))
     private val mMainHandler = Handler(Looper.getMainLooper())
     private lateinit var mContext : Context
     private fun getBrightness(mCtx: Context) = PreferenceManager.getDefaultSharedPreferences(mCtx).getInt(FlashFragment.PREF_FLASH_INTENSITY, 1)

     override fun onStartCommand(i: Intent?, flags: Int, startid : Int) : Int {
	  contentResolver.registerContentObserver(FlashFragment.mFlashUrl, false, mFlashObserver)
	  mContext = this
	  return START_STICKY
     }

     override fun onBind(i: Intent) : IBinder? = null

     private val mFlashObserver = object : ContentObserver(mMainHandler) {
         override fun onChange(s: Boolean) {
	      super.onChange(s)
	      val isOn = Settings.Secure.getInt(mContext.contentResolver, Settings.Secure.FLASHLIGHT_ENABLED, 0)
	      if (isOn == 1) {
	          val mBrightSetting = getBrightness(mContext)
	          mMainHandler.postDelayed({
		       Log.d("FlashControlSVC", "Setting $mBrightSetting")
                       mService?.setBrightness(mBrightSetting)
                  }, 25)
	      }
          }
      }
}
