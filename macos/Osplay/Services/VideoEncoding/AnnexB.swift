import Foundation

/// H.264 byte-stream framing (ITU-T H.264 Annex B), the wire format of protocol v1.
enum AnnexB {
    static let startCode: [UInt8] = [0, 0, 0, 1]

    enum ConversionError: Error, Equatable {
        case unsupportedLengthSize(Int)
        case truncated
        case emptyUnit
    }

    /// Converts length-prefixed NAL units (AVCC, as VideoToolbox emits them) to Annex B, with
    /// `parameterSets` (SPS, PPS) in front so every keyframe is decodable on its own.
    static func fromLengthPrefixed(
        _ avcc: UnsafeRawBufferPointer, lengthSize: Int, parameterSets: [Data] = []
    ) throws(ConversionError) -> Data {
        guard [1, 2, 4].contains(lengthSize) else { throw .unsupportedLengthSize(lengthSize) }
        var output = Data()
        output.reserveCapacity(avcc.count + parameterSets.reduce(0) { $0 + $1.count + startCode.count })
        for parameterSet in parameterSets {
            output.append(contentsOf: startCode)
            output.append(parameterSet)
        }
        var offset = 0
        while offset < avcc.count {
            guard avcc.count - offset >= lengthSize else { throw .truncated }
            var length = 0
            for index in 0..<lengthSize {
                length = length << 8 | Int(avcc[offset + index])
            }
            offset += lengthSize
            guard length > 0 else { throw .emptyUnit }
            guard avcc.count - offset >= length else { throw .truncated }
            output.append(contentsOf: startCode)
            output.append(contentsOf: UnsafeRawBufferPointer(rebasing: avcc[offset..<offset + length]))
            offset += length
        }
        return output
    }
}
