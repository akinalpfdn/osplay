#include "media/audio_playout.hpp"

#include "media/audio_encoder.hpp"

#include <opus.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace osplay::media {
namespace {

constexpr double kMicrosPerSecond = 1'000'000.0;

/// Packets from `from` to `to`, negative when `to` is older (sequence numbers wrap).
std::int32_t distance(std::uint32_t from, std::uint32_t to) {
    return static_cast<std::int32_t>(to - from);
}

/// Catmull-Rom interpolation between y1 and y2 at t in [0, 1); exact at t = 0.
float interpolate(float y0, float y1, float y2, float y3, float t) {
    const float a = (-0.5F * y0) + (1.5F * y1) - (1.5F * y2) + (0.5F * y3);
    const float b = y0 - (2.5F * y1) + (2.0F * y2) - (0.5F * y3);
    const float c = (-0.5F * y0) + (0.5F * y2);
    return (((((a * t) + b) * t) + c) * t) + y1;
}

} // namespace

void AudioPlayout::DecoderDeleter::operator()(OpusDecoder* decoder) const {
    opus_decoder_destroy(decoder);
}

AudioPlayout::AudioPlayout(AudioPlayoutTunables tunables)
    : tunables_(tunables), target_(tunables.target_delay) {}

AudioPlayout::~AudioPlayout() = default;

Status<Error> AudioPlayout::configure(const protocol::AudioConfig& config) {
    if (!protocol::encode_control(config)) {
        return Status<Error>::failure(Error::ValueOutOfRange);
    }
    int status = OPUS_OK;
    std::unique_ptr<OpusDecoder, DecoderDeleter> decoder(opus_decoder_create(
        static_cast<opus_int32>(config.sample_rate_hz), config.channels, &status));
    if (status != OPUS_OK || !decoder) {
        return Status<Error>::failure(Error::Internal);
    }
    std::vector<float> fifo(std::size_t{2} * kMaxDecodedFrames * config.channels, 0.0F);

    const std::scoped_lock lock(mutex_);
    config_ = config;
    decoder_ = std::move(decoder);
    channels_ = config.channels;
    frame_samples_ = audio_frame_samples(config.frame_duration_us);
    fifo_ = std::move(fifo);
    reset_buffer();
    started_ = false;
    target_ = tunables_.target_delay;
    smoothed_us_ = 0.0;
    min_depth_us_ = kNoDepth;
    return Status<Error>::ok();
}

void AudioPlayout::stop() {
    const std::scoped_lock lock(mutex_);
    config_.reset();
    decoder_.reset();
    channels_ = 0;
    reset_buffer();
    started_ = false;
}

void AudioPlayout::reset_buffer() {
    for (Slot& entry : slots_) {
        entry.filled = false;
    }
    receiving_ = false;
    playing_ = false;
    concealed_in_a_row_ = 0;
    fifo_frames_ = 0;
    position_ = 0.0;
    rate_ = 1.0;
}

std::optional<protocol::AudioConfig> AudioPlayout::config() const {
    const std::scoped_lock lock(mutex_);
    return config_;
}

AudioPlayoutStats AudioPlayout::stats() const {
    const std::scoped_lock lock(mutex_);
    AudioPlayoutStats stats = counters_;
    stats.playing = playing_;
    stats.buffered = Microseconds{static_cast<std::int64_t>(smoothed_us_)};
    stats.target = target_;
    return stats;
}

std::uint64_t AudioPlayout::frames_in(Microseconds duration) {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(duration.count(), 0)) *
           protocol::kAudioSampleRate / 1'000'000;
}

std::uint64_t AudioPlayout::buffered_frames() const {
    const auto played = static_cast<std::size_t>(position_);
    std::uint64_t frames = fifo_frames_ > played ? fifo_frames_ - played : 0;
    if (receiving_) {
        const std::int32_t span = distance(next_sequence_, newest_sequence_);
        if (span >= 0) {
            frames += (static_cast<std::uint64_t>(span) + 1) * frame_samples_;
        }
    }
    return frames;
}

void AudioPlayout::push(const protocol::AudioPacket& packet) {
    const std::scoped_lock lock(mutex_);
    if (!decoder_ || packet.payload.empty() || packet.payload.size() > protocol::kMaxAudioPayload) {
        return;
    }
    ++counters_.packets;
    const std::uint32_t sequence = packet.audio_sequence;
    if (!receiving_) {
        receiving_ = true;
        next_sequence_ = sequence;
        newest_sequence_ = sequence;
        next_capture_us_ = packet.capture_time_us;
    }
    const std::int32_t ahead = distance(next_sequence_, sequence);
    if (ahead < 0) {
        // Before the first sound, an older packet that overtook nothing yet still counts.
        if (started_ || std::cmp_greater_equal(distance(sequence, newest_sequence_), kSlots)) {
            ++counters_.late_packets;
            return;
        }
        next_sequence_ = sequence;
        next_capture_us_ = packet.capture_time_us;
    } else if (std::cmp_greater_equal(ahead, kSlots)) {
        // A jump beyond the buffer (a long outage): start over from here.
        reset_buffer();
        receiving_ = true;
        next_sequence_ = sequence;
        newest_sequence_ = sequence;
        next_capture_us_ = packet.capture_time_us;
    }
    Slot& entry = slot(sequence);
    entry.filled = true;
    entry.sequence = sequence;
    entry.capture_time_us = packet.capture_time_us;
    entry.size = packet.payload.size();
    std::ranges::copy(packet.payload, entry.data.begin());
    if (distance(newest_sequence_, sequence) > 0) {
        newest_sequence_ = sequence;
    }
    drop_excess();
}

void AudioPlayout::drop_excess() {
    // Waiting to start, hold exactly the reserve, so sound starts with the newest audio; playing,
    // the rate change drains small excesses and only a burst is cut.
    const Microseconds limit = playing_ ? target_ + tunables_.max_excess : target_;
    // A packet goes only if the reserve still holds the limit without it.
    const std::uint64_t keep = frames_in(limit) + frame_samples_;
    while (receiving_ && next_sequence_ != newest_sequence_ && buffered_frames() >= keep) {
        Slot& entry = slot(next_sequence_);
        if (entry.filled && entry.sequence == next_sequence_) {
            entry.filled = false;
            ++counters_.dropped_packets;
        }
        ++next_sequence_;
        next_capture_us_ += audio_duration_us(frame_samples_);
    }
}

void AudioPlayout::ran_dry(TimePoint now) {
    playing_ = false;
    ++counters_.underruns;
    target_ = std::min(target_ + tunables_.target_step, tunables_.max_target_delay);
    min_depth_us_ = kNoDepth;
    review_started_ = now;
}

bool AudioPlayout::decode_next(TimePoint now) {
    if (!decoder_ || !receiving_) {
        return false;
    }
    if (!playing_) {
        if (buffered_frames() < frames_in(target_)) {
            return false;
        }
        playing_ = true;
        concealed_in_a_row_ = 0;
        if (!started_) {
            started_ = true;
            review_started_ = now;
        }
    }
    if ((fifo_frames_ + kMaxDecodedFrames) * channels_ > fifo_.size()) {
        compact();
    }
    float* destination = &fifo_[fifo_frames_ * channels_];
    Slot& entry = slot(next_sequence_);
    int decoded = 0;
    std::uint64_t capture = next_capture_us_;
    if (entry.filled && entry.sequence == next_sequence_) {
        decoded = opus_decode_float(decoder_.get(), entry.data.data(),
                                    static_cast<opus_int32>(entry.size), destination,
                                    static_cast<int>(kMaxDecodedFrames), 0);
        capture = entry.capture_time_us;
        entry.filled = false;
        concealed_in_a_row_ = 0;
    } else {
        const bool later_arrived = distance(next_sequence_, newest_sequence_) > 0;
        if (!later_arrived && concealed_in_a_row_ >= tunables_.max_concealed_in_a_row) {
            ran_dry(now);
            return false;
        }
        ++concealed_in_a_row_;
        ++counters_.concealed_frames;
        decoded = opus_decode_float(decoder_.get(), nullptr, 0, destination,
                                    static_cast<int>(frame_samples_), 0);
    }
    if (decoded <= 0) { // a corrupt packet: play silence in its place
        decoded = static_cast<int>(frame_samples_);
        std::fill_n(destination, frame_samples_ * channels_, 0.0F);
    }
    const auto frames = static_cast<std::size_t>(decoded);
    ++next_sequence_;
    next_capture_us_ = capture + audio_duration_us(frames);
    fifo_capture_us_ = static_cast<std::int64_t>(capture) -
                       static_cast<std::int64_t>(audio_duration_us(fifo_frames_));
    fifo_frames_ += frames;
    return true;
}

void AudioPlayout::compact() {
    const auto index = static_cast<std::size_t>(position_);
    if (index < 2) {
        return;
    }
    const std::size_t drop = std::min(index - 1, fifo_frames_); // keep one frame of history
    const auto offset = static_cast<std::ptrdiff_t>(drop * channels_);
    const auto end = static_cast<std::ptrdiff_t>(fifo_frames_ * channels_);
    std::copy(fifo_.begin() + offset, fifo_.begin() + end, fifo_.begin());
    fifo_frames_ -= drop;
    position_ -= static_cast<double>(drop);
    fifo_capture_us_ += static_cast<std::int64_t>(audio_duration_us(drop));
}

void AudioPlayout::update_rate(TimePoint now) {
    const auto depth_us = static_cast<double>(audio_duration_us(buffered_frames()));
    if (!playing_) { // the average starts when sound does
        smoothed_us_ = depth_us;
    } else {
        const auto elapsed = std::chrono::duration_cast<Microseconds>(now - last_update_);
        const double weight =
            std::clamp(static_cast<double>(elapsed.count()) / kMicrosPerSecond, 0.0, 1.0);
        smoothed_us_ += weight * (depth_us - smoothed_us_);
    }
    last_update_ = now;
    if (!playing_) {
        rate_ = 1.0;
        return;
    }
    min_depth_us_ = std::min(min_depth_us_, depth_us);
    review_target(now);

    const double error_us = smoothed_us_ - static_cast<double>(target_.count());
    if (std::abs(error_us) <= static_cast<double>(tunables_.rate_deadband.count())) {
        if (rate_ != 1.0) {
            rate_ = 1.0;
            position_ = std::floor(position_); // back to whole samples: exact playback
        }
        return;
    }
    rate_ = 1.0 + std::clamp(error_us / kMicrosPerSecond * tunables_.rate_gain_per_second,
                             -tunables_.max_rate_change, tunables_.max_rate_change);
}

void AudioPlayout::review_target(TimePoint now) {
    if (now - review_started_ < tunables_.review_interval || min_depth_us_ == kNoDepth) {
        return;
    }
    // The reserve never fell below the minimum seen: that much more than the margin was not needed.
    const double spare_us = min_depth_us_ - static_cast<double>(tunables_.review_margin.count());
    if (spare_us > 0) {
        target_ = std::max(target_ - Microseconds{static_cast<std::int64_t>(spare_us)},
                           tunables_.target_delay);
    }
    min_depth_us_ = kNoDepth;
    review_started_ = now;
}

float AudioPlayout::sample(std::size_t frame, std::size_t channel) const {
    return fifo_[(frame * channels_) + channel];
}

std::uint64_t AudioPlayout::read(std::span<float> out, std::uint8_t channels, TimePoint now) {
    std::ranges::fill(out, 0.0F);
    const std::scoped_lock lock(mutex_);
    if (!decoder_ || channels != channels_ || channels_ == 0) {
        return 0;
    }
    const std::size_t frames = out.size() / channels_;
    update_rate(now);
    std::optional<std::int64_t> first_capture;
    for (std::size_t i = 0; i < frames; ++i) {
        // Interpolation reads the frames on both sides of the position: two ahead must exist.
        while (static_cast<std::size_t>(position_) + 2 >= fifo_frames_ && decode_next(now)) {
        }
        const auto index = static_cast<std::size_t>(position_);
        if (index + 2 >= fifo_frames_) {
            break; // refilling: the rest stays silent
        }
        if (!first_capture) {
            first_capture = fifo_capture_us_ + static_cast<std::int64_t>(audio_duration_us(index));
        }
        const auto t = static_cast<float>(position_ - static_cast<double>(index));
        const std::size_t before = index == 0 ? 0 : index - 1;
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            out[(i * channels_) + channel] =
                interpolate(sample(before, channel), sample(index, channel),
                            sample(index + 1, channel), sample(index + 2, channel), t);
        }
        position_ += rate_;
    }
    compact();
    if (!first_capture) {
        return 0;
    }
    const auto capture = static_cast<std::uint64_t>(std::max<std::int64_t>(*first_capture, 1));
    counters_.last_capture_time_us = capture;
    counters_.last_read_at = now;
    return capture;
}

} // namespace osplay::media
