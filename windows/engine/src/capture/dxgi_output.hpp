#pragma once

// The DXGI output that shows the virtual monitor, and a D3D11 device on its adapter: Desktop
// Duplication, the video processor and the encoder all run on that one device (DEVPLAN).

#include "host/stream_pipeline.hpp"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>

namespace osplay::engine {

struct DuplicationTarget {
    Microsoft::WRL::ComPtr<IDXGIOutput1> output;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    MonitorRect rect;
};

/// Finds the output whose GDI name is `gdi_name` (\\.\DISPLAYn) and creates a video-capable,
/// multithread-protected D3D11 device on its adapter.
[[nodiscard]] win::Expected<DuplicationTarget, PipelineError>
open_duplication_target(const std::wstring& gdi_name);

/// The adapter driving the monitor at `rect` (desktop coordinates); the default adapter if none
/// matches exactly.
[[nodiscard]] Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_for_monitor(const MonitorRect& rect);

/// Looks the output up again on the target's adapter, e.g. after a mode change replaced it.
/// False if it is gone (or now on another adapter, which needs a new pipeline).
[[nodiscard]] bool refresh_duplication_output(DuplicationTarget& target,
                                              const std::wstring& gdi_name);

} // namespace osplay::engine
