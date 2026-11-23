#pragma once

// Host side of the PC's sound (Phase 17, AUD-03): records what the speakers play, with the
// speakers themselves muted, and sends it to one display at a time. The mute key, which would
// unmute the speakers, mutes the display's sound instead. Portable and unit tested; WASAPI
// loopback is the Windows recorder (wasapi_system_sound).

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace osplay::engine {

/// Receives the recorded sound: 48 kHz interleaved stereo, with its capture time on the core
/// clock. Called from the recorder's thread.
class SoundSink {
public:
    virtual ~SoundSink() = default;
    virtual void on_sound(std::span<const float> samples, std::uint64_t capture_time_us) = 0;
    /// The user pressed the mute key while the speakers were silenced for a display.
    virtual void on_mute_key() = 0;
};

/// Records the speakers' mix while their own output is muted.
class SystemSound {
public:
    virtual ~SystemSound() = default;
    /// Mutes the speakers and starts recording into `sink`; false when it cannot (the speakers
    /// are left as they were).
    [[nodiscard]] virtual bool start(SoundSink& sink) = 0;
    /// Stops recording, waits for its thread and gives the speakers back their own mute.
    virtual void stop() = 0;
};

/// A host session that can send sound to its viewer.
class SoundReceiver {
public:
    virtual ~SoundReceiver() = default;
    /// Tells the viewer sound follows (AudioConfig); false when the session cannot.
    [[nodiscard]] virtual bool start_audio() = 0;
    virtual void push_audio(std::span<const float> samples, std::uint64_t capture_time_us) = 0;
    virtual void stop_audio() = 0;
};

class HostSound final : private SoundSink {
public:
    explicit HostSound(SystemSound& system);
    ~HostSound() override;
    HostSound(const HostSound&) = delete;
    HostSound& operator=(const HostSound&) = delete;
    HostSound(HostSound&&) = delete;
    HostSound& operator=(HostSound&&) = delete;

    /// Sends the PC's sound to `receiver`; the display that had it stops. False when the session
    /// or the recorder could not start: then no display has the sound.
    [[nodiscard]] bool route(SoundReceiver& receiver);
    /// Stops sending to `receiver` if it has the sound; the speakers play again.
    void release(SoundReceiver& receiver);
    [[nodiscard]] bool routes_to(const SoundReceiver& receiver) const;
    /// The mute key silenced the display's sound.
    [[nodiscard]] bool display_muted() const;

private:
    void on_sound(std::span<const float> samples, std::uint64_t capture_time_us) override;
    void on_mute_key() override;
    void stop_recording();

    SystemSound* system_; // never null; outlives this
    /// Serializes route and release; never held by the recorder's thread, which stop() joins.
    std::mutex control_;
    mutable std::mutex mutex_; ///< guards the fields below, taken by the recorder's thread
    SoundReceiver* receiver_ = nullptr;
    bool muted_ = false;
    bool recording_ = false; ///< guarded by control_
    std::vector<float> silence_;
};

/// Turns a recorder's own format into 48 kHz interleaved stereo when Windows cannot: the first
/// two channels (the centre mixed into both when there are more), resampled linearly.
class StereoResampler {
public:
    StereoResampler(std::uint32_t sample_rate, std::uint16_t channels);

    /// Appends the converted frames of `input` (interleaved, `channels` per frame) to `output`.
    void convert(std::span<const float> input, std::vector<float>& output);

private:
    double step_;            ///< input frames per output frame
    std::uint16_t channels_; ///< at least 1
    double position_ = 0;    ///< where the next output frame falls, from the previous last frame
    float last_left_ = 0;
    float last_right_ = 0;
    bool primed_ = false;
};

} // namespace osplay::engine
