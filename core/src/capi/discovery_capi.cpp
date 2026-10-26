#include "protocol/advertisement.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace {

namespace protocol = osplay::protocol;

static_assert(OSPLAY_DEVICE_ID_SIZE == protocol::kDeviceIdSize);
static_assert(OSPLAY_PROTOCOL_MINOR == protocol::kMinorVersion);
static_assert(OSPLAY_ROLE_HOST == protocol::kRoleHost &&
              OSPLAY_ROLE_VIEWER == protocol::kRoleViewer);

/// A NUL-terminated value of at most OSPLAY_TXT_VALUE_SIZE - 1 bytes; anything longer is cut,
/// which the checks then reject.
std::string read_value(std::span<const char, OSPLAY_TXT_VALUE_SIZE> value) {
    return {value.begin(), std::ranges::find(value, '\0')};
}

void write_value(const std::string& text, std::span<char, OSPLAY_TXT_VALUE_SIZE> out) {
    const std::size_t length = std::min(text.size(), out.size() - 1);
    std::copy_n(text.data(), length, out.data());
    out[length] = '\0';
}

} // namespace

extern "C" {

osplay_result osplay_advertisement_to_txt(const osplay_advertisement* advertisement,
                                          osplay_txt_record* out_record) {
    if (advertisement == nullptr || out_record == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    protocol::Advertisement source;
    if (advertisement->platform < OSPLAY_PLATFORM_MACOS ||
        advertisement->platform > OSPLAY_PLATFORM_ANDROID) {
        return OSPLAY_ERROR_VALUE_OUT_OF_RANGE;
    }
    source.protocol_version = advertisement->protocol_version;
    source.protocol_minor = advertisement->protocol_minor;
    source.platform = static_cast<protocol::Platform>(advertisement->platform);
    source.roles = advertisement->roles;
    std::copy_n(std::begin(advertisement->device_id), source.device_id.size(),
                source.device_id.begin());
    // A name filling the whole array without a NUL is too long and fails the check.
    source.name = read_value(std::span<const char, OSPLAY_TXT_VALUE_SIZE>(advertisement->name));
    const auto values = protocol::to_txt(source);
    if (!values) {
        return static_cast<osplay_result>(values.error());
    }
    *out_record = osplay_txt_record{};
    write_value(values.value().protocol_version, out_record->protocol_version);
    write_value(values.value().name, out_record->name);
    write_value(values.value().device_id, out_record->device_id);
    write_value(values.value().roles, out_record->roles);
    write_value(values.value().os, out_record->os);
    write_value(values.value().protocol_minor, out_record->protocol_minor);
    return OSPLAY_OK;
}

osplay_result osplay_advertisement_from_txt(const osplay_txt_record* record,
                                            osplay_advertisement* out_advertisement) {
    if (record == nullptr || out_advertisement == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    const protocol::TxtValues values{
        .protocol_version = read_value(record->protocol_version),
        .name = read_value(record->name),
        .device_id = read_value(record->device_id),
        .roles = read_value(record->roles),
        .os = read_value(record->os),
        .protocol_minor = read_value(record->protocol_minor),
    };
    const auto parsed = protocol::from_txt(values);
    if (!parsed) {
        return static_cast<osplay_result>(parsed.error());
    }
    osplay_advertisement result{.protocol_version = parsed.value().protocol_version,
                                .platform = static_cast<osplay_platform>(parsed.value().platform),
                                .roles = parsed.value().roles,
                                .protocol_minor = parsed.value().protocol_minor,
                                .device_id = {},
                                .name = {}};
    std::ranges::copy(parsed.value().device_id, std::begin(result.device_id));
    write_value(parsed.value().name, result.name);
    *out_advertisement = result;
    return OSPLAY_OK;
}

} // extern "C"
