#include <osplay/win/helper_protocol.hpp>

#include <optional>

namespace osplay::win::helper {
namespace {

constexpr std::size_t kConfigureFixedSize = 14; // width, height, refresh, gpu name length
constexpr std::size_t kStatusPayloadSize = 16;

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) {
        for (int shift = 0; shift < 16; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void raw(const std::string& text) { bytes_.insert(bytes_.end(), text.begin(), text.end()); }

    /// Writes the common header followed by the payload produced by `body`.
    template <typename Body>
    static Bytes message(std::uint16_t code, const Body& body) {
        Writer payload;
        body(payload);
        Writer out;
        out.u32(kMagic);
        out.u16(kVersion);
        out.u16(code);
        out.u32(static_cast<std::uint32_t>(payload.bytes_.size()));
        out.bytes_.insert(out.bytes_.end(), payload.bytes_.begin(), payload.bytes_.end());
        return std::move(out.bytes_);
    }

private:
    Bytes bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }

    std::optional<std::uint8_t> u8() {
        if (remaining() < 1) {
            return std::nullopt;
        }
        return bytes_[offset_++];
    }
    std::optional<std::uint16_t> u16() {
        if (remaining() < 2) {
            return std::nullopt;
        }
        const auto value = static_cast<std::uint16_t>(bytes_[offset_] | (bytes_[offset_ + 1] << 8));
        offset_ += 2;
        return value;
    }
    std::optional<std::uint32_t> u32() {
        if (remaining() < 4) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes_[offset_ + i]) << (8 * i);
        }
        offset_ += 4;
        return value;
    }
    std::optional<std::string> text(std::size_t length) {
        if (remaining() < length) {
            return std::nullopt;
        }
        const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(offset_);
        std::string value(first, first + static_cast<std::ptrdiff_t>(length));
        offset_ += length;
        return value;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

struct Header {
    std::uint16_t code = 0;
    std::uint32_t payload_length = 0;
};

Expected<Header, DecodeError> read_header(Reader& reader, std::size_t message_size) {
    if (message_size > kMaxMessageSize) {
        return Expected<Header, DecodeError>::failure(DecodeError::TooLarge);
    }
    const auto magic = reader.u32();
    const auto version = reader.u16();
    const auto code = reader.u16();
    const auto length = reader.u32();
    if (!magic || !version || !code || !length) {
        return Expected<Header, DecodeError>::failure(DecodeError::Truncated);
    }
    if (*magic != kMagic) {
        return Expected<Header, DecodeError>::failure(DecodeError::BadMagic);
    }
    if (*version != kVersion) {
        return Expected<Header, DecodeError>::failure(DecodeError::UnsupportedVersion);
    }
    if (*length != reader.remaining()) {
        return Expected<Header, DecodeError>::failure(DecodeError::LengthMismatch);
    }
    return Header{.code = *code, .payload_length = *length};
}

bool is_valid_utf8_without_controls(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t code_point = 0;
        if (lead < 0x80) {
            code_point = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            extra = 1;
            code_point = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            extra = 2;
            code_point = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            extra = 3;
            code_point = lead & 0x07U;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3FU);
        }
        constexpr std::array<std::uint32_t, 4> kMinimumForLength{0x0, 0x80, 0x800, 0x10000};
        const bool overlong = code_point < kMinimumForLength.at(extra);
        const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
        const bool control = code_point < 0x20 || (code_point >= 0x7F && code_point < 0xA0);
        if (overlong || surrogate || control || code_point > 0x10FFFF) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

} // namespace

bool is_valid(const MonitorConfig& config) {
    return config.width >= kMinWidth && config.width <= kMaxWidth && config.height >= kMinHeight &&
           config.height <= kMaxHeight && config.refresh_millihertz >= kMinRefreshMillihertz &&
           config.refresh_millihertz <= kMaxRefreshMillihertz &&
           config.gpu_name.size() <= kMaxGpuNameBytes &&
           is_valid_utf8_without_controls(config.gpu_name);
}

Bytes encode(const Request& request) {
    return Writer::message(static_cast<std::uint16_t>(request.command), [&](Writer& out) {
        if (request.command == Command::Configure) {
            out.u32(request.config.width);
            out.u32(request.config.height);
            out.u32(request.config.refresh_millihertz);
            out.u16(static_cast<std::uint16_t>(request.config.gpu_name.size()));
            out.raw(request.config.gpu_name);
        }
    });
}

Bytes encode(const Response& response) {
    return Writer::message(static_cast<std::uint16_t>(response.result), [&](Writer& out) {
        const MonitorStatus& status = response.status;
        out.u8(status.vdd_installed ? 1 : 0);
        out.u8(status.device_enabled ? 1 : 0);
        out.u8(status.configured ? 1 : 0);
        out.u8(0); // reserved
        out.u32(status.width);
        out.u32(status.height);
        out.u32(status.refresh_millihertz);
    });
}

Expected<Request, DecodeError> decode_request(std::span<const std::uint8_t> message) {
    using Result = Expected<Request, DecodeError>;
    Reader reader(message);
    const auto header = read_header(reader, message.size());
    if (!header) {
        return Result::failure(header.error());
    }

    Request request;
    switch (static_cast<Command>(header.value().code)) {
    case Command::Enable:
    case Command::Disable:
    case Command::Status:
        if (header.value().payload_length != 0) {
            return Result::failure(DecodeError::LengthMismatch);
        }
        request.command = static_cast<Command>(header.value().code);
        return request;
    case Command::Configure: {
        if (header.value().payload_length < kConfigureFixedSize) {
            return Result::failure(DecodeError::Truncated);
        }
        const auto width = reader.u32();
        const auto height = reader.u32();
        const auto refresh = reader.u32();
        const auto name_length = reader.u16();
        if (!width || !height || !refresh || !name_length) {
            return Result::failure(DecodeError::Truncated);
        }
        if (*name_length != reader.remaining()) {
            return Result::failure(DecodeError::LengthMismatch);
        }
        if (*name_length > kMaxGpuNameBytes) {
            return Result::failure(DecodeError::InvalidGpuName);
        }
        const auto name = reader.text(*name_length);
        if (!name) {
            return Result::failure(DecodeError::Truncated);
        }
        request.command = Command::Configure;
        request.config = MonitorConfig{
            .width = *width, .height = *height, .refresh_millihertz = *refresh, .gpu_name = *name};
        if (!is_valid_utf8_without_controls(request.config.gpu_name)) {
            return Result::failure(DecodeError::InvalidGpuName);
        }
        if (!is_valid(request.config)) {
            return Result::failure(DecodeError::ValueOutOfRange);
        }
        return request;
    }
    }
    return Result::failure(DecodeError::UnknownCommand);
}

Expected<Response, DecodeError> decode_response(std::span<const std::uint8_t> message) {
    using Result = Expected<Response, DecodeError>;
    Reader reader(message);
    const auto header = read_header(reader, message.size());
    if (!header) {
        return Result::failure(header.error());
    }
    if (header.value().code > static_cast<std::uint16_t>(ResultCode::NotConfigured)) {
        return Result::failure(DecodeError::ValueOutOfRange);
    }
    if (header.value().payload_length != kStatusPayloadSize) {
        return Result::failure(DecodeError::LengthMismatch);
    }

    const auto installed = reader.u8();
    const auto enabled = reader.u8();
    const auto configured = reader.u8();
    const auto reserved = reader.u8();
    const auto width = reader.u32();
    const auto height = reader.u32();
    const auto refresh = reader.u32();
    if (!installed || !enabled || !configured || !reserved || !width || !height || !refresh) {
        return Result::failure(DecodeError::Truncated);
    }
    return Response{
        .result = static_cast<ResultCode>(header.value().code),
        .status = {.vdd_installed = *installed != 0,
                   .device_enabled = *enabled != 0,
                   .configured = *configured != 0,
                   .width = *width,
                   .height = *height,
                   .refresh_millihertz = *refresh},
    };
}

ResultCode result_for(DecodeError error) {
    switch (error) {
    case DecodeError::UnsupportedVersion:
        return ResultCode::UnsupportedVersion;
    case DecodeError::UnknownCommand:
        return ResultCode::UnknownCommand;
    case DecodeError::ValueOutOfRange:
    case DecodeError::InvalidGpuName:
        return ResultCode::ValueOutOfRange;
    case DecodeError::Truncated:
    case DecodeError::TooLarge:
    case DecodeError::BadMagic:
    case DecodeError::LengthMismatch:
        return ResultCode::MalformedRequest;
    }
    return ResultCode::MalformedRequest;
}

} // namespace osplay::win::helper
