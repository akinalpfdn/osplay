import OsplayCore
import Testing

@Suite("Core version")
struct CoreVersionTests {
    @Test("Linked library matches the header the module was built against")
    func linkedLibraryMatchesHeader() {
        let version = osplay_core_version()

        #expect(Int(version.major) == Int(OSPLAY_CORE_VERSION_MAJOR))
        #expect(Int(version.minor) == Int(OSPLAY_CORE_VERSION_MINOR))
        #expect(Int(version.patch) == Int(OSPLAY_CORE_VERSION_PATCH))
    }
}
