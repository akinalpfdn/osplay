#include "host_sound.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace osplay::engine {
namespace {

/// The centre's share in each side when more than two channels are mixed down (-3 dB).
constexpr float kCentreGain = 0.7071F;
constexpr std::size_t kCentre = 2;

} // namespace

HostSound::HostSound(SystemSound& system) : system_(&system) {}

HostSound::~HostSound() {
    const std::scoped_lock control(control_);
    stop_recording();
}

bool HostSound::route(SoundReceiver& receiver) {
    const std::scoped_lock control(control_);
    SoundReceiver* previous = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        if (receiver_ == &receiver) {
            return true;
        }
        previous = receiver_;
        receiver_ = nullptr;
    }
    if (previous != nullptr) {
        previous->stop_audio();
    }
    if (!receiver.start_audio()) {
        stop_recording();
        return false;
    }
    if (!recording_) {
        recording_ = system_->start(*this);
        if (!recording_) {
            receiver.stop_audio();
            return false;
        }
    }
    const std::scoped_lock lock(mutex_);
    receiver_ = &receiver;
    muted_ = false;
    return true;
}

void HostSound::release(SoundReceiver& receiver) {
    const std::scoped_lock control(control_);
    {
        const std::scoped_lock lock(mutex_);
        if (receiver_ != &receiver) {
            return;
        }
        receiver_ = nullptr;
        muted_ = false;
    }
    stop_recording();
    receiver.stop_audio();
}

bool HostSound::routes_to(const SoundReceiver& receiver) const {
    const std::scoped_lock lock(mutex_);
    return receiver_ == &receiver;
}

bool HostSound::display_muted() const {
    const std::scoped_lock lock(mutex_);
    return muted_;
}

void HostSound::stop_recording() {
    if (recording_) {
        system_->stop();
        recording_ = false;
    }
}

void HostSound::on_sound(std::span<const float> samples, std::uint64_t capture_time_us) {
    const std::scoped_lock lock(mutex_);
    if (receiver_ == nullptr) {
        return;
    }
    if (!muted_) {
        receiver_->push_audio(samples, capture_time_us);
        return;
    }
    // Silence keeps the viewer's reserve and clock going, so the sound comes back at once.
    silence_.assign(samples.size(), 0.0F);
    receiver_->push_audio(silence_, capture_time_us);
}

void HostSound::on_mute_key() {
    const std::scoped_lock lock(mutex_);
    if (receiver_ != nullptr) {
        muted_ = !muted_;
    }
}

StereoResampler::StereoResampler(std::uint32_t sample_rate, std::uint16_t channels)
    : step_(static_cast<double>(sample_rate == 0 ? OSPLAY_AUDIO_SAMPLE_RATE : sample_rate) /
            OSPLAY_AUDIO_SAMPLE_RATE),
      channels_(std::max<std::uint16_t>(channels, 1)) {}

void StereoResampler::convert(std::span<const float> input, std::vector<float>& output) {
    const std::size_t frames = input.size() / channels_;
    const auto side = [&](std::size_t frame, std::size_t channel) {
        const std::size_t base = frame * channels_;
        if (channels_ == 1) {
            return input[base];
        }
        const float value = input[base + channel];
        return channels_ > kCentre ? value + (kCentreGain * input[base + kCentre]) : value;
    };
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float left = side(frame, 0);
        const float right = side(frame, 1);
        if (!primed_) {
            last_left_ = left;
            last_right_ = right;
            primed_ = true;
            continue;
        }
        // Output frames between the previous input frame (0) and this one (1).
        while (position_ <= 1.0) {
            const auto weight = static_cast<float>(position_);
            output.push_back(last_left_ + ((left - last_left_) * weight));
            output.push_back(last_right_ + ((right - last_right_) * weight));
            position_ += step_;
        }
        position_ -= 1.0;
        last_left_ = left;
        last_right_ = right;
    }
}

} // namespace osplay::engine
