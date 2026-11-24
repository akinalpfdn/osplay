#include "media/audio_encoder.hpp"
#include "media/audio_playout.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace {

namespace media = osplay::media;
namespace protocol = osplay::protocol;
using osplay::Microseconds;
using osplay::TimePoint;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr std::size_t kFrame = 480; // 10 ms at 48 kHz
constexpr double kAmplitude = 0.5;
constexpr double kToneHz = 1'000.0;

protocol::AudioConfig stereo() {
    return {.codec = protocol::AudioCodec::Opus,
            .channels = 2,
            .frame_duration_us = 10'000,
            .sample_rate_hz = protocol::kAudioSampleRate,
            .bitrate_kbps = 128};
}

/// Interleaved stereo tone; sample `first` onward.
std::vector<float> tone(std::size_t first, std::size_t frames) {
    std::vector<float> samples(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const double phase = 2.0 * std::numbers::pi * kToneHz * static_cast<double>(first + i) /
                             protocol::kAudioSampleRate;
        const auto value = static_cast<float>(kAmplitude * std::sin(phase));
        samples[2 * i] = value;
        samples[(2 * i) + 1] = value;
    }
    return samples;
}

struct Packet {
    std::uint32_t sequence = 0;
    std::uint64_t capture_time_us = 0;
    std::vector<std::uint8_t> payload;

    [[nodiscard]] protocol::AudioPacket view() const {
        return {.audio_sequence = sequence, .capture_time_us = capture_time_us, .payload = payload};
    }
};

/// `count` 10 ms packets of the tone, capture times starting at 1 s.
std::vector<Packet> encoded_tone(std::size_t count) {
    auto encoder = media::AudioEncoder::create(stereo());
    EXPECT_TRUE(encoder.has_value());
    std::vector<Packet> packets;
    const auto samples = tone(0, count * kFrame);
    const auto status = encoder.value()->push(
        samples, 1'000'000, [&](std::uint64_t capture, std::span<const std::uint8_t> payload) {
            packets.push_back({.sequence = static_cast<std::uint32_t>(packets.size()),
                               .capture_time_us = capture,
                               .payload = {payload.begin(), payload.end()}});
        });
    EXPECT_TRUE(status.has_value());
    return packets;
}

double rms(std::span<const float> samples) {
    double sum = 0.0;
    for (const float sample : samples) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

/// Drives a playout in simulated time: packets arrive every `send_interval` (late ones held
/// back by `delays`), the device reads `read_frames` every `read_interval`.
struct Player {
    explicit Player(media::AudioPlayoutTunables tunables = {}) : playout(tunables) {
        EXPECT_TRUE(playout.configure(stereo()).has_value());
    }

    /// Reads one device buffer and records whether it held sound.
    void read(std::size_t frames = kFrame) {
        std::vector<float> out(frames * 2);
        const std::uint64_t capture = playout.read(out, 2, now);
        const bool sound = capture != 0;
        if (sound) {
            started = true;
            output.insert(output.end(), out.begin(), out.end());
            last_capture = capture;
        } else if (started) {
            ++silent_reads_after_start;
        }
    }

    media::AudioPlayout playout;
    TimePoint now{seconds(1000)};
    bool started = false;
    int silent_reads_after_start = 0;
    std::uint64_t last_capture = 0;
    std::vector<float> output;
};

TEST(AudioEncoder, CutsAnyPushSizesIntoTenMillisecondFramesWithTheirCaptureTimes) {
    auto encoder = media::AudioEncoder::create(stereo());
    ASSERT_TRUE(encoder.has_value());
    std::vector<std::uint64_t> captures;
    const auto sink = [&](std::uint64_t capture, std::span<const std::uint8_t> payload) {
        EXPECT_FALSE(payload.empty());
        EXPECT_LE(payload.size(), protocol::kMaxAudioPayload);
        captures.push_back(capture);
    };
    // 300 frames, then 900, then 240: 1440 frames = three 10 ms frames.
    std::uint64_t capture = 5'000'000;
    for (const std::size_t frames : {300U, 900U, 240U}) {
        ASSERT_TRUE(encoder.value()->push(tone(0, frames), capture, sink).has_value());
        capture += media::audio_duration_us(frames);
    }
    EXPECT_EQ(captures, (std::vector<std::uint64_t>{5'000'000, 5'010'000, 5'020'000}));
}

TEST(AudioEncoder, RefusesPartialSampleFramesAndInvalidFormats) {
    auto encoder = media::AudioEncoder::create(stereo());
    ASSERT_TRUE(encoder.has_value());
    const std::vector<float> odd(3, 0.0F);
    EXPECT_FALSE(encoder.value()->push(odd, 0, [](auto, auto) {}).has_value());

    protocol::AudioConfig wrong = stereo();
    wrong.sample_rate_hz = 44'100;
    EXPECT_FALSE(media::AudioEncoder::create(wrong).has_value());
    wrong = stereo();
    wrong.frame_duration_us = 7'000;
    EXPECT_FALSE(media::AudioEncoder::create(wrong).has_value());
}

TEST(AudioPlayout, WaitsForItsReserveThenPlaysTheToneBack) {
    Player player;
    const auto packets = encoded_tone(200);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        player.playout.push(packets[i].view());
        player.now += milliseconds(10);
        player.read();
        if (i < 2) {
            EXPECT_FALSE(player.started) << "30 ms reserve first, packet " << i;
        }
    }
    EXPECT_TRUE(player.started);
    EXPECT_EQ(player.silent_reads_after_start, 0);
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.concealed_frames, 0U);
    EXPECT_EQ(stats.underruns, 0U);
    // Past the codec's start-up, the tone comes back at its level.
    const std::span<const float> settled = std::span(player.output).subspan(kFrame * 2 * 10);
    EXPECT_NEAR(rms(settled), kAmplitude / std::numbers::sqrt2, 0.05);
}

TEST(AudioPlayout, ReorderedPacketsPlayInOrderWithoutConcealment) {
    Player player;
    auto packets = encoded_tone(100);
    for (std::size_t i = 0; i + 1 < packets.size(); i += 4) {
        std::swap(packets[i], packets[i + 1]);
    }
    for (const auto& packet : packets) {
        player.playout.push(packet.view());
        player.now += milliseconds(10);
        player.read();
    }
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.concealed_frames, 0U);
    EXPECT_EQ(stats.late_packets, 0U);
    EXPECT_EQ(player.silent_reads_after_start, 0);
}

TEST(AudioPlayout, ConcealsFivePercentLossWithoutGaps) {
    Player player;
    const auto packets = encoded_tone(1'000);
    std::uint64_t lost = 0;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (i % 20 == 7) {
            ++lost;
        } else {
            player.playout.push(packets[i].view());
        }
        player.now += milliseconds(10);
        player.read();
    }
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.concealed_frames, lost);
    EXPECT_EQ(stats.underruns, 0U);
    EXPECT_EQ(player.silent_reads_after_start, 0) << "no audible gap";
    EXPECT_NEAR(rms(std::span(player.output).subspan(kFrame * 2 * 10)),
                kAmplitude / std::numbers::sqrt2, 0.08);
}

TEST(AudioPlayout, APacketArrivingAfterItsTurnIsDroppedAsLate) {
    Player player;
    const auto packets = encoded_tone(60);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (i != 20) {
            player.playout.push(packets[i].view());
        }
        if (i == 30) {
            player.playout.push(packets[20].view()); // long after it was concealed
        }
        player.now += milliseconds(10);
        player.read();
    }
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.late_packets, 1U);
    EXPECT_EQ(stats.concealed_frames, 1U);
}

TEST(AudioPlayout, AStallRefillsALargerReserveAndPlaysOn) {
    Player player;
    const auto packets = encoded_tone(300);
    std::vector<const Packet*> held;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        // 200 ms without packets, then everything at once.
        if (i >= 100 && i < 120) {
            held.push_back(&packets[i]);
        } else {
            for (const Packet* packet : held) {
                player.playout.push(packet->view());
            }
            held.clear();
            player.playout.push(packets[i].view());
        }
        player.now += milliseconds(10);
        player.read();
    }
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.underruns, 1U);
    EXPECT_EQ(stats.target, Microseconds{50'000}) << "30 ms + one 20 ms step";
    EXPECT_GT(stats.late_packets + stats.dropped_packets, 0U);
    EXPECT_TRUE(stats.playing) << "sound came back";
}

TEST(AudioPlayout, ACalmNetworkLowersTheReserveAgain) {
    Player player;
    const auto packets = encoded_tone(3'500);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (i < 100 || i >= 125) { // one 250 ms stall early on
            player.playout.push(packets[i].view());
        }
        player.now += milliseconds(10);
        player.read();
    }
    const auto stats = player.playout.stats();
    EXPECT_GE(stats.underruns, 1U);
    EXPECT_EQ(stats.target, Microseconds{30'000}) << "back to the least reserve";
}

/// The device's clock runs `ppm` parts per million faster (positive) or slower than the host's.
void play_with_drift(double ppm) {
    Player player;
    const std::size_t count = 6'000; // a minute of sound
    const auto packets = encoded_tone(count);
    const auto read_interval =
        std::chrono::duration<double, std::micro>(10'000.0 / (1.0 + ppm / 1e6));
    TimePoint next_read = player.now;
    TimePoint next_packet = player.now;
    std::size_t sent = 0;
    Microseconds most{0};
    Microseconds least{seconds(1)};
    while (sent < packets.size()) {
        if (next_packet <= next_read) {
            player.now = next_packet;
            player.playout.push(packets[sent++].view());
            next_packet += milliseconds(10);
        } else {
            player.now = next_read;
            player.read();
            next_read += std::chrono::duration_cast<Microseconds>(read_interval);
            const auto buffered = player.playout.stats().buffered;
            if (player.started && sent > 500) {
                most = std::max(most, buffered);
                least = std::min(least, buffered);
            }
        }
    }
    const auto stats = player.playout.stats();
    EXPECT_EQ(stats.underruns, 0U) << ppm << " ppm";
    EXPECT_EQ(stats.dropped_packets, 0U) << ppm << " ppm";
    EXPECT_EQ(player.silent_reads_after_start, 0) << ppm << " ppm";
    // The reserve stays around its target instead of growing or draining with the drift.
    EXPECT_LE(most, Microseconds{75'000}) << ppm << " ppm";
    EXPECT_GE(least, Microseconds{15'000}) << ppm << " ppm";
}

TEST(AudioPlayout, FollowsADeviceClockThatRunsFasterOrSlowerThanTheHosts) {
    play_with_drift(+1'000.0);
    play_with_drift(-1'000.0);
}

TEST(AudioPlayout, ABurstWhilePlayingIsCutBackToTheReserve) {
    Player player;
    const auto packets = encoded_tone(200);
    for (std::size_t i = 0; i < 50; ++i) {
        player.playout.push(packets[i].view());
        player.now += milliseconds(10);
        player.read();
    }
    for (std::size_t i = 50; i < 100; ++i) { // half a second in one go
        player.playout.push(packets[i].view());
    }
    const auto stats = player.playout.stats();
    EXPECT_GT(stats.dropped_packets, 0U);
    player.now += milliseconds(10);
    player.read();
    EXPECT_EQ(player.silent_reads_after_start, 0);
}

TEST(AudioPlayout, ReportsTheCaptureTimeOfWhatItPlays) {
    Player player;
    const auto packets = encoded_tone(100);
    for (const auto& packet : packets) {
        player.playout.push(packet.view());
        player.now += milliseconds(10);
        player.read();
    }
    // The last read played audio captured about the reserve before the newest packet.
    const std::uint64_t newest = packets.back().capture_time_us;
    EXPECT_GE(player.last_capture, newest - 60'000);
    EXPECT_LE(player.last_capture, newest);
}

TEST(AudioPlayout, IsSilentForOtherChannelCountsAndAfterStop) {
    Player player;
    const auto packets = encoded_tone(20);
    for (const auto& packet : packets) {
        player.playout.push(packet.view());
    }
    std::vector<float> mono(kFrame, 1.0F);
    EXPECT_EQ(player.playout.read(mono, 1, player.now), 0U);
    EXPECT_TRUE(std::ranges::all_of(mono, [](float sample) { return sample == 0.0F; }));

    player.playout.stop();
    std::vector<float> out(kFrame * 2, 1.0F);
    EXPECT_EQ(player.playout.read(out, 2, player.now), 0U);
    EXPECT_TRUE(std::ranges::all_of(out, [](float sample) { return sample == 0.0F; }));
    EXPECT_FALSE(player.playout.config().has_value());
}

} // namespace
