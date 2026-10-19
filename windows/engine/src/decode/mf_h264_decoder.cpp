#include "decode/mf_h264_decoder.hpp"

#include <codecapi.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wmcodecdsp.h>

#include <cstring>
#include <utility>

namespace osplay::engine {

using Microsoft::WRL::ComPtr;

MfH264Decoder::MfH264Decoder(ComPtr<IMFTransform> transform, Output output)
    : transform_(std::move(transform)), output_(std::move(output)) {}

MfH264Decoder::~MfH264Decoder() {
    static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
    static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0));
}

win::Expected<std::unique_ptr<MfH264Decoder>, PipelineError>
MfH264Decoder::create(ID3D11Device* device, std::uint32_t width, std::uint32_t height,
                      Output output) {
    using Result = win::Expected<std::unique_ptr<MfH264Decoder>, PipelineError>;
    ComPtr<IMFTransform> transform;
    if (FAILED(CoCreateInstance(CLSID_MSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&transform)))) {
        return Result::failure(PipelineError::DecoderUnavailable);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the constructor is private
    std::unique_ptr<MfH264Decoder> decoder(
        new MfH264Decoder(std::move(transform), std::move(output)));
    if (!decoder->configure(device, width, height)) {
        return Result::failure(PipelineError::DecoderUnavailable);
    }
    return {std::move(decoder)};
}

bool MfH264Decoder::configure(ID3D11Device* device, std::uint32_t width, std::uint32_t height) {
    ComPtr<IMFAttributes> attributes;
    UINT32 d3d11_aware = FALSE;
    if (FAILED(transform_->GetAttributes(&attributes)) ||
        FAILED(attributes->GetUINT32(MF_SA_D3D11_AWARE, &d3d11_aware)) || d3d11_aware == FALSE) {
        return false;
    }
    static_cast<void>(attributes->SetUINT32(MF_LOW_LATENCY, TRUE)); // decode-order output, no delay

    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &device_manager_)) ||
        FAILED(device_manager_->ResetDevice(device, token)) ||
        FAILED(transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                          reinterpret_cast<ULONG_PTR>(device_manager_.Get())))) {
        return false;
    }
    ComPtr<ICodecAPI> codec_api;
    if (SUCCEEDED(transform_.As(&codec_api))) {
        VARIANT low_latency{};
        if (SUCCEEDED(InitVariantFromBoolean(TRUE, &low_latency))) {
            static_cast<void>(codec_api->SetValue(&CODECAPI_AVLowLatencyMode, &low_latency));
        }
    }

    ComPtr<IMFMediaType> input;
    if (FAILED(MFCreateMediaType(&input)) ||
        FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
        FAILED(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height)) ||
        FAILED(input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(transform_->SetInputType(0, input.Get(), 0))) {
        return false;
    }
    return select_nv12_output() &&
           SUCCEEDED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) &&
           SUCCEEDED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
}

bool MfH264Decoder::select_nv12_output() {
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        if (FAILED(transform_->GetOutputAvailableType(0, index, &type))) {
            return false;
        }
        GUID subtype{};
        if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
            IsEqualGUID(subtype, MFVideoFormat_NV12) != FALSE) {
            return SUCCEEDED(transform_->SetOutputType(0, type.Get(), 0));
        }
    }
}

bool MfH264Decoder::decode(std::span<const std::uint8_t> annexb, std::uint64_t capture_time_us) {
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    BYTE* bytes = nullptr;
    if (annexb.empty() ||
        FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(annexb.size()), &buffer)) ||
        FAILED(buffer->Lock(&bytes, nullptr, nullptr))) {
        return false;
    }
    std::memcpy(bytes, annexb.data(), annexb.size());
    static_cast<void>(buffer->Unlock());
    if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(annexb.size()))) ||
        FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
        FAILED(sample->SetSampleTime(static_cast<LONGLONG>(capture_time_us) * 10))) {
        return false;
    }
    const HRESULT accepted = transform_->ProcessInput(0, sample.Get(), 0);
    if (accepted == MF_E_NOTACCEPTING) {
        // Output is waiting: drain it, then this frame fits.
        if (!drain() || FAILED(transform_->ProcessInput(0, sample.Get(), 0))) {
            return false;
        }
    } else if (FAILED(accepted)) {
        return false;
    }
    return drain();
}

bool MfH264Decoder::drain() {
    while (true) {
        MFT_OUTPUT_DATA_BUFFER data{
            .dwStreamID = 0, .pSample = nullptr, .dwStatus = 0, .pEvents = nullptr};
        DWORD status = 0;
        const HRESULT processed = transform_->ProcessOutput(0, 1, &data, &status);
        ComPtr<IMFCollection> events;
        events.Attach(data.pEvents);
        ComPtr<IMFSample> sample;
        sample.Attach(data.pSample); // the decoder provides its D3D11 samples
        if (processed == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            return true;
        }
        if (processed == MF_E_TRANSFORM_STREAM_CHANGE) {
            if (!select_nv12_output()) {
                return false;
            }
            continue;
        }
        if (FAILED(processed) || sample == nullptr) {
            return false;
        }
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> dxgi_buffer;
        ComPtr<ID3D11Texture2D> texture;
        UINT slice = 0;
        LONGLONG time = 0;
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) || FAILED(buffer.As(&dxgi_buffer)) ||
            FAILED(dxgi_buffer->GetResource(IID_PPV_ARGS(&texture))) ||
            FAILED(dxgi_buffer->GetSubresourceIndex(&slice))) {
            return false;
        }
        static_cast<void>(sample->GetSampleTime(&time)); // 0 if missing: latency unknown
        output_(texture.Get(), slice, static_cast<std::uint64_t>(time / 10));
    }
}

} // namespace osplay::engine
