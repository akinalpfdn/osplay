import Foundation
import IOKit
import IOKit.usb

/// Vendor requests on a USB device's endpoint 0 through IOKit's device interface, which needs no
/// exclusive open. IOUSBHostDevice does, and adb (which Android Studio keeps running) holds an
/// Android device with USB debugging on, so switching it to Osplay's accessory would fail (TAB-05).
final class USBControlChannel {
    private let device: UnsafeMutablePointer<UnsafeMutablePointer<IOUSBDeviceInterface182>?>

    init?(_ service: io_service_t) {
        var plugIn: UnsafeMutablePointer<UnsafeMutablePointer<IOCFPlugInInterface>?>?
        var score: Int32 = 0
        guard
            IOCreatePlugInInterfaceForService(
                service, Self.deviceUserClientType, Self.plugInInterface, &plugIn, &score) == KERN_SUCCESS,
            let plugIn, let functions = plugIn.pointee
        else { return nil }
        defer { _ = functions.pointee.Release(plugIn) }
        var interface: LPVOID?
        guard functions.pointee.QueryInterface(plugIn, CFUUIDGetUUIDBytes(Self.deviceInterface182), &interface) == S_OK,
            let interface
        else { return nil }
        device = interface.assumingMemoryBound(to: UnsafeMutablePointer<IOUSBDeviceInterface182>?.self)
    }

    deinit {
        _ = device.pointee?.pointee.Release(device)
    }

    /// Sends one request with `data` as its data stage; how many bytes it moved, or nil.
    func send(
        requestType: UInt8, request: UInt8, value: UInt16, index: UInt16, data: UnsafeMutableRawBufferPointer,
        timeoutMilliseconds: UInt32
    ) -> Int? {
        guard let functions = device.pointee else { return nil }
        var raw = IOUSBDevRequestTO(
            bmRequestType: requestType, bRequest: request, wValue: value, wIndex: index,
            wLength: UInt16(clamping: data.count), pData: data.baseAddress, wLenDone: 0,
            noDataTimeout: timeoutMilliseconds, completionTimeout: timeoutMilliseconds)
        guard functions.pointee.DeviceRequestTO(device, &raw) == kIOReturnSuccess else { return nil }
        return Int(raw.wLenDone)
    }

    // IOUSBLib defines these with CFUUIDGetConstantUUIDWithBytes macros, which Swift cannot import;
    // the function returns the same constant UUID object on every call.
    private static var deviceUserClientType: CFUUID {
        uuid(
            0x9D, 0xC7, 0xB7, 0x80, 0x9E, 0xC0, 0x11, 0xD4, 0xA5, 0x4F, 0x00, 0x0A, 0x27, 0x05, 0x28, 0x61)
    }
    private static var plugInInterface: CFUUID {
        uuid(
            0xC2, 0x44, 0xE8, 0x58, 0x10, 0x9C, 0x11, 0xD4, 0x91, 0xD4, 0x00, 0x50, 0xE4, 0xC6, 0x42, 0x6F)
    }
    private static var deviceInterface182: CFUUID {
        uuid(
            0x15, 0x2F, 0xC4, 0x96, 0x48, 0x91, 0x11, 0xD5, 0x9D, 0x52, 0x00, 0x0A, 0x27, 0x80, 0x1E, 0x86)
    }

    private static func uuid(
        _ b0: UInt8, _ b1: UInt8, _ b2: UInt8, _ b3: UInt8, _ b4: UInt8, _ b5: UInt8, _ b6: UInt8, _ b7: UInt8,
        _ b8: UInt8, _ b9: UInt8, _ b10: UInt8, _ b11: UInt8, _ b12: UInt8, _ b13: UInt8, _ b14: UInt8, _ b15: UInt8
    ) -> CFUUID {
        CFUUIDGetConstantUUIDWithBytes(nil, b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15)
    }
}
