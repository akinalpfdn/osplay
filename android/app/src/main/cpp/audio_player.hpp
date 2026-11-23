// Plays the host's sound on the Android device's default output through AAudio. The stream's data
// callback pulls PCM from the core's jitter buffer (osplay_session_read_audio) on AAudio's own
// thread; nothing on the sound path goes through Kotlin.
#pragma once

#include <osplay/osplay.h>

#include <aaudio/AAudio.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace osplay::android {

class AudioPlayer {
public:
    AudioPlayer() = default;
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;
    ~AudioPlayer();

    /// The session the callback reads from; set once it exists, before any sound can come.
    void attach(osplay_session* session);

    /// Opens (or reopens) the output for the host's format; false when no output could be opened.
    bool configure(const osplay_audio_config& config);

    /// Closes the output; the host sends no sound now.
    void stop();

    /// Closes the output for good and ends the reopening thread; call before the session goes.
    void shutdown();

private:
    static aaudio_data_callback_result_t on_data(AAudioStream* stream, void* user, void* data,
                                                 int32_t frames);
    static void on_error(AAudioStream* stream, void* user, aaudio_result_t error);

    bool open_locked();
    void close_locked();
    void reopen_loop();

    std::atomic<osplay_session*> session_{nullptr};
    std::mutex mutex_;
    std::condition_variable wake_;
    AAudioStream* stream_ = nullptr;
    int32_t channels_ = 0; ///< 0 while the host sends no sound
    bool reopen_ = false;
    bool shut_ = false;
    std::thread reopener_;
};

} // namespace osplay::android
