#pragma once

// Viewer side of the audio stream (PROTOCOL.md "Audio"). Packets go in on the session thread; the
// platform's audio device reads PCM on its own thread. In between sits a jitter buffer: it plays
// packets in order, conceals lost ones with Opus' own concealment, holds a small reserve before
// playing (more after the network let it run dry, less again once the network calms down), and
// keeps that reserve by playing up to 0.2 % faster or slower, so the host's and the viewer's sample
// clocks never drift apart.

#include "protocol/constants.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

struct OpusDecoder;

namespace osplay::media {

struct AudioPlayoutTunables {
    /// Audio held before playing starts, and the least the reserve is lowered to.
    Microseconds target_delay{40'000};
    Microseconds max_target_delay{200'000};
    /// The reserve grows by this each time it ran dry.
    Microseconds target_step{20'000};
    /// This often the reserve is lowered to the smallest one seen in that time plus a margin.
    Microseconds review_interval{10'000'000};
    Microseconds review_margin{15'000};
    /// While playing, more than this above the reserve is dropped at once (a burst after a stall).
    Microseconds max_excess{100'000};
    /// The playback rate changes at most this much to hold the reserve...
    double max_rate_change = 0.002;
    /// ...by this much per second of reserve error...
    double rate_gain_per_second = 0.1;
    /// ...and not at all while the reserve is this close to its target.
    Microseconds rate_deadband{8'000};
    /// Frames concealed while nothing arrives before playback stops to refill the reserve.
    std::uint32_t max_concealed_in_a_row = 3;
};

struct AudioPlayoutStats {
    bool playing = false;
    Microseconds buffered{0}; ///< smoothed over about a second
    Microseconds target{0};
    std::uint64_t packets = 0;
    std::uint64_t late_packets = 0;
    std::uint64_t concealed_frames = 0;
    std::uint64_t underruns = 0;
    std::uint64_t dropped_packets = 0;
    /// The host capture time of the first sample of the latest read that played sound (0 before
    /// one), and when that read happened.
    std::uint64_t last_capture_time_us = 0;
    TimePoint last_read_at;
};

class AudioPlayout {
public:
    explicit AudioPlayout(AudioPlayoutTunables tunables = {});
    ~AudioPlayout();
    AudioPlayout(const AudioPlayout&) = delete;
    AudioPlayout& operator=(const AudioPlayout&) = delete;
    AudioPlayout(AudioPlayout&&) = delete;
    AudioPlayout& operator=(AudioPlayout&&) = delete;

    /// Session thread: start over in this format. ValueOutOfRange for a config the protocol
    /// refuses.
    [[nodiscard]] Status<Error> configure(const protocol::AudioConfig& config);
    /// Session thread: silence until the next configure.
    void stop();
    /// Session thread: one packet as it arrived.
    void push(const protocol::AudioPacket& packet);
    /// Audio device thread: fills `out` with interleaved PCM of `channels` channels (whole sample
    /// frames). Silence while nothing is configured, the channel count differs, or the reserve is
    /// refilling. Returns the host capture time of the first sample, 0 when it is silence.
    std::uint64_t read(std::span<float> out, std::uint8_t channels, TimePoint now);

    [[nodiscard]] AudioPlayoutStats stats() const;
    /// The current format, if any.
    [[nodiscard]] std::optional<protocol::AudioConfig> config() const;

private:
    static constexpr std::size_t kSlots = 64;
    /// The longest Opus packet decodes to 120 ms.
    static constexpr std::size_t kMaxDecodedFrames = 5'760;
    static constexpr double kNoDepth = std::numeric_limits<double>::infinity();

    struct Slot {
        bool filled = false;
        std::uint32_t sequence = 0;
        std::uint64_t capture_time_us = 0;
        std::size_t size = 0;
        std::array<std::uint8_t, protocol::kMaxAudioPayload> data{};
    };
    struct DecoderDeleter {
        void operator()(OpusDecoder* decoder) const;
    };

    // Everything below runs with mutex_ held.
    [[nodiscard]] bool decode_next(TimePoint now);
    [[nodiscard]] std::uint64_t buffered_frames() const;
    [[nodiscard]] static std::uint64_t frames_in(Microseconds duration);
    void update_rate(TimePoint now);
    void review_target(TimePoint now);
    void drop_excess();
    void compact();
    void reset_buffer();
    void ran_dry(TimePoint now);
    [[nodiscard]] float sample(std::size_t frame, std::size_t channel) const;
    [[nodiscard]] Slot& slot(std::uint32_t sequence) { return slots_.at(sequence % kSlots); }

    const AudioPlayoutTunables tunables_;
    mutable std::mutex mutex_;
    std::optional<protocol::AudioConfig> config_;
    std::unique_ptr<OpusDecoder, DecoderDeleter> decoder_;
    std::size_t channels_ = 0;
    std::size_t frame_samples_ = 0;
    std::array<Slot, kSlots> slots_{};

    bool receiving_ = false;            ///< a packet arrived since the buffer was last emptied
    std::uint32_t next_sequence_ = 0;   ///< the next packet to decode
    std::uint32_t newest_sequence_ = 0; ///< the newest packet received
    std::uint64_t next_capture_us_ = 0; ///< capture time expected for next_sequence_
    bool playing_ = false;
    bool started_ = false; ///< has played since the last configure
    std::uint32_t concealed_in_a_row_ = 0;

    /// Decoded PCM not yet played: interleaved, frame 0 at fifo_capture_us_; position_ is the
    /// fractional read position in frames.
    std::vector<float> fifo_;
    std::size_t fifo_frames_ = 0;
    std::int64_t fifo_capture_us_ = 0;
    double position_ = 0.0;
    double rate_ = 1.0;

    Microseconds target_{0};
    double smoothed_us_ = 0.0;
    TimePoint last_update_;
    double min_depth_us_ = kNoDepth; ///< smallest reserve while playing, this review interval
    TimePoint review_started_;

    AudioPlayoutStats counters_;
};

} // namespace osplay::media
