import CoreGraphics

@testable import Osplay

@MainActor
final class FakeVirtualDisplayProvider: VirtualDisplayProviding {
    var creationError: VirtualDisplayError?
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
        createdConfigurations.append(configuration)
        let handle = FakeVirtualDisplayHandle(displayID: nextDisplayID, configuration: configuration) {
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
    private let onDestroy: (FakeVirtualDisplayHandle) -> Void

    init(
        displayID: CGDirectDisplayID,
        configuration: VirtualDisplayConfiguration,
        onDestroy: @escaping (FakeVirtualDisplayHandle) -> Void
    ) {
        self.displayID = displayID
        self.configuration = configuration
        self.onDestroy = onDestroy
    }

    func availableResolutions() -> [DisplayResolution] {
        configuration.modes.map {
            DisplayResolution(points: $0, pixels: $0, refreshRate: configuration.refreshRate)
        } + [configuration.crispHiDPIResolution].compactMap { $0 }
    }

    func currentResolution() -> DisplayResolution? { activated.last }

    func activate(_ resolution: DisplayResolution) throws(VirtualDisplayError) {
        guard isActive else { throw .displayDestroyed }
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
