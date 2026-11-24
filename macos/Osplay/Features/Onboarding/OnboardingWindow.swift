import AppKit
import SwiftUI

/// Shows the onboarding view in its own window; a menu bar app has no main window to host it.
@MainActor
final class OnboardingWindow {
    private var window: NSWindow?
    private var closeObserver: NSObjectProtocol?
    private let viewModel: OnboardingViewModel
    /// Called each time the window closes, by its close button or from the view.
    var onClose: (() -> Void)?

    init(viewModel: OnboardingViewModel) {
        self.viewModel = viewModel
    }

    func show() {
        if let window {
            NSApp.activate()
            window.makeKeyAndOrderFront(nil)
            window.orderFrontRegardless()  // a menu bar app is not active by itself
            return
        }
        let view = OnboardingView(viewModel: viewModel) { [weak self] in self?.close() }
        let window = NSWindow(
            contentRect: .zero, styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = String(localized: "onboarding.windowTitle")
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: view)
        window.setContentSize(window.contentView?.fittingSize ?? .zero)
        window.center()
        closeObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.onClose?() }
        }
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
        self.window = window
    }

    func close() {
        window?.close()
        window = nil
        if let closeObserver { NotificationCenter.default.removeObserver(closeObserver) }
        closeObserver = nil
    }
}
