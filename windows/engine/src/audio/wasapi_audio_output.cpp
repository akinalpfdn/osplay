#include "audio/wasapi_audio_output.hpp"

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <thread>
#include <utility>

namespace osplay::engine {
namespace {

using Microsoft::WRL::ComPtr;

/// The device buffer: one of the host's 10 ms frames, the shared engine's usual period.
constexpr REFERENCE_TIME kBufferDuration = 100'000; // 100 ns units
/// Without an event for this long the device is checked anyway (it may have gone).
constexpr DWORD kWaitMilliseconds = 200;
/// A lost or missing device is tried again after this pause.
constexpr DWORD kReopenDelayMilliseconds = 500;
constexpr std::uint32_t kBytesPerSample = sizeof(float);

/// An event handle closed on destruction.
class Event {
public:
    explicit Event(bool manual_reset)
        : handle_(CreateEventW(nullptr, manual_reset ? TRUE : FALSE, FALSE, nullptr)) {}
    ~Event() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    [[nodiscard]] HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

std::string describe(const char* step, HRESULT result) {
    return std::format("Audio output: {} failed ({:#010x})", step,
                       static_cast<std::uint32_t>(result));
}

class WasapiAudioOutput final : public AudioOutput {
public:
    WasapiAudioOutput(const osplay_audio_config& config, AudioSource& source, AudioLog log)
        : channels_(config.channels), source_(&source), log_(std::move(log)), stop_(true),
          thread_([this] { run(); }) {}

    ~WasapiAudioOutput() override {
        SetEvent(stop_.get());
        thread_.join();
    }
    WasapiAudioOutput(const WasapiAudioOutput&) = delete;
    WasapiAudioOutput& operator=(const WasapiAudioOutput&) = delete;
    WasapiAudioOutput(WasapiAudioOutput&&) = delete;
    WasapiAudioOutput& operator=(WasapiAudioOutput&&) = delete;

private:
    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD task_index = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
        // Plays until stopped; a device that is lost or missing is tried again after a pause.
        while (!play() &&
               WaitForSingleObject(stop_.get(), kReopenDelayMilliseconds) == WAIT_TIMEOUT) {
        }
        if (task != nullptr) {
            AvRevertMmThreadCharacteristics(task);
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    }

    /// True when stopped on purpose, false when the device failed or went away.
    bool play() {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (FAILED(result)) {
            return failed("the device list", result);
        }
        ComPtr<IMMDevice> device;
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(result)) {
            return failed("finding the speakers", result);
        }
        ComPtr<IAudioClient> client;
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(result)) {
            return failed("opening the speakers", result);
        }
        const WAVEFORMATEXTENSIBLE format{
            .Format = {.wFormatTag = WAVE_FORMAT_EXTENSIBLE,
                       .nChannels = channels_,
                       .nSamplesPerSec = OSPLAY_AUDIO_SAMPLE_RATE,
                       .nAvgBytesPerSec = OSPLAY_AUDIO_SAMPLE_RATE * channels_ * kBytesPerSample,
                       .nBlockAlign = static_cast<WORD>(channels_ * kBytesPerSample),
                       .wBitsPerSample = 8 * kBytesPerSample,
                       .cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)},
            .Samples = {.wValidBitsPerSample = 8 * kBytesPerSample},
            .dwChannelMask = channels_ == 1 ? DWORD{SPEAKER_FRONT_CENTER}
                                            : DWORD{SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT},
            .SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT};
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    kBufferDuration, 0, &format.Format, nullptr);
        if (FAILED(result)) {
            return failed("setting the format", result);
        }
        const Event ready(false);
        UINT32 buffer_frames = 0;
        ComPtr<IAudioRenderClient> render;
        result = client->SetEventHandle(ready.get());
        if (SUCCEEDED(result)) {
            result = client->GetBufferSize(&buffer_frames);
        }
        if (SUCCEEDED(result)) {
            result = client->GetService(IID_PPV_ARGS(&render));
        }
        if (SUCCEEDED(result)) {
            result = client->Start();
        }
        if (FAILED(result)) {
            return failed("starting", result);
        }
        log_(OSPLAY_LOG_INFO, std::format("Audio output: {} channels, {} frame buffer",
                                          static_cast<unsigned>(channels_), buffer_frames));
        warned_ = false;
        const bool stopped =
            render_until_stopped(*client.Get(), *render.Get(), ready.get(), buffer_frames);
        static_cast<void>(client->Stop()); // the client is released next either way
        return stopped;
    }

    bool render_until_stopped(IAudioClient& client, IAudioRenderClient& render, HANDLE ready,
                              UINT32 buffer_frames) {
        const std::array<HANDLE, 2> waits{stop_.get(), ready};
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(waits.size()),
                                                      waits.data(), FALSE, kWaitMilliseconds);
            if (woke == WAIT_OBJECT_0) {
                return true;
            }
            UINT32 padding = 0;
            HRESULT result = client.GetCurrentPadding(&padding);
            if (FAILED(result)) {
                return failed("the device", result); // AUDCLNT_E_DEVICE_INVALIDATED: unplugged
            }
            const UINT32 frames = buffer_frames - padding;
            if (frames == 0) {
                continue;
            }
            BYTE* data = nullptr;
            result = render.GetBuffer(frames, &data);
            if (FAILED(result)) {
                return failed("the device buffer", result);
            }
            source_->read(std::span(reinterpret_cast<float*>(data),
                                    static_cast<std::size_t>(frames) * channels_),
                          static_cast<std::uint8_t>(channels_));
            result = render.ReleaseBuffer(frames, 0);
            if (FAILED(result)) {
                return failed("the device buffer", result);
            }
        }
    }

    /// Logs the first failure of a run of them: a PC without speakers retries twice a second.
    bool failed(const char* step, HRESULT result) {
        if (!warned_) {
            warned_ = true;
            log_(OSPLAY_LOG_WARNING, describe(step, result));
        }
        return false;
    }

    WORD channels_;
    AudioSource* source_; // never null; outlives the output
    AudioLog log_;
    bool warned_ = false; // the output's thread only
    Event stop_;
    std::thread thread_; // last: starts once everything above is ready
};

} // namespace

WasapiAudioOutputs::WasapiAudioOutputs(AudioLog log) : log_(std::move(log)) {}

std::unique_ptr<AudioOutput> WasapiAudioOutputs::open(const osplay_audio_config& config,
                                                      AudioSource& source) {
    if (config.channels == 0 || config.channels > 2 ||
        config.sample_rate_hz != OSPLAY_AUDIO_SAMPLE_RATE) {
        return nullptr;
    }
    return std::make_unique<WasapiAudioOutput>(config, source, log_);
}

} // namespace osplay::engine
