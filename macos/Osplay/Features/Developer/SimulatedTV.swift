#if DEBUG
    import CoreBridge
    import Foundation

    /// A TV this Mac pretends to be when it views another computer (TV-01): the host sees one
    /// monitor of unknown size at a TV's scale and a TV chip's decoder limits, and the stream is
    /// shown full screen on the first offered display. Tests the host side without a TV.
    enum SimulatedTV: String, CaseIterable, Identifiable, Sendable {
        case off
        case uhd4K
        case uhd4KDecoder30
        case fullHD

        var id: Self { self }

        var label: LocalizedStringResource {
            switch self {
            case .off: "developer.tv.off"
            case .uhd4K: "developer.tv.uhd4K"
            case .uhd4KDecoder30: "developer.tv.uhd4KDecoder30"
            case .fullHD: "developer.tv.fullHD"
            }
        }

        /// The TV's mode and the decoder's limits; nil when off.
        private var profile: (width: Int, height: Int, decoderRefreshHz: Int)? {
            switch self {
            case .off: nil
            case .uhd4K: (3840, 2160, 60)
            case .uhd4KDecoder30: (3840, 2160, 30)
            case .fullHD: (1920, 1080, 60)
            }
        }

        /// The first offered display posing as the TV, or the displays unchanged when off.
        func offered(_ displays: [LocalDisplay]) -> [LocalDisplay] {
            guard let profile, let first = displays.first else { return displays }
            let tv = MonitorDescription(
                id: first.id, widthPixels: profile.width, heightPixels: profile.height,
                refreshMillihertz: Tunables.Developer.simulatedTVRefreshMillihertz, physicalWidthMillimeters: 0,
                physicalHeightMillimeters: 0, scalePercent: Tunables.Developer.simulatedTVScalePercent,
                name: String(localized: "developer.tv.name"))
            return [LocalDisplay(description: tv, displayID: first.displayID, key: first.key, frame: first.frame)]
        }

        /// What the TV's decoder handles; nil when off.
        func capabilities(maximumStreams: Int) -> SessionCapabilities? {
            profile.map { profile in
                SessionCapabilities(
                    maximumStreams: maximumStreams, maximumWidth: profile.width, maximumHeight: profile.height,
                    maximumRefreshHz: profile.decoderRefreshHz)
            }
        }
    }
#endif
