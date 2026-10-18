import Carbon.HIToolbox
import OSLog

/// `RegisterEventHotKey`: delivered through the main run loop even while the viewer covers the
/// screen or frames stop arriving, and it needs no Accessibility permission (DEVPLAN).
@MainActor
final class CarbonHotkeys: HotkeyRegistering {
    private static let signature: OSType = 0x4F53_504C  // "OSPL"
    private var handler: EventHandlerRef?
    private var registered: [UInt32: (ref: EventHotKeyRef, action: @MainActor () -> Void)] = [:]
    private var nextID: UInt32 = 1

    func register(
        _ hotkey: Hotkey, action: @escaping @MainActor () -> Void
    ) throws(HotkeyError)
        -> HotkeyRegistration
    {
        try installHandlerIfNeeded()
        let id = nextID
        var ref: EventHotKeyRef?
        let status = RegisterEventHotKey(
            hotkey.keyCode, hotkey.modifiers.rawValue, EventHotKeyID(signature: Self.signature, id: id),
            GetApplicationEventTarget(), 0, &ref)
        guard status == noErr, let ref else { throw .unavailable(status) }
        nextID += 1
        registered[id] = (ref, action)
        return HotkeyRegistration(id: id)
    }

    func unregister(_ registration: HotkeyRegistration) {
        guard let entry = registered.removeValue(forKey: registration.id) else { return }
        UnregisterEventHotKey(entry.ref)
    }

    fileprivate func pressed(_ id: UInt32) {
        registered[id]?.action()
    }

    private func installHandlerIfNeeded() throws(HotkeyError) {
        guard handler == nil else { return }
        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        let status = InstallEventHandler(
            GetApplicationEventTarget(), hotkeyHandler, 1, &spec, Unmanaged.passUnretained(self).toOpaque(),
            &handler)
        guard status == noErr else { throw .unavailable(status) }
    }
}

/// Carbon calls this on the main thread for every registered hotkey.
private func hotkeyHandler(
    _ next: EventHandlerCallRef?, _ event: EventRef?, _ context: UnsafeMutableRawPointer?
) -> OSStatus {
    guard let event, let context else { return OSStatus(eventNotHandledErr) }
    var id = EventHotKeyID()
    let status = GetEventParameter(
        event, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID), nil,
        MemoryLayout<EventHotKeyID>.size, nil, &id)
    guard status == noErr else { return status }
    // CarbonHotkeys outlives its handler: it is owned by the app's dependencies.
    let hotkeys = Unmanaged<CarbonHotkeys>.fromOpaque(context).takeUnretainedValue()
    MainActor.assumeIsolated { hotkeys.pressed(id.id) }
    return noErr
}
