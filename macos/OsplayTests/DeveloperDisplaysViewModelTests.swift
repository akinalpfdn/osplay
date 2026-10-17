import Testing

@testable import Osplay

@MainActor
@Suite("Developer displays view model")
struct DeveloperDisplaysViewModelTests {
    @Test("Creating two displays gives them distinct identities")
    func createsTwoDistinctDisplays() async {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 2)

        #expect(viewModel.rows.count == 2)
        #expect(Set(provider.createdConfigurations.map(\.identity)).count == 2)
        #expect(viewModel.errorMessage == nil)
    }

    @Test("Creating again replaces the existing displays and reuses identities")
    func recreateReusesIdentities() async {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 2)
        let firstIdentities = provider.createdConfigurations.map(\.identity)
        await viewModel.createDisplays(count: 2)

        #expect(provider.displays.count == 2)
        #expect(Array(provider.createdConfigurations.suffix(2).map(\.identity)) == firstIdentities)
    }

    @Test("HiDPI displays start at the crisp resolution when enabled")
    func activatesCrispResolution() async throws {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 1)

        let handle = try #require(provider.handles.first)
        #expect(handle.activated == [handle.configuration.crispHiDPIResolution])
    }

    @Test("Without HiDPI no resolution is forced")
    func noForcedResolutionWithoutHiDPI() async throws {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)
        viewModel.isHiDPI = false

        await viewModel.createDisplays(count: 1)

        #expect(try #require(provider.handles.first).activated.isEmpty)
    }

    @Test("Provider errors are shown as a localized message")
    func surfacesErrors() async {
        let provider = FakeVirtualDisplayProvider()
        provider.creationError = .creationFailed
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 1)

        #expect(viewModel.errorMessage == VirtualDisplayError.creationFailed.message)
        #expect(viewModel.rows.isEmpty)
    }

    @Test("Remove all destroys every display")
    func removesAll() async {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)
        await viewModel.createDisplays(count: 2)

        viewModel.removeAll()

        #expect(provider.displays.isEmpty)
        #expect(viewModel.rows.isEmpty)
    }

    @Test("A readiness failure does not prevent creating the second display")
    func readinessFailureKeepsBothDisplays() async {
        let provider = FakeVirtualDisplayProvider()
        provider.readinessError = .notReady
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 2)

        #expect(provider.displays.count == 2)
        #expect(viewModel.errorMessage == VirtualDisplayError.notReady.message)
    }

    @Test("Resolutions are changed only after the display is ready")
    func waitsForReadinessBeforeActivating() async throws {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 2)

        for handle in provider.handles {
            #expect(handle.waitedUntilReady)
            #expect(handle.activated.count == 1)
        }
        #expect(viewModel.errorMessage == nil)
    }

    @Test("A failure creating the second display keeps the first one")
    func partialCreationFailure() async {
        let provider = FakeVirtualDisplayProvider()
        provider.failingCreationIndex = 1
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        await viewModel.createDisplays(count: 2)

        #expect(provider.displays.count == 1)
        #expect(viewModel.errorMessage == VirtualDisplayError.creationFailed.message)
        #expect(!viewModel.isBusy)
    }
}
