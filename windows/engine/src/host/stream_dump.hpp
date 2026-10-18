#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>

namespace osplay::engine {

/// Debug option: appends the stream's Annex B output to a raw `.h264` file (playable with VLC,
/// ffplay or mpv). Thread-safe.
class StreamDump {
public:
    /// The file is created lazily as `<directory>/<name>`.
    StreamDump(std::filesystem::path directory, std::string name);

    void write(std::span<const std::uint8_t> annexb);
    [[nodiscard]] std::filesystem::path path() const { return directory_ / name_; }

private:
    std::filesystem::path directory_;
    std::string name_;
    std::mutex mutex_;
    std::ofstream file_;
    bool failed_ = false;
};

} // namespace osplay::engine
