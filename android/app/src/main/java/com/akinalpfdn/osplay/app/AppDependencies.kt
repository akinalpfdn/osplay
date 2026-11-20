package com.akinalpfdn.osplay.app

import android.content.Context
import com.akinalpfdn.osplay.discovery.CombinedDiscovery
import com.akinalpfdn.osplay.discovery.MulticastDiscovery
import com.akinalpfdn.osplay.discovery.NsdDiscovery
import com.akinalpfdn.osplay.discovery.PeerDiscovering
import com.akinalpfdn.osplay.network.WifiPreference
import com.akinalpfdn.osplay.screen.AndroidScreen
import com.akinalpfdn.osplay.screen.ScreenDescribing
import com.akinalpfdn.osplay.screen.isTelevision
import com.akinalpfdn.osplay.storage.AndroidKeyStore
import com.akinalpfdn.osplay.storage.KeyStoring
import com.akinalpfdn.osplay.usb.AndroidUsbCable
import com.akinalpfdn.osplay.usb.UsbCabling
import com.akinalpfdn.osplay.video.DecodingLimits
import com.akinalpfdn.osplay.video.MediaCodecDecoder
import com.akinalpfdn.osplay.video.MediaCodecSupport
import com.akinalpfdn.osplay.viewer.CoreViewerSessions
import com.akinalpfdn.osplay.viewer.MonitorDescription
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
    val cable: UsbCabling,
) {
    companion object {
        /** A TV's decoder may stop below its panel (4K at 30 Hz); phones and tablets take any desktop. */
        private fun decodingLimits(television: Boolean): (MonitorDescription) -> DecodingLimits =
            if (television) {
                { monitor ->
                    DecodingLimits.forMonitor(
                        monitor.widthPixels,
                        monitor.heightPixels,
                        (monitor.refreshMillihertz + 500) / 1000,
                        MediaCodecSupport::plays,
                    )
                }
            } else {
                { DecodingLimits.Generous }
            }

        fun create(context: Context): AppDependencies {
            WifiPreference(context).start() // for the life of the process
            val television = context.isTelevision()
            val stage = StreamStage { surface, shape, needsKeyframe ->
                MediaCodecDecoder(surface, shape.width, shape.height, shape.frameRate, needsKeyframe) { }
            }
            return AppDependencies(
                keyStore = AndroidKeyStore(context),
                discovery = CombinedDiscovery(listOf(NsdDiscovery(context), MulticastDiscovery(context))),
                screen = AndroidScreen(context, television),
                settings = SettingsStore(context),
                stage = stage,
                sessions = CoreViewerSessions(stage, decodingLimits(television)),
                cable = AndroidUsbCable(context),
            )
        }
    }
}
