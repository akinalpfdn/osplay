#pragma once

// Host side of the audio stream (PROTOCOL.md "Audio"): interleaved float PCM in, one Opus frame
// per AudioConfig frame duration out. Pushes may hold any number of whole sample frames; the rest
// of a frame waits for the next push.

#include "protocol/constants.hpp"
#include "protocol/control_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

struct OpusEncoder;

namespace osplay::media {

/// Receives each encoded frame; the payload is valid only during the call.
using EncodedAudioSink =
    std::function<void(std::uint64_t capture_time_us, std::span<const std::uint8_t> payload)>;

class AudioEncoder {
public:
    /// Fails with ValueOutOfRange for a config the protocol refuses.
    [[nodiscard]] static Expected<std::unique_ptr<AudioEncoder>, Error>
    create(const protocol::AudioConfig& config);

    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;
    AudioEncoder(AudioEncoder&&) = delete;
    AudioEncoder& operator=(AudioEncoder&&) = delete;

    /// `samples`: whole sample frames of interleaved PCM in [-1, 1], `capture_time_us` the first
    /// one's capture time on the core clock. Calls `sink` once per completed frame, in order.
    [[nodiscard]] Status<Error> push(std::span<const float> samples, std::uint64_t capture_time_us,
                                     const EncodedAudioSink& sink);

    [[nodiscard]] const protocol::AudioConfig& config() const { return config_; }
    /// Samples per channel in one frame.
    [[nodiscard]] std::size_t frame_samples() const { return frame_samples_; }

private:
    struct Deleter {
        void operator()(OpusEncoder* encoder) const;
    };

    AudioEncoder(const protocol::AudioConfig& config,
                 std::unique_ptr<OpusEncoder, Deleter> encoder);
    [[nodiscard]] Status<Error> encode(const float* frame, std::uint64_t capture_time_us,
                                       const EncodedAudioSink& sink);

    protocol::AudioConfig config_;
    std::unique_ptr<OpusEncoder, Deleter> encoder_;
    std::size_t frame_samples_;
    std::vector<float> pending_; ///< interleaved, less than one frame
    std::uint64_t pending_capture_us_ = 0;
    std::array<std::uint8_t, protocol::kMaxAudioPayload> packet_{};
};

/// Microseconds that `frames` sample frames last at the protocol's sample rate.
[[nodiscard]] constexpr std::uint64_t audio_duration_us(std::uint64_t frames) {
    return frames * 1'000'000 / protocol::kAudioSampleRate;
}

/// Sample frames in one frame of `duration_us`.
[[nodiscard]] constexpr std::size_t audio_frame_samples(std::uint16_t duration_us) {
    return std::size_t{protocol::kAudioSampleRate} * duration_us / 1'000'000;
}

} // namespace osplay::media
