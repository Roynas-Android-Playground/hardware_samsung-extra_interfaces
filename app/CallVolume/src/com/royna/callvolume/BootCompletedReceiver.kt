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

package com.royna.callvolume

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioSystem
import android.os.Handler
import android.os.HandlerThread
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager

class BootCompletedReceiver : BroadcastReceiver() {
    private val telephonyCallback: TelephonyCallback = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
        override fun onCallStateChanged(p0: Int) {
            if (p0 == TelephonyManager.CALL_STATE_OFFHOOK) {
                AudioSystem.setParameters("g_call_state=514") // CALL_STATUS_VOLTE_CP_VOICE_CALL_ON
            } else {
                AudioSystem.setParameters("g_call_state=257") // CALL_STATUS_VOLTE_CP_VOICE_VIDEO_CALL_OFF
            }
        }
    }
    override fun onReceive(context: Context, intent: Intent) {
        val handler = Handler(HandlerThread("CallThread").apply { start() }.looper)

        val tm = context.getSystemService(TelephonyManager::class.java)
        tm.registerTelephonyCallback({ p0 -> handler.post(p0) }, telephonyCallback)
    }
}
