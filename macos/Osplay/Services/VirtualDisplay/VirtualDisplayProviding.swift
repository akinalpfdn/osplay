import CoreGraphics

/// Creates OS-level virtual displays. Implemented over the private CGVirtualDisplay API; everything
/// else in the app depends only on this protocol.
///
/// Displays live exactly as long as the provider keeps them: `destroy()` on a handle, `destroyAll()`
/// or releasing the provider removes them, and macOS moves their windows to the remaining screens.
@MainActor
protocol VirtualDisplayProviding: AnyObject {
    var displays: [any VirtualDisplayHandle] { get }

    func createDisplay(
        _ configuration: VirtualDisplayConfiguration
    ) throws(VirtualDisplayError)
        -> any VirtualDisplayHandle

    func destroyAll()
}

/// One live virtual display.
@MainActor
protocol VirtualDisplayHandle: AnyObject {
    var configuration: VirtualDisplayConfiguration { get }
    /// Window server ID; valid until the display is destroyed.
    var displayID: CGDirectDisplayID { get }
    var isActive: Bool { get }

    /// Waits until this process can see the display's resolutions. A new display exists for the
    /// window server immediately, but an app learns its modes only after processing window server
    /// notifications; until then resolution queries return nothing. Call before `activate`.
    func waitUntilReady() async throws(VirtualDisplayError)
    /// Resolutions the user can currently pick, largest first.
    func availableResolutions() -> [DisplayResolution]
    func currentResolution() -> DisplayResolution?
    /// Switches resolution and stores it as the choice for this display identity, like System Settings.
    func activate(_ resolution: DisplayResolution) throws(VirtualDisplayError)
    /// Replaces the mode list, e.g. after the remote monitor changed. The display ID stays the same.
    func updateConfiguration(_ configuration: VirtualDisplayConfiguration) throws(VirtualDisplayError)
    func destroy()
}

enum VirtualDisplayError: Error, Equatable, Sendable {
    enum ConfigurationProblem: Equatable, Sendable {
        case pixelSizeOutOfRange
        case refreshRateOutOfRange
        case physicalSizeInvalid
        case nameEmpty
        case identityChanged
    }

    case invalidConfiguration(ConfigurationProblem)
    case duplicateIdentity
    case tooManyDisplays
    case creationFailed
    case settingsRejected
    case resolutionUnavailable
    case notReady
    case displayDestroyed
    case reconfigurationFailed(CGError.RawValue)
}
