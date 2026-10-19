import CoreBridge
import CoreGraphics
import Foundation

/// A virtual display standing in for one of the viewer's monitors.
struct LentDisplay: Sendable, Hashable {
    let monitor: MonitorDescription
    let displayID: CGDirectDisplayID
}

/// Creates the virtual displays a host session streams; injected so the session can be tested
/// without the window server.
@MainActor
protocol DisplayLending: AnyObject, Sendable {
    /// One display per monitor, ready, in the HiDPI choice's resolution and at `placement`. All or
    /// nothing.
    func lend(
        _ monitors: [MonitorDescription], peerID: Data, hiDPI: HiDPIChoice, placement: DisplayPlacement
    ) async throws(VirtualDisplayError) -> [LentDisplay]
    func returnDisplay(_ displayID: CGDirectDisplayID)
    func returnAll()
}

@MainActor
final class VirtualDisplayLender: DisplayLending {
    private let provider: any VirtualDisplayProviding
    private let isHiDPI: Bool

    typealias Arrange =
        @MainActor ([(id: CGDirectDisplayID, size: CGSize)], DisplayPlacement) throws(VirtualDisplayError) -> Void
    private let arrange: Arrange

    init(
        provider: any VirtualDisplayProviding, isHiDPI: Bool = Tunables.Host.defaultHiDPI,
        arrange: @escaping Arrange = DisplayArranger.arrange
    ) {
        self.provider = provider
        self.isHiDPI = isHiDPI
        self.arrange = arrange
    }

    func lend(
        _ monitors: [MonitorDescription], peerID: Data, hiDPI: HiDPIChoice, placement: DisplayPlacement
    ) async throws(VirtualDisplayError) -> [LentDisplay] {
        returnAll()
        do throws(VirtualDisplayError) {
            var lent: [(MonitorDescription, any VirtualDisplayHandle)] = []
            for (index, monitor) in monitors.enumerated() {
                lent.append((monitor, try provider.createDisplay(configuration(monitor, index: index, peerID: peerID))))
            }
            var arranged: [(id: CGDirectDisplayID, size: CGSize)] = []
            for (monitor, handle) in lent {
                try await handle.waitUntilReady()
                let resolution = Self.initialResolution(
                    for: monitor, choice: hiDPI, configuration: handle.configuration)
                try handle.activate(resolution)
                let points = resolution.points
                arranged.append((handle.displayID, CGSize(width: points.width, height: points.height)))
            }
            do throws(VirtualDisplayError) {
                try arrange(arranged, placement)
            } catch {
                // The displays work anywhere; the user can still move them in System Settings.
                Log.virtualDisplay.error("Arranging displays failed: \(String(describing: error), privacy: .public)")
            }
            return lent.map { LentDisplay(monitor: $0.0, displayID: $0.1.displayID) }
        } catch {
            returnAll()
            throw error
        }
    }

    func returnDisplay(_ displayID: CGDirectDisplayID) {
        provider.displays.first { $0.displayID == displayID }?.destroy()
    }

    func returnAll() {
        provider.destroyAll()
    }

    /// The user's choice for this viewer, or by default the viewer's own scaling: a monitor run
    /// at 100% shows the desktop at full size, a high-density one (150% and up) the crisp HiDPI
    /// resolution.
    static func initialResolution(
        for monitor: MonitorDescription, choice: HiDPIChoice, configuration: VirtualDisplayConfiguration
    ) -> DisplayResolution {
        let wantsHiDPI =
            switch choice {
            case .automatic: monitor.scalePercent >= Tunables.Host.hiDPIMinimumScalePercent
            case .always: true
            case .never: false
            }
        if wantsHiDPI, let crisp = configuration.crispHiDPIResolution {
            return crisp
        }
        return configuration.nativeResolution
    }

    private func configuration(
        _ monitor: MonitorDescription, index: Int, peerID: Data
    ) throws(VirtualDisplayError) -> VirtualDisplayConfiguration {
        try VirtualDisplayConfiguration(
            name: monitor.name.isEmpty ? String(localized: "host.displayName \(index + 1)") : monitor.name,
            identity: .derived(peerID: peerID, monitorIndex: Int(monitor.id)),
            pixelSize: DisplaySize(width: monitor.widthPixels, height: monitor.heightPixels),
            refreshRate: monitor.refreshRate,
            physicalSizeMillimeters: CGSize(
                width: monitor.physicalWidthMillimeters, height: monitor.physicalHeightMillimeters),
            isHiDPI: isHiDPI)
    }
}
