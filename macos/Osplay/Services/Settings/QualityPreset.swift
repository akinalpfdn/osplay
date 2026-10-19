import Foundation

/// How much bandwidth a shared display may use (DEVPLAN: bits per pixel per frame, so the bitrate
/// follows resolution and refresh rate).
enum QualityPreset: String, CaseIterable, Codable, Sendable, Identifiable {
    case balanced
    case high
    case ultra

    var id: Self { self }

    var bitsPerPixel: Double {
        switch self {
        case .balanced: Tunables.Encoding.balancedBitsPerPixel
        case .high: Tunables.Encoding.highBitsPerPixel
        case .ultra: Tunables.Encoding.ultraBitsPerPixel
        }
    }

    func bitrateKbps(width: Int, height: Int, frameRate: Double) -> Int {
        let kbps = Int(Double(width * height) * frameRate * bitsPerPixel / 1000)
        let range = Tunables.Encoding.bitrateRangeKbps
        return min(max(kbps, range.lowerBound), range.upperBound)
    }
}

/// How a virtual display standing in for one remote monitor starts.
enum HiDPIChoice: String, CaseIterable, Codable, Sendable, Identifiable {
    /// Follow the remote monitor's own scaling: HiDPI from 150%.
    case automatic
    /// Sharp text at half the size ("looks like" half the pixels).
    case always
    /// One point per pixel: the most space.
    case never

    var id: Self { self }
}
