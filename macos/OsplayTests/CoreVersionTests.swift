import Testing

@testable import Osplay

@Suite("CoreBridge version")
struct CoreVersionTests {
    @Test("App links the core version its headers describe")
    func linkedVersionMatchesHeader() {
        #expect(CoreVersion.linked == CoreVersion.header)
    }

    @Test("Version renders as dotted triple")
    func description() {
        #expect(CoreVersion(major: 1, minor: 2, patch: 3).description == "1.2.3")
    }
}
