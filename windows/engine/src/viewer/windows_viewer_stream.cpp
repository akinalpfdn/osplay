#include "viewer/windows_viewer_stream.hpp"

#include "capture/dxgi_output.hpp"
#include "decode/mf_h264_decoder.hpp"
#include "engine_tunables.hpp"
#include "media/media_thread_scope.hpp"

#include <windows.h>

#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace osplay::engine {
namespace {

using Microsoft::WRL::ComPtr;

struct QueuedUnit {
    std::vector<std::uint8_t> annexb;
    bool keyframe = false;
    std::uint64_t capture_time_us = 0;
};

/// The largest rectangle with the stream's aspect ratio centred in the monitor.
RECT aspect_fit(UINT source_width, UINT source_height, UINT target_width, UINT target_height) {
    if (source_width == 0 || source_height == 0) {
        return RECT{.left = 0,
                    .top = 0,
                    .right = static_cast<LONG>(target_width),
                    .bottom = static_cast<LONG>(target_height)};
    }
    const auto scaled_width = static_cast<UINT>(static_cast<unsigned long long>(target_height) *
                                                source_width / source_height);
    if (scaled_width <= target_width) {
        const auto left = static_cast<LONG>((target_width - scaled_width) / 2);
        return RECT{.left = left,
                    .top = 0,
                    .right = left + static_cast<LONG>(scaled_width),
                    .bottom = static_cast<LONG>(target_height)};
    }
    const auto scaled_height = static_cast<UINT>(static_cast<unsigned long long>(target_width) *
                                                 source_height / source_width);
    const auto top = static_cast<LONG>((target_height - scaled_height) / 2);
    return RECT{.left = 0,
                .top = top,
                .right = static_cast<LONG>(target_width),
                .bottom = top + static_cast<LONG>(scaled_height)};
}

class WindowsViewerStream final : public ViewerStream {
public:
    WindowsViewerStream(PresentationThread& presentation, const osplay_stream_config& stream,
                        const ViewerMonitor& monitor, ViewerStreamSink& sink)
        : presentation_(&presentation), sink_(&sink), stream_(stream), monitor_(monitor) {}

    ~WindowsViewerStream() override {
        try {
            {
                const std::scoped_lock lock(mutex_);
                stopping_ = true;
            }
            changed_.notify_all();
            if (thread_.joinable()) {
                thread_.join();
            }
            if (window_ != nullptr) {
                presentation_->destroy_window(window_);
            }
        } catch (...) {
            std::terminate(); // the decode thread cannot be left running behind its owner
        }
    }
    WindowsViewerStream(const WindowsViewerStream&) = delete;
    WindowsViewerStream& operator=(const WindowsViewerStream&) = delete;
    WindowsViewerStream(WindowsViewerStream&&) = delete;
    WindowsViewerStream& operator=(WindowsViewerStream&&) = delete;

    /// Creates the window and starts the decode thread; waits until the decoder is ready.
    std::optional<PipelineError> start(bool visible) {
        window_ = presentation_->create_window(monitor_.rect, visible);
        if (window_ == nullptr) {
            return PipelineError::PresentationFailed;
        }
        std::promise<std::optional<PipelineError>> ready;
        auto started = ready.get_future();
        thread_ =
            std::thread([this, promise = std::move(ready)]() mutable { run(std::move(promise)); });
        return started.get();
    }

    void submit(std::span<const std::uint8_t> annexb, bool keyframe,
                std::uint64_t capture_time_us) override {
        {
            const std::scoped_lock lock(mutex_);
            if (queue_.size() >= kViewerQueueLimit) {
                // The decoder fell far behind: start over from the next keyframe.
                queue_.clear();
                waiting_for_keyframe_ = true;
            }
            queue_.push_back({.annexb = std::vector<std::uint8_t>(annexb.begin(), annexb.end()),
                              .keyframe = keyframe,
                              .capture_time_us = capture_time_us});
        }
        changed_.notify_all();
    }

    void set_visible(bool visible) override { PresentationThread::show_window(window_, visible); }

    [[nodiscard]] ViewerStreamStats stats() const override {
        return {.frames_presented = presented_.load(), .decode_errors = errors_.load()};
    }

private:
    void run(std::promise<std::optional<PipelineError>> ready) {
        const MediaThreadScope scope;
        if (!scope.ready()) {
            ready.set_value(PipelineError::DecoderUnavailable);
            return;
        }
        if (const auto failure = initialize()) {
            release();
            ready.set_value(failure);
            return;
        }
        ready.set_value(std::nullopt);
        loop();
        release();
    }

    /// Media Foundation and COM objects go before this thread leaves its apartment.
    void release() {
        decoder_.reset();
        processor_.Reset();
        enumerator_.Reset();
        swap_chain_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        context_.Reset();
        device_.Reset();
        if (waitable_ != nullptr) {
            CloseHandle(waitable_);
            waitable_ = nullptr;
        }
    }

    std::optional<PipelineError> initialize() {
        const auto adapter = adapter_for_monitor(monitor_.rect);
        if (FAILED(D3D11CreateDevice(
                adapter.Get(),
                adapter == nullptr ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
                D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
            return PipelineError::DecoderUnavailable;
        }
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(device_.As(&multithread))) {
            multithread->SetMultithreadProtected(TRUE); // the decoder uses the device too
        }
        if (FAILED(device_.As(&video_device_)) || FAILED(context_.As(&video_context_))) {
            return PipelineError::DecoderUnavailable;
        }
        if (!create_swap_chain()) {
            return PipelineError::PresentationFailed;
        }
        auto decoder = MfH264Decoder::create(
            device_.Get(), stream_.width, stream_.height,
            [this](ID3D11Texture2D* texture, UINT slice, std::uint64_t /*capture_time_us*/) {
                present(texture, slice);
            });
        if (!decoder) {
            return decoder.error();
        }
        decoder_ = std::move(decoder.value());
        return std::nullopt;
    }

    bool create_swap_chain() {
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(device_.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
            return false;
        }
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = monitor_.description.width_px;
        desc.Height = monitor_.description.height_px;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_NONE; // the window has exactly the monitor's size
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> swap_chain;
        if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), window_, &desc, nullptr, nullptr,
                                                   &swap_chain)) ||
            FAILED(swap_chain.As(&swap_chain_)) || FAILED(swap_chain_->SetMaximumFrameLatency(1))) {
            return false;
        }
        static_cast<void>(factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER));
        waitable_ = swap_chain_->GetFrameLatencyWaitableObject();
        return true;
    }

    void loop() {
        while (true) {
            QueuedUnit unit;
            bool skip = false;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_) {
                    return;
                }
                unit = std::move(queue_.front());
                queue_.pop_front();
                if (waiting_for_keyframe_ && !unit.keyframe) {
                    skip = true; // a frame that depends on lost data would only show garbage
                } else {
                    waiting_for_keyframe_ = false;
                }
            }
            if (skip) {
                notify_keyframe_needed();
                continue;
            }
            if (!decoder_->decode(unit.annexb, unit.capture_time_us)) {
                ++errors_;
                {
                    const std::scoped_lock lock(mutex_);
                    waiting_for_keyframe_ = true;
                }
                notify_keyframe_needed();
            }
        }
    }

    void notify_keyframe_needed() {
        const ULONGLONG now = GetTickCount64();
        if (now - last_keyframe_request_ >= kViewerKeyframeRequestSpacingMilliseconds) {
            last_keyframe_request_ = now;
            sink_->on_keyframe_needed(stream_.stream_id);
        }
    }

    /// Converts the decoded slice into the back buffer and presents it at once.
    void present(ID3D11Texture2D* texture, UINT slice) {
        // At most one frame queued for the display: wait until the previous one is picked up.
        if (waitable_ != nullptr) {
            WaitForSingleObjectEx(waitable_, kPresentWaitMilliseconds, TRUE);
        }
        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) ||
            !blit(texture, slice, back_buffer.Get())) {
            return;
        }
        if (SUCCEEDED(swap_chain_->Present(0, 0))) {
            ++presented_;
        }
    }

    bool blit(ID3D11Texture2D* source, UINT slice, ID3D11Texture2D* target) {
        D3D11_TEXTURE2D_DESC source_desc{};
        source->GetDesc(&source_desc);
        const UINT target_width = monitor_.description.width_px;
        const UINT target_height = monitor_.description.height_px;
        if (processor_ == nullptr || source_desc.Width != processor_width_ ||
            source_desc.Height != processor_height_) {
            if (!create_processor(source_desc.Width, source_desc.Height, target_width,
                                  target_height)) {
                return false;
            }
        }
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
        input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): D3D11 API
        input_desc.Texture2D.ArraySlice = slice;
        ComPtr<ID3D11VideoProcessorInputView> input;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
        output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> output;
        if (FAILED(video_device_->CreateVideoProcessorInputView(source, enumerator_.Get(),
                                                                &input_desc, &input)) ||
            FAILED(video_device_->CreateVideoProcessorOutputView(target, enumerator_.Get(),
                                                                 &output_desc, &output))) {
            return false;
        }
        // The decoder pads its textures; only the stream's area is picture.
        const RECT source_rect{.left = 0,
                               .top = 0,
                               .right = static_cast<LONG>(stream_.width),
                               .bottom = static_cast<LONG>(stream_.height)};
        const RECT destination =
            aspect_fit(stream_.width, stream_.height, target_width, target_height);
        video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source_rect);
        video_context_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &destination);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input.Get();
        return SUCCEEDED(
            video_context_->VideoProcessorBlt(processor_.Get(), output.Get(), 0, 1, &stream));
    }

    bool create_processor(UINT source_width, UINT source_height, UINT target_width,
                          UINT target_height) {
        processor_.Reset();
        enumerator_.Reset();
        const DXGI_RATIONAL rate{.Numerator = stream_.refresh_millihertz, .Denominator = 1000};
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputFrameRate = rate;
        desc.InputWidth = source_width;
        desc.InputHeight = source_height;
        desc.OutputFrameRate = rate;
        desc.OutputWidth = target_width;
        desc.OutputHeight = target_height;
        desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        if (FAILED(video_device_->CreateVideoProcessorEnumerator(&desc, &enumerator_)) ||
            FAILED(video_device_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_))) {
            return false;
        }
        video_context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
        video_context_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0,
                                                           D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        D3D11_VIDEO_COLOR black{};
        black.RGBA.A = 1.0F; // NOLINT(cppcoreguidelines-pro-type-union-access): D3D11 API
        video_context_->VideoProcessorSetOutputBackgroundColor(processor_.Get(), FALSE, &black);
        ComPtr<ID3D11VideoContext1> context1;
        if (SUCCEEDED(video_context_.As(&context1))) {
            context1->VideoProcessorSetStreamColorSpace1(
                processor_.Get(), 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
            context1->VideoProcessorSetOutputColorSpace1(processor_.Get(),
                                                         DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        }
        processor_width_ = source_width;
        processor_height_ = source_height;
        return true;
    }

    PresentationThread* presentation_; // never null
    ViewerStreamSink* sink_;           // never null
    osplay_stream_config stream_;
    ViewerMonitor monitor_;
    HWND window_ = nullptr;

    // Decode thread only.
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    UINT processor_width_ = 0;
    UINT processor_height_ = 0;
    ComPtr<IDXGISwapChain2> swap_chain_;
    HANDLE waitable_ = nullptr;
    std::unique_ptr<MfH264Decoder> decoder_;
    ULONGLONG last_keyframe_request_ = 0;

    std::atomic<std::uint64_t> presented_{0};
    std::atomic<std::uint64_t> errors_{0};

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<QueuedUnit> queue_;
    bool waiting_for_keyframe_ = true;
    bool stopping_ = false;
    std::thread thread_;
};

} // namespace

win::Expected<std::unique_ptr<ViewerStream>, PipelineError>
WindowsViewerStreamFactory::start(const osplay_stream_config& stream, const ViewerMonitor& monitor,
                                  bool visible, ViewerStreamSink& sink) {
    using Result = win::Expected<std::unique_ptr<ViewerStream>, PipelineError>;
    auto viewer_stream =
        std::make_unique<WindowsViewerStream>(*presentation_, stream, monitor, sink);
    if (const auto failure = viewer_stream->start(visible)) {
        return Result::failure(*failure);
    }
    return {std::unique_ptr<ViewerStream>(std::move(viewer_stream))};
}

} // namespace osplay::engine
