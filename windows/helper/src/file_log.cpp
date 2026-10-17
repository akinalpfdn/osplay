#include "file_log.hpp"

#include "helper_constants.hpp"

#include <shlobj.h>
#include <windows.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

namespace osplay::helper {
namespace {

std::filesystem::path log_directory() {
    PWSTR program_data = nullptr;
    std::filesystem::path directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &program_data))) {
        directory = std::filesystem::path(program_data) / L"Osplay" / L"logs";
    }
    CoTaskMemFree(program_data);
    return directory;
}

std::string utc_timestamp() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear,
                  now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return text.data();
}

} // namespace

FileLog::FileLog() {
    const std::filesystem::path directory = log_directory();
    if (directory.empty()) {
        return;
    }
    std::error_code ignored; // Logging must never stop the service; failures leave path_ empty.
    std::filesystem::create_directories(directory, ignored);
    path_ = directory / L"helper.log";
}

void FileLog::write(std::string_view level, std::string_view message) {
    const std::string line =
        utc_timestamp() + " " + std::string(level) + " " + std::string(message) + "\n";
    OutputDebugStringA(line.c_str());
    if (path_.empty()) {
        return;
    }

    const std::lock_guard lock(mutex_);
    std::error_code size_error;
    if (std::filesystem::file_size(path_, size_error) > kMaxLogFileBytes && !size_error) {
        std::error_code ignored;
        std::filesystem::rename(path_, std::filesystem::path(path_).concat(L".1"), ignored);
    }
    std::ofstream file(path_, std::ios::app | std::ios::binary);
    file << line;
}

} // namespace osplay::helper
