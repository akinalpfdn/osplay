import Testing

@testable import Osplay

@MainActor
@Suite("Developer displays view model")
struct DeveloperDisplaysViewModelTests {
    @Test("Creating two displays gives them distinct identities")
    func createsTwoDistinctDisplays() {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        viewModel.createDisplays(count: 2)

        #expect(viewModel.rows.count == 2)
        #expect(Set(provider.createdConfigurations.map(\.identity)).count == 2)
        #expect(viewModel.errorMessage == nil)
    }

    @Test("Creating again replaces the existing displays and reuses identities")
    func recreateReusesIdentities() {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        viewModel.createDisplays(count: 2)
        let firstIdentities = provider.createdConfigurations.map(\.identity)
        viewModel.createDisplays(count: 2)

        #expect(provider.displays.count == 2)
        #expect(Array(provider.createdConfigurations.suffix(2).map(\.identity)) == firstIdentities)
    }

    @Test("HiDPI displays start at the crisp resolution when enabled")
    func activatesCrispResolution() throws {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        viewModel.createDisplays(count: 1)

        let handle = try #require(provider.handles.first)
        #expect(handle.activated == [handle.configuration.crispHiDPIResolution])
    }

    @Test("Without HiDPI no resolution is forced")
    func noForcedResolutionWithoutHiDPI() throws {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)
        viewModel.isHiDPI = false

        viewModel.createDisplays(count: 1)

        #expect(try #require(provider.handles.first).activated.isEmpty)
    }

    @Test("Provider errors are shown as a localized message")
    func surfacesErrors() {
        let provider = FakeVirtualDisplayProvider()
        provider.creationError = .creationFailed
        let viewModel = DeveloperDisplaysViewModel(provider: provider)

        viewModel.createDisplays(count: 1)

        #expect(viewModel.errorMessage == VirtualDisplayError.creationFailed.message)
        #expect(viewModel.rows.isEmpty)
    }

    @Test("Remove all destroys every display")
    func removesAll() {
        let provider = FakeVirtualDisplayProvider()
        let viewModel = DeveloperDisplaysViewModel(provider: provider)
        viewModel.createDisplays(count: 2)

        viewModel.removeAll()

        #expect(provider.displays.isEmpty)
        #expect(viewModel.rows.isEmpty)
    }
}
