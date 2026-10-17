#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <array>
#include <span>

namespace {

using osplay::protocol::Error;

static_assert(static_cast<int>(Error::InvalidArgument) == OSPLAY_ERROR_INVALID_ARGUMENT);
static_assert(static_cast<int>(Error::BufferTooSmall) == OSPLAY_ERROR_BUFFER_TOO_SMALL);
static_assert(static_cast<int>(Error::Truncated) == OSPLAY_ERROR_TRUNCATED);
static_assert(static_cast<int>(Error::BadMagic) == OSPLAY_ERROR_BAD_MAGIC);
static_assert(static_cast<int>(Error::UnsupportedVersion) == OSPLAY_ERROR_UNSUPPORTED_VERSION);
static_assert(static_cast<int>(Error::UnknownType) == OSPLAY_ERROR_UNKNOWN_TYPE);
static_assert(static_cast<int>(Error::TrailingBytes) == OSPLAY_ERROR_TRAILING_BYTES);
static_assert(static_cast<int>(Error::ValueOutOfRange) == OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
static_assert(static_cast<int>(Error::InvalidString) == OSPLAY_ERROR_INVALID_STRING);
static_assert(static_cast<int>(Error::TooLarge) == OSPLAY_ERROR_TOO_LARGE);
static_assert(OSPLAY_PROTOCOL_VERSION == osplay::protocol::kVersion);
static_assert(OSPLAY_MAX_CONTROL_MESSAGE_SIZE == osplay::protocol::kMaxControlMessageSize);
static_assert(OSPLAY_MAX_MEDIA_PLAINTEXT_SIZE == osplay::protocol::kMaxMediaPlaintextSize);

osplay_result to_result(Error error) {
    return static_cast<osplay_result>(error);
}

bool valid_input(osplay_wire_kind kind, const std::uint8_t* data, std::size_t length) {
    const bool known_kind = kind == OSPLAY_WIRE_CONTROL_MESSAGE || kind == OSPLAY_WIRE_MEDIA_PACKET;
    return known_kind && (data != nullptr || length == 0);
}

std::span<const std::uint8_t> as_span(const std::uint8_t* data, std::size_t length) {
    return length == 0 ? std::span<const std::uint8_t>{}
                       : std::span<const std::uint8_t>(data, length);
}

} // namespace

extern "C" {

osplay_result osplay_protocol_validate(osplay_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out_type) {
    if (!valid_input(kind, data, length) || out_type == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    const auto input = as_span(data, length);
    if (kind == OSPLAY_WIRE_CONTROL_MESSAGE) {
        const auto message = osplay::protocol::decode_control(input);
        if (!message) {
            return to_result(message.error());
        }
        *out_type = static_cast<std::uint8_t>(osplay::protocol::type_of(message.value()));
        return OSPLAY_OK;
    }
    const auto packet = osplay::protocol::decode_media(input);
    if (!packet) {
        return to_result(packet.error());
    }
    *out_type = static_cast<std::uint8_t>(osplay::protocol::type_of(packet.value()));
    return OSPLAY_OK;
}

osplay_result osplay_protocol_reencode(osplay_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out, size_t capacity, size_t* out_length) {
    if (!valid_input(kind, data, length) || (out == nullptr && capacity > 0) ||
        out_length == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    const auto input = as_span(data, length);
    const std::span<std::uint8_t> output =
        capacity == 0 ? std::span<std::uint8_t>{} : std::span<std::uint8_t>(out, capacity);

    if (kind == OSPLAY_WIRE_CONTROL_MESSAGE) {
        const auto message = osplay::protocol::decode_control(input);
        if (!message) {
            return to_result(message.error());
        }
        const auto encoded = osplay::protocol::encode_control(message.value());
        if (!encoded) {
            return to_result(encoded.error());
        }
        if (encoded.value().size() > output.size()) {
            return OSPLAY_ERROR_BUFFER_TOO_SMALL;
        }
        std::ranges::copy(encoded.value(), output.begin());
        *out_length = encoded.value().size();
        return OSPLAY_OK;
    }
    const auto packet = osplay::protocol::decode_media(input);
    if (!packet) {
        return to_result(packet.error());
    }
    const auto size = osplay::protocol::encode_media(packet.value(), output);
    if (!size) {
        return to_result(size.error());
    }
    *out_length = size.value();
    return OSPLAY_OK;
}

} // extern "C"
