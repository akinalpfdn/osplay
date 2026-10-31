import Foundation
import OsplayCore

/// A blocking pipe over an Android accessory's two bulk endpoints (docs/PROTOCOL.md "Android
/// accessory links"). The core calls it from threads of its own, read and write at the same time.
public protocol AccessoryPipe: AnyObject, Sendable {
    /// Blocks until bytes arrive on the IN endpoint; how many (0 for a zero-length packet), or nil
    /// once the cable is gone or after `cancel`.
    func read(into buffer: UnsafeMutableRawBufferPointer) -> Int?
    /// Blocks until some of `data` is sent on the OUT endpoint; how much, or nil.
    func write(_ data: UnsafeRawBufferPointer) -> Int?
    /// Unblocks read and write for good.
    func cancel()
}

/// Switching Android devices into Android Open Accessory mode with Osplay's identity.
public enum AndroidAccessory {
    /// One vendor request on endpoint 0.
    public struct ControlRequest: Sendable, Equatable {
        public let requestType: UInt8
        public let request: UInt8
        public let value: UInt16
        public let index: UInt16

        /// The data stage runs from the device to the host.
        public var isIn: Bool { requestType & 0x80 != 0 }
    }

    /// The USB IDs of an Android device in accessory mode.
    public static func isAccessory(vendorID: UInt16, productID: UInt16) -> Bool {
        osplay_aoa_is_accessory(vendorID, productID) != 0
    }

    /// Asks for the AOA protocol, sends Osplay's identity and START. `transfer` performs one request
    /// with `data` as its data stage and returns how many bytes it moved, or nil when it failed. The
    /// device then leaves the bus and comes back as an accessory. Throws `.unsupportedVersion` for
    /// a device without AOA (not Android).
    public static func switchToAccessory(
        _ transfer: (ControlRequest, UnsafeMutableRawBufferPointer) -> Int?
    ) throws(CoreError) {
        let result = withoutActuallyEscaping(transfer) { transfer in
            let box = TransferBox(transfer)
            return withExtendedLifetime(box) {
                osplay_aoa_switch(Unmanaged.passUnretained(box).toOpaque()) {
                    context, requestType, request, value, index, data, length, transferred in
                    guard let context, let transferred else { return OSPLAY_ERROR_INVALID_ARGUMENT }
                    let box = Unmanaged<TransferBox>.fromOpaque(context).takeUnretainedValue()
                    let buffer = UnsafeMutableRawBufferPointer(start: data, count: Int(length))
                    let request = ControlRequest(requestType: requestType, request: request, value: value, index: index)
                    guard let moved = box.transfer(request, buffer) else { return OSPLAY_ERROR_NETWORK }
                    transferred.pointee = UInt16(clamping: moved)
                    return OSPLAY_OK
                }
            }
        }
        if let error = CoreError(result: result) { throw error }
    }

    private final class TransferBox {
        let transfer: (ControlRequest, UnsafeMutableRawBufferPointer) -> Int?
        init(_ transfer: @escaping (ControlRequest, UnsafeMutableRawBufferPointer) -> Int?) { self.transfer = transfer }
    }
}

/// Holds an `AccessoryPipe` for the core until its release callback.
final class AccessoryPipeBox: Sendable {
    let pipe: any AccessoryPipe

    init(_ pipe: any AccessoryPipe) { self.pipe = pipe }

    /// The C pipe; its context keeps this box alive until the core releases it.
    func retainedRawPipe() -> osplay_byte_pipe {
        osplay_byte_pipe(
            context: Unmanaged.passRetained(self).toOpaque(),
            read: { context, buffer, capacity, size in
                guard let pipe = AccessoryPipeBox.pipe(context), let size else { return OSPLAY_ERROR_INVALID_ARGUMENT }
                guard let read = pipe.read(into: UnsafeMutableRawBufferPointer(start: buffer, count: capacity)) else {
                    return OSPLAY_ERROR_NETWORK
                }
                size.pointee = read
                return OSPLAY_OK
            },
            write: { context, data, count, written in
                guard let pipe = AccessoryPipeBox.pipe(context), let written else {
                    return OSPLAY_ERROR_INVALID_ARGUMENT
                }
                guard let sent = pipe.write(UnsafeRawBufferPointer(start: data, count: count)) else {
                    return OSPLAY_ERROR_NETWORK
                }
                written.pointee = sent
                return OSPLAY_OK
            },
            cancel: { context in AccessoryPipeBox.pipe(context)?.cancel() },
            release: { context in
                guard let context else { return }
                Unmanaged<AccessoryPipeBox>.fromOpaque(context).release()
            })
    }

    private static func pipe(_ context: UnsafeMutableRawPointer?) -> (any AccessoryPipe)? {
        context.map { Unmanaged<AccessoryPipeBox>.fromOpaque($0).takeUnretainedValue().pipe }
    }
}
