/// Composition root: the single place where concrete services are created and injected.
@MainActor
final class AppDependencies {
    let virtualDisplays: any VirtualDisplayProviding
    let captures: any ScreenCaptureMaking
    let encoders: any VideoEncoderMaking
    let permissions: any PermissionChecking
    let decoders: any VideoDecoderMaking
    let presenters: any FramePresenterMaking
    let power: any PowerAssertionManaging
    let hotkeys: any HotkeyRegistering
    let keyStore: any KeyStoring
    let discovery: any PeerDiscovering
    let usbDevices: any USBDeviceWatching
    let androidAccessories: any AndroidAccessoryWatching
    let pairingPrompt: any PairingPrompting
    let settings: AppSettings

    init(
        virtualDisplays: any VirtualDisplayProviding = CGVirtualDisplayProvider(),
        captures: any ScreenCaptureMaking = ScreenCaptureKitCaptureFactory(),
        encoders: any VideoEncoderMaking = VideoToolboxEncoderFactory(),
        permissions: any PermissionChecking = SystemPermissions(),
        decoders: any VideoDecoderMaking = VideoToolboxDecoderFactory(),
        presenters: any FramePresenterMaking = SampleBufferPresenterFactory(),
        power: any PowerAssertionManaging = SystemPower(),
        hotkeys: any HotkeyRegistering = CarbonHotkeys(),
        keyStore: any KeyStoring = KeychainKeyStore(),
        discovery: any PeerDiscovering = DNSServiceDiscovery(),
        usbDevices: any USBDeviceWatching = CoreUSBDevices(),
        androidAccessories: any AndroidAccessoryWatching = IOKitAndroidAccessories(),
        pairingPrompt: (any PairingPrompting)? = nil,
        settings: AppSettings? = nil
    ) {
        self.virtualDisplays = virtualDisplays
        self.captures = captures
        self.encoders = encoders
        self.permissions = permissions
        self.decoders = decoders
        self.presenters = presenters
        self.power = power
        self.hotkeys = hotkeys
        self.keyStore = keyStore
        self.discovery = discovery
        self.usbDevices = usbDevices
        self.androidAccessories = androidAccessories
        self.pairingPrompt = pairingPrompt ?? PairingPanel()
        self.settings = settings ?? AppSettings()
    }
}
