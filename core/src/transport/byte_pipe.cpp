#include "transport/byte_pipe.hpp"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace osplay::transport {

#ifndef _WIN32

namespace {

class FdPipe final : public BytePipe {
public:
    explicit FdPipe(int fd) : fd_(fd), socket_(is_socket(fd)) {}

    ~FdPipe() override { ::close(fd_); }
    FdPipe(const FdPipe&) = delete;
    FdPipe& operator=(const FdPipe&) = delete;
    FdPipe(FdPipe&&) = delete;
    FdPipe& operator=(FdPipe&&) = delete;

    Expected<std::size_t, Error> read(std::span<std::uint8_t> buffer) override {
        for (;;) {
            const auto size = ::read(fd_, buffer.data(), buffer.size());
            if (size > 0) {
                return static_cast<std::size_t>(size);
            }
            if (size == 0) {
                if (socket_) {
                    return Expected<std::size_t, Error>::failure(Error::ConnectionClosed);
                }
                return std::size_t{0}; // a zero-length packet
            }
            if (errno != EINTR) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
        }
    }

    Expected<std::size_t, Error> write(std::span<const std::uint8_t> data) override {
        for (;;) {
            const auto size = ::write(fd_, data.data(), data.size());
            if (size >= 0) {
                return static_cast<std::size_t>(size);
            }
            if (errno != EINTR) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
        }
    }

    void cancel() override {
        if (socket_) {
            ::shutdown(fd_, SHUT_RDWR);
        }
    }

private:
    static bool is_socket(int fd) {
        struct stat status{};
        return ::fstat(fd, &status) == 0 && S_ISSOCK(status.st_mode);
    }

    int fd_;
    bool socket_;
};

} // namespace

std::unique_ptr<BytePipe> make_fd_pipe(int fd) {
    return std::make_unique<FdPipe>(fd);
}

#endif

} // namespace osplay::transport
