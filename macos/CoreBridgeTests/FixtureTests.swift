import CoreBridge
import Foundation
import Testing

/// One line of protocol/fixtures/manifest.tsv (docs/PROTOCOL.md, "Fixtures").
struct Fixture: Sendable, CustomTestStringConvertible {
    let file: String
    let kind: WireKind
    let isValid: Bool
    /// Message type when valid, error code otherwise.
    let detail: UInt32
    let description: String

    var testDescription: String { "\(file) — \(description)" }

    static let directory = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()  // CoreBridgeTests
        .deletingLastPathComponent()  // macos
        .deletingLastPathComponent()  // repository root
        .appendingPathComponent("protocol/fixtures")

    static let all: [Fixture] = {
        guard let manifest = try? String(contentsOf: directory.appendingPathComponent("manifest.tsv"), encoding: .utf8)
        else { return [] }  // FixtureTests.manifestIsReadable reports this
        return manifest.split(separator: "\n").compactMap { line -> Fixture? in
            guard !line.hasPrefix("#") else { return nil }
            let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard fields.count == 5, let detail = UInt32(fields[3]) else { return nil }
            return Fixture(
                file: fields[0],
                kind: fields[1] == "control" ? .controlMessage : .mediaPacket,
                isValid: fields[2] == "valid",
                detail: detail,
                description: fields[4]
            )
        }
    }()

    func bytes() throws -> [UInt8] {
        [UInt8](try Data(contentsOf: Fixture.directory.appendingPathComponent(file)))
    }
}

@Suite("Protocol fixtures through CoreBridge")
struct FixtureTests {
    @Test("The manifest is readable and lists every fixture")
    func manifestIsReadable() {
        #expect(Fixture.all.count == 82)
    }

    @Test("Each fixture reaches the outcome the manifest promises", arguments: Fixture.all)
    func fixtureMatchesManifest(_ fixture: Fixture) throws {
        let bytes = try fixture.bytes()

        switch ProtocolDiagnostics.validate(bytes, as: fixture.kind) {
        case .success(let type):
            #expect(fixture.isValid)
            #expect(UInt32(type) == fixture.detail)
            #expect(try ProtocolDiagnostics.reencode(bytes, as: fixture.kind).get() == bytes)
        case .failure(let error):
            #expect(!fixture.isValid)
            #expect(error.code == fixture.detail)
        }
    }
}
