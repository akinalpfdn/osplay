#include "host/stream_dump.hpp"

#include <system_error>
#include <utility>

namespace osplay::engine {

StreamDump::StreamDump(std::filesystem::path directory, std::string name)
    : directory_(std::move(directory)), name_(std::move(name)) {}

void StreamDump::write(std::span<const std::uint8_t> annexb) {
    const std::scoped_lock lock(mutex_);
    if (failed_) {
        return;
    }
    if (!file_.is_open()) {
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        file_.open(path(), std::ios::binary | std::ios::trunc);
        if (!file_) {
            failed_ = true;
            return;
        }
    }
    file_.write(reinterpret_cast<const char*>(annexb.data()),
                static_cast<std::streamsize>(annexb.size()));
    failed_ = !file_;
}

} // namespace osplay::engine
