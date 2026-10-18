import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osplay

/// Phase 7 acceptance on real hardware: the Mac hosts a 1920×1080 at 60 Hz virtual display with a
/// moving pattern and views it in a window through capture, encode, core, decode and present.
/// Needs Screen Recording permission for the debug Osplay app.
@MainActor
@Suite("Viewer loopback on this Mac", .serialized)
struct ViewerLoopbackHardwareTests {
    static let reportURL = FileManager.default.temporaryDirectory.appending(path: "osplay-viewer-loopback.txt")

    @Test("The loopback presents 60 fps and recovers from a corrupted frame within a second")
    func loopbackKeepsPaceAndRecovers() async throws {
        try #require(CGPreflightScreenCaptureAccess(), "grant Screen Recording to the debug Osplay app first")
        let capabilities = SessionCapabilities(
            maximumStreams: 1, maximumWidth: 7680, maximumHeight: 4320, maximumRefreshHz: 240)
        let hostCore = try CoreSession.host(
            device: SessionDevice(identity: try .generate(), name: "Loopback Host", capabilities: capabilities),
            trust: { _ in .trusted })
        let viewerCore = try CoreSession.viewer(
            device: SessionDevice(identity: try .generate(), name: "Loopback Viewer", capabilities: capabilities),
            monitors: [
                MonitorDescription(
                    id: 0, widthPixels: 1920, heightPixels: 1080, refreshMillihertz: 60_000,
                    physicalWidthMillimeters: 597, physicalHeightMillimeters: 336, scalePercent: 100,
                    name: "Osplay Loopback Test")
            ], host: "127.0.0.1", port: hostCore.controlPort, trust: { _ in .trusted })
        let host = HostSession(
            core: hostCore, peerID: Data("osplay-hardware-loopback-peer".utf8),
            displays: VirtualDisplayLender(provider: CGVirtualDisplayProvider()),
            captures: ScreenCaptureKitCaptureFactory(),
            encoders: VideoToolboxEncoderFactory(), permissions: SystemPermissions())
        let viewer = ViewerSession(
            core: viewerCore, decoders: VideoToolboxDecoderFactory(), presenters: SampleBufferPresenterFactory(),
            power: SystemPower(), target: { _ in .window(title: "Osplay Loopback Test") })
        let patterns = TestPatternWindows()
        defer { patterns.close() }

        let hostRun = Task { await host.run() }
        let viewerRun = Task { await viewer.run() }
        let patternTask = Task {
            for await event in host.events {
                if case .streaming(let displays) = event { patterns.show(on: displays.map(\.displayID)) }
            }
        }
        defer { patternTask.cancel() }

        try await Task.sleep(for: .seconds(3))  // displays, capture and encoders warm up
        let start = await viewer.statistics()[0]?.framesPresented ?? 0
        try await Task.sleep(for: .seconds(10))
        let presented = (await viewer.statistics()[0]?.framesPresented ?? 0) - start
        let framesPerSecond = Double(presented) / 10

        await viewer.corruptNextFrames()
        try await Task.sleep(for: .milliseconds(1_500))
        let afterCorruption = await viewer.statistics()[0]

        await viewer.stop()
        await host.stop()
        #expect(await viewerRun.value == nil)
        #expect(await hostRun.value == nil)

        let line =
            "presented \(framesPerSecond) fps; decode errors \(afterCorruption?.decodeErrors ?? 0); "
            + "recovery \(afterCorruption?.lastRecoveryMilliseconds ?? -1) ms"
        try line.write(to: Self.reportURL, atomically: true, encoding: .utf8)
        #expect(framesPerSecond > 58, Comment(rawValue: line))
        #expect((afterCorruption?.decodeErrors ?? 0) > 0, Comment(rawValue: line))
        let recovery = try #require(afterCorruption?.lastRecoveryMilliseconds, Comment(rawValue: line))
        #expect(recovery < 1_000, Comment(rawValue: line))
    }
}
