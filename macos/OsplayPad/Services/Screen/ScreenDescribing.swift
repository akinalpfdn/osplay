import CoreBridge
import UIKit

/// The monitor this iPad lends to a host: its screen, in the orientation it is held.
@MainActor
protocol ScreenDescribing {
    func monitor() -> MonitorDescription?
}

/// Reads the screen of the app's window scene.
struct SceneScreen: ScreenDescribing {
    func monitor() -> MonitorDescription? {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        guard let scene = scenes.first(where: { $0.activationState == .foregroundActive }) ?? scenes.first else {
            return nil
        }
        let screen = scene.screen
        return Self.monitor(
            nativePixels: screen.nativeBounds.size,
            landscape: scene.effectiveGeometry.interfaceOrientation.isLandscape,
            scale: screen.nativeScale, maximumFramesPerSecond: screen.maximumFramesPerSecond,
            name: UIDevice.current.model)
    }

    /// `nativePixels` is the portrait size iPadOS reports; the monitor takes the held orientation.
    nonisolated static func monitor(
        nativePixels: CGSize, landscape: Bool, scale: CGFloat, maximumFramesPerSecond: Int, name: String
    ) -> MonitorDescription {
        let long = Int(max(nativePixels.width, nativePixels.height))
        let short = Int(min(nativePixels.width, nativePixels.height))
        let width = landscape ? long : short
        let height = landscape ? short : long
        let refresh = maximumFramesPerSecond > 0 ? Double(maximumFramesPerSecond) : Tunables.Viewer.defaultRefreshRate
        return MonitorDescription(
            id: 0, widthPixels: width, heightPixels: height, refreshMillihertz: UInt32((refresh * 1000).rounded()),
            physicalWidthMillimeters: millimeters(width), physicalHeightMillimeters: millimeters(height),
            scalePercent: min(max(Int((scale * 100).rounded()), 100), 400), name: ProtocolText.name(name))
    }

    private nonisolated static func millimeters(_ pixels: Int) -> Int {
        Int((Double(pixels) / Tunables.Pad.assumedPixelsPerInch * 25.4).rounded())
    }
}
