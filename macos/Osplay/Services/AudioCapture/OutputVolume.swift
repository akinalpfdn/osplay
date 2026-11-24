import AudioToolbox
import CoreAudio
import Foundation
import OSLog

/// The Mac's own volume and mute for the default output, as a gain on the sound sent to a display,
/// so the volume keys work on the display like on a TV over HDMI. The tap captures the mix before
/// the device's volume, which would otherwise not reach the display at all.
final class OutputVolume: @unchecked Sendable {
    // @unchecked: `gainValue` is guarded by `lock`; listeners are added and removed by the owner,
    // which holds its own lock around `init` and `stop`.
    private let device: AudioObjectID
    private let queue: DispatchQueue
    private let lock = NSLock()
    private var gainValue: Float = 1
    private var listeners: [(AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []

    /// The gain to multiply the captured samples by; 1 where the output has no volume control.
    var gain: Float { lock.withLock { gainValue } }

    init(device: AudioObjectID, queue: DispatchQueue) {
        self.device = device
        self.queue = queue
        update()
        for watched in Self.watched {
            var address = watched
            guard AudioObjectHasProperty(device, &address) else { continue }
            let listener: AudioObjectPropertyListenerBlock = { [weak self] _, _ in self?.update() }
            if AudioObjectAddPropertyListenerBlock(device, &address, queue, listener) == noErr {
                listeners.append((address, listener))
            }
        }
    }

    func stop() {
        for (address, listener) in listeners {
            var address = address
            AudioObjectRemovePropertyListenerBlock(device, &address, queue, listener)
        }
        listeners = []
    }

    /// A muted output sends silence; otherwise the device's attenuation in decibels, else its
    /// volume scalar; with neither, the sound goes out unchanged.
    static func gain(muted: Bool, decibels: Float?, scalar: Float?) -> Float {
        if muted { return 0 }
        if let decibels { return decibels.isFinite ? min(1, powf(10, decibels / 20)) : 0 }
        if let scalar { return max(0, min(1, scalar)) }
        return 1
    }

    private func update() {
        let muted = (Self.read(UInt32.self, kAudioDevicePropertyMute, of: device) ?? 0) != 0
        let decibels =
            Self.read(Float32.self, kAudioDevicePropertyVolumeDecibels, of: device)
            ?? Self.read(Float32.self, kAudioDevicePropertyVolumeDecibels, of: device, element: 1)
        let scalar = Self.read(Float32.self, kAudioHardwareServiceDeviceProperty_VirtualMainVolume, of: device)
        let gain = Self.gain(muted: muted, decibels: decibels, scalar: scalar)
        lock.withLock { gainValue = gain }
    }

    /// What the volume keys and the menu bar's volume change.
    private static let watched: [AudioObjectPropertyAddress] = [
        address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume),
        address(kAudioDevicePropertyMute),
    ]

    private static func address(
        _ selector: AudioObjectPropertySelector, element: UInt32 = 0
    )
        -> AudioObjectPropertyAddress
    {
        AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioDevicePropertyScopeOutput, mElement: element)
    }

    private static func read<Value: Numeric & BitwiseCopyable>(
        _ type: Value.Type, _ selector: AudioObjectPropertySelector, of device: AudioObjectID, element: UInt32 = 0
    ) -> Value? {
        var address = address(selector, element: element)
        guard AudioObjectHasProperty(device, &address) else { return nil }
        var value: Value = 0
        var size = UInt32(MemoryLayout<Value>.size)
        return AudioObjectGetPropertyData(device, &address, 0, nil, &size, &value) == noErr ? value : nil
    }
}
