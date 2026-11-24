#include "audio/wasapi_system_sound.hpp"

#include <osplay/osplay.h>

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace osplay::engine {
namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kBufferDuration = 2'000'000; // 200 ms in 100 ns units
/// The loopback stream is drained this often (loopback has no reliable event before Windows 10).
constexpr DWORD kPollMilliseconds = 5;
/// A lost device (unplugged, default changed) is opened again after this pause.
constexpr DWORD kReopenDelayMilliseconds = 300;
constexpr std::uint16_t kStereo = 2;
constexpr std::uint32_t kBytesPerSample = sizeof(float);
constexpr std::int64_t kHundredNanosecondsPerSecond = 10'000'000;
constexpr std::int64_t kHundredNanosecondsPerMicrosecond = 10;

/// Marks Osplay's own mute changes, so the endpoint callback can tell them from the user's.
constexpr GUID kOsplayContext = {.Data1 = 0x6f73706c,
                                 .Data2 = 0x6179,
                                 .Data3 = 0x4175,
                                 .Data4 = {0x80, 0x03, 0x6d, 0x75, 0x74, 0x65, 0x73, 0x21}};

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
    return std::format("System sound: {} failed ({:#010x})", step,
                       static_cast<std::uint32_t>(result));
}

/// %LOCALAPPDATA%\Osplay\speakers-muted: the endpoint ID of speakers Osplay muted.
std::optional<std::filesystem::path> marker_path() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        CoTaskMemFree(folder);
        return std::nullopt;
    }
    const std::filesystem::path path(folder);
    CoTaskMemFree(folder);
    return path / L"Osplay" / L"speakers-muted";
}

void write_marker(const std::wstring& device_id) {
    const auto path = marker_path();
    if (!path) {
        return;
    }
    std::error_code ignored;
    std::filesystem::create_directories(path->parent_path(), ignored);
    std::wofstream file(*path, std::ios::trunc);
    file << device_id;
}

void remove_marker() noexcept {
    try {
        if (const auto path = marker_path()) {
            std::error_code ignored;
            std::filesystem::remove(*path, ignored);
        }
    } catch (...) { // NOLINT(bugprone-empty-catch): only out of memory; the marker stays, harmless
    }
}

/// Counts age with the performance counter, in the 100 ns units WASAPI uses for positions.
std::int64_t now_in_hundred_nanoseconds() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    const std::int64_t per_second = frequency.QuadPart > 0 ? frequency.QuadPart : 1;
    return (now.QuadPart / per_second * kHundredNanosecondsPerSecond) +
           (now.QuadPart % per_second * kHundredNanosecondsPerSecond / per_second);
}

/// The capture time on the core clock: the age of the packet keeps its meaning across clocks.
std::uint64_t core_time(UINT64 qpc_position) {
    const std::int64_t age = now_in_hundred_nanoseconds() - static_cast<std::int64_t>(qpc_position);
    const auto age_us = static_cast<std::uint64_t>(std::max<std::int64_t>(age, 0) /
                                                   kHundredNanosecondsPerMicrosecond);
    const std::uint64_t now = osplay_clock_microseconds();
    return now > age_us ? now - age_us : 0;
}

/// What the endpoint and the device list report, read by the recorder's thread.
struct Signals {
    std::atomic<bool> user_unmuted{false};
    std::atomic<bool> default_changed{false};
    HANDLE wake = nullptr; // auto-reset; owned by the recorder
};

/// The endpoint's volume callback and the device list's callback in one COM object.
class EndpointEvents final : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    explicit EndpointEvents(Signals& signals) : signals_(&signals) {}
    EndpointEvents(const EndpointEvents&) = delete;
    EndpointEvents& operator=(const EndpointEvents&) = delete;
    EndpointEvents(EndpointEvents&&) = delete;
    EndpointEvents& operator=(EndpointEvents&&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualIID(id, __uuidof(IUnknown)) != FALSE ||
            IsEqualIID(id, __uuidof(IAudioEndpointVolumeCallback)) != FALSE) {
            *object = static_cast<IAudioEndpointVolumeCallback*>(this);
        } else if (IsEqualIID(id, __uuidof(IMMNotificationClient)) != FALSE) {
            *object = static_cast<IMMNotificationClient*>(this);
        } else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0) {
            delete this; // NOLINT(cppcoreguidelines-owning-memory): COM reference counting
        }
        return left;
    }

    /// The user unmuted the speakers (the mute key): the recorder mutes them again.
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
        if (data != nullptr && data->bMuted == FALSE &&
            IsEqualGUID(data->guidEventContext, kOsplayContext) == FALSE) {
            signals_->user_unmuted = true;
            SetEvent(signals_->wake);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                     LPCWSTR /*id*/) override {
        if (flow == eRender && role == eConsole) {
            signals_->default_changed = true;
            SetEvent(signals_->wake);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR /*id*/, DWORD /*state*/) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR /*id*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR /*id*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR /*id*/,
                                                     const PROPERTYKEY /*key*/) override {
        return S_OK;
    }

private:
    ~EndpointEvents() = default; // released through Release only
    Signals* signals_;           // outlives every callback: unregistered before the recorder ends
    std::atomic<ULONG> references_{1};
};

bool is_float(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&format)->SubFormat,
                       KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
}

/// A loopback recording of one device, in 48 kHz stereo or in the device's mix format with a
/// resampler.
struct Loopback {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WORD channels = kStereo;
    std::optional<StereoResampler> resampler;
};

/// Windows converts to 48 kHz stereo where it can; otherwise the mix format is converted here.
HRESULT open_loopback(IMMDevice& device, Loopback& loopback) {
    const WAVEFORMATEXTENSIBLE wanted{
        .Format = {.wFormatTag = WAVE_FORMAT_EXTENSIBLE,
                   .nChannels = kStereo,
                   .nSamplesPerSec = OSPLAY_AUDIO_SAMPLE_RATE,
                   .nAvgBytesPerSec = OSPLAY_AUDIO_SAMPLE_RATE * kStereo * kBytesPerSample,
                   .nBlockAlign = static_cast<WORD>(kStereo * kBytesPerSample),
                   .wBitsPerSample = 8 * kBytesPerSample,
                   .cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)},
        .Samples = {.wValidBitsPerSample = 8 * kBytesPerSample},
        .dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
        .SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT};
    HRESULT result = device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(loopback.client.GetAddressOf()));
    if (SUCCEEDED(result)) {
        result = loopback.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                             AUDCLNT_STREAMFLAGS_LOOPBACK |
                                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                             kBufferDuration, 0, &wanted.Format, nullptr);
    }
    if (FAILED(result)) {
        loopback.client.Reset();
        result = device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(loopback.client.GetAddressOf()));
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(result)) {
            result = loopback.client->GetMixFormat(&mix);
        }
        if (SUCCEEDED(result)) {
            result = is_float(*mix) ? S_OK : AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        if (SUCCEEDED(result)) {
            loopback.channels = mix->nChannels;
            loopback.resampler.emplace(mix->nSamplesPerSec, mix->nChannels);
            result =
                loopback.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                            kBufferDuration, 0, mix, nullptr);
        }
        CoTaskMemFree(mix);
    }
    if (SUCCEEDED(result)) {
        result = loopback.client->GetService(IID_PPV_ARGS(&loopback.capture));
    }
    return result;
}

/// Mutes the speakers for as long as it lives, then gives them back the user's own mute. The
/// marker file names them meanwhile, so a crash leaves nothing muted after the next start.
class SpeakerMute {
public:
    SpeakerMute(IMMDevice& device, IAudioEndpointVolume& volume) : volume_(&volume) {
        volume_->GetMute(&user_muted_);
        if (user_muted_ == FALSE) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(device.GetId(&id))) {
                write_marker(id);
            }
            CoTaskMemFree(id);
        }
        mute_again();
    }
    ~SpeakerMute() {
        volume_->SetMute(user_muted_, &kOsplayContext);
        remove_marker();
    }
    SpeakerMute(const SpeakerMute&) = delete;
    SpeakerMute& operator=(const SpeakerMute&) = delete;
    SpeakerMute(SpeakerMute&&) = delete;
    SpeakerMute& operator=(SpeakerMute&&) = delete;

    void mute_again() const { volume_->SetMute(TRUE, &kOsplayContext); }

private:
    IAudioEndpointVolume* volume_; // outlives this
    BOOL user_muted_ = FALSE;
};

/// Registers the endpoint and device-list callbacks for as long as it lives.
class EventRegistration {
public:
    EventRegistration(IMMDeviceEnumerator& devices, IAudioEndpointVolume& volume, Signals& signals)
        : devices_(&devices), volume_(&volume),
          events_(new EndpointEvents(signals)), // NOLINT(cppcoreguidelines-owning-memory): COM
          watches_volume_(SUCCEEDED(volume.RegisterControlChangeNotify(events_))),
          watches_devices_(SUCCEEDED(devices.RegisterEndpointNotificationCallback(events_))) {}
    ~EventRegistration() {
        if (watches_devices_) {
            devices_->UnregisterEndpointNotificationCallback(events_);
        }
        if (watches_volume_) {
            volume_->UnregisterControlChangeNotify(events_);
        }
        events_->Release(); // no callback runs once unregistered
    }
    EventRegistration(const EventRegistration&) = delete;
    EventRegistration& operator=(const EventRegistration&) = delete;
    EventRegistration(EventRegistration&&) = delete;
    EventRegistration& operator=(EventRegistration&&) = delete;

private:
    IMMDeviceEnumerator* devices_; // outlives this
    IAudioEndpointVolume* volume_; // outlives this
    EndpointEvents* events_;       // one reference, released here
    bool watches_volume_;
    bool watches_devices_;
};

} // namespace

/// One recording: a thread that owns every COM object, from start until stop.
struct WasapiSystemSound::Recorder {
    Recorder(SoundSink& sink, AudioLog log) : sink(&sink), log(std::move(log)) {
        signals.wake = wake.get();
    }
    ~Recorder() {
        SetEvent(stop.get());
        if (thread.joinable()) {
            thread.join();
        }
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    /// Starts the thread; true once the first device records.
    bool start() {
        std::promise<bool> opened;
        auto first = opened.get_future();
        thread = std::thread([this, opened = std::move(opened)]() mutable { run(opened); });
        return first.get();
    }

    void run(std::promise<bool>& opened);
    /// Records one device until stopped (true) or the device is lost or changed (false). The
    /// first call answers `opened` and clears it.
    bool record(IMMDeviceEnumerator& devices, std::promise<bool>*& opened);
    /// Drains the recording until stopped (true) or the device is lost or changed (false).
    bool listen(Loopback& loopback, const SpeakerMute& speakers);
    bool drain(IAudioCaptureClient& capture, WORD channels,
               std::optional<StereoResampler>& resampler);
    bool failed(const char* step, HRESULT result);

    SoundSink* sink; // never null; outlives the recording
    AudioLog log;
    Signals signals;
    Event stop{true};
    Event wake{false};
    bool warned = false;
    bool ever_started = false;
    std::vector<float> zeros;     // the recorder's thread only
    std::vector<float> converted; // the recorder's thread only
    std::thread thread;
};

void WasapiSystemSound::Recorder::run(std::promise<bool>& opened) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_index = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    ComPtr<IMMDeviceEnumerator> devices;
    const HRESULT listed =
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices));
    if (FAILED(listed)) {
        failed("the device list", listed);
        opened.set_value(false);
    } else {
        std::promise<bool>* first = &opened;
        // Records until stopped; a device that is lost, or no longer the default, is left and the
        // new default recorded after a pause.
        while (!record(*devices.Get(), first)) {
            if (!ever_started) {
                break; // the first device could not record: start() reports it
            }
            if (WaitForSingleObject(stop.get(), kReopenDelayMilliseconds) != WAIT_TIMEOUT) {
                break;
            }
        }
    }
    if (task != nullptr) {
        AvRevertMmThreadCharacteristics(task);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

bool WasapiSystemSound::Recorder::record(IMMDeviceEnumerator& devices,
                                         std::promise<bool>*& opened) {
    const auto answer = [&](bool started) {
        if (opened != nullptr) {
            opened->set_value(started);
            opened = nullptr;
        }
    };
    const auto give_up = [&](const char* step, HRESULT result) {
        const bool stopped = failed(step, result);
        answer(false);
        return stopped;
    };
    signals.default_changed = false;
    ComPtr<IMMDevice> device;
    HRESULT result = devices.GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result)) {
        return give_up("finding the speakers", result);
    }
    ComPtr<IAudioEndpointVolume> volume;
    result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(volume.GetAddressOf()));
    if (FAILED(result)) {
        return give_up("opening the speakers", result);
    }

    Loopback loopback;
    result = open_loopback(*device.Get(), loopback);
    if (FAILED(result)) {
        return give_up("setting up the recording", result);
    }
    const SpeakerMute speakers(*device.Get(), *volume.Get());
    const EventRegistration events(devices, *volume.Get(), signals);
    result = loopback.client->Start();
    ever_started = ever_started || SUCCEEDED(result);
    answer(SUCCEEDED(result));
    if (FAILED(result)) {
        return failed("starting the recording", result);
    }
    log(OSPLAY_LOG_INFO, loopback.resampler
                             ? std::format("System sound: recording {} channels, converted here",
                                           static_cast<unsigned>(loopback.channels))
                             : std::string("System sound: recording"));
    warned = false;
    const bool stopped = listen(loopback, speakers);
    static_cast<void>(loopback.client->Stop()); // the client is released next either way
    return stopped;
}

bool WasapiSystemSound::Recorder::listen(Loopback& loopback, const SpeakerMute& speakers) {
    const std::array<HANDLE, 2> waits{stop.get(), wake.get()};
    for (;;) {
        const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(),
                                                  FALSE, kPollMilliseconds);
        if (woke == WAIT_OBJECT_0) {
            return true;
        }
        if (signals.user_unmuted.exchange(false)) {
            speakers.mute_again();
            sink->on_mute_key();
        }
        if (signals.default_changed ||
            !drain(*loopback.capture.Get(), loopback.channels, loopback.resampler)) {
            return false; // the new default is recorded next; or the device was unplugged
        }
    }
}

bool WasapiSystemSound::Recorder::drain(IAudioCaptureClient& capture, WORD channels,
                                        std::optional<StereoResampler>& resampler) {
    UINT32 packet = 0;
    HRESULT result = capture.GetNextPacketSize(&packet);
    while (SUCCEEDED(result) && packet > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 qpc_position = 0;
        result = capture.GetBuffer(&data, &frames, &flags, nullptr, &qpc_position);
        if (FAILED(result)) {
            break;
        }
        const std::size_t count = static_cast<std::size_t>(frames) * channels;
        std::span<const float> input(reinterpret_cast<const float*>(data), count);
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
            zeros.assign(count, 0.0F); // the buffer's contents are undefined
            input = zeros;
        }
        if (resampler) {
            converted.clear();
            resampler->convert(input, converted);
            input = converted;
        }
        if (!input.empty()) {
            sink->on_sound(input, core_time(qpc_position));
        }
        result = capture.ReleaseBuffer(frames);
        if (SUCCEEDED(result)) {
            result = capture.GetNextPacketSize(&packet);
        }
    }
    if (FAILED(result)) {
        return failed("the recording", result); // AUDCLNT_E_DEVICE_INVALIDATED: unplugged
    }
    return true;
}

bool WasapiSystemSound::Recorder::failed(const char* step, HRESULT result) {
    if (!warned) {
        warned = true;
        log(OSPLAY_LOG_WARNING, describe(step, result));
    }
    return false;
}

WasapiSystemSound::WasapiSystemSound(AudioLog log) : log_(std::move(log)) {}

WasapiSystemSound::~WasapiSystemSound() {
    stop();
}

bool WasapiSystemSound::start(SoundSink& sink) {
    stop();
    recorder_ = std::make_unique<Recorder>(sink, log_);
    if (!recorder_->start()) {
        recorder_.reset();
        return false;
    }
    return true;
}

void WasapiSystemSound::stop() {
    recorder_.reset(); // joins the thread, which gives the speakers back their mute
}

void restore_speakers_after_crash() {
    const auto path = marker_path();
    if (!path) {
        return;
    }
    std::wstring device_id;
    {
        std::wifstream file(*path);
        if (!file || !std::getline(file, device_id) || device_id.empty()) {
            return;
        }
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&devices))) &&
        SUCCEEDED(devices->GetDevice(device_id.c_str(), &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(volume.GetAddressOf())))) {
        volume->SetMute(FALSE, &kOsplayContext);
    }
    remove_marker();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

} // namespace osplay::engine
