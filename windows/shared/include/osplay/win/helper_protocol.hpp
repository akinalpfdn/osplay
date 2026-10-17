#pragma once

// Wire format between the user-level engine and the privileged OsplayHelper service.
// Little-endian, fixed layouts, one request and one response per named pipe message.
// Field-by-field description: docs/ARCHITECTURE.md → "OsplayHelper pipe protocol".

#include <osplay/win/expected.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace osplay::win::helper {

inline constexpr std::uint32_t kMagic = 0x4850'534F; // "OSPH" as little-endian bytes
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 12;
inline constexpr std::size_t kMaxMessageSize = 512;
inline constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\OsplayHelper";

// Bounds for virtual monitor settings; anything outside is rejected before it reaches the driver.
inline constexpr std::uint32_t kMinWidth = 640;
inline constexpr std::uint32_t kMaxWidth = 7680;
inline constexpr std::uint32_t kMinHeight = 480;
inline constexpr std::uint32_t kMaxHeight = 4320;
inline constexpr std::uint32_t kMinRefreshMillihertz = 24'000;
inline constexpr std::uint32_t kMaxRefreshMillihertz = 240'000;
inline constexpr std::size_t kMaxGpuNameBytes = 128;

enum class Command : std::uint16_t {
    Configure = 1,
    Enable = 2,
    Disable = 3,
    Status = 4,
};

enum class ResultCode : std::uint16_t {
    Ok = 0,
    MalformedRequest = 1,
    UnsupportedVersion = 2,
    UnknownCommand = 3,
    ValueOutOfRange = 4,
    VddNotInstalled = 5,
    DeviceControlFailed = 6,
    ConfigWriteFailed = 7,
    NotConfigured = 8,
};

enum class DecodeError : std::uint8_t {
    Truncated,
    TooLarge,
    BadMagic,
    UnsupportedVersion,
    UnknownCommand,
    LengthMismatch,
    ValueOutOfRange,
    InvalidGpuName,
};

/// Settings for the single virtual monitor. An empty GPU name lets the driver choose.
struct MonitorConfig {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t refresh_millihertz = 0;
    std::string gpu_name;

    bool operator==(const MonitorConfig&) const = default;
};

struct Request {
    Command command = Command::Status;
    MonitorConfig config; // meaningful for Command::Configure only

    bool operator==(const Request&) const = default;
};

struct MonitorStatus {
    bool vdd_installed = false;
    bool device_enabled = false;
    bool configured = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t refresh_millihertz = 0;

    bool operator==(const MonitorStatus&) const = default;
};

struct Response {
    ResultCode result = ResultCode::Ok;
    MonitorStatus status; // filled for Command::Status

    bool operator==(const Response&) const = default;
};

using Bytes = std::vector<std::uint8_t>;

/// True when every value is within the documented bounds and the GPU name is clean UTF-8.
[[nodiscard]] bool is_valid(const MonitorConfig& config);

[[nodiscard]] Bytes encode(const Request& request);
[[nodiscard]] Bytes encode(const Response& response);
[[nodiscard]] Expected<Request, DecodeError> decode_request(std::span<const std::uint8_t> message);
[[nodiscard]] Expected<Response, DecodeError>
decode_response(std::span<const std::uint8_t> message);

/// The response code a server sends back for a request it could not decode.
[[nodiscard]] ResultCode result_for(DecodeError error);

} // namespace osplay::win::helper
