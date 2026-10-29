package com.akinalpfdn.osplay.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.akinalpfdn.osplay.app.MainViewModel
import com.akinalpfdn.osplay.viewer.StreamStage

/** The list of computers, or the viewer while connecting and viewing; the pairing code above both. */
@Composable
fun OsplayApp(viewModel: MainViewModel, stage: StreamStage) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    var showsSettings by remember { mutableStateOf(false) }
    when {
        state.showsViewer -> ViewerScreen(state, stage, viewModel::disconnect, viewModel::setShowsStats, viewModel::viewportChanged)
        showsSettings -> SettingsScreen(
            state = state,
            onName = viewModel::setDeviceName,
            onStats = viewModel::setShowsStats,
            onRevoke = viewModel::revoke,
            onRemove = viewModel::remove,
            onClose = { showsSettings = false },
        )
        else -> HomeScreen(
            state = state,
            onStart = viewModel::start,
            onConnect = viewModel::connect,
            onCable = viewModel::connectCable,
            onCancelReconnect = viewModel::disconnect,
            onDismissFailure = viewModel::dismissFailure,
            onSettings = { showsSettings = true },
        )
    }
    state.pairing?.let { PairingDialog(it, viewModel::answerPairing) }
}
