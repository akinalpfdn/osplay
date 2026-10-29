#pragma once

// Android Open Accessory (AOA 1.0/2.0), the part Osplay uses: a computer, as USB host, switches an
// Android device into accessory mode with three vendor control requests on endpoint 0; the device
// comes back as Google's accessory device with one interface of two bulk endpoints, and the
// Android app that claims Osplay's identity reads and writes them (PROTOCOL.md "Android accessory
// links"). The platform performs the transfers; this file only says which.

#include "util/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace osplay::transport::aoa {

inline constexpr std::uint16_t kGoogleVendorId = 0x18D1;
inline constexpr std::uint16_t kAccessoryProductId = 0x2D00;
inline constexpr std::uint16_t kAccessoryAdbProductId = 0x2D01; ///< with USB debugging on

/// bmRequestType of the vendor requests: to and from the device.
inline constexpr std::uint8_t kRequestTypeOut = 0x40;
inline constexpr std::uint8_t kRequestTypeIn = 0xC0;
inline constexpr std::uint8_t kGetProtocol = 51;
inline constexpr std::uint8_t kSendString = 52;
inline constexpr std::uint8_t kStart = 53;

/// Who Osplay is to Android, in SEND_STRING order. The Android app's accessory filter
/// (res/xml/accessory_filter.xml) matches the manufacturer and the model.
inline constexpr std::array<std::string_view, 6> kIdentity = {
    "Osplay",                                    // manufacturer
    "Osplay Display",                            // model
    "Shows a Mac or a PC screen on this device", // description
    "1",                                         // version
    "https://github.com/akinalpfdn/osplay",      // URI Android offers when no app claims it
    "1",                                         // serial
};

/// The largest transfer on the accessory's bulk endpoints: Android's accessory driver moves at
/// most 16 KiB at a time.
inline constexpr std::size_t kTransferSize = std::size_t{16} * 1024;

struct ControlRequest {
    std::uint8_t request_type = kRequestTypeOut;
    std::uint8_t request = 0;
    std::uint16_t value = 0;
    std::uint16_t index = 0;
    /// OUT: the data stage. IN: as many zero bytes as the device may return.
    std::vector<std::uint8_t> data;
};

[[nodiscard]] bool is_accessory(std::uint16_t vendor_id, std::uint16_t product_id);

/// GET_PROTOCOL: the device answers with a 16-bit little-endian version, 0 without AOA.
[[nodiscard]] ControlRequest protocol_request();

/// The AOA version the device answered with; fails for fewer than two bytes or version 0.
[[nodiscard]] Expected<std::uint16_t, Error> parse_protocol(std::span<const std::uint8_t> answer);

/// SEND_STRING for each identity string (zero-terminated), then START.
[[nodiscard]] std::vector<ControlRequest> start_requests();

} // namespace osplay::transport::aoa
