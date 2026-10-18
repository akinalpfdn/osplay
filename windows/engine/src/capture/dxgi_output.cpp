#include "capture/dxgi_output.hpp"

#include <d3d11_4.h>

#include <array>
#include <cwchar>
#include <iterator>

namespace osplay::engine {

using Microsoft::WRL::ComPtr;

win::Expected<DuplicationTarget, PipelineError>
open_duplication_target(const std::wstring& gdi_name) {
    using Result = win::Expected<DuplicationTarget, PipelineError>;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return Result::failure(PipelineError::CaptureFailed);
    }
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> found;
    DXGI_OUTPUT_DESC found_desc{};
    for (UINT a = 0;
         found == nullptr && factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) == S_OK;
         ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) == S_OK; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) &&
                _wcsicmp(std::data(desc.DeviceName), gdi_name.c_str()) == 0) {
                found = output;
                found_desc = desc;
                break;
            }
        }
    }
    if (found == nullptr) {
        return Result::failure(PipelineError::MonitorNotFound);
    }

    DuplicationTarget target;
    if (FAILED(found.As(&target.output))) {
        return Result::failure(PipelineError::CaptureFailed);
    }
    constexpr std::array<D3D_FEATURE_LEVEL, 2> kLevels{D3D_FEATURE_LEVEL_11_1,
                                                       D3D_FEATURE_LEVEL_11_0};
    // The adapter is given, so the driver type must be UNKNOWN.
    if (FAILED(
            D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                              D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                              kLevels.data(), static_cast<UINT>(kLevels.size()), D3D11_SDK_VERSION,
                              &target.device, nullptr, &target.context))) {
        return Result::failure(PipelineError::CaptureFailed);
    }
    // Media Foundation uses the device from its own threads.
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(target.device.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }
    const RECT& desktop = found_desc.DesktopCoordinates;
    target.rect = MonitorRect{
        .left = desktop.left, .top = desktop.top, .right = desktop.right, .bottom = desktop.bottom};
    return target;
}

bool refresh_duplication_output(DuplicationTarget& target, const std::wstring& gdi_name) {
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(target.device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter))) {
        return false;
    }
    ComPtr<IDXGIOutput> output;
    for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) == S_OK; ++o) {
        DXGI_OUTPUT_DESC desc{};
        ComPtr<IDXGIOutput1> output1;
        if (SUCCEEDED(output->GetDesc(&desc)) &&
            _wcsicmp(std::data(desc.DeviceName), gdi_name.c_str()) == 0 &&
            SUCCEEDED(output.As(&output1))) {
            target.output = output1;
            const RECT& desktop = desc.DesktopCoordinates;
            target.rect = MonitorRect{.left = desktop.left,
                                      .top = desktop.top,
                                      .right = desktop.right,
                                      .bottom = desktop.bottom};
            return true;
        }
    }
    return false;
}

} // namespace osplay::engine
