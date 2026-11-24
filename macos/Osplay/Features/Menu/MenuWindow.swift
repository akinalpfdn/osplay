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

    /// Opens the menu bar window as a click on Osplay's menu bar icon does, once the icon is there.
    /// SwiftUI has no call for it, so this clicks the status item's button.
    static func open() {
        Task {
            for _ in 0..<Tunables.Menu.openAttempts {
                if window?.isVisible == true { return }
                if let button = statusItemButton() {
                    button.performClick(nil)
                    return
                }
                try? await Task.sleep(for: Tunables.Menu.openRetry)  // cancelled only with the app
            }
            Log.connection.error("The menu could not be opened: no menu bar icon")
        }
    }

    /// The button of Osplay's own status item; the app's windows include the one it sits in.
    private static func statusItemButton() -> NSStatusBarButton? {
        NSApp.windows.lazy.compactMap { $0.contentView.flatMap(statusItemButton(in:)) }.first
    }

    private static func statusItemButton(in view: NSView) -> NSStatusBarButton? {
        if let button = view as? NSStatusBarButton { return button }
        return view.subviews.lazy.compactMap(statusItemButton(in:)).first
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
