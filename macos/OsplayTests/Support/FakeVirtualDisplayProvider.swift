import CoreGraphics

@testable import Osplay

@MainActor
final class FakeVirtualDisplayProvider: VirtualDisplayProviding {
    var creationError: VirtualDisplayError?
    /// Fails creation of the display with this index (0-based), to test partial failures.
    var failingCreationIndex: Int?
    var readinessError: VirtualDisplayError?
    /// Offers no 2× modes, like a window server that refused them.
    var hidesHiDPIModes = false
    private(set) var handles: [FakeVirtualDisplayHandle] = []
    private(set) var createdConfigurations: [VirtualDisplayConfiguration] = []
    private var nextDisplayID: CGDirectDisplayID = 100

    var displays: [any VirtualDisplayHandle] { handles }

    func createDisplay(
        _ configuration: VirtualDisplayConfiguration
    ) throws(VirtualDisplayError)
        -> any VirtualDisplayHandle
    {
        if let creationError { throw creationError }
        if failingCreationIndex == createdConfigurations.count { throw .creationFailed }
        createdConfigurations.append(configuration)
        let handle = FakeVirtualDisplayHandle(
            displayID: nextDisplayID,
            configuration: configuration,
            readinessError: readinessError,
            hidesHiDPIModes: hidesHiDPIModes
        ) {
            [weak self] handle in
            self?.handles.removeAll { $0 === handle }
        }
        nextDisplayID += 1
        handles.append(handle)
        return handle
    }

    func destroyAll() {
        for handle in handles {
            handle.destroy()
        }
    }
}

@MainActor
final class FakeVirtualDisplayHandle: VirtualDisplayHandle {
    let displayID: CGDirectDisplayID
    private(set) var configuration: VirtualDisplayConfiguration
    private(set) var isActive = true
    private(set) var activated: [DisplayResolution] = []
    private(set) var waitedUntilReady = false
    private let readinessError: VirtualDisplayError?
    private let hidesHiDPIModes: Bool
    private let onDestroy: (FakeVirtualDisplayHandle) -> Void

    init(
        displayID: CGDirectDisplayID,
        configuration: VirtualDisplayConfiguration,
        readinessError: VirtualDisplayError?,
        hidesHiDPIModes: Bool = false,
        onDestroy: @escaping (FakeVirtualDisplayHandle) -> Void
    ) {
        self.displayID = displayID
        self.configuration = configuration
        self.readinessError = readinessError
        self.hidesHiDPIModes = hidesHiDPIModes
        self.onDestroy = onDestroy
    }

    func waitUntilReady() async throws(VirtualDisplayError) {
        if let readinessError { throw readinessError }
        waitedUntilReady = true
    }

    /// Like the window server: every mode at 1×, and at 2× when that backing fits the maximum.
    func availableResolutions() -> [DisplayResolution] {
        let rate = configuration.refreshRate
        let maximum = configuration.maximumPixelSize
        let single = configuration.modes.map { DisplayResolution(points: $0, pixels: $0, refreshRate: rate) }
        let double = configuration.modes.compactMap { mode -> DisplayResolution? in
            let backing = DisplaySize(width: mode.width * 2, height: mode.height * 2)
            guard configuration.isHiDPI, !hidesHiDPIModes, backing.width <= maximum.width,
                backing.height <= maximum.height
            else { return nil }
            return DisplayResolution(points: mode, pixels: backing, refreshRate: rate)
        }
        return single + double
    }

    func currentResolution() -> DisplayResolution? { activated.last }

    func activate(_ resolution: DisplayResolution) throws(VirtualDisplayError) {
        guard isActive else { throw .displayDestroyed }
        guard waitedUntilReady else { throw .notReady }
        guard availableResolutions().contains(resolution) else { throw .resolutionUnavailable }
        activated.append(resolution)
    }

    func updateConfiguration(_ configuration: VirtualDisplayConfiguration) throws(VirtualDisplayError) {
        self.configuration = configuration
    }

    func destroy() {
        guard isActive else { return }
        isActive = false
        onDestroy(self)
    }
}
