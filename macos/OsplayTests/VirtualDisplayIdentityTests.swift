import Foundation
import Testing

@testable import Osplay

@Suite("Virtual display identity")
struct VirtualDisplayIdentityTests {
    private let peer = Data([0x01, 0x02, 0x03, 0x04])

    @Test("Same peer and monitor index always give the same identity")
    func isDeterministic() {
        #expect(
            VirtualDisplayIdentity.derived(peerID: peer, monitorIndex: 0)
                == VirtualDisplayIdentity.derived(peerID: peer, monitorIndex: 0))
    }

    @Test("Monitor index and peer both change the serial number")
    func distinguishesMonitorsAndPeers() {
        let first = VirtualDisplayIdentity.derived(peerID: peer, monitorIndex: 0)
        let second = VirtualDisplayIdentity.derived(peerID: peer, monitorIndex: 1)
        let otherPeer = VirtualDisplayIdentity.derived(peerID: Data([0x09]), monitorIndex: 0)

        #expect(first.serialNumber != second.serialNumber)
        #expect(first.serialNumber != otherPeer.serialNumber)
    }

    @Test("Vendor and product IDs are fixed and the serial number is never zero")
    func fixedVendorAndNonZeroSerial() {
        for index in 0..<64 {
            let identity = VirtualDisplayIdentity.derived(peerID: peer, monitorIndex: index)
            #expect(identity.vendorID == VirtualDisplayIdentity.osplayVendorID)
            #expect(identity.productID == VirtualDisplayIdentity.osplayProductID)
            #expect(identity.serialNumber != 0)
        }
    }

    @Test("Known input keeps its serial number across releases")
    func serialIsStableAcrossReleases() {
        // Changing the derivation would reset every user's display arrangement.
        let identity = VirtualDisplayIdentity.derived(peerID: Data("osplay".utf8), monitorIndex: 0)
        #expect(identity.serialNumber == 0x992B_7515)
    }
}
