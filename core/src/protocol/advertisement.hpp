#pragma once

// The Bonjour TXT record (PROTOCOL.md "Discovery"): one implementation of its format and checks
// for both platforms' DNS-SD code.

#include "protocol/control_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace osplay::protocol {

inline constexpr std::string_view kServiceType = "_osplay._tcp";
inline constexpr std::size_t kDeviceIdSize = 16;

/// Role bits of Advertisement::roles.
inline constexpr std::uint8_t kRoleHost = 1U;
inline constexpr std::uint8_t kRoleViewer = 2U;

struct Advertisement {
    std::uint32_t protocol_version = 0;
    /// 0 when the record has no "pm" key: Osplay 0.1 (protocol 1.1 and older), which refuses
    /// the iPadOS and Android platforms.
    std::uint8_t protocol_minor = 0;
    Platform platform = Platform::MacOS;
    std::uint8_t roles = 0; ///< kRole* bits, at least one
    std::array<std::uint8_t, kDeviceIdSize> device_id{};
    std::string name;
};

/// The TXT values in key order pv, name, id, roles, os, pm; pm may be empty (protocol 1.1 and
/// older did not send it).
struct TxtValues {
    std::string protocol_version;
    std::string name;
    std::string device_id;
    std::string roles;
    std::string os;
    std::string protocol_minor;
};

/// InvalidString for a bad name, ValueOutOfRange for anything else invalid.
[[nodiscard]] Expected<TxtValues, Error> to_txt(const Advertisement& advertisement);

/// Parses a peer's record. A different protocol version is returned, not refused, so the UI can
/// say "update the other device".
[[nodiscard]] Expected<Advertisement, Error> from_txt(const TxtValues& values);

} // namespace osplay::protocol
