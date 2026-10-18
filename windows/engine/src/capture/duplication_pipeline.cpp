#include "capture/duplication_pipeline.hpp"

#include "capture/dxgi_output.hpp"
#include "display/vdd_monitor.hpp"
#include "encode/mf_h264_encoder.hpp"
#include "encode/nv12_converter.hpp"
#include "engine_tunables.hpp"

#include <osplay/osplay.h>

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <future>
#include <utility>
#include <vector>

namespace osplay::engine {

using Microsoft::WRL::ComPtr;

namespace {

/// NV12 textures in rotation: the encoder may still read one while the next is converted.
constexpr std::size_t kNv12Ring = MfH264Encoder::kMaxPendingInputs + 4;

/// COM and Media Foundation for the calling thread, for the lifetime of the object.
class MediaThreadScope {
public:
    MediaThreadScope()
        : com_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))),
          media_foundation_(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {}
    ~MediaThreadScope() {
        if (media_foundation_) {
            static_cast<void>(MFShutdown());
        }
        if (com_) {
            CoUninitialize();
        }
    }
    MediaThreadScope(const MediaThreadScope&) = delete;
    MediaThreadScope& operator=(const MediaThreadScope&) = delete;
    MediaThreadScope(MediaThreadScope&&) = delete;
    MediaThreadScope& operator=(MediaThreadScope&&) = delete;

    [[nodiscard]] bool ready() const { return com_ && media_foundation_; }

private:
    bool com_;
    bool media_foundation_;
};

std::int64_t performance_counter() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

} // namespace

/// State owned by the pipeline thread.
struct DuplicationPipeline::Worker {
    DuplicationPipeline* owner = nullptr; // outlives the thread
    PipelineSink* sink = nullptr;         // outlives the pipeline
    StreamSettings settings;
    std::wstring gdi_name;
    DuplicationTarget target;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> desktop;
    std::array<ComPtr<ID3D11Texture2D>, kNv12Ring> nv12{};
    std::size_t next_slot = 0;
    std::unique_ptr<Nv12Converter> converter;
    std::unique_ptr<MfH264Encoder> encoder;
    /// When the last frame went to the encoder; output is polled closely for a while after.
    std::int64_t last_submit = 0;
    std::int64_t counter_frequency = 1;
    ULONGLONG next_reconnect = 0;
    std::vector<std::uint8_t> shape_buffer;
    std::uint32_t shape_id = 0;
    POINT hotspot{};
    bool pointer_visible = false;
    POINT pointer_position{};
    UINT desktop_width = 0;
    UINT desktop_height = 0;

    std::optional<PipelineError> initialize();
    void run();

private:
    /// One loop iteration; false once the pipeline has failed (already reported).
    bool step();
    bool wait_for_duplication();
    bool encode_if_needed(bool new_image, std::int64_t captured_at);
    bool poll_encoder();
    bool reconnect();
    bool capture_frame(IDXGIResource* resource);
    void update_pointer(const DXGI_OUTDUPL_FRAME_INFO& info);
    bool encode(bool keyframe, std::int64_t captured_at);
    [[nodiscard]] std::uint64_t core_time(std::int64_t counter) const;
    void fail(PipelineError error) const { sink->on_pipeline_failed(error); }
};

std::optional<PipelineError> DuplicationPipeline::Worker::initialize() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    counter_frequency = frequency.QuadPart > 0 ? frequency.QuadPart : 1;

    auto name = vdd_gdi_device_name();
    if (!name) {
        return PipelineError::MonitorNotFound;
    }
    gdi_name = name.value();
    auto opened = open_duplication_target(gdi_name);
    if (!opened) {
        return opened.error();
    }
    target = std::move(opened.value());

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = settings.width;
    desc.Height = settings.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    for (auto& texture : nv12) {
        if (FAILED(target.device->CreateTexture2D(&desc, nullptr, &texture))) {
            return PipelineError::EncoderUnavailable;
        }
    }
    converter = std::make_unique<Nv12Converter>(target.device, target.context, settings.width,
                                                settings.height, settings.refresh_millihertz);
    auto created = MfH264Encoder::create(
        target.device.Get(), settings,
        [this](std::span<const std::uint8_t> annexb, bool keyframe, std::int64_t sample_time) {
            // Sample times carry the core clock in 100 ns units (see encode()).
            sink->on_access_unit(annexb, keyframe, static_cast<std::uint64_t>(sample_time / 10));
        });
    if (!created) {
        return created.error();
    }
    encoder = std::move(created.value());
    if (!reconnect()) {
        return PipelineError::CaptureFailed;
    }
    return std::nullopt;
}

void DuplicationPipeline::Worker::run() {
    while (!owner->stopping_ && step()) {
    }
}

bool DuplicationPipeline::Worker::step() {
    if (duplication == nullptr && !wait_for_duplication()) {
        return poll_encoder();
    }
    // Short waits while the encoder has frames to return, so output is not delayed.
    const bool encoder_busy =
        performance_counter() - last_submit < counter_frequency * kEncoderBusyMilliseconds / 1000;
    const UINT timeout = encoder_busy ? kEncoderPollMilliseconds : kIdleAcquireMilliseconds;
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const HRESULT acquired = duplication->AcquireNextFrame(timeout, &info, &resource);
    if (acquired == DXGI_ERROR_DEVICE_REMOVED || acquired == DXGI_ERROR_DEVICE_RESET) {
        fail(PipelineError::CaptureFailed);
        return false;
    }
    if (FAILED(acquired) && acquired != DXGI_ERROR_WAIT_TIMEOUT) {
        // Access lost: mode change, secure desktop (UAC, lock screen) or a full-screen app.
        duplication.Reset();
        return true;
    }
    bool new_image = false;
    std::int64_t captured_at = performance_counter();
    if (SUCCEEDED(acquired)) {
        if (info.LastPresentTime.QuadPart != 0) {
            new_image = capture_frame(resource.Get());
            captured_at = info.LastPresentTime.QuadPart;
        }
        update_pointer(info);
        static_cast<void>(duplication->ReleaseFrame()); // a lost duplication shows up next time
    }
    return encode_if_needed(new_image, captured_at) && poll_encoder();
}

bool DuplicationPipeline::Worker::wait_for_duplication() {
    if (GetTickCount64() >= next_reconnect && !reconnect()) {
        next_reconnect = GetTickCount64() + kDuplicationRetryMilliseconds;
    }
    if (duplication == nullptr) {
        Sleep(kDuplicationIdleMilliseconds);
        return false;
    }
    return true;
}

bool DuplicationPipeline::Worker::encode_if_needed(bool new_image, std::int64_t captured_at) {
    const bool keyframe = owner->keyframe_requested_.exchange(false);
    if (!new_image && (!keyframe || desktop == nullptr)) {
        if (keyframe) {
            owner->keyframe_requested_ = true; // nothing captured yet: the first frame will be one
        }
        return true;
    }
    if (!encode(keyframe, captured_at)) {
        fail(PipelineError::EncoderFailed);
        return false;
    }
    return true;
}

bool DuplicationPipeline::Worker::poll_encoder() {
    if (!encoder->poll()) {
        fail(PipelineError::EncoderFailed);
        return false;
    }
    return true;
}

bool DuplicationPipeline::Worker::reconnect() {
    duplication.Reset();
    const HRESULT duplicated = target.output->DuplicateOutput(target.device.Get(), &duplication);
    if (SUCCEEDED(duplicated)) {
        return true;
    }
    duplication.Reset();
    // Access denied while the secure desktop is up, or busy: retry later with the same output.
    // Anything else may mean a mode change replaced it: look it up again for the next attempt.
    if (duplicated != E_ACCESSDENIED && duplicated != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE &&
        duplicated != DXGI_ERROR_SESSION_DISCONNECTED) {
        static_cast<void>(refresh_duplication_output(target, gdi_name));
    }
    return false;
}

bool DuplicationPipeline::Worker::capture_frame(IDXGIResource* resource) {
    ComPtr<ID3D11Texture2D> frame;
    if (resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(&frame)))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    frame->GetDesc(&desc);
    if (desktop == nullptr || desc.Width != desktop_width || desc.Height != desktop_height) {
        D3D11_TEXTURE2D_DESC copy = desc;
        copy.MipLevels = 1;
        copy.ArraySize = 1;
        copy.SampleDesc.Count = 1;
        copy.SampleDesc.Quality = 0;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        copy.CPUAccessFlags = 0;
        copy.MiscFlags = 0;
        desktop.Reset();
        if (FAILED(target.device->CreateTexture2D(&copy, nullptr, &desktop))) {
            return false;
        }
        desktop_width = desc.Width;
        desktop_height = desc.Height;
    }
    // Copied so the frame can be released at once and re-encoded for keyframe requests.
    target.context->CopyResource(desktop.Get(), frame.Get());
    return true;
}

void DuplicationPipeline::Worker::update_pointer(const DXGI_OUTDUPL_FRAME_INFO& info) {
    bool changed = false;
    if (info.PointerShapeBufferSize > 0) {
        shape_buffer.resize(info.PointerShapeBufferSize);
        UINT required = 0;
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
        if (SUCCEEDED(duplication->GetFramePointerShape(info.PointerShapeBufferSize,
                                                        shape_buffer.data(), &required, &shape)) &&
            (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ||
             shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ||
             shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR)) {
            const auto image = convert_pointer_shape(
                {.type = static_cast<PointerShapeType>(shape.Type),
                 .width = shape.Width,
                 .height = shape.Height,
                 .pitch = shape.Pitch,
                 .hotspot_x = static_cast<std::uint32_t>(std::max(shape.HotSpot.x, LONG{0})),
                 .hotspot_y = static_cast<std::uint32_t>(std::max(shape.HotSpot.y, LONG{0}))},
                std::span(shape_buffer)
                    .first(std::min<std::size_t>(required, shape_buffer.size())));
            if (image) {
                ++shape_id;
                hotspot = POINT{.x = image->hotspot_x, .y = image->hotspot_y};
                sink->on_cursor_shape(shape_id, *image);
                changed = true;
            }
        }
    }
    if (info.LastMouseUpdateTime.QuadPart != 0) {
        pointer_visible = info.PointerPosition.Visible != FALSE;
        pointer_position = info.PointerPosition.Position;
        changed = true;
    }
    if (!changed || desktop_width == 0 || desktop_height == 0) {
        return;
    }
    // Desktop pixels → stream pixels (equal unless the monitor's mode was changed by hand).
    const auto scale = [](LONG value, UINT from, std::uint32_t to) {
        return static_cast<std::int32_t>(static_cast<long long>(value) * to / from);
    };
    sink->on_cursor_position(pointer_visible, shape_id,
                             scale(pointer_position.x + hotspot.x, desktop_width, settings.width),
                             scale(pointer_position.y + hotspot.y, desktop_height, settings.height),
                             core_time(info.LastMouseUpdateTime.QuadPart != 0
                                           ? info.LastMouseUpdateTime.QuadPart
                                           : performance_counter()));
}

bool DuplicationPipeline::Worker::encode(bool keyframe, std::int64_t captured_at) {
    ID3D11Texture2D* slot = nv12.at(next_slot).Get();
    next_slot = (next_slot + 1) % nv12.size();
    if (!converter->convert(desktop.Get(), slot)) {
        return false;
    }
    last_submit = performance_counter();
    encoder->submit(slot, static_cast<std::int64_t>(core_time(captured_at)) * 10, keyframe);
    return true;
}

std::uint64_t DuplicationPipeline::Worker::core_time(std::int64_t counter) const {
    // The age of the event keeps its meaning across clocks.
    const std::int64_t age_counts = std::max<std::int64_t>(performance_counter() - counter, 0);
    const auto age_us = static_cast<std::uint64_t>(age_counts * 1'000'000 / counter_frequency);
    const std::uint64_t now = osplay_clock_microseconds();
    return now > age_us ? now - age_us : 0;
}

win::Expected<std::unique_ptr<StreamPipeline>, PipelineError>
DuplicationPipeline::start(const StreamSettings& settings, PipelineSink& sink) {
    using Result = win::Expected<std::unique_ptr<StreamPipeline>, PipelineError>;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the constructor is private
    std::unique_ptr<DuplicationPipeline> pipeline(new DuplicationPipeline());
    std::promise<std::optional<PipelineError>> started;
    auto result = started.get_future();
    DuplicationPipeline* owner = pipeline.get();
    pipeline->thread_ = std::thread([owner, settings, &sink, &started] {
        const MediaThreadScope scope;
        Worker worker;
        worker.owner = owner;
        worker.sink = &sink;
        worker.settings = settings;
        const auto failure =
            scope.ready() ? worker.initialize() : PipelineError::EncoderUnavailable;
        if (!failure) {
            owner->rect_ = worker.target.rect;
        }
        started.set_value(failure);
        if (!failure) {
            worker.run();
        }
    });
    if (const auto failure = result.get()) {
        pipeline->thread_.join();
        return Result::failure(*failure);
    }
    return {std::unique_ptr<StreamPipeline>(std::move(pipeline))};
}

DuplicationPipeline::~DuplicationPipeline() {
    stopping_ = true;
    try {
        if (thread_.joinable()) {
            thread_.join();
        }
    } catch (...) {
        // Joining fails only on a broken thread state; the pipeline cannot stop safely then.
        std::terminate();
    }
}

} // namespace osplay::engine
