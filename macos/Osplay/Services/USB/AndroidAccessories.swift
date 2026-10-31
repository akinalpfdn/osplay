import CoreBridge
import Foundation
import IOKit
import IOUSBHost

/// What happens on the USB cable to Android devices while sharing (TAB-05).
enum AndroidAccessoryEvent: Sendable {
    /// An Android device came back in Osplay's accessory mode; host sessions run on `link`.
    case attached(AndroidAccessoryLink)
    case detached(id: UInt64)
}

/// The Android devices on the USB cable; `IOKitAndroidAccessories` in the app, a fake in tests.
protocol AndroidAccessoryWatching: Sendable {
    /// While the stream is consumed, every Android device plugged in (or already there) is
    /// switched to Osplay's accessory, and each accessory is reported.
    func watch() -> AsyncStream<AndroidAccessoryEvent>
}

/// An Android device in Osplay's accessory mode, its bulk endpoints open; one host session at a
/// time runs on a pipe from `makePipe()`. Closed when the device goes or the watch ends.
final class AndroidAccessoryLink: @unchecked Sendable {  // IOUSBHost objects are thread-safe
    let id: UInt64
    private let interface: IOUSBHostInterface
    private let input: IOUSBHostPipe
    private let output: IOUSBHostPipe
    private let opened: @Sendable () -> Void

    fileprivate init(
        id: UInt64, interface: IOUSBHostInterface, input: IOUSBHostPipe, output: IOUSBHostPipe,
        opened: @escaping @Sendable () -> Void
    ) {
        self.id = id
        self.interface = interface
        self.input = input
        self.output = output
        self.opened = opened
    }

    func makePipe() -> any AccessoryPipe { BulkPipe(input: input, output: output) }

    /// Osplay on the device opened the accessory: its switches start counting again, so the device
    /// is switched however often the cable is plugged in. Only switches nobody answered count
    /// towards the limit, which keeps an ignored "Open Osplay?" prompt from coming back.
    func markOpened() { opened() }

    fileprivate func close() { interface.destroy() }
}

/// One session's view of the accessory's endpoints: synchronous transfers on the core's threads.
private final class BulkPipe: AccessoryPipe, @unchecked Sendable {  // the flag is behind the lock
    private let input: IOUSBHostPipe
    private let output: IOUSBHostPipe
    private let lock = NSLock()
    private var cancelled = false

    init(input: IOUSBHostPipe, output: IOUSBHostPipe) {
        self.input = input
        self.output = output
    }

    func read(into buffer: UnsafeMutableRawBufferPointer) -> Int? {
        guard !isCancelled, let address = buffer.baseAddress, let data = NSMutableData(length: buffer.count) else {
            return nil
        }
        var transferred = 0
        do {
            try input.__sendIORequest(with: data, bytesTransferred: &transferred, completionTimeout: 0)
        } catch {
            return nil
        }
        data.getBytes(address, length: min(transferred, buffer.count))
        return transferred
    }

    func write(_ data: UnsafeRawBufferPointer) -> Int? {
        guard !isCancelled, let baseAddress = data.baseAddress else { return nil }
        let payload = NSMutableData(bytes: baseAddress, length: data.count)
        var transferred = 0
        do {
            try output.__sendIORequest(with: payload, bytesTransferred: &transferred, completionTimeout: 0)
        } catch {
            return nil
        }
        return transferred
    }

    func cancel() {
        lock.withLock { cancelled = true }
        // try? throughout: an aborted or missing pipe has nothing left to stop.
        try? output.__abort(with: .synchronous)
        try? input.__abort(with: .synchronous)
    }

    private var isCancelled: Bool { lock.withLock { cancelled } }
}

/// Follows USB devices through IOKit notifications on a serial queue of its own.
struct IOKitAndroidAccessories: AndroidAccessoryWatching {
    func watch() -> AsyncStream<AndroidAccessoryEvent> {
        AsyncStream { continuation in
            let watch = AccessoryWatch(continuation)
            watch.start()
            continuation.onTermination = { _ in watch.stop() }
        }
    }
}

/// The state of one watch; only touched on `queue`.
private final class AccessoryWatch: @unchecked Sendable {
    private let continuation: AsyncStream<AndroidAccessoryEvent>.Continuation
    private let queue = DispatchQueue(label: "com.akinalpfdn.osplay.android-accessories")
    private var port: IONotificationPortRef?
    private var iterators: [io_iterator_t] = []
    private var handlers: [Unmanaged<HandlerBox>] = []
    private var links: [UInt64: AndroidAccessoryLink] = [:]
    /// Unanswered switches per device (by serial number and port) in this watch: Android drops back
    /// to its normal mode when macOS has not allowed the accessory within 10 s, or nobody opened it,
    /// and then it is switched again. An accessory that is opened resets its device's count.
    private var switches: [String: Int] = [:]
    private var stopped = false

    init(_ continuation: AsyncStream<AndroidAccessoryEvent>.Continuation) {
        self.continuation = continuation
    }

    func start() {
        queue.async { [self] in
            guard let port = IONotificationPortCreate(kIOMainPortDefault) else {
                Log.connection.error("Cannot follow USB devices for Android accessories")
                continuation.finish()
                return
            }
            self.port = port
            IONotificationPortSetDispatchQueue(port, queue)
            subscribe(port, kIOFirstMatchNotification, "IOUSBHostDevice") { watch, service in
                watch.deviceAppeared(service)
            }
            subscribe(port, kIOFirstMatchNotification, "IOUSBHostInterface") { watch, service in
                watch.interfaceAppeared(service)
            }
            subscribe(port, kIOTerminatedNotification, "IOUSBHostInterface") { watch, service in
                watch.interfaceGone(service)
            }
        }
    }

    /// No event follows; the links close. Keeps itself alive until the queue has finished.
    func stop() {
        queue.async { [self] in
            stopped = true
            iterators.forEach { IOObjectRelease($0) }
            iterators = []
            if let port { IONotificationPortDestroy(port) }
            port = nil
            handlers.forEach { $0.release() }
            handlers = []
            links.values.forEach { $0.close() }
            links = [:]
        }
    }

    private typealias Arrival = (AccessoryWatch, io_service_t) -> Void

    /// Registers for `type` notifications on `className` and handles the services already there.
    private func subscribe(
        _ port: IONotificationPortRef, _ type: String, _ className: String, _ arrival: @escaping Arrival
    ) {
        // Released in stop(), once the port that calls it is gone.
        let handlerBox = Unmanaged.passRetained(HandlerBox(watch: self, arrival: arrival))
        var iterator: io_iterator_t = 0
        let result = IOServiceAddMatchingNotification(
            port, type, IOServiceMatching(className),
            { refcon, iterator in
                guard let refcon else { return }
                Unmanaged<HandlerBox>.fromOpaque(refcon).takeUnretainedValue().drain(iterator)
            }, handlerBox.toOpaque(), &iterator)
        guard result == KERN_SUCCESS else {
            handlerBox.release()
            return
        }
        handlers.append(handlerBox)
        iterators.append(iterator)
        handlerBox.takeUnretainedValue().drain(iterator)  // arms the notification
    }

    private final class HandlerBox {
        unowned let watch: AccessoryWatch  // the watch owns the port that calls this
        let arrival: Arrival

        init(watch: AccessoryWatch, arrival: @escaping Arrival) {
            self.watch = watch
            self.arrival = arrival
        }

        func drain(_ iterator: io_iterator_t) {
            while case let service = IOIteratorNext(iterator), service != 0 {
                if !watch.stopped { arrival(watch, service) }
                IOObjectRelease(service)
            }
        }
    }

    // MARK: - Devices

    private func deviceAppeared(_ service: io_service_t) {
        let vendorID = UInt16(truncatingIfNeeded: property(service, "idVendor") ?? 0)
        let productID = UInt16(truncatingIfNeeded: property(service, "idProduct") ?? 0)
        guard !AndroidAccessory.isAccessory(vendorID: vendorID, productID: productID) else { return }
        guard vendorID != Self.appleVendorID, property(service, "bDeviceClass") != Self.hubClass else { return }
        IOObjectRetain(service)
        // Its interfaces register after the device; they say whether it may be Android.
        queue.asyncAfter(deadline: .now() + Tunables.Connection.accessoryProbeDelay) { [self] in
            defer { IOObjectRelease(service) }
            guard !stopped, mayBeAndroid(service) else { return }
            switchToAccessory(service)
        }
    }

    /// False when every interface is of a class no Android device has in its normal mode.
    private func mayBeAndroid(_ device: io_service_t) -> Bool {
        let classes = interfaceClasses(device)
        return classes.isEmpty || !classes.allSatisfy(Self.notAndroidClasses.contains)
    }

    /// The key `switches` counts a device by; the same in its normal and its accessory mode.
    private func switchKey(_ device: io_service_t) -> String {
        (stringProperty(device, "USB Serial Number") ?? "") + "@\(property(device, "locationID") ?? 0)"
    }

    private func switchToAccessory(_ service: io_service_t) {
        let serial = switchKey(service)
        let attempts = switches[serial, default: 0]
        guard attempts < Tunables.Connection.accessorySwitchAttempts else { return }
        guard let channel = USBControlChannel(service) else { return }
        do throws(CoreError) {
            try AndroidAccessory.switchToAccessory { request, data in
                channel.send(
                    requestType: request.requestType, request: request.request, value: request.value,
                    index: request.index, data: data,
                    timeoutMilliseconds: Tunables.Connection.accessoryControlTimeoutMilliseconds)
            }
            switches[serial] = attempts + 1
            Log.connection.info("Switched an Android device to Osplay's accessory")
        } catch {
            return  // not Android, or it refused
        }
    }

    // MARK: - Accessories

    private func interfaceAppeared(_ service: io_service_t) {
        let vendorID = UInt16(truncatingIfNeeded: property(service, "idVendor") ?? 0)
        let productID = UInt16(truncatingIfNeeded: property(service, "idProduct") ?? 0)
        guard AndroidAccessory.isAccessory(vendorID: vendorID, productID: productID),
            property(service, "bInterfaceNumber") == 0
        else { return }
        let id = registryID(service)
        guard links[id] == nil else { return }
        let interface: IOUSBHostInterface
        do {
            interface = try IOUSBHostInterface(__ioService: service, options: [], queue: nil, interestHandler: nil)
        } catch {
            Log.connection.error("Cannot open the Android accessory: \(error.localizedDescription, privacy: .public)")
            return
        }
        guard let (input, output) = bulkPipes(interface) else {
            interface.destroy()
            return
        }
        let key = deviceKey(ofInterface: service)
        let link = AndroidAccessoryLink(id: id, interface: interface, input: input, output: output) { [weak self] in
            guard let self, let key else { return }
            queue.async { self.switches[key] = nil }
        }
        links[id] = link
        continuation.yield(.attached(link))
    }

    private func interfaceGone(_ service: io_service_t) {
        let id = registryID(service)
        guard let link = links.removeValue(forKey: id) else { return }
        link.close()
        continuation.yield(.detached(id: id))
    }

    private func bulkPipes(_ interface: IOUSBHostInterface) -> (IOUSBHostPipe, IOUSBHostPipe)? {
        var input: IOUSBHostPipe?
        var output: IOUSBHostPipe?
        for number in 1...15 {
            for address in [number | 0x80, number] {
                guard let pipe = try? interface.copyPipe(withAddress: address) else { continue }  // no such endpoint
                guard pipe.descriptors.pointee.descriptor.bmAttributes & 0x03 == Self.bulkTransfer else { continue }
                if address & 0x80 != 0 { input = input ?? pipe } else { output = output ?? pipe }
            }
        }
        guard let input, let output else { return nil }
        return (input, output)
    }

    // MARK: - Registry

    private func deviceKey(ofInterface interface: io_service_t) -> String? {
        var device: io_registry_entry_t = 0
        guard IORegistryEntryGetParentEntry(interface, kIOServicePlane, &device) == KERN_SUCCESS else { return nil }
        defer { IOObjectRelease(device) }
        return switchKey(device)
    }

    private func interfaceClasses(_ device: io_service_t) -> [Int] {
        var iterator: io_iterator_t = 0
        guard IORegistryEntryGetChildIterator(device, kIOServicePlane, &iterator) == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }
        var classes: [Int] = []
        while case let child = IOIteratorNext(iterator), child != 0 {
            if let interfaceClass = property(child, "bInterfaceClass") { classes.append(interfaceClass) }
            IOObjectRelease(child)
        }
        return classes
    }

    private func registryID(_ service: io_service_t) -> UInt64 {
        var id: UInt64 = 0
        IORegistryEntryGetRegistryEntryID(service, &id)
        return id
    }

    private func property(_ service: io_service_t, _ key: String) -> Int? {
        IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue() as? Int
    }

    private func stringProperty(_ service: io_service_t, _ key: String) -> String? {
        IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue() as? String
    }

    private static let appleVendorID: UInt16 = 0x05AC
    private static let hubClass = 9
    private static let bulkTransfer: UInt8 = 2
    /// Audio, HID, printer, mass storage, hub, smart card, video, wireless, application specific.
    private static let notAndroidClasses: Set<Int> = [1, 3, 7, 8, 9, 0x0B, 0x0E, 0xE0, 0xFE]
}
