import AppKit
import SwiftUI

/// The window SwiftUI shows for `MenuBarExtra(.window)`, which has no dismiss action of its own.
/// The menu view reports its window here.
@MainActor
enum MenuWindow {
    private static weak var window: NSWindow?

    /// Closes the menu bar window, as clicking outside it would; the status item reopens it.
    static func close() {
        window?.close()
    }

    fileprivate static func attached(_ window: NSWindow) {
        self.window = window
        closesOnClicksElsewhere()
    }

    /// A click in another Osplay window (a full-screen viewer) does not dismiss a menu bar window
    /// by itself, unlike a click in another app; this closes it then.
    private static var clickMonitor: Any?

    private static func closesOnClicksElsewhere() {
        guard clickMonitor == nil else { return }
        clickMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { event in
            MainActor.assumeIsolated {
                if let window, window.isVisible, event.window !== window {
                    close()
                }
            }
            return event
        }
    }
}

/// Finds the NSWindow hosting the menu view and hands it to `MenuWindow`.
struct MenuWindowReader: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        ReportingView()
    }

    func updateNSView(_ nsView: NSView, context: Context) {}

    private final class ReportingView: NSView {
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let window { MenuWindow.attached(window) }
        }
    }
}
