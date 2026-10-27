package com.akinalpfdn.osplay.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest

/**
 * Keeps Osplay's sockets on Wi-Fi when Android routes the rest of the device elsewhere. A Wi-Fi
 * network without internet, such as a PC's own network (TAB-04), is not the default network while
 * mobile data is on, and unbound sockets then leave over mobile data and never reach the computer.
 * Bound only while Wi-Fi is connected but not the default, so USB tethering and ordinary Wi-Fi stay
 * as Android routes them.
 */
class WifiPreference(context: Context) {
    private val connectivity = context.getSystemService(ConnectivityManager::class.java)
    private val lock = Any()
    private var wifi: Network? = null
    private var default: Network? = null
    private var bound: Network? = null

    fun start() {
        val wifiRequest = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        connectivity.registerNetworkCallback(
            wifiRequest,
            object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) = update { wifi = network }

                override fun onLost(network: Network) = update { if (wifi == network) wifi = null }
            },
        )
        connectivity.registerDefaultNetworkCallback(
            object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) = update { default = network }

                override fun onLost(network: Network) = update { if (default == network) default = null }
            },
        )
    }

    private fun update(change: () -> Unit) = synchronized(lock) {
        change()
        val wanted = wifi?.takeIf { it != default }
        if (wanted != bound) {
            bound = wanted
            connectivity.bindProcessToNetwork(wanted)
        }
    }
}
