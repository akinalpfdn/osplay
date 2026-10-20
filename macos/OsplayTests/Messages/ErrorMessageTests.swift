import CoreBridge
import Foundation
import OsplayCore
import Testing

@testable import Osplay

@Suite("Error messages")
struct ErrorMessageTests {
    /// Every documented core result code (docs/PROTOCOL.md "Error codes").
    private static let coreCodes: [UInt32] = Array(1...27)

    @Test("Every core error code has its own message; only internal failures show the code")
    func everyCoreErrorIsExplained() throws {
        for code in Self.coreCodes {
            let error = try #require(CoreError(result: osplay_result(rawValue: code)), "code \(code)")
            let message = error.sessionMessage
            #expect(!message.isEmpty)
            if error != .internalFailure {
                #expect(!message.contains(String(code)), "code \(code) falls back to the generic text")
            }
        }
    }

    @Test("Service errors map to text that names the fix")
    func serviceErrors() {
        let errors: [ConnectionError] = [
            .keyStore(.damaged), .keyStore(.keychain(-25300)), .discovery(.localNetworkDenied),
            .discovery(.service(-65563)), .noDisplay, .incompatible, .host(.screenRecordingPermissionMissing),
            .roleSwap(.cannotHost), .roleSwap(.busy), .roleSwap(.noAnswer), .roleSwapUnsupported,
            .roleSwapPeerNotFound,
        ]
        for error in errors {
            #expect(!error.message.isEmpty)
        }
        #expect(ConnectionError.discovery(.localNetworkDenied).needsLocalNetworkPermission)
        #expect(ConnectionError.host(.screenRecordingPermissionMissing).needsScreenRecordingPermission)
    }
}
