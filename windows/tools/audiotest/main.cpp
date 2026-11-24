// AUD-03 experiment: records the default speakers through WASAPI loopback while they play
// normally, while they are muted and at 10 % volume, and prints the level of each. It answers
// whether the PC can send its sound to a display while its own speakers stay silent, and whether
// the PC's volume reaches the captured sound. The speakers' mute and volume are restored on exit.
#include <windows.h>

#include <audioclient.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <thread>

namespace {

using Microsoft::WRL::ComPtr;

constexpr auto kPhase = std::chrono::seconds(4);
constexpr auto kPoll = std::chrono::milliseconds(10);
constexpr REFERENCE_TIME kBufferDuration = 2'000'000; // 200 ms in 100 ns units
constexpr float kLowVolume = 0.1F;

struct Level {
    std::uint64_t frames = 0;
    std::uint64_t silent_frames = 0; ///< frames the engine flagged as silence
    double sum_of_squares = 0;
    std::uint64_t samples = 0;

    [[nodiscard]] double decibels() const {
        if (samples == 0 || sum_of_squares <= 0) {
            return -200;
        }
        return 10 * std::log10(sum_of_squares / static_cast<double>(samples));
    }
};

bool is_float(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
        return false;
    }
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&format);
    return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
}

/// Records the speakers' mix for one phase.
std::optional<Level> record(IMMDevice& device) {
    ComPtr<IAudioClient> client;
    if (FAILED(device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(client.GetAddressOf())))) {
        return std::nullopt;
    }
    WAVEFORMATEX* format = nullptr;
    if (FAILED(client->GetMixFormat(&format))) {
        return std::nullopt;
    }
    const bool floats = is_float(*format);
    const WORD channels = format->nChannels;
    const HRESULT initialized =
        client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration,
                           0, format, nullptr);
    CoTaskMemFree(format);
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(initialized) || !floats ||
        FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(capture.GetAddressOf()))) ||
        FAILED(client->Start())) {
        std::cout << std::format("  loopback could not start (float mix format: {})\n",
                                 floats ? "yes" : "no");
        return std::nullopt;
    }
    Level level;
    const auto end = std::chrono::steady_clock::now() + kPhase;
    while (std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(kPoll);
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            level.frames += frames;
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                level.silent_frames += frames;
            } else {
                const auto* samples = reinterpret_cast<const float*>(data);
                const std::uint64_t count = static_cast<std::uint64_t>(frames) * channels;
                for (std::uint64_t i = 0; i < count; ++i) {
                    level.sum_of_squares += static_cast<double>(samples[i]) * samples[i];
                }
                level.samples += count;
            }
            capture->ReleaseBuffer(frames);
        }
    }
    client->Stop();
    return level;
}

void report(const char* phase, const std::optional<Level>& level) {
    if (!level) {
        std::cout << std::format("{:<26} failed\n", phase);
        return;
    }
    std::cout << std::format("{:<26} level {:7.1f} dBFS, {} frames, {} flagged silent\n", phase,
                             level->decibels(), level->frames, level->silent_frames);
}

/// Puts the speakers' mute and volume back as they were, whatever happens.
class VolumeRestorer {
public:
    explicit VolumeRestorer(IAudioEndpointVolume& volume) : volume_(volume) {
        volume_.GetMute(&muted_);
        volume_.GetMasterVolumeLevelScalar(&level_);
    }
    VolumeRestorer(const VolumeRestorer&) = delete;
    VolumeRestorer& operator=(const VolumeRestorer&) = delete;
    VolumeRestorer(VolumeRestorer&&) = delete;
    VolumeRestorer& operator=(VolumeRestorer&&) = delete;
    ~VolumeRestorer() {
        volume_.SetMasterVolumeLevelScalar(level_, nullptr);
        volume_.SetMute(muted_, nullptr);
    }

private:
    IAudioEndpointVolume& volume_;
    BOOL muted_ = FALSE;
    float level_ = 1;
};

int run() {
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> speakers;
    ComPtr<IAudioEndpointVolume> volume;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(devices.GetAddressOf()))) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, speakers.GetAddressOf())) ||
        FAILED(speakers->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(volume.GetAddressOf())))) {
        std::cout << "No default speakers.\n";
        return 1;
    }
    const VolumeRestorer restorer(*volume.Get());
    std::cout << std::format(
        "Keep music playing. Three phases of {} s each; the speakers are put back after.\n\n",
        kPhase.count());

    report("1. Normal", record(*speakers.Get()));

    volume->SetMute(TRUE, nullptr);
    report("2. Speakers muted", record(*speakers.Get()));
    volume->SetMute(FALSE, nullptr);

    volume->SetMasterVolumeLevelScalar(kLowVolume, nullptr);
    report("3. Volume at 10 %", record(*speakers.Get()));

    std::cout << "\nIf 2 is about as loud as 1, the PC can stay silent while it sends its sound.\n"
                 "If 3 is quieter than 1, the PC's volume already reaches the captured sound.\n";
    return 0;
}

} // namespace

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 1;
    }
    int code = 1;
    try {
        code = run();
    } catch (const std::exception& error) {
        // C stdio cannot throw, unlike iostreams.
        static_cast<void>(std::fputs("error: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
    } catch (...) {
        static_cast<void>(std::fputs("error: unknown exception\n", stderr));
    }
    CoUninitialize();
    return code;
}
