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

    enum NALType {
        static let idr: UInt8 = 5
        static let sps: UInt8 = 7
        static let pps: UInt8 = 8
        static let accessUnitDelimiter: UInt8 = 9
    }

    /// The NAL units of an Annex B buffer, without start codes, in order.
    static func nalUnits(in data: Data) throws(ConversionError) -> [Data] {
        let bytes = [UInt8](data)
        var starts: [(code: Int, payload: Int)] = []
        var index = 0
        while index + 3 <= bytes.count {
            if bytes[index] == 0, bytes[index + 1] == 0, bytes[index + 2] == 1 {
                let code = index > 0 && bytes[index - 1] == 0 ? index - 1 : index
                starts.append((code, index + 3))
                index += 3
            } else {
                index += 1
            }
        }
        guard let first = starts.first, bytes[0..<first.code].allSatisfy({ $0 == 0 }) else {
            throw .truncated  // not Annex B: data before the first start code
        }
        var units: [Data] = []
        for (position, start) in starts.enumerated() {
            let end = position + 1 < starts.count ? starts[position + 1].code : bytes.count
            guard end > start.payload else { throw .emptyUnit }
            units.append(Data(bytes[start.payload..<end]))
        }
        return units
    }

    static func type(of unit: Data) -> UInt8 {
        (unit.first ?? 0) & 0x1F
    }

    /// Length-prefixed (4-byte, big-endian) NAL units for VideoToolbox, leaving out parameter
    /// sets and delimiters, which travel in the format description.
    static func lengthPrefixedSlices(_ units: [Data]) -> Data {
        var output = Data()
        for unit in units {
            let type = type(of: unit)
            guard type != NALType.sps, type != NALType.pps, type != NALType.accessUnitDelimiter else { continue }
            var length = UInt32(unit.count).bigEndian
            withUnsafeBytes(of: &length) { output.append(contentsOf: $0) }
            output.append(unit)
        }
        return output
    }
}
