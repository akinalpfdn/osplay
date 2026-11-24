#pragma once

// The PC's sound on Windows (AUD-03): WASAPI loopback of the default speakers, which keeps the
// full level while the endpoint is muted and already follows the endpoint's volume (measured on
// the user's PC, 2026-11-24). The speakers are muted while recording; the mute key, which would
// unmute them, is caught, undone and reported. A marker file names speakers Osplay muted, so a
// crash leaves nothing muted after the next start (restore_speakers_after_crash).

#include "audio/host_sound.hpp"
#include "audio/wasapi_audio_output.hpp"

#include <memory>

namespace osplay::engine {

class WasapiSystemSound final : public SystemSound {
public:
    explicit WasapiSystemSound(AudioLog log);
    ~WasapiSystemSound() override;
    WasapiSystemSound(const WasapiSystemSound&) = delete;
    WasapiSystemSound& operator=(const WasapiSystemSound&) = delete;
    WasapiSystemSound(WasapiSystemSound&&) = delete;
    WasapiSystemSound& operator=(WasapiSystemSound&&) = delete;

    [[nodiscard]] bool start(SoundSink& sink) override;
    void stop() override;

private:
    struct Recorder;
    AudioLog log_;
    std::unique_ptr<Recorder> recorder_;
};

/// Unmutes the speakers a previous Osplay muted and could not give back (it crashed or was ended
/// while a display had the sound). Call once at startup.
void restore_speakers_after_crash();

} // namespace osplay::engine
