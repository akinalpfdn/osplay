#include "vdd_settings_file.hpp"

#include "helper_constants.hpp"

#include <windows.h>

#include <array>
#include <filesystem>
#include <string>

namespace osplay::helper {
namespace {

std::filesystem::path settings_directory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    auto size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kVddRegistryKey, kVddRegistryPathValue, RRF_RT_REG_SZ,
                     nullptr, buffer.data(), &size) == ERROR_SUCCESS &&
        buffer[0] != L'\0') {
        return std::filesystem::path(buffer.data());
    }
    return std::filesystem::path(kVddDefaultDirectory);
}

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};

} // namespace

bool VddSettingsFile::write(const std::string& contents) {
    const std::filesystem::path directory = settings_directory();
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    if (directory_error) {
        log_.error("Cannot create the driver settings directory: " + directory_error.message());
        return false;
    }

    const std::filesystem::path target = directory / kVddSettingsFileName;
    const std::filesystem::path temporary = std::filesystem::path(target).concat(L".osplay.tmp");
    {
        const std::unique_ptr<void, HandleCloser> file(CreateFileW(temporary.c_str(), GENERIC_WRITE,
                                                                   0, nullptr, CREATE_ALWAYS,
                                                                   FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) {
            log_.error("Cannot create the temporary settings file, error " +
                       std::to_string(GetLastError()));
            return false;
        }
        DWORD written = 0;
        if (!WriteFile(file.get(), contents.data(), static_cast<DWORD>(contents.size()), &written,
                       nullptr) ||
            written != contents.size() || !FlushFileBuffers(file.get())) {
            log_.error("Cannot write the temporary settings file, error " +
                       std::to_string(GetLastError()));
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log_.error("Cannot replace the driver settings file, error " +
                   std::to_string(GetLastError()));
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace osplay::helper
