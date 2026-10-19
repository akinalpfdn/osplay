import CoreBridge
import Testing

@Suite("Bonjour advertisement")
struct AdvertisementTests {
    @Test("A Mac host's record uses the documented values and reads back")
    func roundTrip() throws {
        let identity = try DeviceIdentity.generate()
        let advertisement = Advertisement(identity: identity, name: "MacBook Air", roles: .host)
        let record = try advertisement.txtRecord()

        #expect(record["pv"] == "1")
        #expect(record["os"] == "macos")
        #expect(record["roles"] == "host")
        #expect(record["id"] == identity.deviceID)
        #expect(try Advertisement(txtRecord: record) == advertisement)
    }

    @Test("A Windows record with both roles is understood; unknown keys are ignored")
    func windowsRecord() throws {
        let parsed = try Advertisement(txtRecord: [
            "pv": "1", "name": "Akinalp's PC", "id": String(repeating: "ab", count: 16), "roles": "host,viewer",
            "os": "windows", "future": "x",
        ])
        #expect(parsed.platform == .windows)
        #expect(parsed.roles == [.host, .viewer])
        #expect(parsed.name == "Akinalp's PC")
    }

    @Test("Incomplete or malformed records are rejected")
    func malformed() {
        #expect(throws: CoreError.self) { try Advertisement(txtRecord: ["pv": "1", "name": "PC"]) }
        #expect(throws: CoreError.self) {
            try Advertisement(txtRecord: [
                "pv": "1", "name": "PC", "id": String(repeating: "AB", count: 16), "roles": "host", "os": "windows",
            ])
        }
        let tooLong = Advertisement(
            protocolVersion: 1, platform: .macOS, roles: .host, deviceID: String(repeating: "ab", count: 16),
            name: String(repeating: "x", count: 65))
        #expect(throws: CoreError.self) { try tooLong.txtRecord() }
    }
}
