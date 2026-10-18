import Carbon.HIToolbox

/// A global key combination.
struct Hotkey: Hashable, Sendable {
    struct Modifiers: OptionSet, Hashable, Sendable {
        let rawValue: UInt32
        static let command = Modifiers(rawValue: UInt32(cmdKey))
        static let option = Modifiers(rawValue: UInt32(optionKey))
        static let control = Modifiers(rawValue: UInt32(controlKey))
        static let shift = Modifiers(rawValue: UInt32(shiftKey))
    }

    /// A virtual key code (`kVK_*`).
    let keyCode: UInt32
    let modifiers: Modifiers
}

enum HotkeyError: Error, Equatable, Sendable {
    /// Another app already owns the combination.
    case unavailable(OSStatus)
}

/// Global hotkeys that work whichever app is active and without Accessibility permission.
@MainActor
protocol HotkeyRegistering: AnyObject {
    /// `action` runs on the main actor each time the combination is pressed, until `unregister`.
    func register(_ hotkey: Hotkey, action: @escaping @MainActor () -> Void) throws(HotkeyError) -> HotkeyRegistration
    func unregister(_ registration: HotkeyRegistration)
}

struct HotkeyRegistration: Hashable, Sendable {
    let id: UInt32
}
