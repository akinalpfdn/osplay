#include "audio/viewer_audio.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <vector>

namespace {

using osplay::engine::AudioOutput;
using osplay::engine::AudioOutputFactory;
using osplay::engine::AudioSource;
using osplay::engine::ViewerAudio;

struct Counts {
    int opened = 0;
    int running = 0;
    std::vector<std::uint8_t> channels;
};

class FakeOutput final : public AudioOutput {
public:
    explicit FakeOutput(Counts& counts) : counts_(&counts) { ++counts_->running; }
    ~FakeOutput() override { --counts_->running; }
    FakeOutput(const FakeOutput&) = delete;
    FakeOutput& operator=(const FakeOutput&) = delete;
    FakeOutput(FakeOutput&&) = delete;
    FakeOutput& operator=(FakeOutput&&) = delete;

private:
    Counts* counts_;
};

class FakeOutputs final : public AudioOutputFactory {
public:
    Counts counts;
    bool fails = false;

    std::unique_ptr<AudioOutput> open(const osplay_audio_config& config,
                                      AudioSource& /*source*/) override {
        if (fails) {
            return nullptr;
        }
        ++counts.opened;
        counts.channels.push_back(config.channels);
        return std::make_unique<FakeOutput>(counts);
    }
};

class SilentSource final : public AudioSource {
public:
    void read(std::span<float> out, std::uint8_t /*channels*/) override {
        for (float& sample : out) {
            sample = 0.0F;
        }
    }
};

constexpr osplay_audio_config kStereo{
    .sample_rate_hz = 48'000, .channels = 2, .frame_duration_us = 10'000, .bitrate_kbps = 128};

TEST(ViewerAudio, OpensOneOutputPerConfigAndClosesItWhenTheHostStops) {
    FakeOutputs outputs;
    SilentSource source;
    ViewerAudio audio(outputs, source);

    ASSERT_TRUE(audio.configure(kStereo));
    osplay_audio_config mono = kStereo;
    mono.channels = 1;
    ASSERT_TRUE(audio.configure(mono));
    EXPECT_EQ(outputs.counts.opened, 2);
    EXPECT_EQ(outputs.counts.running, 1) << "the first output stopped before the second opened";
    EXPECT_EQ(outputs.counts.channels, (std::vector<std::uint8_t>{2, 1}));

    audio.stop();
    EXPECT_EQ(outputs.counts.running, 0);
    EXPECT_FALSE(audio.playing());
}

TEST(ViewerAudio, AnOutputThatCannotOpenLeavesTheSessionSilent) {
    FakeOutputs outputs;
    outputs.fails = true;
    SilentSource source;
    ViewerAudio audio(outputs, source);
    EXPECT_FALSE(audio.configure(kStereo));
    EXPECT_FALSE(audio.playing());
}

TEST(ViewerAudio, NothingOpensOnceTheSessionIsEnding) {
    FakeOutputs outputs;
    SilentSource source;
    ViewerAudio audio(outputs, source);
    ASSERT_TRUE(audio.configure(kStereo));

    audio.shutdown();
    EXPECT_EQ(outputs.counts.running, 0);
    EXPECT_FALSE(audio.configure(kStereo)) << "a late AudioConfig from the I/O thread";
    EXPECT_EQ(outputs.counts.opened, 1);
}

} // namespace
