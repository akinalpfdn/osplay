import CoreBridge
import Foundation
import OSLog

/// Debug option: appends each stream's Annex B bytes to a raw `.h264` file that ffplay, VLC or
/// IINA can play. Writes happen on a background queue so the encoder never waits for the disk.
final class StreamDump: @unchecked Sendable {
    // @unchecked: `handles` is only touched on `queue`.
    private let directory: URL
    private let prefix: String
    private let queue = DispatchQueue(label: "com.akinalpfdn.osplay.dump", qos: .utility)
    private var handles: [UInt8: FileHandle] = [:]
    private var names: [UInt8: String] = [:]

    init(directory: URL, startedAt date: Date = Date()) {
        self.directory = directory
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        prefix = "osplay-\(formatter.string(from: date))"
    }

    /// Call before the stream's first frame.
    func prepare(_ stream: StreamConfiguration) {
        let name = "\(prefix)-stream\(stream.streamID)-\(stream.width)x\(stream.height).h264"
        queue.async { [self] in names[stream.streamID] = name }
    }

    func write(_ unit: AccessUnit) {
        queue.async { [self] in
            guard let handle = handle(for: unit.streamID) else { return }
            do {
                try handle.write(contentsOf: unit.data)
            } catch {
                Log.host.error("Dump write failed: \(error.localizedDescription, privacy: .public)")
                handles[unit.streamID] = nil
                names[unit.streamID] = nil
            }
        }
    }

    /// Closes every file; returns their URLs.
    func finish() -> [URL] {
        queue.sync {
            for handle in handles.values {
                try? handle.close()  // a failed close leaves a complete file; nothing to recover
            }
            handles.removeAll()
            return names.values.sorted().map { directory.appending(path: $0) }
        }
    }

    private func handle(for streamID: UInt8) -> FileHandle? {
        if let handle = handles[streamID] { return handle }
        guard let name = names[streamID] else { return nil }
        let url = directory.appending(path: name)
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            guard FileManager.default.createFile(atPath: url.path, contents: nil) else { return nil }
            let handle = try FileHandle(forWritingTo: url)
            handles[streamID] = handle
            return handle
        } catch {
            Log.host.error(
                "Cannot create dump \(url.path, privacy: .public): \(error.localizedDescription, privacy: .public)")
            names[streamID] = nil
            return nil
        }
    }
}
