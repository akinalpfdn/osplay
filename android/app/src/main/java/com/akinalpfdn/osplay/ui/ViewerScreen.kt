package com.akinalpfdn.osplay.ui

import android.content.res.Configuration
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.compose.LocalActivity
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.BarChart
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalIconToggleButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.akinalpfdn.osplay.R
import com.akinalpfdn.osplay.app.Screen
import com.akinalpfdn.osplay.app.UiState
import com.akinalpfdn.osplay.ui.theme.OsplayColors
import com.akinalpfdn.osplay.ui.theme.OsplayText
import com.akinalpfdn.osplay.ui.theme.Shapes
import com.akinalpfdn.osplay.ui.theme.Spacing
import com.akinalpfdn.osplay.viewer.StatsFigures
import com.akinalpfdn.osplay.viewer.StreamStage
import java.util.Locale
import kotlinx.coroutines.delay

private const val CONTROLS_HIDE_MILLIS = 4_000L

/** The computer's display, full screen with the system bars hidden. A tap shows the controls. */
@Composable
fun ViewerScreen(
    state: UiState,
    stage: StreamStage,
    onDisconnect: () -> Unit,
    onShowsStats: (Boolean) -> Unit,
    onViewport: (Boolean) -> Unit,
) {
    val stream by stage.stream.collectAsStateWithLifecycle()
    val cursor by stage.cursor.collectAsStateWithLifecycle()
    var tapCount by remember { mutableIntStateOf(0) }
    var showsControls by remember { mutableStateOf(false) }
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE

    ImmersiveWhileShown()
    LaunchedEffect(landscape) { onViewport(landscape) }
    LaunchedEffect(tapCount) {
        if (tapCount == 0) return@LaunchedEffect
        showsControls = true
        delay(CONTROLS_HIDE_MILLIS)
        showsControls = false
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(OsplayColors.ViewerBackground)
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { tapCount++ },
        contentAlignment = Alignment.Center,
    ) {
        val shape = stream
        val video = if (shape != null) Modifier.aspectRatio(shape.width.toFloat() / shape.height) else Modifier.fillMaxSize()
        Box(video) {
            AndroidView(
                factory = { context -> SurfaceView(context).apply { holder.addCallback(stage.callback()) } },
                modifier = Modifier.fillMaxSize(),
            )
            cursor?.let { current ->
                Canvas(Modifier.fillMaxSize()) {
                    val streamWidth = shape?.width ?: return@Canvas
                    val scale = size.width / streamWidth
                    drawImage(
                        current.image.asImageBitmap(),
                        dstOffset = IntOffset(
                            ((current.x - current.hotspotX) * scale).toInt(),
                            ((current.y - current.hotspotY) * scale).toInt(),
                        ),
                        dstSize = IntSize(
                            (current.image.width * scale).toInt(),
                            (current.image.height * scale).toInt(),
                        ),
                    )
                }
            }
        }
        if (shape == null) Waiting(state.screen)
        if (state.showsStats) {
            state.stats?.let { StatsLabel(it, Modifier.align(Alignment.TopStart).safeDrawingPadding().padding(Spacing.Large)) }
        }
        AnimatedVisibility(
            visible = showsControls || shape == null,
            enter = fadeIn(),
            exit = fadeOut(),
            modifier = Modifier.align(Alignment.TopEnd).safeDrawingPadding().padding(Spacing.Large),
        ) {
            Row(
                Modifier.background(OsplayColors.ControlsBackground, Shapes.Controls).padding(Spacing.Small),
                horizontalArrangement = Arrangement.spacedBy(Spacing.Small),
            ) {
                FilledTonalIconToggleButton(checked = state.showsStats, onCheckedChange = onShowsStats) {
                    Icon(Icons.Outlined.BarChart, contentDescription = stringResource(R.string.viewer_stats))
                }
                IconButton(onClick = onDisconnect) {
                    Icon(
                        Icons.Outlined.Close,
                        contentDescription = stringResource(R.string.viewer_disconnect),
                        tint = OsplayColors.OnViewer,
                    )
                }
            }
        }
    }
}

@Composable
private fun Waiting(screen: Screen) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(Spacing.Medium)) {
        CircularProgressIndicator(color = OsplayColors.OnViewer)
        Text(
            if (screen is Screen.Connecting) {
                if (screen.peer.isEmpty()) {
                    stringResource(R.string.status_connecting_cable) // the computer names itself shortly
                } else {
                    stringResource(R.string.status_connecting, screen.peer)
                }
            } else {
                stringResource(R.string.viewer_waiting)
            },
            color = OsplayColors.OnViewer,
            style = MaterialTheme.typography.bodyLarge,
        )
    }
}

@Composable
private fun StatsLabel(figures: StatsFigures, modifier: Modifier) {
    Text(
        stringResource(
            R.string.stats_overlay,
            figures.framesPerSecond,
            String.format(Locale.getDefault(), "%.1f", figures.bitrateKbps / 1000.0),
            figures.roundTripMilliseconds,
            String.format(Locale.getDefault(), "%.1f", figures.lossPercent),
            figures.latencyMilliseconds?.toString() ?: "–",
        ),
        style = OsplayText.Stats,
        color = OsplayColors.OnViewer,
        modifier = modifier.background(OsplayColors.StatsBackground, Shapes.Stats).padding(Spacing.Small),
    )
}

/** Hides the system bars and keeps the screen on while the viewer is shown. */
@Composable
private fun ImmersiveWhileShown() {
    val activity = LocalActivity.current ?: return
    DisposableEffect(activity) {
        val window = activity.window
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        onDispose {
            controller.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}

/** Hands the surface view's surface to the stage. */
private fun StreamStage.callback() = object : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) = surfaceCreated(holder.surface)

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

    override fun surfaceDestroyed(holder: SurfaceHolder) = surfaceDestroyed()
}
