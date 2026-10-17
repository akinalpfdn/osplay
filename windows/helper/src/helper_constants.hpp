#pragma once

#include <cstdint>

namespace osplay::helper {

inline constexpr const wchar_t* kServiceName = L"OsplayHelper";
inline constexpr const wchar_t* kServiceDisplayName = L"Osplay Helper";
inline constexpr const wchar_t* kServiceDescription =
    L"Adds and removes the Osplay virtual monitor while a display session is active.";

/// Hardware ID the Virtual Display Driver installs its root-enumerated device with (MttVDD.inf).
inline constexpr const wchar_t* kVddHardwareId = L"Root\\MttVDD";
inline constexpr const wchar_t* kVddRegistryKey = L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver";
inline constexpr const wchar_t* kVddRegistryPathValue = L"VDDPATH";
inline constexpr const wchar_t* kVddDefaultDirectory = L"C:\\VirtualDisplayDriver";
inline constexpr const wchar_t* kVddSettingsFileName = L"vdd_settings.xml";

inline constexpr std::uint64_t kMaxLogFileBytes = 1024ULL * 1024ULL;
inline constexpr std::uint32_t kNoConsoleUserRetryMilliseconds = 2000;
inline constexpr std::uint32_t kServiceRestartDelayMilliseconds = 5000;
inline constexpr std::uint32_t kServiceStopTimeoutMilliseconds = 10000;

} // namespace osplay::helper
