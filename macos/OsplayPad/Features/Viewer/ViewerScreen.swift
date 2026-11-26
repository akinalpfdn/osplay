import SwiftUI
import UIKit

/// The computer's display, full screen. A tap shows the controls for a few seconds. On Apple TV the
/// remote's select button shows them and moves focus onto them, where they stay; Menu hides them,
/// or ends the session when they are hidden.
struct ViewerScreen: View {
    @Bindable var connection: PadConnectionViewModel
    @State private var showsControls = false
    @State private var hideTask: Task<Void, Never>?
    @FocusState private var focusedControl: Control?

    private enum Control: Hashable {
        case statistics
        case sound
        case disconnect
    }

    var body: some View {
        ZStack(alignment: .topTrailing) {
            picture
            if showsControls || connection.stage == nil {
                controls
                    .padding(Theme.Spacing.large)
                    .transition(.opacity)
            }
        }
        #if os(tvOS)
            .onExitCommand {
                if showsControls {
                    withAnimation { showsControls = false }
                } else {
                    connection.disconnect()
                }
            }
        #else
            .contentShape(Rectangle())
            .onTapGesture { revealControls() }
            .statusBarHidden()
        #endif
        .onGeometryChange(for: Bool.self) {
            $0.size.width > $0.size.height
        } action: { landscape in
            connection.viewportChanged(landscape: landscape)
        }
        .persistentSystemOverlays(.hidden)
        .onDisappear { hideTask?.cancel() }
    }

    /// The stream, or the wait for it. On Apple TV it alone takes focus while the controls are hidden,
    /// so select reveals them. It is the controls' sibling, not their parent: a focusable parent
    /// would make the remote unable to move between the buttons.
    private var picture: some View {
        ZStack {
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
        }
        #if os(tvOS)
            .contentShape(Rectangle())
            .focusable(!showsControls)
            .onTapGesture { revealControls() }
        #endif
    }

    private var controls: some View {
        HStack(spacing: Theme.Spacing.medium) {
            statisticsControl
                .focused($focusedControl, equals: .statistics)
            if connection.receivesSound {
                soundControl
                    .focused($focusedControl, equals: .sound)
            }
            Button(role: .destructive, action: connection.disconnect) {
                Label("pad.viewer.disconnect", systemImage: Theme.Symbol.disconnect)
            }
            .focused($focusedControl, equals: .disconnect)
        }
        .buttonStyle(.bordered)
        .padding(Theme.Spacing.small)
        .background(Theme.Viewer.controlBackground, in: Capsule())
        #if os(tvOS)
            .focusSection()  // left and right stay among the buttons
        #endif
    }

    /// Switches the stats overlay. tvOS has no button-style toggle, so there it is a button.
    @ViewBuilder private var statisticsControl: some View {
        #if os(tvOS)
            Button {
                connection.settings.showsStatsOverlay.toggle()
            } label: {
                Label("pad.viewer.stats", systemImage: Theme.Symbol.statistics)
            }
        #else
            Toggle(isOn: Bindable(connection.settings).showsStatsOverlay) {
                Label("pad.viewer.stats", systemImage: Theme.Symbol.statistics)
            }
            .toggleStyle(.button)
        #endif
    }

    /// Mutes the computer's sound here at once, or plays it again.
    private var soundControl: some View {
        Button {
            connection.setSoundMuted(!connection.soundMuted)
        } label: {
            Label(
                connection.soundMuted ? "pad.viewer.unmute" : "pad.viewer.mute",
                systemImage: connection.soundMuted ? Theme.Symbol.soundMuted : Theme.Symbol.soundOn)
        }
    }

    private var waitingText: String {
        if case .connecting(let peer) = connection.status {
            return String(localized: "pad.status.connecting \(peer)")
        }
        return String(localized: "pad.viewer.waiting")
    }

    private func revealControls() {
        withAnimation { showsControls = true }
        #if os(tvOS)
            // The remote's focus moves onto the controls, which stay until Menu.
            focusedControl = .statistics
        #else
            hideTask?.cancel()
            hideTask = Task { @MainActor in
                try? await Task.sleep(for: Tunables.Pad.controlsHideDelay)  // cancelled by the next tap
                guard !Task.isCancelled else { return }
                withAnimation { showsControls = false }
            }
        #endif
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
