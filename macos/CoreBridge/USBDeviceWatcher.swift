import Foundation
import OsplayCore

/// A change among the iPads on a USB cable.
public enum USBDeviceEvent: Equatable, Sendable {
    /// usbmuxd's ID for the device; it changes when the cable is plugged in again.
    case attached(deviceID: UInt32)
    case detached(deviceID: UInt32)
}

/// Follows the iPads on a USB cable through usbmuxd (built into macOS). When usbmuxd restarts the
/// watcher reconnects and reports the devices it knew as detached. Devices usbmuxd reaches over
/// Wi-Fi are left out.
public final class USBDeviceWatcher: Sendable {
    public let events: AsyncStream<USBDeviceEvent>

    private let handle: WatcherHandle
    /// Holds the callbacks' context; must outlive the core watcher.
    private let box: WatcherBox

    public init() throws(CoreError) {
        let (events, continuation) = AsyncStream.makeStream(of: USBDeviceEvent.self)
        let box = WatcherBox(continuation: continuation)
        var callbacks = osplay_usb_callbacks()
        callbacks.context = Unmanaged.passUnretained(box).toOpaque()
        callbacks.on_attached = { context, deviceID in
            WatcherBox.from(context)?.continuation.yield(.attached(deviceID: deviceID))
        }
        callbacks.on_detached = { context, deviceID in
            WatcherBox.from(context)?.continuation.yield(.detached(deviceID: deviceID))
        }
        var created: OpaquePointer?
        if let error = CoreError(result: osplay_usb_watcher_create(&callbacks, &created)) {
            continuation.finish()
            throw error
        }
        guard let created else { throw .internalFailure }
        self.events = events
        self.box = box
        handle = WatcherHandle(created)
    }

    deinit {
        // Joins the watcher's thread: no callback runs once it returns.
        osplay_usb_watcher_destroy(handle.pointer)
        box.continuation.finish()
    }
}

/// The core handle; only `deinit` uses it.
private final class WatcherHandle: @unchecked Sendable {
    let pointer: OpaquePointer

    init(_ pointer: OpaquePointer) { self.pointer = pointer }
}

private final class WatcherBox: Sendable {
    let continuation: AsyncStream<USBDeviceEvent>.Continuation

    init(continuation: AsyncStream<USBDeviceEvent>.Continuation) { self.continuation = continuation }

    /// The core passes back the context it was given; USBDeviceWatcher keeps the box alive.
    static func from(_ context: UnsafeMutableRawPointer?) -> WatcherBox? {
        context.map { Unmanaged<WatcherBox>.fromOpaque($0).takeUnretainedValue() }
    }
}
