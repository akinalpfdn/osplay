package com.akinalpfdn.osplay.core

/** A computer's Bonjour record (PROTOCOL.md "Discovery"), checked by the core. */
data class Advertisement(
    val protocolVersion: Int,
    /** 0 for Osplay 0.1, which sends no minor version and refuses tablets. */
    val protocolMinor: Int,
    val platform: Platform,
    val roles: Int,
    /** Lowercase hex, as in the record. */
    val deviceId: String,
    val name: String,
) {
    val isHost: Boolean get() = roles and Protocol.ROLE_HOST != 0

    companion object {
        /** Parses a peer's TXT values; null when the core rejects them. */
        fun fromTxt(values: Map<String, ByteArray>): Advertisement? {
            fun value(key: String) = values[key] ?: ByteArray(0)
            val fields = Native.advertisementFromTxt(
                value("pv"), value("name"), value("id"), value("roles"), value("os"), value("pm"),
            )
            if (fields[0] != 0) return null
            return Advertisement(
                protocolVersion = fields[1],
                protocolMinor = fields[2],
                platform = Platform.of(fields[3]),
                roles = fields[4],
                deviceId = value("id").decodeToString(),
                name = value("name").decodeToString(),
            )
        }
    }
}
