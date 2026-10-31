import CoreBridge
import Foundation
import Testing
import os

@Suite("Android accessory")
struct AndroidAccessoryTests {
    /// What the core did with a pipe.
    private final class PipeCounts: Sendable {
        let cancels = OSAllocatedUnfairLock(initialState: 0)
        let releases = OSAllocatedUnfairLock(initialState: 0)
    }

    /// A pipe whose read waits for cancel.
    private final class WaitingPipe: AccessoryPipe {
        private let counts: PipeCounts
        private let condition = NSCondition()
        private let cancelled = OSAllocatedUnfairLock(initialState: false)

        init(_ counts: PipeCounts) { self.counts = counts }

        deinit { counts.releases.withLock { $0 += 1 } }

        func read(into buffer: UnsafeMutableRawBufferPointer) -> Int? {
            condition.lock()
            while !cancelled.withLock({ $0 }) { condition.wait() }
            condition.unlock()
            return nil
        }

        func write(_ data: UnsafeRawBufferPointer) -> Int? { data.count }

        func cancel() {
            condition.lock()
            cancelled.withLock { $0 = true }
            counts.cancels.withLock { $0 += 1 }
            condition.broadcast()
            condition.unlock()
        }
    }

    @Test("Switching asks for the protocol, sends the identity and starts")
    func switches() throws {
        var requests: [AndroidAccessory.ControlRequest] = []
        var strings: [String] = []
        try AndroidAccessory.switchToAccessory { request, data in
            requests.append(request)
            if request.isIn {
                data[0] = 2
                data[1] = 0
            } else if request.request == 52 {
                strings.append(String(decoding: data.dropLast(), as: UTF8.self))
            }
            return data.count
        }
        #expect(requests.map(\.request) == [51, 52, 52, 52, 52, 52, 52, 53])
        #expect(Array(strings.prefix(2)) == ["Osplay", "Osplay Display"])
    }

    @Test("A device without AOA is left alone")
    func refusesWithoutAoa() {
        var count = 0
        #expect(throws: CoreError.unsupportedVersion) {
            try AndroidAccessory.switchToAccessory { _, data in
                count += 1
                data.initializeMemory(as: UInt8.self, repeating: 0)
                return data.count
            }
        }
        #expect(count == 1)
        #expect(throws: CoreError.network) { try AndroidAccessory.switchToAccessory { _, _ in nil } }
    }

    @Test("Accessory IDs")
    func knowsAccessories() {
        #expect(AndroidAccessory.isAccessory(vendorID: 0x18D1, productID: 0x2D00))
        #expect(AndroidAccessory.isAccessory(vendorID: 0x18D1, productID: 0x2D01))
        #expect(!AndroidAccessory.isAccessory(vendorID: 0x04E8, productID: 0x6860))
    }

    @Test("A host on an accessory cancels and releases its pipe once")
    func hostReleasesThePipe() throws {
        let counts = PipeCounts()
        var host: CoreSession? = try CoreSession.hostOverAccessory(
            device: SessionDevice(
                identity: try .generate(), name: "Mac",
                capabilities: SessionCapabilities(
                    maximumStreams: 2, maximumWidth: 7680, maximumHeight: 4320, maximumRefreshHz: 240)),
            pipe: WaitingPipe(counts), trust: { _ in .trusted })
        #expect(host != nil)
        host = nil
        #expect(counts.cancels.withLock { $0 } == 1)
        #expect(counts.releases.withLock { $0 } == 1)
    }
}
