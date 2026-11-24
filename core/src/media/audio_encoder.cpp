#include "media/audio_encoder.hpp"

#include <opus.h>

#include <algorithm>
#include <utility>

namespace osplay::media {

void AudioEncoder::Deleter::operator()(OpusEncoder* encoder) const {
    opus_encoder_destroy(encoder);
}

Expected<std::unique_ptr<AudioEncoder>, Error>
AudioEncoder::create(const protocol::AudioConfig& config) {
    using Result = Expected<std::unique_ptr<AudioEncoder>, Error>;
    if (!protocol::encode_control(config)) {
        return Result::failure(Error::ValueOutOfRange);
    }
    int status = OPUS_OK;
    // Restricted low delay: CELT only, 2.5 ms of look-ahead instead of 6.5 ms. Desktop sound is
    // mostly music and video, which CELT carries well at these bitrates (DECISIONS.md).
    std::unique_ptr<OpusEncoder, Deleter> encoder(
        opus_encoder_create(static_cast<opus_int32>(config.sample_rate_hz), config.channels,
                            OPUS_APPLICATION_RESTRICTED_LOWDELAY, &status));
    if (status != OPUS_OK || !encoder) {
        return Result::failure(Error::Internal);
    }
    // The request constant without its OPUS_SET_BITRATE wrapper, whose cast is C style.
    const opus_int32 bitrate = opus_int32{config.bitrate_kbps} * 1000;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): libopus' control API is variadic
    if (opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE_REQUEST, bitrate) != OPUS_OK) {
        return Result::failure(Error::Internal);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): private constructor
    return std::unique_ptr<AudioEncoder>(new AudioEncoder(config, std::move(encoder)));
}

AudioEncoder::AudioEncoder(const protocol::AudioConfig& config,
                           std::unique_ptr<OpusEncoder, Deleter> encoder)
    : config_(config), encoder_(std::move(encoder)),
      frame_samples_(audio_frame_samples(config.frame_duration_us)) {
    pending_.reserve(frame_samples_ * config.channels);
}

AudioEncoder::~AudioEncoder() = default;

Status<Error> AudioEncoder::encode(const float* frame, std::uint64_t capture_time_us,
                                   const EncodedAudioSink& sink) {
    const opus_int32 size =
        opus_encode_float(encoder_.get(), frame, static_cast<int>(frame_samples_), packet_.data(),
                          static_cast<opus_int32>(packet_.size()));
    if (size <= 0) {
        return Status<Error>::failure(Error::Internal);
    }
    sink(capture_time_us,
         std::span<const std::uint8_t>(packet_).first(static_cast<std::size_t>(size)));
    return Status<Error>::ok();
}

Status<Error> AudioEncoder::push(std::span<const float> samples, std::uint64_t capture_time_us,
                                 const EncodedAudioSink& sink) {
    const std::size_t channels = config_.channels;
    if (samples.size() % channels != 0) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    const std::size_t frame_floats = frame_samples_ * channels;
    // Capture time of the sample at `offset` floats into this push.
    const auto capture_at = [&](std::size_t offset) {
        return capture_time_us + audio_duration_us(offset / channels);
    };
    std::size_t offset = 0;
    while (offset < samples.size()) {
        if (pending_.empty() && samples.size() - offset >= frame_floats) {
            if (const auto encoded = encode(&samples[offset], capture_at(offset), sink); !encoded) {
                return encoded;
            }
            offset += frame_floats;
            continue;
        }
        if (pending_.empty()) {
            pending_capture_us_ = capture_at(offset);
        }
        const std::size_t take = std::min(frame_floats - pending_.size(), samples.size() - offset);
        const auto part = samples.subspan(offset, take);
        pending_.insert(pending_.end(), part.begin(), part.end());
        offset += take;
        if (pending_.size() == frame_floats) {
            const auto encoded = encode(pending_.data(), pending_capture_us_, sink);
            pending_.clear();
            if (!encoded) {
                return encoded;
            }
        }
    }
    return Status<Error>::ok();
}

} // namespace osplay::media
