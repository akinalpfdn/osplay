package com.akinalpfdn.osplay.core

/** osplay_platform. */
enum class Platform(val code: Int) {
    MacOS(1),
    Windows(2),
    IPadOS(3),
    Android(4),
    ;

    companion object {
        fun of(code: Int): Platform = entries.firstOrNull { it.code == code } ?: MacOS
    }
}

/** osplay_trust. */
enum class Trust(val code: Int) {
    Trusted(1),
    Unknown(2),
    Blocked(3),
}

/** osplay_session_state. */
enum class SessionState(val code: Int) {
    Connecting(1),
    Handshaking(2),
    AwaitingPairing(3),
    Negotiating(4),
    Streaming(5),
    Paused(6),
    Closed(7),
    ;

    companion object {
        fun of(code: Int): SessionState = entries.firstOrNull { it.code == code } ?: Closed
    }
}

/** osplay_result, without OSPLAY_OK. */
enum class CoreError(val code: Int) {
    InvalidArgument(1),
    BufferTooSmall(2),
    Truncated(3),
    BadMagic(4),
    UnsupportedVersion(5),
    UnknownType(6),
    TrailingBytes(7),
    ValueOutOfRange(8),
    InvalidString(9),
    TooLarge(10),
    DecryptionFailed(11),
    HandshakeOutOfOrder(12),
    Replayed(13),
    NonceExhausted(14),
    InvalidKey(15),
    RandomUnavailable(16),
    TimedOut(17),
    ConnectionClosed(18),
    PeerError(19),
    UnexpectedMessage(20),
    NotTrusted(21),
    PairingRejected(22),
    PeerBusy(23),
    InvalidState(24),
    Network(25),
    Internal(26),
    PeerSleeping(27),
    ;

    companion object {
        /** Null for OSPLAY_OK; an unknown code is reported as [Internal]. */
        fun of(code: Int): CoreError? = if (code == 0) null else entries.firstOrNull { it.code == code } ?: Internal
    }
}

/** osplay_role_swap_answer. */
enum class RoleSwapAnswer(val code: Int) {
    Accepted(1),
    CannotHost(2),
    Busy(3),
}

/** The protocol constants the app needs (osplay.h). */
object Protocol {
    const val VERSION = 1
    const val MINOR = 2
    const val SERVICE_TYPE = "_osplay._tcp"
    const val ROLE_HOST = 1
    const val CURSOR_SEPARATE = 1
}
