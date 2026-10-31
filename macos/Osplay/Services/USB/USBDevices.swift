import CoreBridge
import Foundation

/// The iPads on a USB cable; `CoreUSBDevices` in the app, a fake in tests.
protocol USBDeviceWatching: Sendable {
    /// Attach and detach events until the stream's consumer stops; empty when usbmuxd cannot be
    /// watched.
    func watch() -> AsyncStream<USBDeviceEvent>
}

/// Follows usbmuxd through the core for as long as a stream is consumed.
struct CoreUSBDevices: USBDeviceWatching {
    func watch() -> AsyncStream<USBDeviceEvent> {
        AsyncStream { continuation in
            let watcher: USBDeviceWatcher
            do throws(CoreError) {
                watcher = try USBDeviceWatcher()
            } catch {
                Log.connection.error("Cannot watch USB devices: \(String(describing: error), privacy: .public)")
                continuation.finish()
                return
            }
            let forwarding = Task {
                for await event in watcher.events {
                    continuation.yield(event)
                }
                continuation.finish()
            }
            continuation.onTermination = { _ in
                forwarding.cancel()
                withExtendedLifetime(watcher) {}  // the watcher stops when this closure goes
            }
        }
    }
}
