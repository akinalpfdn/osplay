#pragma once

// Hardware H.264 decoding with the Media Foundation H.264 decoder (DXVA through a DXGI device
// manager), low-latency mode, NV12 D3D11 output (DEVPLAN). Not thread-safe: one thread creates it
// and decodes.

#include "host/stream_pipeline.hpp"

#include <windows.h>

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace osplay::engine {

class MfH264Decoder {
public:
    /// A decoded picture: one slice of the decoder's texture array, and its capture time.
    using Output =
        std::function<void(ID3D11Texture2D* texture, UINT slice, std::uint64_t capture_time_us)>;

    /// Requires MFStartup on this thread.
    [[nodiscard]] static win::Expected<std::unique_ptr<MfH264Decoder>, PipelineError>
    create(ID3D11Device* device, std::uint32_t width, std::uint32_t height, Output output);

    ~MfH264Decoder();
    MfH264Decoder(const MfH264Decoder&) = delete;
    MfH264Decoder& operator=(const MfH264Decoder&) = delete;
    MfH264Decoder(MfH264Decoder&&) = delete;
    MfH264Decoder& operator=(MfH264Decoder&&) = delete;

    /// Decodes one Annex B access unit; decoded pictures go to `Output` before this returns.
    /// False when the frame could not be decoded: ask the host for a keyframe.
    [[nodiscard]] bool decode(std::span<const std::uint8_t> annexb, std::uint64_t capture_time_us);

private:
    explicit MfH264Decoder(Microsoft::WRL::ComPtr<IMFTransform> transform, Output output);
    bool configure(ID3D11Device* device, std::uint32_t width, std::uint32_t height);
    bool select_nv12_output();
    bool drain();

    Microsoft::WRL::ComPtr<IMFTransform> transform_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> device_manager_;
    Output output_;
};

} // namespace osplay::engine
