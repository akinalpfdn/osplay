import Foundation
import OSLog

enum LogExportError: Error, Equatable, Sendable {
    case unreadable(String)
    case unwritable(String)
}

/// Writes Osplay's recent log messages to ~/Library/Logs/Osplay for bug reports (DEVPLAN:
/// Advanced › logs folder). Logging itself stays in the unified log.
protocol LogExporting: Sendable {
    var folder: URL { get }
    func export() async throws(LogExportError) -> URL
}

struct UnifiedLogExporter: LogExporting {
    var folder: URL {
        FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0].appending(path: "Logs/Osplay")
    }

    func export() async throws(LogExportError) -> URL {
        let entries: [OSLogEntryLog]
        do {
            let store = try OSLogStore(scope: .currentProcessIdentifier)
            let since = store.position(date: Date().addingTimeInterval(-Tunables.Logs.exportWindow))
            entries = try store.getEntries(at: since, matching: NSPredicate(format: "subsystem == %@", Log.subsystem))
                .compactMap { $0 as? OSLogEntryLog }
        } catch {
            throw .unreadable(error.localizedDescription)
        }
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let text = entries.map { entry in
            "\(formatter.string(from: entry.date)) [\(entry.category)] \(entry.composedMessage)"
        }.joined(separator: "\n")
        let stamp = formatter.string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let file = folder.appending(path: "Osplay-\(stamp).log")
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try (text + "\n").write(to: file, atomically: true, encoding: .utf8)
        } catch {
            throw .unwritable(error.localizedDescription)
        }
        return file
    }
}
