#if DEBUG
    import CoreGraphics
    import Foundation
    import Observation

    /// Drives the debug-only menu section that creates test virtual displays (DEVPLAN Phase 1).
    @MainActor
    @Observable
    final class DeveloperDisplaysViewModel {
        struct DisplayRow: Identifiable {
            let id: CGDirectDisplayID
            let name: String
            let current: DisplayResolution?
            let available: [DisplayResolution]
        }

        let resolutions = Tunables.Developer.testResolutions
        var selectedResolution: DisplaySize
        var isHiDPI = true
        var startsAtCrispResolution = true
        private(set) var rows: [DisplayRow] = []
        private(set) var errorMessage: String?
        private(set) var isBusy = false

        private let provider: any VirtualDisplayProviding

        init(provider: any VirtualDisplayProviding) {
            self.provider = provider
            self.selectedResolution = Tunables.Developer.testResolutions[0]
        }

        /// Replaces any existing test displays with `count` new ones. All displays are created before
        /// any resolution is changed, so a resolution problem never prevents the other display.
        func createDisplays(count: Int) async {
            isBusy = true
            defer { isBusy = false }
            provider.destroyAll()
            errorMessage = nil

            var created: [any VirtualDisplayHandle] = []
            do throws(VirtualDisplayError) {
                for index in 0..<count {
                    created.append(try provider.createDisplay(configuration(index: index)))
                }
            } catch {
                errorMessage = error.message
            }
            refresh()

            for handle in created {
                do throws(VirtualDisplayError) {
                    try await handle.waitUntilReady()
                    if startsAtCrispResolution, let crisp = handle.configuration.crispHiDPIResolution {
                        try handle.activate(crisp)
                    }
                } catch {
                    errorMessage = error.message
                }
            }
            refresh()
        }

        func removeAll() {
            provider.destroyAll()
            errorMessage = nil
            refresh()
        }

        func select(_ resolution: DisplayResolution, forDisplay displayID: CGDirectDisplayID) {
            guard let handle = provider.displays.first(where: { $0.displayID == displayID }) else { return }
            do throws(VirtualDisplayError) {
                try handle.activate(resolution)
                errorMessage = nil
            } catch {
                errorMessage = error.message
            }
            refresh()
        }

        func refresh() {
            rows = provider.displays.map { handle in
                DisplayRow(
                    id: handle.displayID,
                    name: handle.configuration.name,
                    current: handle.currentResolution(),
                    available: handle.availableResolutions()
                )
            }
        }

        private func configuration(index: Int) throws(VirtualDisplayError) -> VirtualDisplayConfiguration {
            try VirtualDisplayConfiguration(
                name: String(localized: "developer.displayName \(index + 1)"),
                identity: .derived(peerID: Tunables.Developer.testPeerID, monitorIndex: index),
                pixelSize: selectedResolution,
                refreshRate: Tunables.Developer.testRefreshRate,
                physicalSizeMillimeters: Tunables.Developer.testPhysicalSizeMillimeters,
                isHiDPI: isHiDPI
            )
        }
    }
#endif
