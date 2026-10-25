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

    @Test("The ¾ mode keeps even dimensions; the half mode is exact, so its 2× backing is the monitor")
    func scaledDimensions() throws {
        #expect(
            try make(pixelSize: DisplaySize(width: 1366, height: 768)).modes == [
                DisplaySize(width: 1366, height: 768),
                DisplaySize(width: 1024, height: 576),
                DisplaySize(width: 683, height: 384),
            ])
    }

    @Test("A MacBook panel at More Space (3420×2214) gets a crisp mode that looks like 1710×1107")
    func oddHalfSize() throws {
        let configuration = try make(pixelSize: DisplaySize(width: 3420, height: 2214))
        let crisp = try #require(configuration.crispHiDPIResolution)
        #expect(crisp.points == DisplaySize(width: 1710, height: 1107))
        #expect(crisp.pixels == DisplaySize(width: 3420, height: 2214))
        #expect(configuration.modes.contains(crisp.points), "the window server gets the mode it has to back")
        #expect(try make(pixelSize: DisplaySize(width: 1365, height: 768)).crispHiDPIResolution == nil)
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
