#include "protocol/advertisement.hpp"

#include "protocol/constants.hpp"
#include "protocol/text.hpp"

#include <charconv>
#include <span>
#include <system_error>

namespace osplay::protocol {
namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";

bool valid_name(std::string_view name) {
    return !name.empty() && name.size() <= kMaxStringBytes &&
           is_valid_text(
               std::span(reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
}

std::string_view platform_text(Platform platform) {
    return platform == Platform::Windows ? "windows" : "macos";
}

std::string roles_text(std::uint8_t roles) {
    if (roles == (kRoleHost | kRoleViewer)) {
        return "host,viewer";
    }
    return roles == kRoleHost ? "host" : "viewer";
}

int hex_value(char c) {
    const auto position = kHexDigits.find(c);
    return position == std::string_view::npos ? -1 : static_cast<int>(position);
}

} // namespace

Expected<TxtValues, Error> to_txt(const Advertisement& advertisement) {
    using Result = Expected<TxtValues, Error>;
    const bool roles_ok =
        advertisement.roles != 0 && (advertisement.roles & ~(kRoleHost | kRoleViewer)) == 0;
    const bool platform_ok =
        advertisement.platform == Platform::MacOS || advertisement.platform == Platform::Windows;
    if (advertisement.protocol_version == 0 || !roles_ok || !platform_ok) {
        return Result::failure(Error::ValueOutOfRange);
    }
    if (!valid_name(advertisement.name)) {
        return Result::failure(Error::InvalidString);
    }
    TxtValues values;
    values.protocol_version = std::to_string(advertisement.protocol_version);
    values.name = advertisement.name;
    for (const std::uint8_t byte : advertisement.device_id) {
        values.device_id.push_back(kHexDigits.at(byte >> 4U));
        values.device_id.push_back(kHexDigits.at(byte & 0x0FU));
    }
    values.roles = roles_text(advertisement.roles);
    values.os = std::string(platform_text(advertisement.platform));
    return values;
}

Expected<Advertisement, Error> from_txt(const TxtValues& values) {
    using Result = Expected<Advertisement, Error>;
    Advertisement advertisement;
    const std::string_view version = values.protocol_version;
    const auto [end, error] = std::from_chars(version.data(), version.data() + version.size(),
                                              advertisement.protocol_version);
    if (version.empty() || error != std::errc{} || end != version.data() + version.size() ||
        advertisement.protocol_version == 0) {
        return Result::failure(Error::ValueOutOfRange);
    }
    if (!valid_name(values.name)) {
        return Result::failure(Error::InvalidString);
    }
    advertisement.name = values.name;
    if (values.device_id.size() != kDeviceIdSize * 2) {
        return Result::failure(Error::ValueOutOfRange);
    }
    for (std::size_t i = 0; i < kDeviceIdSize; ++i) {
        const int high = hex_value(values.device_id.at(i * 2));
        const int low = hex_value(values.device_id.at((i * 2) + 1));
        if (high < 0 || low < 0) {
            return Result::failure(Error::ValueOutOfRange);
        }
        advertisement.device_id.at(i) = static_cast<std::uint8_t>((high << 4) | low);
    }
    if (values.roles == "host") {
        advertisement.roles = kRoleHost;
    } else if (values.roles == "viewer") {
        advertisement.roles = kRoleViewer;
    } else if (values.roles == "host,viewer") {
        advertisement.roles = kRoleHost | kRoleViewer;
    } else {
        return Result::failure(Error::ValueOutOfRange);
    }
    if (values.os == "macos") {
        advertisement.platform = Platform::MacOS;
    } else if (values.os == "windows") {
        advertisement.platform = Platform::Windows;
    } else {
        return Result::failure(Error::ValueOutOfRange);
    }
    return advertisement;
}

} // namespace osplay::protocol
