#pragma once

// The viewer's sound on Windows: WASAPI shared mode on the default render device, event driven,
// in the host's format (48 kHz float); Windows converts to the device's rate and channels. A lost
// device (headphones unplugged) is opened again on the new default.

#include "audio/viewer_audio.hpp"

#include <osplay/osplay.h>

#include <functional>
#include <memory>
#include <string>

namespace osplay::engine {

using AudioLog = std::function<void(osplay_log_level, const std::string&)>;

class WasapiAudioOutputs final : public AudioOutputFactory {
public:
    explicit WasapiAudioOutputs(AudioLog log);
    [[nodiscard]] std::unique_ptr<AudioOutput> open(const osplay_audio_config& config,
                                                    AudioSource& source) override;

private:
    AudioLog log_;
};

} // namespace osplay::engine
