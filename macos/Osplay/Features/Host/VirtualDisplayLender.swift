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
    /// One display per monitor, ready and set to the crisp resolution. All or nothing.
    func lend(_ monitors: [MonitorDescription], peerID: Data) async throws(VirtualDisplayError) -> [LentDisplay]
    func returnDisplay(_ displayID: CGDirectDisplayID)
    func returnAll()
}

@MainActor
final class VirtualDisplayLender: DisplayLending {
    private let provider: any VirtualDisplayProviding
    private let isHiDPI: Bool

    init(provider: any VirtualDisplayProviding, isHiDPI: Bool = Tunables.Host.defaultHiDPI) {
        self.provider = provider
        self.isHiDPI = isHiDPI
    }

    func lend(_ monitors: [MonitorDescription], peerID: Data) async throws(VirtualDisplayError) -> [LentDisplay] {
        returnAll()
        do throws(VirtualDisplayError) {
            var lent: [(MonitorDescription, any VirtualDisplayHandle)] = []
            for (index, monitor) in monitors.enumerated() {
                lent.append((monitor, try provider.createDisplay(configuration(monitor, index: index, peerID: peerID))))
            }
            for (monitor, handle) in lent {
                try await handle.waitUntilReady()
                try handle.activate(Self.initialResolution(for: monitor, configuration: handle.configuration))
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

    /// The viewer's own scaling decides: a monitor the user runs at 100% shows the desktop at its
    /// full size, a high-density one (150% and up) gets the crisp HiDPI resolution.
    static func initialResolution(
        for monitor: MonitorDescription, configuration: VirtualDisplayConfiguration
    ) -> DisplayResolution {
        if monitor.scalePercent >= Tunables.Host.hiDPIMinimumScalePercent,
            let crisp = configuration.crispHiDPIResolution
        {
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
