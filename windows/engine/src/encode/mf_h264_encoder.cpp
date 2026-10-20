#include "encode/mf_h264_encoder.hpp"

#include <mferror.h>
#include <propvarutil.h>

#include <utility>
#include <vector>

namespace osplay::engine {
namespace {

using Microsoft::WRL::ComPtr;

/// A GOP this long never ends on its own; keyframes come on request (DEVPLAN).
constexpr UINT32 kGopSize = 0xFFFFFFFF;
constexpr std::int64_t kHundredNanosecondsPerSecond = 10'000'000;

/// The first hardware encoder taking NV12 in and producing H.264, activated.
ComPtr<IMFTransform> activate_hardware_encoder() {
    const MFT_REGISTER_TYPE_INFO input{.guidMajorType = MFMediaType_Video,
                                       .guidSubtype = MFVideoFormat_NV12};
    const MFT_REGISTER_TYPE_INFO output{.guidMajorType = MFMediaType_Video,
                                        .guidSubtype = MFVideoFormat_H264};
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                         MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &input, &output,
                         &activates, &count))) {
        return nullptr;
    }
    ComPtr<IMFTransform> transform;
    for (UINT32 i = 0; i < count; ++i) {
        if (transform == nullptr &&
            FAILED(activates[i]->ActivateObject(IID_PPV_ARGS(&transform)))) {
            transform.Reset();
        }
        activates[i]->Release();
    }
    CoTaskMemFree(static_cast<void*>(activates));
    return transform;
}

HRESULT set_codec_value(ICodecAPI* api, const GUID& property, UINT32 value) {
    VARIANT variant{};
    if (const HRESULT init = InitVariantFromUInt32(value, &variant); FAILED(init)) {
        return init;
    }
    return api->SetValue(&property, &variant);
}

HRESULT set_codec_flag(ICodecAPI* api, const GUID& property, bool value) {
    VARIANT variant{};
    if (const HRESULT init = InitVariantFromBoolean(value ? TRUE : FALSE, &variant); FAILED(init)) {
        return init;
    }
    return api->SetValue(&property, &variant);
}

ComPtr<IMFMediaType> video_type(const GUID& subtype, const StreamSettings& settings) {
    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type)) ||
        FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(type->SetGUID(MF_MT_SUBTYPE, subtype)) ||
        FAILED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, settings.width, settings.height)) ||
        FAILED(
            MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, settings.refresh_millihertz, 1000)) ||
        FAILED(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))) {
        return nullptr;
    }
    return type;
}

} // namespace

MfH264Encoder::MfH264Encoder(ComPtr<IMFTransform> transform, Output output)
    : transform_(std::move(transform)), output_(std::move(output)) {}

MfH264Encoder::~MfH264Encoder() {
    // Ends streaming so the transform releases the input textures and the device manager.
    static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
    static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
    static_cast<void>(transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0));
}

win::Expected<std::unique_ptr<MfH264Encoder>, PipelineError>
MfH264Encoder::create(ID3D11Device* device, const StreamSettings& settings, Output output) {
    using Result = win::Expected<std::unique_ptr<MfH264Encoder>, PipelineError>;
    auto transform = activate_hardware_encoder();
    if (transform == nullptr) {
        return Result::failure(PipelineError::EncoderUnavailable);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the constructor is private
    std::unique_ptr<MfH264Encoder> encoder(
        new MfH264Encoder(std::move(transform), std::move(output)));
    if (!encoder->configure(device, settings)) {
        return Result::failure(PipelineError::EncoderUnavailable);
    }
    return {std::move(encoder)};
}

bool MfH264Encoder::configure(ID3D11Device* device, const StreamSettings& settings) {
    ComPtr<IMFAttributes> attributes;
    if (FAILED(transform_->GetAttributes(&attributes))) {
        return false;
    }
    UINT32 is_async = FALSE;
    // Hardware encoders are asynchronous and must be unlocked before use.
    if (FAILED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &is_async)) || is_async == FALSE ||
        FAILED(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE)) ||
        FAILED(transform_.As(&events_))) {
        return false;
    }
    static_cast<void>(attributes->SetUINT32(MF_LOW_LATENCY, TRUE)); // optional hint

    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &device_manager_)) ||
        FAILED(device_manager_->ResetDevice(device, token)) ||
        FAILED(transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                          reinterpret_cast<ULONG_PTR>(device_manager_.Get())))) {
        return false;
    }

    if (const HRESULT ids = transform_->GetStreamIDs(1, &input_id_, 1, &output_id_);
        FAILED(ids) && ids != E_NOTIMPL) {
        return false;
    }

    // Output first: encoders derive the accepted input types from it.
    auto output_type = video_type(MFVideoFormat_H264, settings);
    if (output_type == nullptr ||
        FAILED(output_type->SetUINT32(MF_MT_AVG_BITRATE, settings.bitrate_kbps * 1000U)) ||
        FAILED(output_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High))) {
        return false;
    }
    // BT.709, studio range: what the video processor produces.
    static_cast<void>(output_type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
    static_cast<void>(output_type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709));
    static_cast<void>(output_type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
    static_cast<void>(output_type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
    if (FAILED(transform_->SetOutputType(output_id_, output_type.Get(), 0))) {
        return false;
    }
    auto input_type = video_type(MFVideoFormat_NV12, settings);
    if (input_type == nullptr || FAILED(transform_->SetInputType(input_id_, input_type.Get(), 0))) {
        return false;
    }

    if (SUCCEEDED(transform_.As(&codec_api_))) {
        // Each setting is best effort: encoders differ in what they support.
        static_cast<void>(set_codec_flag(codec_api_.Get(), CODECAPI_AVLowLatencyMode, true));
        static_cast<void>(set_codec_value(codec_api_.Get(), CODECAPI_AVEncCommonRateControlMode,
                                          eAVEncCommonRateControlMode_CBR));
        static_cast<void>(set_codec_value(codec_api_.Get(), CODECAPI_AVEncCommonMeanBitRate,
                                          settings.bitrate_kbps * 1000U));
        static_cast<void>(set_codec_value(codec_api_.Get(), CODECAPI_AVEncMPVGOPSize, kGopSize));
        static_cast<void>(
            set_codec_value(codec_api_.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0));
    }

    frame_duration_ = settings.refresh_millihertz == 0
                          ? 0
                          : kHundredNanosecondsPerSecond * 1000 / settings.refresh_millihertz;
    return SUCCEEDED(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0)) &&
           SUCCEEDED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) &&
           SUCCEEDED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
}

void MfH264Encoder::submit(ID3D11Texture2D* nv12, std::int64_t sample_time, bool keyframe) {
    if (pending_.size() >= kMaxPendingInputs) {
        // The encoder is behind; drop the oldest frame but keep its keyframe request.
        keyframe = keyframe || pending_.front().keyframe;
        pending_.pop_front();
    }
    pending_.push_back({.texture = nv12, .sample_time = sample_time, .keyframe = keyframe});
}

void MfH264Encoder::set_bitrate(std::uint32_t kbps) {
    if (codec_api_ != nullptr) {
        // Best effort: the session continues at the previous bitrate if the encoder refuses.
        static_cast<void>(
            set_codec_value(codec_api_.Get(), CODECAPI_AVEncCommonMeanBitRate, kbps * 1000U));
    }
}

bool MfH264Encoder::poll() {
    while (true) {
        if (!feed()) {
            return false;
        }
        ComPtr<IMFMediaEvent> event;
        const HRESULT got = events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
        if (got == MF_E_NO_EVENTS_AVAILABLE) {
            return true;
        }
        MediaEventType type = MEUnknown;
        HRESULT status = S_OK;
        if (FAILED(got) || FAILED(event->GetType(&type)) || FAILED(event->GetStatus(&status)) ||
            FAILED(status)) {
            return false;
        }
        if (type == METransformNeedInput) {
            ++input_credits_;
        } else if (type == METransformHaveOutput && !drain_output()) {
            return false;
        }
    }
}

bool MfH264Encoder::feed() {
    while (input_credits_ > 0 && !pending_.empty()) {
        const Input input = std::move(pending_.front());
        pending_.pop_front();
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), input.texture.Get(), 0,
                                             FALSE, &buffer)) ||
            FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
            FAILED(sample->SetSampleTime(input.sample_time)) ||
            FAILED(sample->SetSampleDuration(frame_duration_))) {
            return false;
        }
        ComPtr<IMF2DBuffer> buffer_2d;
        DWORD length = 0;
        if (SUCCEEDED(buffer.As(&buffer_2d)) &&
            SUCCEEDED(buffer_2d->GetContiguousLength(&length))) {
            static_cast<void>(buffer->SetCurrentLength(length)); // some encoders check it
        }
        if (input.keyframe && codec_api_ != nullptr) {
            static_cast<void>(
                set_codec_value(codec_api_.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1));
        }
        if (FAILED(transform_->ProcessInput(input_id_, sample.Get(), 0))) {
            return false;
        }
        --input_credits_;
    }
    return true;
}

bool MfH264Encoder::drain_output() {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform_->GetOutputStreamInfo(output_id_, &info))) {
        return false;
    }
    const bool provides_samples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                                   MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    ComPtr<IMFSample> own_sample;
    if (!provides_samples) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateSample(&own_sample)) ||
            FAILED(MFCreateMemoryBuffer(info.cbSize, &buffer)) ||
            FAILED(own_sample->AddBuffer(buffer.Get()))) {
            return false;
        }
    }
    MFT_OUTPUT_DATA_BUFFER data{
        .dwStreamID = output_id_, .pSample = own_sample.Get(), .dwStatus = 0, .pEvents = nullptr};
    DWORD status = 0;
    const HRESULT processed = transform_->ProcessOutput(0, 1, &data, &status);
    ComPtr<IMFCollection> events;
    events.Attach(data.pEvents); // released with the ComPtr
    ComPtr<IMFSample> sample;
    if (provides_samples) {
        sample.Attach(data.pSample); // the encoder's sample, owned by us now
    } else {
        sample = own_sample;
    }
    if (processed == MF_E_TRANSFORM_STREAM_CHANGE) {
        return renegotiate_output();
    }
    if (processed == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        return true;
    }
    if (FAILED(processed) || sample == nullptr) {
        return false;
    }

    ComPtr<IMFMediaBuffer> contiguous;
    BYTE* bytes = nullptr;
    DWORD length = 0;
    if (FAILED(sample->ConvertToContiguousBuffer(&contiguous)) ||
        FAILED(contiguous->Lock(&bytes, nullptr, &length))) {
        return false;
    }
    bool keyframe = false;
    const std::vector<std::uint8_t> annexb =
        parameter_sets_.prepare(std::span<const std::uint8_t>(bytes, length), keyframe);
    static_cast<void>(contiguous->Unlock());
    LONGLONG time = 0;
    static_cast<void>(sample->GetSampleTime(&time)); // 0 if missing: capture time unknown
    if (!annexb.empty()) {
        output_(annexb, keyframe, time);
    }
    return true;
}

bool MfH264Encoder::renegotiate_output() {
    ComPtr<IMFMediaType> type;
    if (FAILED(transform_->GetOutputAvailableType(output_id_, 0, &type)) ||
        FAILED(transform_->SetOutputType(output_id_, type.Get(), 0))) {
        return false;
    }
    UINT32 size = 0;
    if (SUCCEEDED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size > 0) {
        std::vector<std::uint8_t> header(size);
        if (SUCCEEDED(type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), size, nullptr))) {
            parameter_sets_.remember(header);
        }
    }
    return true;
}

} // namespace osplay::engine
