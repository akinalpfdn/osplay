/// Composition root: the single place where concrete services are created and injected.
@MainActor
final class AppDependencies {
    let virtualDisplays: any VirtualDisplayProviding

    init(virtualDisplays: any VirtualDisplayProviding = CGVirtualDisplayProvider()) {
        self.virtualDisplays = virtualDisplays
    }
}
