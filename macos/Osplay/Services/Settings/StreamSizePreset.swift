import CoreBridge
import Foundation

/// The largest size a shared display is streamed at (DEVPLAN Phase 13c). The virtual display keeps
/// the viewer monitor's resolution; the stream is fitted inside this box and the viewer scales it
/// up, which keeps several large monitors within what the encoder and the network can carry.
enum StreamSizePreset: String, CaseIterable, Codable, Sendable, Identifiable {
    case hd720
    case hd1080
    case qhd1440
    case native

    var id: Self { self }

    /// Width × height of the box; nil for the monitor's own size.
    var box: (width: Int, height: Int)? {
        switch self {
        case .hd720: Tunables.Encoding.streamBox720
        case .hd1080: Tunables.Encoding.streamBox1080
        case .qhd1440: Tunables.Encoding.streamBox1440
        case .native: nil
        }
    }

    /// The stream size for a monitor of this many pixels.
    func streamSize(width: Int, height: Int) -> (width: Int, height: Int) {
        StreamSizing.fit(width: width, height: height, inside: box)
    }
}
