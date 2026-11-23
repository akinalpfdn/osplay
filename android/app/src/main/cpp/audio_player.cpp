#include "audio_player.hpp"

#include <android/log.h>

#include <algorithm>
#include <chrono>

namespace osplay::android {
namespace {

constexpr const char* kLogTag = "OsplayAudio";
/// Bursts of the device kept queued: low latency, with room for one late callback.
constexpr int32_t kBufferBursts = 2;
/// A lost output (headphones pulled) is opened again on the new default after this pause.
constexpr auto kReopenDelay = std::chrono::milliseconds(200);

} // namespace

AudioPlayer::~AudioPlayer() {
    shutdown();
}

void AudioPlayer::attach(osplay_session* session) {
    session_.store(session);
}

bool AudioPlayer::configure(const osplay_audio_config& config) {
    const std::scoped_lock lock(mutex_);
    if (shut_) {
        return false;
    }
    if (!reopener_.joinable()) {
        reopener_ = std::thread([this] { reopen_loop(); });
    }
    close_locked();
    channels_ = config.channels;
    return open_locked();
}

void AudioPlayer::stop() {
    const std::scoped_lock lock(mutex_);
    close_locked();
    channels_ = 0;
}

void AudioPlayer::shutdown() {
    {
        const std::scoped_lock lock(mutex_);
        close_locked();
        channels_ = 0;
        shut_ = true;
    }
    wake_.notify_all();
    if (reopener_.joinable()) {
        reopener_.join();
    }
}

bool AudioPlayer::open_locked() {
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, channels_);
    AAudioStreamBuilder_setSampleRate(builder, OSPLAY_AUDIO_SAMPLE_RATE);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(builder, on_data, this);
    AAudioStreamBuilder_setErrorCallback(builder, on_error, this);
    AAudioStream* stream = nullptr;
    aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Output could not open: %s",
                            AAudio_convertResultToText(opened));
        return false;
    }
    AAudioStream_setBufferSizeInFrames(stream,
                                       AAudioStream_getFramesPerBurst(stream) * kBufferBursts);
    if (const aaudio_result_t started = AAudioStream_requestStart(stream); started != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Output could not start: %s",
                            AAudio_convertResultToText(started));
        AAudioStream_close(stream);
        return false;
    }
    stream_ = stream;
    return true;
}

void AudioPlayer::close_locked() {
    if (stream_ != nullptr) {
        // Waits for a running data callback, so the session is not read after this.
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
}

aaudio_data_callback_result_t AudioPlayer::on_data(AAudioStream* stream, void* user, void* data,
                                                   int32_t frames) {
    auto* self = static_cast<AudioPlayer*>(user);
    const int32_t channels = AAudioStream_getChannelCount(stream);
    auto* out = static_cast<float*>(data);
    osplay_session* session = self->session_.load();
    const bool read =
        session != nullptr &&
        osplay_session_read_audio(session, out, static_cast<size_t>(frames),
                                  static_cast<uint8_t>(channels), nullptr) == OSPLAY_OK;
    if (!read || self->muted_.load()) {
        std::fill(out, out + (static_cast<size_t>(frames) * static_cast<size_t>(channels)), 0.0F);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AudioPlayer::on_error(AAudioStream* /*stream*/, void* user, aaudio_result_t error) {
    // AAudio's own thread: the stream may not be closed here, so another thread reopens it.
    auto* self = static_cast<AudioPlayer*>(user);
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "Output lost: %s",
                        AAudio_convertResultToText(error));
    {
        const std::scoped_lock lock(self->mutex_);
        self->reopen_ = true;
    }
    self->wake_.notify_all();
}

void AudioPlayer::reopen_loop() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return reopen_ || shut_; });
        if (shut_) {
            return;
        }
        reopen_ = false;
        lock.unlock();
        std::this_thread::sleep_for(kReopenDelay);
        lock.lock();
        if (shut_ || channels_ == 0) {
            continue;
        }
        close_locked();
        if (open_locked()) {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "Output reopened on the current device");
        }
    }
}

} // namespace osplay::android
