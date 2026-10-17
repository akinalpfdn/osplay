import OsplayCore

/// Version of the shared core, either as compiled into the headers or as actually linked.
struct CoreVersion: Equatable, Sendable, CustomStringConvertible {
    let major: Int
    let minor: Int
    let patch: Int

    static var linked: CoreVersion {
        let version = osplay_core_version()
        return CoreVersion(major: Int(version.major), minor: Int(version.minor), patch: Int(version.patch))
    }

    static let header = CoreVersion(
        major: Int(OSPLAY_CORE_VERSION_MAJOR),
        minor: Int(OSPLAY_CORE_VERSION_MINOR),
        patch: Int(OSPLAY_CORE_VERSION_PATCH)
    )

    var description: String { "\(major).\(minor).\(patch)" }
}
