import OSLog

/// Unified logging: one subsystem, one category per module.
enum Log {
    static let subsystem = "com.akinalpfdn.osplay"

    static let virtualDisplay = Logger(subsystem: subsystem, category: "VirtualDisplay")
}
