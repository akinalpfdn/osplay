import CoreGraphics
import Testing

@testable import Osplay

@Suite("Virtual display configuration")
struct VirtualDisplayConfigurationTests {
    private func make(
        pixelSize: DisplaySize = DisplaySize(width: 2560, height: 1440),
        refreshRate: Double = 60,
        millimeters: CGSize = CGSize(width: 597, height: 336),
        name: String = "Test",
        isHiDPI: Bool = true
    ) throws(VirtualDisplayError) -> VirtualDisplayConfiguration {
        try VirtualDisplayConfiguration(
            name: name,
            identity: VirtualDisplayIdentity(vendorID: 1, productID: 1, serialNumber: 1),
            pixelSize: pixelSize,
            refreshRate: refreshRate,
            physicalSizeMillimeters: millimeters,
            isHiDPI: isHiDPI
        )
    }

    @Test("Out-of-range values are rejected with a specific problem")
    func rejectsInvalidValues() {
        #expect(throws: VirtualDisplayError.invalidConfiguration(.pixelSizeOutOfRange)) {
            try make(pixelSize: DisplaySize(width: 100, height: 1440))
        }
        #expect(throws: VirtualDisplayError.invalidConfiguration(.pixelSizeOutOfRange)) {
            try make(pixelSize: DisplaySize(width: 2560, height: 9000))
        }
        #expect(throws: VirtualDisplayError.invalidConfiguration(.refreshRateOutOfRange)) {
            try make(refreshRate: 0)
        }
        #expect(throws: VirtualDisplayError.invalidConfiguration(.physicalSizeInvalid)) {
            try make(millimeters: .zero)
        }
        #expect(throws: VirtualDisplayError.invalidConfiguration(.nameEmpty)) {
            try make(name: "")
        }
    }

    @Test("HiDPI doubles the maximum backing size")
    func maximumPixelSize() throws {
        #expect(try make(isHiDPI: true).maximumPixelSize == DisplaySize(width: 5120, height: 2880))
        #expect(try make(isHiDPI: false).maximumPixelSize == DisplaySize(width: 2560, height: 1440))
    }

    @Test("Modes are the native size and its looks-like fractions, largest first")
    func modes() throws {
        #expect(
            try make().modes == [
                DisplaySize(width: 2560, height: 1440),
                DisplaySize(width: 1920, height: 1080),
                DisplaySize(width: 1280, height: 720),
            ])
    }

    @Test("Scaled modes keep even dimensions")
    func evenDimensions() throws {
        for mode in try make(pixelSize: DisplaySize(width: 1366, height: 768)).modes {
            #expect(mode.width.isMultiple(of: 2))
            #expect(mode.height.isMultiple(of: 2))
        }
    }

    @Test("The crisp HiDPI resolution has a backing equal to the remote monitor")
    func crispResolution() throws {
        let crisp = try #require(try make().crispHiDPIResolution)
        #expect(crisp.points == DisplaySize(width: 1280, height: 720))
        #expect(crisp.pixels == DisplaySize(width: 2560, height: 1440))
        #expect(crisp.isHiDPI)
        #expect(try make(isHiDPI: false).crispHiDPIResolution == nil)
    }
}
