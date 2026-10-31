package com.akinalpfdn.osplay.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.akinalpfdn.osplay.BuildConfig
import com.akinalpfdn.osplay.R
import com.akinalpfdn.osplay.app.UiState
import com.akinalpfdn.osplay.storage.TrustedPeer
import com.akinalpfdn.osplay.ui.theme.LocalExtraColors
import com.akinalpfdn.osplay.ui.theme.Shapes
import com.akinalpfdn.osplay.ui.theme.Spacing

/** The device's name, the statistics overlay and the paired computers. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    state: UiState,
    onName: (String) -> Unit,
    onStats: (Boolean) -> Unit,
    onRevoke: (TrustedPeer) -> Unit,
    onRemove: (TrustedPeer) -> Unit,
    onClose: () -> Unit,
) {
    BackHandler(onBack = onClose)
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.settings_title)) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.AutoMirrored.Outlined.ArrowBack, contentDescription = stringResource(R.string.action_cancel))
                    }
                },
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(padding),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(Spacing.Large),
            verticalArrangement = Arrangement.spacedBy(Spacing.Medium),
        ) {
            item {
                OutlinedTextField(
                    value = state.deviceName,
                    onValueChange = onName,
                    label = { Text(stringResource(R.string.settings_name)) },
                    supportingText = { Text(stringResource(R.string.settings_name_footer)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            item {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.settings_stats), style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                    Switch(checked = state.showsStats, onCheckedChange = onStats)
                }
            }
            item {
                Text(
                    stringResource(R.string.settings_paired),
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(top = Spacing.Medium),
                )
            }
            if (state.peers.isEmpty()) {
                item {
                    Text(
                        stringResource(R.string.settings_no_paired),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            items(state.peers, key = { it.publicKey }) { peer -> PeerCard(peer, onRevoke, onRemove) }
            item {
                Text(
                    stringResource(R.string.settings_version, BuildConfig.VERSION_NAME),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = Spacing.Large),
                )
            }
        }
    }
}

@Composable
private fun PeerCard(peer: TrustedPeer, onRevoke: (TrustedPeer) -> Unit, onRemove: (TrustedPeer) -> Unit) {
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.padding(Spacing.Large), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(Spacing.Small)) {
                Text(peer.name, style = MaterialTheme.typography.titleMedium)
                if (peer.revoked) {
                    Text(
                        stringResource(R.string.settings_revoked),
                        style = MaterialTheme.typography.bodySmall,
                        color = LocalExtraColors.current.warning,
                    )
                }
            }
            if (!peer.revoked) TextButton(onClick = { onRevoke(peer) }) { Text(stringResource(R.string.settings_revoke)) }
            TextButton(onClick = { onRemove(peer) }) { Text(stringResource(R.string.settings_remove)) }
        }
    }
}
