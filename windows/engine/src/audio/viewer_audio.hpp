#pragma once

// Viewer side of the host's sound (Phase 17, AUD-04): opens the audio output when the host starts
// sending sound and closes it when the host stops or the session ends. The output pulls PCM from
// the core's jitter buffer on its own thread. Portable and unit tested; WASAPI is the Windows
// output.

#include <osplay/osplay.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

namespace osplay::engine {

/// Where an output takes its samples: the core session's jitter buffer.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    /// Fills `out` with interleaved PCM of `channels` channels; silence when there is none.
    /// Called from the output's thread.
    virtual void read(std::span<float> out, std::uint8_t channels) = 0;
};

/// A running output; destroying it stops it and waits for its thread.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;
};

class AudioOutputFactory {
public:
    virtual ~AudioOutputFactory() = default;
    /// Starts playing `source` in `config`'s format; null when no output could be opened.
    [[nodiscard]] virtual std::unique_ptr<AudioOutput> open(const osplay_audio_config& config,
                                                            AudioSource& source) = 0;
};

class ViewerAudio {
public:
    ViewerAudio(AudioOutputFactory& outputs, AudioSource& source);
    ~ViewerAudio();
    ViewerAudio(const ViewerAudio&) = delete;
    ViewerAudio& operator=(const ViewerAudio&) = delete;
    ViewerAudio(ViewerAudio&&) = delete;
    ViewerAudio& operator=(ViewerAudio&&) = delete;

    /// The host sends sound in this format (again): the output starts over. False when no output
    /// could be opened; the session goes on without sound.
    [[nodiscard]] bool configure(const osplay_audio_config& config);
    /// The host stopped its sound.
    void stop();
    /// The session is ending: stops and ignores any later configure.
    void shutdown();
    [[nodiscard]] bool playing() const;

private:
    AudioOutputFactory* outputs_; // never null; outlives this
    AudioSource* source_;         // never null; outlives this
    mutable std::mutex mutex_;
    std::unique_ptr<AudioOutput> output_;
    bool closed_ = false;
};

} // namespace osplay::engine
