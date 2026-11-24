#if DEBUG
    import CoreBridge
    import CoreGraphics
    import Testing

    @testable import Osplay

    @Suite("Simulated TV")
    struct SimulatedTVTests {
        private let displays = [
            LocalDisplay(
                description: MonitorDescription(
                    id: 0, widthPixels: 3420, heightPixels: 2214, refreshMillihertz: 120_000,
                    physicalWidthMillimeters: 331, physicalHeightMillimeters: 214, scalePercent: 200,
                    name: "Built-in Retina Display"),
                displayID: 1, key: "built-in", frame: .zero),
            LocalDisplay(
                description: MonitorDescription(
                    id: 1, widthPixels: 2560, heightPixels: 1440, refreshMillihertz: 60_000,
                    physicalWidthMillimeters: 597, physicalHeightMillimeters: 336, scalePercent: 100,
                    name: "Studio"),
                displayID: 2, key: "studio", frame: .zero),
        ]

        @Test("Off offers this Mac's displays with its own decoder")
        func offKeepsTheDisplays() {
            #expect(SimulatedTV.off.offered(displays) == displays)
            #expect(SimulatedTV.off.capabilities(maximumStreams: 2) == nil)
        }

        @Test("A TV is one monitor of unknown size at 200%, shown on the first display")
        func aTVReplacesTheDisplays() throws {
            let offered = SimulatedTV.uhd4KDecoder30.offered(displays)
            let tv = try #require(offered.first)
            #expect(offered.count == 1)
            #expect(tv.displayID == 1)
            #expect(tv.description.widthPixels == 3840 && tv.description.heightPixels == 2160)
            #expect(tv.description.physicalWidthMillimeters == 0 && tv.description.physicalHeightMillimeters == 0)
            #expect(tv.description.scalePercent == 200)
            #expect(
                SimulatedTV.uhd4KDecoder30.capabilities(maximumStreams: 1)
                    == SessionCapabilities(
                        maximumStreams: 1, maximumWidth: 3840, maximumHeight: 2160, maximumRefreshHz: 30))
        }
    }
#endif
