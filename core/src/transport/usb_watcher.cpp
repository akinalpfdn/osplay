#include "transport/usb_watcher.hpp"

#include "transport/usbmux.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#ifndef _WIN32
#include <asio/local/stream_protocol.hpp>
#endif
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <array>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace osplay::transport {

class UsbWatcher::Impl {
public:
    // A port on Windows, where clang-tidy rejects moving it; a path elsewhere.
    // NOLINTNEXTLINE(modernize-pass-by-value)
    Impl(UsbObserver& observer, const UsbmuxAddress& address, Microseconds retry_interval)
        : observer_(observer), address_(address), retry_interval_(retry_interval),
          work_(asio::make_work_guard(io_)), socket_(io_), timer_(io_) {
        asio::post(io_, [this] { connect(); });
        thread_ = std::thread([this] { io_.run(); });
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        asio::post(io_, [this] {
            stopped_ = true;
            asio::error_code ignored;
            socket_.close(ignored);
            timer_.cancel();
        });
        work_.reset();
        thread_.join();
    }

private:
#ifdef _WIN32
    using Socket = asio::ip::tcp::socket;
    [[nodiscard]] Socket::endpoint_type endpoint() const {
        return {asio::ip::address_v4::loopback(), address_.port};
    }
#else
    using Socket = asio::local::stream_protocol::socket;
    [[nodiscard]] Socket::endpoint_type endpoint() const { return {address_.path}; }
#endif

    void connect() {
        socket_.async_connect(endpoint(), [this](const asio::error_code& error) {
            if (stopped_) {
                return;
            }
            if (error) {
                retry();
                return;
            }
            request_ = usbmux::listen_request(1);
            asio::async_write(socket_, asio::buffer(request_),
                              [this](const asio::error_code& write_error, std::size_t) {
                                  if (stopped_) {
                                      return;
                                  }
                                  if (write_error) {
                                      lost();
                                  } else {
                                      read_message();
                                  }
                              });
        });
    }

    /// The Result of Listen first, then one message per device change.
    // Not recursion: each read starts from the previous one's completion handler.
    // NOLINTBEGIN(misc-no-recursion)
    void read_message() {
        asio::async_read(
            socket_, asio::buffer(header_), [this](const asio::error_code& error, std::size_t) {
                if (stopped_) {
                    return;
                }
                const auto size = usbmux::plist_size(header_);
                if (error || !size) {
                    lost();
                    return;
                }
                plist_.assign(size.value(), '\0');
                asio::async_read(socket_, asio::buffer(plist_),
                                 [this](const asio::error_code& read_error, std::size_t) {
                                     if (stopped_) {
                                         return;
                                     }
                                     const auto message = usbmux::parse_message(plist_);
                                     if (read_error || !message) {
                                         lost();
                                         return;
                                     }
                                     if (handle(message.value())) {
                                         read_message();
                                     }
                                 });
            });
    }

    // NOLINTEND(misc-no-recursion)

    /// False when the connection to usbmuxd is given up.
    bool handle(const usbmux::Message& message) {
        if (const auto* result = std::get_if<usbmux::Result>(&message)) {
            if (result->number != usbmux::kResultOk) {
                lost();
                return false;
            }
        } else if (const auto* attached = std::get_if<usbmux::Attached>(&message)) {
            if (attached->usb && devices_.insert(attached->device_id).second) {
                observer_.on_attached(attached->device_id);
            }
        } else if (const auto* detached = std::get_if<usbmux::Detached>(&message)) {
            if (devices_.erase(detached->device_id) > 0) {
                observer_.on_detached(detached->device_id);
            }
        }
        return true;
    }

    void lost() {
        asio::error_code ignored;
        socket_.close(ignored);
        for (const auto device : std::exchange(devices_, {})) {
            observer_.on_detached(device);
        }
        retry();
    }

    void retry() {
        timer_.expires_after(retry_interval_);
        timer_.async_wait([this](const asio::error_code& error) {
            if (!error && !stopped_) {
                connect();
            }
        });
    }

    UsbObserver& observer_;
    UsbmuxAddress address_;
    Microseconds retry_interval_;
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    Socket socket_;
    asio::steady_timer timer_;
    std::vector<std::uint8_t> request_;
    std::array<std::uint8_t, usbmux::kHeaderSize> header_{};
    std::string plist_;
    std::set<std::uint32_t> devices_;
    bool stopped_ = false;
    std::thread thread_;
};

UsbWatcher::UsbWatcher(UsbObserver& observer, const UsbmuxAddress& address,
                       Microseconds retry_interval)
    : impl_(std::make_unique<Impl>(observer, address, retry_interval)) {}

UsbWatcher::~UsbWatcher() = default;

} // namespace osplay::transport
