import SwiftUI
import UIKit

/// The computer's display, full screen. A tap shows the controls for a few seconds.
struct ViewerScreen: View {
    @Bindable var connection: PadConnectionViewModel
    @State private var showsControls = false
    @State private var hideTask: Task<Void, Never>?

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Theme.Viewer.background.ignoresSafeArea()
            if let stage = connection.stage {
                StageView(stage: stage).ignoresSafeArea()
            } else {
                VStack(spacing: Theme.Spacing.medium) {
                    ProgressView()
                    Text(waitingText)
                        .font(Theme.Typography.body)
                }
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            if showsControls || connection.stage == nil {
                controls
                    .padding(Theme.Spacing.large)
                    .transition(.opacity)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture { revealControls() }
        .onGeometryChange(for: Bool.self) {
            $0.size.width > $0.size.height
        } action: { landscape in
            connection.viewportChanged(landscape: landscape)
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .onDisappear { hideTask?.cancel() }
    }

    private var controls: some View {
        HStack(spacing: Theme.Spacing.medium) {
            Toggle(isOn: Bindable(connection.settings).showsStatsOverlay) {
                Label("pad.viewer.stats", systemImage: Theme.Symbol.statistics)
            }
            .toggleStyle(.button)
            Button(role: .destructive, action: connection.disconnect) {
                Label("pad.viewer.disconnect", systemImage: Theme.Symbol.disconnect)
            }
        }
        .buttonStyle(.bordered)
        .padding(Theme.Spacing.small)
        .background(Theme.Viewer.controlBackground, in: Capsule())
    }

    private var waitingText: String {
        if case .connecting(let peer) = connection.status {
            return String(localized: "pad.status.connecting \(peer)")
        }
        return String(localized: "pad.viewer.waiting")
    }

    private func revealControls() {
        withAnimation { showsControls = true }
        hideTask?.cancel()
        hideTask = Task { @MainActor in
            try? await Task.sleep(for: Tunables.Pad.controlsHideDelay)  // cancelled by the next tap
            guard !Task.isCancelled else { return }
            withAnimation { showsControls = false }
        }
    }
}

/// Puts the presenter's view into the SwiftUI hierarchy.
private struct StageView: UIViewRepresentable {
    let stage: PadPresenterView

    func makeUIView(context: Context) -> UIView {
        let container = UIView()
        container.backgroundColor = UIColor(Theme.Viewer.background)
        return container
    }

    func updateUIView(_ container: UIView, context: Context) {
        guard stage.superview !== container else { return }
        container.subviews.forEach { $0.removeFromSuperview() }
        stage.frame = container.bounds
        stage.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        container.addSubview(stage)
    }
}
