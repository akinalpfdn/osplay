package com.akinalpfdn.osplay.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.window.DialogProperties
import com.akinalpfdn.osplay.R
import com.akinalpfdn.osplay.app.PairingRequest
import com.akinalpfdn.osplay.ui.theme.OsplayText
import com.akinalpfdn.osplay.ui.theme.Spacing

/** First contact with a computer: both screens show the same six digits; the user confirms here. */
@Composable
fun PairingDialog(request: PairingRequest, onAnswer: (Boolean) -> Unit) {
    AlertDialog(
        onDismissRequest = {},
        properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
        title = { Text(stringResource(R.string.pairing_title, request.peerName)) },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(Spacing.Medium)) {
                Text(pairingCodeText(request.code), style = OsplayText.PairingCode)
                Text(stringResource(R.string.pairing_message), style = MaterialTheme.typography.bodyMedium)
            }
        },
        confirmButton = { Button(onClick = { onAnswer(true) }) { Text(stringResource(R.string.pairing_accept)) } },
        dismissButton = { TextButton(onClick = { onAnswer(false) }) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/** "123 456", as both devices show it. */
fun pairingCodeText(code: Int): String {
    val digits = "%06d".format(code)
    return digits.take(3) + " " + digits.takeLast(3)
}
