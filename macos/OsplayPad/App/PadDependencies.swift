import Foundation

/// Composition root: the single place where the iPad app's concrete services are created.
@MainActor
final class PadDependencies {
    let decoders: any VideoDecoderMaking
    let presenters: PadPresenterFactory
    let power: any PowerAssertionManaging
    let keyStore: any KeyStoring
    let discovery: any PeerDiscovering
    let screen: any ScreenDescribing
    let settings: PadSettings

    init(
        decoders: any VideoDecoderMaking = VideoToolboxDecoderFactory(),
        presenters: PadPresenterFactory = PadPresenterFactory(),
        power: any PowerAssertionManaging = IdleTimerPower(),
        keyStore: any KeyStoring = KeychainKeyStore(),
        discovery: any PeerDiscovering = DNSServiceDiscovery(),
        screen: any ScreenDescribing = SceneScreen(),
        settings: PadSettings = PadSettings()
    ) {
        self.decoders = decoders
        self.presenters = presenters
        self.power = power
        self.keyStore = keyStore
        self.discovery = discovery
        self.screen = screen
        self.settings = settings
    }
}
