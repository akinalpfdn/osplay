// swift-tools-version: 6.0
import PackageDescription

// Exposes the shared C++ core to Swift. Only the C header in core/include is public; the
// CMake build in core/ is the reference build and must list the same sources.
let package = Package(
    name: "OsplayCore",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "OsplayCore", targets: ["OsplayCore"])
    ],
    targets: [
        .target(
            name: "OsplayCore",
            path: "core",
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [.headerSearchPath("src")]
        ),
        .testTarget(
            name: "OsplayCoreTests",
            dependencies: ["OsplayCore"],
            path: "core/tests/swift/OsplayCoreTests"
        ),
    ],
    cxxLanguageStandard: .cxx20
)
