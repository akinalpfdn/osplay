import SwiftUI

/// First contact with a computer: both screens show the same six digits; the user confirms here.
struct PairingView: View {
    let request: PadConnectionViewModel.PairingRequest
    let answer: (Bool) -> Void

    var body: some View {
        VStack(spacing: Theme.Spacing.extraLarge) {
            Text(String(localized: "pad.pairing.title \(request.peerName)"))
                .font(Theme.Typography.rowTitle)
                .multilineTextAlignment(.center)
            Text(request.code.pairingCodeText)
                .font(Theme.Typography.pairingCode)
                .accessibilityLabel(Text(String(localized: "pad.pairing.codeLabel \(request.code.pairingCodeText)")))
            Text("pad.pairing.message")
                .font(Theme.Typography.body)
                .foregroundStyle(Theme.Colors.secondary)
                .multilineTextAlignment(.center)
            HStack(spacing: Theme.Spacing.medium) {
                Button("pad.pairing.reject", role: .cancel) { answer(false) }
                    .buttonStyle(.bordered)
                Button("pad.pairing.accept") { answer(true) }
                    .buttonStyle(.borderedProminent)
            }
            .controlSize(.large)
        }
        .padding(Theme.Spacing.extraLarge)
    }
}
