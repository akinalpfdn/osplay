import OSLog

/// Unified logging: one subsystem, one category per module.
enum Log {
    static let subsystem = "com.akinalpfdn.osplay"

    static let virtualDisplay = Logger(subsystem: subsystem, category: "VirtualDisplay")
    static let capture = Logger(subsystem: subsystem, category: "Capture")
    static let encoding = Logger(subsystem: subsystem, category: "Encoding")
    static let host = Logger(subsystem: subsystem, category: "Host")
}
