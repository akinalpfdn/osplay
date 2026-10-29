package com.akinalpfdn.osplay.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.DesktopWindows
import androidx.compose.material.icons.outlined.Laptop
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.TabletAndroid
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material.icons.outlined.WifiFind
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import com.akinalpfdn.osplay.R
import com.akinalpfdn.osplay.app.DeviceRow
import com.akinalpfdn.osplay.app.DeviceState
import com.akinalpfdn.osplay.app.Failure
import com.akinalpfdn.osplay.app.Screen
import com.akinalpfdn.osplay.app.UiState
import com.akinalpfdn.osplay.core.Platform
import com.akinalpfdn.osplay.discovery.DiscoveredDevice
import com.akinalpfdn.osplay.ui.theme.LocalExtraColors
import com.akinalpfdn.osplay.ui.theme.Shapes
import com.akinalpfdn.osplay.ui.theme.Sizes
import com.akinalpfdn.osplay.ui.theme.Spacing

/** The computers that share their screens on this network; tapping one shows it here. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    state: UiState,
    onStart: () -> Unit,
    onConnect: (DiscoveredDevice) -> Unit,
    onCable: () -> Unit,
    onCancelReconnect: () -> Unit,
    onDismissFailure: () -> Unit,
    onSettings: () -> Unit,
) {
    LaunchedEffect(Unit) { onStart() }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    IconButton(onClick = onSettings) {
                        Icon(Icons.Outlined.Settings, contentDescription = stringResource(R.string.action_settings))
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
                Text(
                    stringResource(R.string.home_subtitle),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            state.failure?.let { failure -> item { FailureCard(failure, onDismissFailure) } }
            (state.screen as? Screen.Reconnecting)?.let { screen ->
                item { ReconnectingCard(screen.peer, onCancelReconnect) }
            }
            item {
                Text(
                    stringResource(R.string.home_nearby),
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(top = Spacing.Medium),
                )
            }
            if (state.devices.isEmpty()) {
                item { SearchingCard() }
            } else {
                items(state.devices, key = { it.device.label }) { row -> DeviceCard(row) { onConnect(row.device) } }
            }
            item {
                Text(
                    stringResource(R.string.home_cable),
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(top = Spacing.Medium),
                )
            }
            item { CableCard(waiting = state.screen == Screen.WaitingForCable, onWait = onCable, onCancel = onCancelReconnect) }
        }
    }
}

@Composable
private fun DeviceCard(row: DeviceRow, onClick: () -> Unit) {
    val enabled = row.state != DeviceState.Incompatible
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick)) {
        Row(Modifier.padding(Spacing.Large), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(Sizes.SymbolColumn), contentAlignment = Alignment.Center) {
                Icon(
                    row.device.advertisement.platform.icon(),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(Sizes.Symbol),
                )
            }
            Spacer(Modifier.width(Spacing.Medium))
            Column(verticalArrangement = Arrangement.spacedBy(Spacing.Small)) {
                Text(row.name, style = MaterialTheme.typography.titleMedium)
                Text(
                    stringResource(row.state.detail()),
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (row.state == DeviceState.NeedsUpdate) {
                        LocalExtraColors.current.warning
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                )
            }
        }
    }
}

@Composable
private fun CableCard(waiting: Boolean, onWait: () -> Unit, onCancel: () -> Unit) {
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth().clickable(enabled = !waiting, onClick = onWait)) {
        Row(Modifier.padding(Spacing.Large), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(Sizes.SymbolColumn), contentAlignment = Alignment.Center) {
                if (waiting) {
                    CircularProgressIndicator(Modifier.size(Sizes.Symbol))
                } else {
                    Icon(
                        Icons.Outlined.Usb,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(Sizes.Symbol),
                    )
                }
            }
            Spacer(Modifier.width(Spacing.Medium))
            Column(verticalArrangement = Arrangement.spacedBy(Spacing.Small), modifier = Modifier.weight(1f)) {
                Text(
                    stringResource(if (waiting) R.string.cable_waiting else R.string.cable_title),
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    stringResource(R.string.cable_detail),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (waiting) {
                TextButton(onClick = onCancel) { Text(stringResource(R.string.action_cancel)) }
            }
        }
    }
}

@Composable
private fun SearchingCard() {
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.padding(Spacing.Large)) {
            Box(Modifier.width(Sizes.SymbolColumn), contentAlignment = Alignment.TopCenter) {
                Icon(
                    Icons.Outlined.WifiFind,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(Sizes.Symbol),
                )
            }
            Spacer(Modifier.width(Spacing.Medium))
            Column(verticalArrangement = Arrangement.spacedBy(Spacing.Small)) {
                Text(stringResource(R.string.home_searching), style = MaterialTheme.typography.bodyLarge)
                Text(
                    stringResource(R.string.home_same_network),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun ReconnectingCard(peer: String, onCancel: () -> Unit) {
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.padding(Spacing.Large), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(Sizes.SymbolColumn), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(Modifier.size(Sizes.Symbol))
            }
            Spacer(Modifier.width(Spacing.Medium))
            Text(
                stringResource(R.string.status_reconnecting, peer),
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = onCancel) { Text(stringResource(R.string.action_cancel)) }
        }
    }
}

@Composable
private fun FailureCard(failure: Failure, onDismiss: () -> Unit) {
    Card(shape = Shapes.Card, modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(Spacing.Large)) {
            Text(failure.message(), style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.error)
            TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End)) {
                Text(stringResource(R.string.action_ok))
            }
        }
    }
}

private fun Platform.icon(): ImageVector = when (this) {
    Platform.MacOS -> Icons.Outlined.Laptop
    Platform.Windows -> Icons.Outlined.DesktopWindows
    Platform.IPadOS, Platform.Android -> Icons.Outlined.TabletAndroid
}

private fun DeviceState.detail(): Int = when (this) {
    DeviceState.Paired -> R.string.device_paired
    DeviceState.New -> R.string.device_new
    DeviceState.NeedsUpdate -> R.string.device_needs_update
    DeviceState.Incompatible -> R.string.device_incompatible
}
