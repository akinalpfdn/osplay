#include "audio/viewer_audio.hpp"

#include <utility>

namespace osplay::engine {

ViewerAudio::ViewerAudio(AudioOutputFactory& outputs, AudioSource& source)
    : outputs_(&outputs), source_(&source) {}

ViewerAudio::~ViewerAudio() {
    shutdown();
}

bool ViewerAudio::configure(const osplay_audio_config& config) {
    const std::scoped_lock lock(mutex_);
    if (closed_) {
        return false;
    }
    output_.reset(); // one output at a time: the old one stops before the new one opens
    output_ = outputs_->open(config, *source_);
    return output_ != nullptr;
}

void ViewerAudio::stop() {
    std::unique_ptr<AudioOutput> stopped;
    {
        const std::scoped_lock lock(mutex_);
        stopped = std::move(output_);
    }
    // Outside the lock: stopping waits for the output's thread.
}

void ViewerAudio::shutdown() {
    {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
    }
    stop();
}

bool ViewerAudio::playing() const {
    const std::scoped_lock lock(mutex_);
    return output_ != nullptr;
}

} // namespace osplay::engine
