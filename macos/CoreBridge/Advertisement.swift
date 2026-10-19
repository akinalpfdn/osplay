import OsplayCore

/// What a device announces over Bonjour (docs/PROTOCOL.md "Discovery"). Encoding and checks come
/// from core, so both platforms read and write the same TXT record.
public struct Advertisement: Sendable, Equatable {
    public struct Roles: OptionSet, Sendable, Hashable {
        public let rawValue: UInt8
        public init(rawValue: UInt8) { self.rawValue = rawValue }
        public static let host = Roles(rawValue: UInt8(OSPLAY_ROLE_HOST))
        public static let viewer = Roles(rawValue: UInt8(OSPLAY_ROLE_VIEWER))
    }

    public static let serviceType = OSPLAY_SERVICE_TYPE
    public static let protocolVersion = UInt32(OSPLAY_PROTOCOL_VERSION)

    public let protocolVersion: UInt32
    public let platform: PeerPlatform
    public let roles: Roles
    /// Lowercase hex, as in the TXT record.
    public let deviceID: String
    public let name: String

    /// This device: the current protocol version, macOS, and the identity's device ID.
    public init(identity: DeviceIdentity, name: String, roles: Roles) {
        self.init(
            protocolVersion: Self.protocolVersion, platform: .macOS, roles: roles, deviceID: identity.deviceID,
            name: name)
    }

    public init(protocolVersion: UInt32, platform: PeerPlatform, roles: Roles, deviceID: String, name: String) {
        self.protocolVersion = protocolVersion
        self.platform = platform
        self.roles = roles
        self.deviceID = deviceID
        self.name = name
    }

    /// A peer's record; keys it does not know are ignored, missing ones fail the checks.
    public init(txtRecord: [String: String]) throws(CoreError) {
        var record = osplay_txt_record()
        Self.write(txtRecord[OSPLAY_TXT_KEY_PROTOCOL_VERSION] ?? "", into: &record.protocol_version)
        Self.write(txtRecord[OSPLAY_TXT_KEY_NAME] ?? "", into: &record.name)
        Self.write(txtRecord[OSPLAY_TXT_KEY_DEVICE_ID] ?? "", into: &record.device_id)
        Self.write(txtRecord[OSPLAY_TXT_KEY_ROLES] ?? "", into: &record.roles)
        Self.write(txtRecord[OSPLAY_TXT_KEY_OS] ?? "", into: &record.os)
        var parsed = osplay_advertisement()
        if let error = CoreError(result: osplay_advertisement_from_txt(&record, &parsed)) {
            throw error
        }
        self.init(
            protocolVersion: parsed.protocol_version, platform: PeerPlatform(parsed.platform),
            roles: Roles(rawValue: parsed.roles), deviceID: Self.hex(of: parsed.device_id),
            name: Self.read(parsed.name))
    }

    /// The TXT record to register; fails for an invalid name or empty roles.
    public func txtRecord() throws(CoreError) -> [String: String] {
        var advertisement = osplay_advertisement()
        advertisement.protocol_version = protocolVersion
        advertisement.platform = platform.raw
        advertisement.roles = roles.rawValue
        guard let idBytes = Self.bytes(ofHex: deviceID), idBytes.count == Int(OSPLAY_DEVICE_ID_SIZE) else {
            throw .valueOutOfRange
        }
        withUnsafeMutableBytes(of: &advertisement.device_id) { buffer in
            buffer.copyBytes(from: idBytes)
        }
        guard name.utf8.count < MemoryLayout.size(ofValue: advertisement.name) else { throw .invalidString }
        Self.write(name, into: &advertisement.name)
        var record = osplay_txt_record()
        if let error = CoreError(result: osplay_advertisement_to_txt(&advertisement, &record)) {
            throw error
        }
        return [
            OSPLAY_TXT_KEY_PROTOCOL_VERSION: Self.read(record.protocol_version),
            OSPLAY_TXT_KEY_NAME: Self.read(record.name),
            OSPLAY_TXT_KEY_DEVICE_ID: Self.read(record.device_id),
            OSPLAY_TXT_KEY_ROLES: Self.read(record.roles),
            OSPLAY_TXT_KEY_OS: Self.read(record.os),
        ]
    }

    // C `char[OSPLAY_TXT_VALUE_SIZE]` arrays arrive in Swift as fixed-size tuples.

    private static func write<Tuple>(_ text: String, into field: inout Tuple) {
        withUnsafeMutableBytes(of: &field) { buffer in
            buffer.initializeMemory(as: UInt8.self, repeating: 0)
            let bytes = Array(text.utf8.prefix(buffer.count - 1))
            buffer.copyBytes(from: bytes)
        }
    }

    private static func read<Tuple>(_ field: Tuple) -> String {
        withUnsafeBytes(of: field) { buffer in
            String(decoding: buffer.prefix { $0 != 0 }, as: UTF8.self)
        }
    }

    private static func hex<Tuple>(of field: Tuple) -> String {
        withUnsafeBytes(of: field) { buffer in
            buffer.map { byte in byte < 0x10 ? "0" + String(byte, radix: 16) : String(byte, radix: 16) }.joined()
        }
    }

    private static func bytes(ofHex text: String) -> [UInt8]? {
        let digits = Array(text.utf8)
        guard digits.count.isMultiple(of: 2) else { return nil }
        var bytes: [UInt8] = []
        var index = 0
        while index < digits.count {
            guard let high = hexValue(digits[index]), let low = hexValue(digits[index + 1]) else { return nil }
            bytes.append(high << 4 | low)
            index += 2
        }
        return bytes
    }

    private static func hexValue(_ digit: UInt8) -> UInt8? {
        switch digit {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): digit - UInt8(ascii: "0")
        case UInt8(ascii: "a")...UInt8(ascii: "f"): digit - UInt8(ascii: "a") + 10
        default: nil
        }
    }
}
