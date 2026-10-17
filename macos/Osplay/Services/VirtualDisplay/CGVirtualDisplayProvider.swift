import CoreGraphics
import OSLog

/// `VirtualDisplayProviding` over CoreGraphics' private `CGVirtualDisplay`. The only file that uses
/// the declarations in `CGVirtualDisplayPrivate.h`.
@MainActor
final class CGVirtualDisplayProvider: VirtualDisplayProviding {
    private var handles: [CGVirtualDisplayHandle] = []

    var displays: [any VirtualDisplayHandle] { handles }

    func createDisplay(
        _ configuration: VirtualDisplayConfiguration
    ) throws(VirtualDisplayError)
        -> any VirtualDisplayHandle
    {
        guard handles.count < Tunables.VirtualDisplay.maximumDisplayCount else { throw .tooManyDisplays }
        guard !handles.contains(where: { $0.configuration.identity == configuration.identity }) else {
            throw .duplicateIdentity
        }

        let display = try Self.makeDisplay(configuration)
        let handle = CGVirtualDisplayHandle(display: display, configuration: configuration) { [weak self] handle in
            self?.handles.removeAll { $0 === handle }
        }
        handles.append(handle)
        Log.virtualDisplay.info(
            "Created display \(display.displayID) \(configuration.pixelSize.description, privacy: .public) hiDPI=\(configuration.isHiDPI)"
        )
        return handle
    }

    func destroyAll() {
        for handle in handles {
            handle.destroy()
        }
    }

    private static func makeDisplay(
        _ configuration: VirtualDisplayConfiguration
    ) throws(VirtualDisplayError)
        -> CGVirtualDisplay
    {
        let descriptor = CGVirtualDisplayDescriptor()
        descriptor.queue = DispatchQueue.main
        descriptor.name = configuration.name
        descriptor.maxPixelsWide = UInt32(configuration.maximumPixelSize.width)
        descriptor.maxPixelsHigh = UInt32(configuration.maximumPixelSize.height)
        descriptor.sizeInMillimeters = configuration.physicalSizeMillimeters
        descriptor.vendorID = configuration.identity.vendorID
        descriptor.productID = configuration.identity.productID
        descriptor.serialNum = configuration.identity.serialNumber

        guard let display = CGVirtualDisplay(descriptor: descriptor), display.displayID != kCGNullDirectDisplay else {
            Log.virtualDisplay.error("CGVirtualDisplay creation failed")
            throw .creationFailed
        }
        guard display.apply(settings(for: configuration)) else {
            Log.virtualDisplay.error("CGVirtualDisplay rejected settings for \(display.displayID)")
            throw .settingsRejected
        }
        return display
    }

    fileprivate static func settings(for configuration: VirtualDisplayConfiguration) -> CGVirtualDisplaySettings {
        let settings = CGVirtualDisplaySettings()
        settings.hiDPI = configuration.isHiDPI ? 1 : 0
        settings.modes = configuration.modes.map {
            CGVirtualDisplayMode(
                width: UInt32($0.width),
                height: UInt32($0.height),
                refreshRate: configuration.refreshRate
            )
        }
        return settings
    }
}

@MainActor
private final class CGVirtualDisplayHandle: VirtualDisplayHandle {
    private(set) var configuration: VirtualDisplayConfiguration
    let displayID: CGDirectDisplayID
    // Releasing this object is what removes the display from the system.
    private var display: CGVirtualDisplay?
    private let onDestroy: (CGVirtualDisplayHandle) -> Void

    init(
        display: CGVirtualDisplay,
        configuration: VirtualDisplayConfiguration,
        onDestroy: @escaping (CGVirtualDisplayHandle) -> Void
    ) {
        self.display = display
        self.displayID = display.displayID
        self.configuration = configuration
        self.onDestroy = onDestroy
    }

    var isActive: Bool { display != nil }

    func waitUntilReady() async throws(VirtualDisplayError) {
        let tunables = Tunables.VirtualDisplay.self
        let clock = ContinuousClock()
        let deadline = clock.now + tunables.readinessTimeout
        while !isReady {
            guard isActive else { throw .displayDestroyed }
            guard clock.now < deadline else {
                Log.virtualDisplay.error("Display \(self.displayID) modes not visible after timeout")
                throw .notReady
            }
            // Sleeping on the main actor lets the run loop deliver the window server notifications
            // that make the modes visible. Cancellation ends the wait like a timeout.
            do {
                try await Task.sleep(for: tunables.readinessPollInterval)
            } catch {
                throw .notReady
            }
        }
    }

    /// The native 1× mode is always generated, so its presence means this configuration's modes are visible.
    private var isReady: Bool {
        availableResolutions().contains { !$0.isHiDPI && $0.pixels == configuration.pixelSize }
    }

    func availableResolutions() -> [DisplayResolution] {
        guard isActive else { return [] }
        return DisplayModes.available(for: displayID).map(\.resolution)
    }

    func currentResolution() -> DisplayResolution? {
        guard isActive else { return nil }
        return DisplayModes.current(for: displayID)?.resolution
    }

    func activate(_ resolution: DisplayResolution) throws(VirtualDisplayError) {
        guard isActive else { throw .displayDestroyed }
        try DisplayModes.activate(resolution, on: displayID)
    }

    func updateConfiguration(_ newConfiguration: VirtualDisplayConfiguration) throws(VirtualDisplayError) {
        guard let display else { throw .displayDestroyed }
        // Name, identity and maximum size are fixed when the display is created.
        guard newConfiguration.identity == configuration.identity else {
            throw .invalidConfiguration(.identityChanged)
        }
        guard display.apply(CGVirtualDisplayProvider.settings(for: newConfiguration)) else {
            throw .settingsRejected
        }
        configuration = newConfiguration
    }

    func destroy() {
        guard display != nil else { return }
        display = nil
        Log.virtualDisplay.info("Destroyed display \(self.displayID)")
        onDestroy(self)
    }
}
