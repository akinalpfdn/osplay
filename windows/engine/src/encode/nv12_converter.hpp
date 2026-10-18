#pragma once

// BGRA → NV12 on the GPU with the D3D11 video processor (DEVPLAN), scaling the desktop to the
// stream size when they differ (e.g. after a mode change).

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace osplay::engine {

class Nv12Converter {
public:
    Nv12Converter(Microsoft::WRL::ComPtr<ID3D11Device> device,
                  const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context, std::uint32_t width,
                  std::uint32_t height, std::uint32_t refresh_millihertz);

    /// Converts `source` (BGRA, any size) into `target` (NV12, the stream size). False on failure.
    [[nodiscard]] bool convert(ID3D11Texture2D* source, ID3D11Texture2D* target);

private:
    bool prepare(UINT source_width, UINT source_height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;
    std::uint32_t width_;
    std::uint32_t height_;
    std::uint32_t refresh_millihertz_;
    UINT source_width_ = 0;
    UINT source_height_ = 0;
};

} // namespace osplay::engine
