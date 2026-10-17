#pragma once

#include <osplay/win/helper_request_handler.hpp>

#include <filesystem>
#include <mutex>
#include <string_view>

namespace osplay::helper {

/// Appends timestamped lines to %ProgramData%\Osplay\logs\helper.log, keeping one rotated file.
class FileLog final : public win::helper::HelperLog {
public:
    FileLog();

    void info(std::string_view message) override { write("INFO", message); }
    void warning(std::string_view message) override { write("WARN", message); }
    void error(std::string_view message) override { write("ERROR", message); }

private:
    void write(std::string_view level, std::string_view message);

    std::filesystem::path path_;
    std::mutex mutex_;
};

} // namespace osplay::helper
