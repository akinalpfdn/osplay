package com.akinalpfdn.osplay.app

import android.content.Context
import android.provider.Settings

/** What the user sets in the app; a fake in tests. */
interface SettingsStoring {
    var deviceName: String
    var showsStatsOverlay: Boolean
}

/** App-private preferences; the default name is the one the user gave the device in Android. */
class SettingsStore(private val context: Context) : SettingsStoring {
    private val preferences = context.getSharedPreferences("osplay.settings", Context.MODE_PRIVATE)

    override var deviceName: String
        get() = preferences.getString(DEVICE_NAME, null) ?: systemName()
        set(value) = preferences.edit().putString(DEVICE_NAME, value).apply()

    override var showsStatsOverlay: Boolean
        get() = preferences.getBoolean(STATS, false)
        set(value) = preferences.edit().putBoolean(STATS, value).apply()

    private fun systemName(): String =
        Settings.Global.getString(context.contentResolver, Settings.Global.DEVICE_NAME) ?: android.os.Build.MODEL

    private companion object {
        const val DEVICE_NAME = "deviceName"
        const val STATS = "showsStatsOverlay"
    }
}
