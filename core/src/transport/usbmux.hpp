#pragma once

// Apple's usbmuxd protocol, the part Osplay uses: list the iPads on a USB cable and open a TCP
// stream to a port on one of them. usbmuxd is part of macOS; on Windows the Apple Mobile Device
// Service (installed with Apple Devices or iTunes) speaks the same protocol on 127.0.0.1:27015.
// A message is a 16-byte little-endian header (length, version 1, type 8 = plist, tag) followed by
// an XML property list.

#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace osplay::transport::usbmux {

inline constexpr std::size_t kHeaderSize = 16;
/// Device lists are a few kilobytes; anything far larger is not usbmuxd.
inline constexpr std::size_t kMaxPlistSize = std::size_t{256} * 1024;
#ifdef _WIN32
inline constexpr std::uint16_t kWindowsServicePort = 27015;
#else
inline constexpr std::string_view kSocketPath = "/var/run/usbmuxd";
#endif

/// Result numbers usbmuxd answers a request with.
inline constexpr std::int64_t kResultOk = 0;
inline constexpr std::int64_t kResultConnectionRefused = 3;

/// Asks usbmuxd to report attached devices now and on every change.
[[nodiscard]] std::vector<std::uint8_t> listen_request(std::uint32_t tag);
/// Asks usbmuxd to connect this socket to `port` on the device; after a Result of kResultOk the
/// socket is a byte stream to that port.
[[nodiscard]] std::vector<std::uint8_t> connect_request(std::uint32_t device_id, std::uint16_t port,
                                                        std::uint32_t tag);

/// The length of the plist that follows a header, or an error for anything but a plist message.
[[nodiscard]] Expected<std::size_t, Error>
plist_size(std::span<const std::uint8_t, kHeaderSize> header);

struct Result {
    std::int64_t number = 0;
};
struct Attached {
    std::uint32_t device_id = 0;
    bool usb = false; ///< false for a device usbmuxd reaches over Wi-Fi
    std::string serial_number;
};
struct Detached {
    std::uint32_t device_id = 0;
};
/// A message Osplay has no use for (e.g. "Paired").
struct Ignored {};
using Message = std::variant<Result, Attached, Detached, Ignored>;

[[nodiscard]] Expected<Message, Error> parse_message(std::string_view plist);

} // namespace osplay::transport::usbmux
