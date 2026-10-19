import AppKit
import SwiftUI

/// A small floating panel with the pairing code, in front of every app.
@MainActor
final class PairingPanel: PairingPrompting {
    private var panel: NSPanel?

    func show(code: UInt32, peerName: String, decision: @escaping @MainActor (Bool) -> Void) {
        dismiss()
        let view = PairingView(code: code.pairingCodeText, peerName: peerName) { [weak self] accepted in
            self?.dismiss()
            decision(accepted)
        }
        let panel = NSPanel(
            contentRect: .zero, styleMask: [.titled, .nonactivatingPanel, .fullSizeContentView], backing: .buffered,
            defer: false)
        panel.title = String(localized: "pairing.title")
        panel.level = .floating
        panel.isReleasedWhenClosed = false
        panel.contentView = NSHostingView(rootView: view)
        panel.setContentSize(panel.contentView?.fittingSize ?? .zero)
        panel.center()
        panel.orderFrontRegardless()
        self.panel = panel
    }

    func dismiss() {
        panel?.close()
        panel = nil
    }
}

private struct PairingView: View {
    let code: String
    let peerName: String
    let decide: (Bool) -> Void

    var body: some View {
        VStack(spacing: Theme.Spacing.medium) {
            Text("pairing.message \(peerName)")
                .font(Theme.Typography.body)
                .multilineTextAlignment(.center)
            Text(verbatim: code)
                .font(Theme.Typography.pairingCode)
            Text("pairing.hint")
                .font(Theme.Typography.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            HStack(spacing: Theme.Spacing.small) {
                Button("pairing.reject", role: .cancel) { decide(false) }
                Button("pairing.accept") { decide(true) }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.pairingPanelWidth)
    }
}
