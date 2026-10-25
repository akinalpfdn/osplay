#pragma once

#include <cstdint>

namespace osplay::helper {

inline constexpr const wchar_t* kServiceName = L"OsplayHelper";
inline constexpr const wchar_t* kServiceDisplayName = L"Osplay Helper";
inline constexpr const wchar_t* kServiceDescription =
    L"Adds and removes the Osplay virtual monitors while a display session is active.";

inline constexpr const wchar_t* kVddRegistryKey = L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver";
inline constexpr const wchar_t* kVddRegistryPathValue = L"VDDPATH";
inline constexpr const wchar_t* kVddDefaultDirectory = L"C:\\VirtualDisplayDriver";
inline constexpr const wchar_t* kVddSettingsFileName = L"vdd_settings.xml";

inline constexpr std::uint64_t kMaxLogFileBytes = 1024ULL * 1024ULL;
inline constexpr std::uint32_t kNoConsoleUserRetryMilliseconds = 2000;
inline constexpr std::uint32_t kServiceRestartDelayMilliseconds = 5000;
inline constexpr std::uint32_t kServiceStopTimeoutMilliseconds = 10000;
/// A device's UMDF host settings, under its Device Parameters key (WDF's WUDF_SUB_KEY).
inline constexpr const wchar_t* kWudfSubKey = L"WUDF";
/// Written by the INF directive UmdfHostProcessSharing; read when the device starts.
inline constexpr const wchar_t* kHostProcessSharingValue = L"HostProcessSharingEnabled";
/// WdfProcessSharingDisabled: the device gets a driver host process of its own.
inline constexpr std::uint32_t kHostProcessSharingDisabled = 0;
/// Right after the driver is installed, SetupAPI property changes can fail transiently.
inline constexpr int kDeviceChangeAttempts = 3;
inline constexpr std::uint32_t kDeviceChangeRetryDelayMilliseconds = 500;

} // namespace osplay::helper
