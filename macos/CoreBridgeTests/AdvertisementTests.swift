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
        #expect(record["pm"] == "2")
        #expect(try Advertisement(txtRecord: record) == advertisement)
    }

    @Test("A record without a minor version comes from Osplay 0.1")
    func osplay01Record() throws {
        let parsed = try Advertisement(txtRecord: [
            "pv": "1", "name": "Old PC", "id": String(repeating: "ab", count: 16), "roles": "host", "os": "windows",
        ])
        #expect(parsed.protocolMinor == 0)
    }

    @Test("An iPad viewer's record reads back as iPadOS")
    func iPadRecord() throws {
        let iPad = Advertisement(
            protocolVersion: 1, platform: .iPadOS, roles: .viewer, deviceID: String(repeating: "cd", count: 16),
            name: "iPad")
        let record = try iPad.txtRecord()
        #expect(record["os"] == "ipados")
        #expect(try Advertisement(txtRecord: record) == iPad)
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
