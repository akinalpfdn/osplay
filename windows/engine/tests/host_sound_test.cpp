#include "audio/host_sound.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using osplay::engine::HostSound;
using osplay::engine::SoundReceiver;
using osplay::engine::SoundSink;
using osplay::engine::StereoResampler;
using osplay::engine::SystemSound;

class FakeSystemSound final : public SystemSound {
public:
    SoundSink* sink = nullptr;
    int starts = 0;
    int stops = 0;
    bool fails = false;

    bool start(SoundSink& recorder_sink) override {
        ++starts;
        if (fails) {
            return false;
        }
        sink = &recorder_sink;
        return true;
    }
    void stop() override {
        ++stops;
        sink = nullptr;
    }

    void play(float value) const {
        const std::vector<float> samples(4, value);
        sink->on_sound(samples, 1);
    }
};

class FakeReceiver final : public SoundReceiver {
public:
    bool started = false;
    bool refuses = false;
    std::vector<float> received;

    bool start_audio() override {
        started = !refuses;
        return started;
    }
    void push_audio(std::span<const float> samples, std::uint64_t /*capture_time_us*/) override {
        received.assign(samples.begin(), samples.end());
    }
    void stop_audio() override { started = false; }
};

TEST(HostSound, SendsTheRecordedSoundToOneDisplayAndGivesTheSpeakersBack) {
    FakeSystemSound system;
    HostSound sound(system);
    FakeReceiver display;

    ASSERT_TRUE(sound.route(display));
    EXPECT_TRUE(display.started);
    EXPECT_EQ(system.starts, 1);
    system.play(0.5F);
    EXPECT_EQ(display.received, std::vector<float>(4, 0.5F));

    sound.release(display);
    EXPECT_FALSE(display.started);
    EXPECT_EQ(system.stops, 1) << "the speakers play again";
}

TEST(HostSound, MovesTheSoundToTheNextDisplayWithoutRestartingTheRecorder) {
    FakeSystemSound system;
    HostSound sound(system);
    FakeReceiver first;
    FakeReceiver second;

    ASSERT_TRUE(sound.route(first));
    ASSERT_TRUE(sound.route(second));
    EXPECT_FALSE(first.started);
    EXPECT_TRUE(second.started);
    EXPECT_TRUE(sound.routes_to(second));
    EXPECT_EQ(system.starts, 1);
    EXPECT_EQ(system.stops, 0);

    sound.release(first); // no longer has it: nothing changes
    EXPECT_TRUE(second.started);
    EXPECT_EQ(system.stops, 0);
}

TEST(HostSound, TheMuteKeySilencesTheDisplayAndTheNextPressBringsTheSoundBack) {
    FakeSystemSound system;
    HostSound sound(system);
    FakeReceiver display;
    ASSERT_TRUE(sound.route(display));

    system.sink->on_mute_key();
    EXPECT_TRUE(sound.display_muted());
    system.play(0.5F);
    EXPECT_EQ(display.received, std::vector<float>(4, 0.0F)) << "silence keeps the viewer going";

    system.sink->on_mute_key();
    system.play(0.5F);
    EXPECT_EQ(display.received, std::vector<float>(4, 0.5F));
}

TEST(HostSound, NoDisplayHasTheSoundWhenTheRecorderOrTheSessionFails) {
    FakeSystemSound system;
    system.fails = true;
    HostSound sound(system);
    FakeReceiver display;
    EXPECT_FALSE(sound.route(display));
    EXPECT_FALSE(display.started);
    EXPECT_FALSE(sound.routes_to(display));

    system.fails = false;
    FakeReceiver refusing;
    refusing.refuses = true;
    EXPECT_FALSE(sound.route(refusing));
    EXPECT_EQ(system.starts, 1) << "the recorder does not start for a session that refused";
}

TEST(StereoResampler, PassesThe48kHzStereoThroughUnchanged) {
    StereoResampler resampler(48'000, 2);
    std::vector<float> output;
    const std::vector<float> input{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F};
    resampler.convert(input, output);
    resampler.convert(std::vector<float>{0.7F, 0.8F}, output);
    // Every input frame comes out as it was, across calls too.
    const std::vector<float> expected{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
    ASSERT_EQ(output.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_FLOAT_EQ(output[i], expected[i]) << i;
    }
}

TEST(StereoResampler, Turns44_1kHzIntoAbout48kHz) {
    StereoResampler resampler(44'100, 2);
    std::vector<float> output;
    const std::vector<float> second(44'100 * 2, 0.25F);
    resampler.convert(second, output);
    EXPECT_NEAR(static_cast<double>(output.size()) / 2, 48'000.0, 2.0);
    for (const float sample : output) {
        EXPECT_FLOAT_EQ(sample, 0.25F);
    }
}

TEST(StereoResampler, CopiesMonoToBothSidesAndMixesTheCentreIntoSurround) {
    StereoResampler mono(48'000, 1);
    std::vector<float> output;
    mono.convert(std::vector<float>{0.5F, 0.5F}, output);
    EXPECT_EQ(output, (std::vector<float>{0.5F, 0.5F, 0.5F, 0.5F}));

    StereoResampler surround(48'000, 6); // FL FR C LFE SL SR
    output.clear();
    const std::vector<float> frame{0.1F, 0.2F, 0.4F, 0.9F, 0.9F, 0.9F};
    std::vector<float> two_frames(frame);
    two_frames.insert(two_frames.end(), frame.begin(), frame.end());
    surround.convert(two_frames, output);
    ASSERT_EQ(output.size(), 4U);
    EXPECT_NEAR(output[0], 0.1F + (0.7071F * 0.4F), 1e-5);
    EXPECT_NEAR(output[1], 0.2F + (0.7071F * 0.4F), 1e-5);
}

} // namespace
