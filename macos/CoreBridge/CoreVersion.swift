import OsplayCore

/// Version of the shared core, either as compiled into the headers or as actually linked.
public struct CoreVersion: Equatable, Sendable, CustomStringConvertible {
    public let major: Int
    public let minor: Int
    public let patch: Int

    public init(major: Int, minor: Int, patch: Int) {
        self.major = major
        self.minor = minor
        self.patch = patch
    }

    public static var linked: CoreVersion {
        let version = osplay_core_version()
        return CoreVersion(major: Int(version.major), minor: Int(version.minor), patch: Int(version.patch))
    }

    public static let header = CoreVersion(
        major: Int(OSPLAY_CORE_VERSION_MAJOR),
        minor: Int(OSPLAY_CORE_VERSION_MINOR),
        patch: Int(OSPLAY_CORE_VERSION_PATCH)
    )

    public var description: String { "\(major).\(minor).\(patch)" }
}
