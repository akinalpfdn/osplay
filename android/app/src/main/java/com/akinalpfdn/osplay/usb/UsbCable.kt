package com.akinalpfdn.osplay.usb

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbAccessory
import android.hardware.usb.UsbManager
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/** What opening Osplay's accessory on the cable gave. */
sealed interface CableOpening {
    /** The accessory's file descriptor, for the core, which then owns it. */
    data class Opened(val fd: Int) : CableOpening

    /** No computer sharing its screen has switched this device to Osplay on the cable. */
    data object NotAttached : CableOpening

    /** Android asks the user first; [CableEvent.PermissionGranted] follows a yes. */
    data object AskedForPermission : CableOpening

    /** Still held by the previous session until the computer answers; try again shortly. */
    data object Busy : CableOpening
}

enum class CableEvent {
    /** Android handed the accessory to Osplay: the activity's "accessory attached" intent. */
    Attached,
    Detached,
    PermissionGranted,
}

/**
 * Osplay on the USB cable (PROTOCOL.md "Android accessory links"): a Mac or PC sharing its screen
 * switches this device into accessory mode with Osplay's identity, and Android hands the
 * accessory to this app. A fake in tests.
 */
interface UsbCabling {
    /** Detached and PermissionGranted, on the main thread; Attached comes through the activity. */
    val events: SharedFlow<CableEvent>

    fun open(): CableOpening
}

class AndroidUsbCable(private val context: Context) : UsbCabling {
    private val manager = context.getSystemService(UsbManager::class.java)
    private val changes = MutableSharedFlow<CableEvent>(extraBufferCapacity = EVENT_BUFFER)
    override val events: SharedFlow<CableEvent> = changes.asSharedFlow()

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_ACCESSORY_DETACHED -> changes.tryEmit(CableEvent.Detached)
                ACTION_PERMISSION ->
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        changes.tryEmit(CableEvent.PermissionGranted)
                    }
            }
        }
    }

    init {
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED)
            addAction(ACTION_PERMISSION)
        }
        ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
    }

    override fun open(): CableOpening {
        val accessory = manager?.accessoryList?.firstOrNull(::isOsplay) ?: return CableOpening.NotAttached
        if (!manager.hasPermission(accessory)) {
            // UsbManager adds the result to the intent, so it must be mutable; the package keeps it explicit.
            val intent = Intent(ACTION_PERMISSION).setPackage(context.packageName)
            manager.requestPermission(
                accessory,
                PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_MUTABLE),
            )
            return CableOpening.AskedForPermission
        }
        val descriptor = runCatching { manager.openAccessory(accessory) }.getOrNull() ?: return CableOpening.Busy
        return CableOpening.Opened(descriptor.detachFd())
    }

    private fun isOsplay(accessory: UsbAccessory) =
        accessory.manufacturer == MANUFACTURER && accessory.model == MODEL

    private companion object {
        // The core's identity (core/src/transport/aoa.hpp); res/xml/accessory_filter.xml matches the same.
        const val MANUFACTURER = "Osplay"
        const val MODEL = "Osplay Display"
        const val ACTION_PERMISSION = "com.akinalpfdn.osplay.USB_PERMISSION"
        const val EVENT_BUFFER = 8
    }
}
