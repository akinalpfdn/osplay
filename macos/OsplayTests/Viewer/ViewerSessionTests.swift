import CoreBridge
import Foundation
import Testing

@testable import Osplay

@MainActor
@Suite("Viewer session")
struct ViewerSessionTests {
    private let stream = StreamConfiguration(
        streamID: 0, monitorID: 0, width: 1920, height: 1080, refreshMillihertz: 60_000, profile: .high,
        cursorMode: .separate, bitrateKbps: 20_000)

    private struct Harness {
        let core = FakeViewerCore()
        let decoders = FakeDecoderFactory()
        let presenters = FakePresenterFactory()
        let power = FakePower()

        func session() -> ViewerSession {
            ViewerSession(
                core: core, decoders: decoders, presenters: presenters, power: power,
                target: { _ in .fullScreen(1) })
        }
    }

    private func unit(_ time: UInt64, keyframe: Bool = false, corrupt: Bool = false) -> AccessUnit {
        AccessUnit(
            streamID: 0, isKeyframe: keyframe, carriesParameterSets: keyframe, captureTimeMicroseconds: time,
            data: Data([corrupt ? FakeDecoder.corrupt : 0x00, 1, 2]))
    }

    @Test("A configured stream gets a shown presenter and decoded frames reach it")
    func presentsFrames() async throws {
        let harness = Harness()
        let session = harness.session()
        let run = Task { await session.run() }

        harness.core.emit(.streamConfiguration(stream))
        harness.core.emit(.accessUnit(unit(1, keyframe: true)))
        harness.core.emit(.accessUnit(unit(2)))
        try await eventually { harness.presenters.presenters.first?.recording.frames == [1, 2] }

        let presenter = try #require(harness.presenters.presenters.first)
        #expect(presenter.isShown)
        #expect(presenter.target == .fullScreen(1))
        #expect(harness.power.active == 1, "display sleep is prevented while frames are on screen")
        #expect(await session.statistics()[0]?.framesPresented == 2)

        await session.stop()
        #expect(await run.value == nil)
        #expect(presenter.isClosed)
        #expect(harness.power.active == 0)
        #expect(harness.decoders.decoders.allSatisfy { $0.isInvalidated })
    }

    @Test("Decoder errors ask the host for a keyframe, once per burst")
    func decoderErrorsRequestKeyframes() async throws {
        let harness = Harness()
        let session = harness.session()
        let run = Task { await session.run() }
        harness.core.emit(.streamConfiguration(stream))
        for time in 1...5 {
            harness.core.emit(.accessUnit(unit(UInt64(time), corrupt: true)))
        }
        try await eventually { !harness.core.keyframeRequests.isEmpty }
        try await Task.sleep(for: .milliseconds(50))

        #expect(harness.core.keyframeRequests == [0])
        #expect(await session.statistics()[0]?.decodeErrors == 5)

        harness.core.emit(.accessUnit(unit(6, keyframe: true)))
        try await eventually { harness.presenters.presenters.first?.recording.frames == [6] }
        #expect(await session.statistics()[0]?.lastRecoveryMilliseconds != nil)
        await session.stop()
        _ = await run.value
    }

    @Test("Hiding pauses the streams and releases the power assertion; showing resumes")
    func togglesVisibility() async throws {
        let harness = Harness()
        let session = harness.session()
        let run = Task { await session.run() }
        harness.core.emit(.streamConfiguration(stream))
        try await eventually { harness.presenters.presenters.first?.isShown == true }

        await session.toggleVisibility()
        let presenter = try #require(harness.presenters.presenters.first)
        #expect(!presenter.isShown)
        #expect(harness.core.paused == [0])
        #expect(harness.power.active == 0)

        await session.toggleVisibility()
        #expect(presenter.isShown)
        #expect(harness.core.resumed == [0])
        #expect(harness.power.active == 1)
        await session.stop()
        _ = await run.value
    }

    @Test("Cursor shapes and positions go to the stream's presenter")
    func forwardsCursor() async throws {
        let harness = Harness()
        let session = harness.session()
        let run = Task { await session.run() }
        harness.core.emit(.streamConfiguration(stream))
        harness.core.emit(
            .cursorShape(
                CursorShape(
                    streamID: 0, shapeID: 4, width: 1, height: 1, hotspotX: 0, hotspotY: 0, bgra: Data(count: 4))))
        harness.core.emit(
            .cursorPosition(
                CursorPosition(streamID: 0, isVisible: true, shapeID: 4, x: 5, y: 6, captureTimeMicroseconds: 1)))
        try await eventually { harness.presenters.presenters.first?.cursorPositions.count == 1 }

        #expect(harness.presenters.presenters.first?.cursorShapes == [4])
        await session.stop()
        _ = await run.value
    }

    @Test("A session closed with an error reports it")
    func reportsErrors() async {
        let harness = Harness()
        let session = harness.session()
        let run = Task { await session.run() }
        harness.core.emit(.state(.closed, reason: .timedOut))
        #expect(await run.value == .timedOut)
    }
}

@Suite("Cursor geometry")
struct CursorGeometryTests {
    @Test("The hotspot lands on the stream position inside the letterboxed video")
    func mapsIntoLetterbox() throws {
        let shape = CursorShape(streamID: 0, shapeID: 1, width: 32, height: 32, hotspotX: 4, hotspotY: 8, bgra: Data())
        let position = CursorPosition(
            streamID: 0, isVisible: true, shapeID: 1, x: 960, y: 540, captureTimeMicroseconds: 0)
        // A 1920×1080 stream in a 1000×1000 view: scaled by 1000/1920, 218.75 points of bars above.
        let frame = try #require(
            CursorGeometry.frame(
                position: position, shape: shape, streamSize: CGSize(width: 1920, height: 1080),
                bounds: CGRect(x: 0, y: 0, width: 1000, height: 1000)))
        let scale = 1000.0 / 1920
        #expect(abs(frame.minX - (960 - 4) * scale) < 0.001)
        #expect(abs(frame.minY - (218.75 + (540 - 8) * scale)) < 0.001)
        #expect(abs(frame.width - 32 * scale) < 0.001)
    }
}

/// Polls `condition` until it holds; fails the test after 2 s.
@MainActor
private func eventually(_ condition: @MainActor () -> Bool) async throws {
    let deadline = ContinuousClock.now + .seconds(2)
    while !condition() {
        guard ContinuousClock.now < deadline else {
            Issue.record("condition not met within 2 s")
            return
        }
        try await Task.sleep(for: .milliseconds(5))
    }
}
