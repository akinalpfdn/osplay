import Foundation

/// Text as the protocol carries it (PROTOCOL.md "Conventions").
enum ProtocolText {
    private static let nameBytes = 64

    /// A device or monitor name: no control characters, at most 64 bytes of UTF-8, cut between
    /// characters.
    static func name(_ name: String) -> String {
        var result = ""
        for character in name {
            if character.unicodeScalars.contains(where: { $0.properties.generalCategory == .control }) { continue }
            guard result.utf8.count + String(character).utf8.count <= nameBytes else { break }
            result.append(character)
        }
        return result
    }
}
