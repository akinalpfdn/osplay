package com.akinalpfdn.osplay.app

import android.content.Context
import com.akinalpfdn.osplay.discovery.NsdDiscovery
import com.akinalpfdn.osplay.discovery.PeerDiscovering
import com.akinalpfdn.osplay.network.WifiPreference
import com.akinalpfdn.osplay.screen.AndroidScreen
import com.akinalpfdn.osplay.screen.ScreenDescribing
import com.akinalpfdn.osplay.storage.AndroidKeyStore
import com.akinalpfdn.osplay.storage.KeyStoring
import com.akinalpfdn.osplay.video.MediaCodecDecoder
import com.akinalpfdn.osplay.viewer.CoreViewerSessions
import com.akinalpfdn.osplay.viewer.StreamStage
import com.akinalpfdn.osplay.viewer.ViewerSessionMaking

/** Composition root: the single place where concrete services are created (no DI framework). */
class AppDependencies(
    val keyStore: KeyStoring,
    val discovery: PeerDiscovering,
    val screen: ScreenDescribing,
    val settings: SettingsStoring,
    val stage: StreamStage,
    val sessions: ViewerSessionMaking,
) {
    companion object {
        fun create(context: Context): AppDependencies {
            WifiPreference(context).start() // for the life of the process
            val stage = StreamStage { surface, shape, needsKeyframe ->
                MediaCodecDecoder(surface, shape.width, shape.height, needsKeyframe) { }
            }
            return AppDependencies(
                keyStore = AndroidKeyStore(context),
                discovery = NsdDiscovery(context),
                screen = AndroidScreen(context),
                settings = SettingsStore(context),
                stage = stage,
                sessions = CoreViewerSessions(stage),
            )
        }
    }
}
