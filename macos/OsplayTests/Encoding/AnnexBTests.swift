import Foundation
import Testing

@testable import Osplay

@Suite("Annex B conversion")
struct AnnexBTests {
    @Test("Length prefixes become start codes and parameter sets lead")
    func convertsWithParameterSets() throws {
        let avcc: [UInt8] = [0, 0, 0, 3, 0x65, 0xAA, 0xBB, 0, 0, 0, 1, 0x06]
        let sps = Data([0x67, 1, 2])
        let pps = Data([0x68, 3])
        let output = try avcc.withUnsafeBytes { bytes in
            try AnnexB.fromLengthPrefixed(bytes, lengthSize: 4, parameterSets: [sps, pps])
        }
        #expect(
            [UInt8](output) == [
                0, 0, 0, 1, 0x67, 1, 2, 0, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x65, 0xAA, 0xBB, 0, 0, 0, 1, 0x06,
            ])
    }

    @Test("Two-byte length prefixes are supported")
    func convertsShortLengths() throws {
        let avcc: [UInt8] = [0, 2, 0x41, 0x42]
        let output = try avcc.withUnsafeBytes { try AnnexB.fromLengthPrefixed($0, lengthSize: 2) }
        #expect([UInt8](output) == [0, 0, 0, 1, 0x41, 0x42])
    }

    @Test(
        "Malformed input is rejected",
        arguments: [
            ([0, 0, 0, 9, 0x65] as [UInt8], AnnexB.ConversionError.truncated),
            ([0, 0, 0] as [UInt8], .truncated),
            ([0, 0, 0, 0] as [UInt8], .emptyUnit),
        ])
    func rejectsMalformedInput(_ avcc: [UInt8], _ expected: AnnexB.ConversionError) {
        #expect(throws: expected) {
            try avcc.withUnsafeBytes { try AnnexB.fromLengthPrefixed($0, lengthSize: 4) }
        }
    }

    @Test("Unsupported length sizes are rejected")
    func rejectsLengthSize() {
        #expect(throws: AnnexB.ConversionError.unsupportedLengthSize(3)) {
            try [UInt8]().withUnsafeBytes { try AnnexB.fromLengthPrefixed($0, lengthSize: 3) }
        }
    }

    @Test("Annex B splits into NAL units with three- and four-byte start codes")
    func splitsNALUnits() throws {
        let data = Data([0, 0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2, 0, 0, 0, 1, 0x65, 3, 4])
        let units = try AnnexB.nalUnits(in: data)
        #expect(units == [Data([0x67, 1]), Data([0x68, 2]), Data([0x65, 3, 4])])
        #expect(units.map(AnnexB.type(of:)) == [7, 8, 5])
    }

    @Test("Slices become length-prefixed; parameter sets and delimiters are left out")
    func buildsLengthPrefixedSlices() {
        let units = [Data([0x09, 0xF0]), Data([0x67, 1]), Data([0x68, 2]), Data([0x65, 3, 4])]
        #expect([UInt8](AnnexB.lengthPrefixedSlices(units)) == [0, 0, 0, 3, 0x65, 3, 4])
    }

    @Test("Data that is not Annex B is rejected")
    func rejectsGarbage() {
        #expect(throws: AnnexB.ConversionError.truncated) { try AnnexB.nalUnits(in: Data([1, 2, 3, 4])) }
        #expect(throws: AnnexB.ConversionError.emptyUnit) { try AnnexB.nalUnits(in: Data([0, 0, 1, 0, 0, 1, 0x65])) }
    }
}
