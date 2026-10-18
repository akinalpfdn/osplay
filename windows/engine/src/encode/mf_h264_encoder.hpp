#pragma once

// Hardware H.264 encoding through a Media Foundation transform (DEVPLAN): enumerated with
// MFTEnumEx, asynchronous event model, D3D11 NV12 input through a DXGI device manager, low-latency
// mode, no B-frames, a very long GOP and keyframes on request.
//
// Not thread-safe: create it, submit frames and poll on one thread (the pipeline's).

#include "host/stream_pipeline.hpp"
#include "media/h264_stream.hpp"

#include <windows.h>

#include <codecapi.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>

namespace osplay::engine {

class MfH264Encoder {
public:
    /// Annex B access unit (keyframes start with SPS/PPS) and its sample time in 100 ns units.
    using Output = std::function<void(std::span<const std::uint8_t> annexb, bool keyframe,
                                      std::int64_t sample_time)>;

    /// Requires MFStartup on this thread.
    [[nodiscard]] static win::Expected<std::unique_ptr<MfH264Encoder>, PipelineError>
    create(ID3D11Device* device, const StreamSettings& settings, Output output);

    ~MfH264Encoder();
    MfH264Encoder(const MfH264Encoder&) = delete;
    MfH264Encoder& operator=(const MfH264Encoder&) = delete;
    MfH264Encoder(MfH264Encoder&&) = delete;
    MfH264Encoder& operator=(MfH264Encoder&&) = delete;

    /// Queues an NV12 texture of the stream size. The texture must stay unchanged until the
    /// encoder has taken it (at most `kMaxPendingInputs` frames later).
    void submit(ID3D11Texture2D* nv12, std::int64_t sample_time, bool keyframe);

    /// Feeds queued input and drains output; returns false once the encoder has failed.
    [[nodiscard]] bool poll();

    /// Frames waiting for the encoder; the oldest is dropped when the queue is full.
    static constexpr std::size_t kMaxPendingInputs = 2;

private:
    struct Input {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        std::int64_t sample_time = 0;
        bool keyframe = false;
    };

    MfH264Encoder(Microsoft::WRL::ComPtr<IMFTransform> transform, Output output);
    bool configure(ID3D11Device* device, const StreamSettings& settings);
    bool feed();
    bool drain_output();
    bool renegotiate_output();

    Microsoft::WRL::ComPtr<IMFTransform> transform_;
    Microsoft::WRL::ComPtr<IMFMediaEventGenerator> events_;
    Microsoft::WRL::ComPtr<ICodecAPI> codec_api_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> device_manager_;
    Output output_;
    DWORD input_id_ = 0;
    DWORD output_id_ = 0;
    std::int64_t frame_duration_ = 0;
    std::deque<Input> pending_;
    int input_credits_ = 0;
    ParameterSetCache parameter_sets_;
};

} // namespace osplay::engine
