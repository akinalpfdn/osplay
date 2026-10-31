import SwiftUI

@main
struct OsplayPadApp: App {
    @State private var connection = PadConnectionViewModel(dependencies: PadDependencies())
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView(connection: connection)
                .onChange(of: scenePhase) { _, phase in connection.scenePhaseChanged(phase) }
        }
    }
}

/// The list of computers, or the viewer while connecting and viewing; the pairing code above both.
struct RootView: View {
    @Bindable var connection: PadConnectionViewModel

    var body: some View {
        ZStack {
            if connection.isViewerShown {
                ViewerScreen(connection: connection)
            } else {
                HomeView(connection: connection)
            }
        }
        .sheet(item: Binding(get: { connection.pairing }, set: { if $0 == nil { connection.answerPairing(false) } })) {
            request in
            PairingView(request: request, answer: connection.answerPairing)
                .interactiveDismissDisabled()
        }
    }
}
