package com.akinalpfdn.osplay.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.akinalpfdn.osplay.R
import com.akinalpfdn.osplay.app.Failure
import com.akinalpfdn.osplay.core.CoreError
import com.akinalpfdn.osplay.storage.KeyStoreError

/** What happened and what to do, in the user's language. */
@Composable
fun Failure.message(): String = when (this) {
    is Failure.KeyStore -> when (val keyError = error) {
        KeyStoreError.Damaged -> stringResource(R.string.error_key_store_damaged)
        KeyStoreError.IdentityCreationFailed -> stringResource(R.string.error_key_store_identity)
        is KeyStoreError.System -> stringResource(R.string.error_key_store_system, keyError.message)
    }
    is Failure.Discovery -> stringResource(R.string.error_discovery, code)
    is Failure.Session -> error.message()
    Failure.Incompatible -> stringResource(R.string.error_incompatible)
    is Failure.NeedsUpdate -> stringResource(R.string.error_needs_update, name)
    Failure.NoScreen -> stringResource(R.string.error_no_screen)
}

@Composable
private fun CoreError.message(): String = when (this) {
    CoreError.NotTrusted -> stringResource(R.string.error_session_not_trusted)
    CoreError.PairingRejected -> stringResource(R.string.error_session_pairing_rejected)
    CoreError.UnsupportedVersion -> stringResource(R.string.error_incompatible)
    CoreError.PeerBusy -> stringResource(R.string.error_session_peer_busy)
    CoreError.TimedOut, CoreError.Network, CoreError.ConnectionClosed -> stringResource(R.string.error_session_unreachable)
    CoreError.PeerSleeping -> stringResource(R.string.error_session_peer_sleeping)
    CoreError.InvalidArgument, CoreError.BufferTooSmall, CoreError.Truncated, CoreError.BadMagic,
    CoreError.UnknownType, CoreError.TrailingBytes, CoreError.ValueOutOfRange, CoreError.InvalidString,
    CoreError.TooLarge, CoreError.UnexpectedMessage, CoreError.HandshakeOutOfOrder,
    -> stringResource(R.string.error_session_protocol)
    CoreError.DecryptionFailed, CoreError.Replayed, CoreError.NonceExhausted, CoreError.InvalidKey ->
        stringResource(R.string.error_session_encryption)
    CoreError.RandomUnavailable -> stringResource(R.string.error_session_random)
    CoreError.PeerError -> stringResource(R.string.error_session_peer_error)
    CoreError.InvalidState -> stringResource(R.string.error_session_busy)
    CoreError.Internal -> stringResource(R.string.error_session_internal, code)
}
