#include "encode/nv12_converter.hpp"

#include <d3d11_1.h>

#include <utility>

namespace osplay::engine {

using Microsoft::WRL::ComPtr;

Nv12Converter::Nv12Converter(ComPtr<ID3D11Device> device,
                             const ComPtr<ID3D11DeviceContext>& context, std::uint32_t width,
                             std::uint32_t height, std::uint32_t refresh_millihertz)
    : device_(std::move(device)), width_(width), height_(height),
      refresh_millihertz_(refresh_millihertz) {
    // Failures leave the members empty; convert() then reports them.
    static_cast<void>(device_.As(&video_device_));
    static_cast<void>(context.As(&video_context_));
}

bool Nv12Converter::prepare(UINT source_width, UINT source_height) {
    if (processor_ != nullptr && source_width == source_width_ && source_height == source_height_) {
        return true;
    }
    processor_.Reset();
    enumerator_.Reset();
    if (video_device_ == nullptr || video_context_ == nullptr) {
        return false;
    }
    const DXGI_RATIONAL rate{.Numerator = refresh_millihertz_, .Denominator = 1000};
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate = rate;
    desc.InputWidth = source_width;
    desc.InputHeight = source_height;
    desc.OutputFrameRate = rate;
    desc.OutputWidth = width_;
    desc.OutputHeight = height_;
    desc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
    if (FAILED(video_device_->CreateVideoProcessorEnumerator(&desc, &enumerator_)) ||
        FAILED(video_device_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_))) {
        return false;
    }
    video_context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
    // Desktop pixels are full-range sRGB-like BT.709; the stream is studio-range BT.709, which
    // the encoder signals in the bitstream.
    ComPtr<ID3D11VideoContext1> context1;
    if (SUCCEEDED(video_context_.As(&context1))) {
        context1->VideoProcessorSetStreamColorSpace1(processor_.Get(), 0,
                                                     DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        context1->VideoProcessorSetOutputColorSpace1(processor_.Get(),
                                                     DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    }
    source_width_ = source_width;
    source_height_ = source_height;
    return true;
}

bool Nv12Converter::convert(ID3D11Texture2D* source, ID3D11Texture2D* target) {
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if (!prepare(source_desc.Width, source_desc.Height)) {
        return false;
    }
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorInputView> input;
    if (FAILED(video_device_->CreateVideoProcessorInputView(source, enumerator_.Get(), &input_desc,
                                                            &input))) {
        return false;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
    output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> output;
    if (FAILED(video_device_->CreateVideoProcessorOutputView(target, enumerator_.Get(),
                                                             &output_desc, &output))) {
        return false;
    }
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input.Get();
    return SUCCEEDED(
        video_context_->VideoProcessorBlt(processor_.Get(), output.Get(), 0, 1, &stream));
}

} // namespace osplay::engine
