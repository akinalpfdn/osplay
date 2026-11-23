import Testing

@testable import Osplay

@Suite("Output volume")
struct OutputVolumeTests {
    @Test("A muted output sends silence")
    func mutedIsSilent() {
        #expect(OutputVolume.gain(muted: true, decibels: 0, scalar: 1) == 0)
    }

    @Test("The device's attenuation in decibels becomes the gain")
    func decibelsBecomeGain() {
        #expect(OutputVolume.gain(muted: false, decibels: 0, scalar: 0.5) == 1)
        #expect(abs(OutputVolume.gain(muted: false, decibels: -20, scalar: nil) - 0.1) < 0.0001)
        #expect(OutputVolume.gain(muted: false, decibels: -.infinity, scalar: nil) == 0)
    }

    @Test("Without decibels the volume scalar is used, and without any control the sound is unchanged")
    func fallsBack() {
        #expect(OutputVolume.gain(muted: false, decibels: nil, scalar: 0.25) == 0.25)
        #expect(OutputVolume.gain(muted: false, decibels: nil, scalar: nil) == 1)
    }
}
