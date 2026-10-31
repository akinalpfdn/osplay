#include "transport/byte_pipe.hpp"

#ifndef _WIN32
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <mutex>
#include <optional>
#include <thread>
#endif

namespace osplay::transport {

#ifndef _WIN32

namespace {

/// Interrupts a blocked accessory read or write: Android's accessory driver waits interruptibly,
/// but neither a zero-length packet nor a pulled cable ends that wait.
constexpr int kWakeSignal = SIGURG;
constexpr int kWakeAttempts = 50;
constexpr auto kWakeInterval = std::chrono::milliseconds(10);

extern "C" void ignore_wake_signal(int /*signal*/) {}

/// A handler without SA_RESTART, so the blocked call returns EINTR; installed once per process.
void install_wake_handler() {
    static std::once_flag installed;
    std::call_once(installed, [] {
        struct sigaction action{};
        action.sa_handler = ignore_wake_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(kWakeSignal, &action, nullptr);
    });
}

class FdPipe final : public BytePipe {
public:
    explicit FdPipe(int fd) : fd_(fd), socket_(is_socket(fd)) {
        if (!socket_) {
            install_wake_handler();
        }
    }

    ~FdPipe() override { ::close(fd_); }
    FdPipe(const FdPipe&) = delete;
    FdPipe& operator=(const FdPipe&) = delete;
    FdPipe(FdPipe&&) = delete;
    FdPipe& operator=(FdPipe&&) = delete;

    Expected<std::size_t, Error> read(std::span<std::uint8_t> buffer) override {
        for (;;) {
            if (!enter(reader_)) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
            const auto size = ::read(fd_, buffer.data(), buffer.size());
            const int error = errno;
            leave(reader_);
            if (size > 0) {
                return static_cast<std::size_t>(size);
            }
            if (size == 0) {
                if (socket_) {
                    return Expected<std::size_t, Error>::failure(Error::ConnectionClosed);
                }
                return std::size_t{0}; // a zero-length packet
            }
            if (error != EINTR) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
        }
    }

    Expected<std::size_t, Error> write(std::span<const std::uint8_t> data) override {
        for (;;) {
            if (!enter(writer_)) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
            const auto size = ::write(fd_, data.data(), data.size());
            const int error = errno;
            leave(writer_);
            if (size >= 0) {
                return static_cast<std::size_t>(size);
            }
            if (error != EINTR) {
                return Expected<std::size_t, Error>::failure(Error::NetworkError);
            }
        }
    }

    void cancel() override {
        {
            const std::scoped_lock lock(mutex_);
            cancelled_ = true;
        }
        if (socket_) {
            ::shutdown(fd_, SHUT_RDWR);
            return;
        }
        // Signalled until both have left the call: one that was just entering it may miss the
        // first signal and block.
        for (int attempt = 0; attempt < kWakeAttempts; ++attempt) {
            {
                const std::scoped_lock lock(mutex_);
                if (!reader_ && !writer_) {
                    return;
                }
                for (const auto& thread : {reader_, writer_}) {
                    if (thread) {
                        pthread_kill(*thread, kWakeSignal);
                    }
                }
            }
            std::this_thread::sleep_for(kWakeInterval);
        }
    }

private:
    static bool is_socket(int fd) {
        struct stat status{};
        return ::fstat(fd, &status) == 0 && S_ISSOCK(status.st_mode);
    }

    /// Records the calling thread before a blocking call; false after cancel.
    bool enter(std::optional<pthread_t>& slot) {
        const std::scoped_lock lock(mutex_);
        if (cancelled_) {
            return false;
        }
        slot = pthread_self();
        return true;
    }

    void leave(std::optional<pthread_t>& slot) {
        const std::scoped_lock lock(mutex_);
        slot.reset();
    }

    int fd_;
    bool socket_;
    std::mutex mutex_;
    bool cancelled_ = false;
    std::optional<pthread_t> reader_; // threads inside read() and write(), to signal
    std::optional<pthread_t> writer_;
};

} // namespace

std::unique_ptr<BytePipe> make_fd_pipe(int fd) {
    return std::make_unique<FdPipe>(fd);
}

#endif

} // namespace osplay::transport
