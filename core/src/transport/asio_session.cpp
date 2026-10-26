#include "transport/asio_session.hpp"

#include "protocol/constants.hpp"
#include "protocol/link_record.hpp"
#include "transport/traffic_class.hpp"
#include "transport/usbmux.hpp"

// CMake includes Asio as a system header; SwiftPM cannot, so its warnings are silenced here.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/connect.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/ip/v6_only.hpp>
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
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace osplay::transport {

namespace {

using asio::ip::tcp;
using asio::ip::udp;

constexpr int kSocketBufferBytes = 4 * 1024 * 1024; // room for a keyframe burst of fragments
constexpr std::size_t kReadBufferBytes = std::size_t{16} * 1024;
constexpr auto kShutdownGrace = std::chrono::seconds(1);
constexpr auto kCommandTimeout = std::chrono::seconds(5);
/// Stream link: media queued beyond this is dropped, as a full UDP socket buffer would drop it, so
/// a slow cable adds loss (which NACKs and the bitrate controller handle) instead of delay.
constexpr std::size_t kLinkMediaBudget = std::size_t{1024} * 1024;
/// Stream link: the "UDP port" in MediaSetup, which the viewer ignores (PROTOCOL.md).
constexpr std::uint16_t kLinkMediaPort = 1;

} // namespace

class AsioSession::Impl final : public session::SessionTransport {
public:
    Impl(session::SessionConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
         session::SessionObserver& observer, TransportOptions options)
        : work_(asio::make_work_guard(io_)), acceptor_(io_), control_(io_),
#ifndef _WIN32
          local_(io_),
#endif
          media_(io_), resolver_(io_), timer_(io_), connect_timer_(io_), options_(options),
          engine_(std::move(config), std::move(identity), random, observer, *this) {
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() override {
        try {
            stop();
        } catch (...) {
            // Only joining the I/O thread can throw, and a session that cannot stop its own thread
            // is unrecoverable; this makes the implicit terminate of a noexcept destructor
            // explicit.
            std::terminate();
        }
    }

    void set_tick_interval(Microseconds interval) { tick_interval_ = interval; }

    // --- Setup (before the I/O thread starts) ---

    Status<Error> listen() {
        asio::error_code error;
        if (!open_tcp_acceptor(error)) {
            return Status<Error>::failure(Error::NetworkError);
        }
        if (!open_udp(udp::v6(), error) && !open_udp(udp::v4(), error)) {
            return Status<Error>::failure(Error::NetworkError);
        }
        control_port_ = acceptor_.local_endpoint(error).port();
        if (!error) {
            media_port_ = media_.local_endpoint(error).port();
        }
        return error ? Status<Error>::failure(Error::NetworkError) : Status<Error>::ok();
    }

    void start_host() {
        acceptor_.async_accept(control_, [this](const asio::error_code& error) {
            if (error) {
                if (error != asio::error::operation_aborted) {
                    engine_.on_transport_closed(Error::NetworkError);
                }
                return;
            }
            asio::error_code ignored;
            acceptor_.close(ignored); // one viewer per session
            on_control_connected();
        });
        start_thread();
    }

    void start_viewer(const std::string& host, std::uint16_t port, Microseconds connect_timeout) {
        start_connect_timer(connect_timeout);
        resolver_.async_resolve(
            host, std::to_string(port),
            [this](const asio::error_code& error, const tcp::resolver::results_type& results) {
                if (connect_timed_out_) {
                    return; // already reported
                }
                if (error) {
                    connect_timer_.cancel();
                    engine_.on_transport_closed(Error::NetworkError);
                    return;
                }
                asio::async_connect(
                    control_, results,
                    [this](const asio::error_code& connect_error, const tcp::endpoint& endpoint) {
                        if (connect_timed_out_) {
                            return; // already reported
                        }
                        connected_ = true;
                        connect_timer_.cancel();
                        if (connect_error) {
                            engine_.on_transport_closed(Error::NetworkError);
                            return;
                        }
                        asio::error_code open_error;
                        const bool v4 = endpoint.protocol() == tcp::v4();
                        if (!open_udp(v4 ? udp::v4() : udp::v6(), open_error)) {
                            engine_.on_transport_closed(Error::NetworkError);
                            return;
                        }
                        on_control_connected();
                    });
            });
        start_thread();
    }

    /// Stream link, viewer: listens on the loopback port, where usbmuxd delivers the host's
    /// connection from the cable.
    Status<Error> listen_on_loopback(std::uint16_t port) {
        link_ = true;
        asio::error_code error;
        const tcp::endpoint endpoint(asio::ip::address_v4::loopback(), port);
        acceptor_.open(endpoint.protocol(), error);
#ifndef _WIN32
        if (!error) {
            acceptor_.set_option(tcp::acceptor::reuse_address(true), error); // as for the host
        }
#endif
        if (!error) {
            acceptor_.bind(endpoint, error);
        }
        if (!error) {
            acceptor_.listen(1, error);
        }
        if (!error) {
            control_port_ = acceptor_.local_endpoint(error).port();
        }
        return error ? Status<Error>::failure(Error::NetworkError) : Status<Error>::ok();
    }

    /// Stream link, host: asks usbmuxd for a connection to the device's port. The session ends
    /// with NETWORK when usbmuxd refuses (no Osplay listening there, device gone).
    void start_host_usb(const UsbTarget& target, Microseconds connect_timeout) {
        link_ = true;
        mux_request_ = usbmux::connect_request(target.device_id, target.port, 1);
        start_connect_timer(connect_timeout);
        auto opened = [this](const asio::error_code& error) {
            if (connect_timed_out_) {
                return;
            }
            if (error) {
                connect_timer_.cancel();
                engine_.on_transport_closed(Error::NetworkError);
                return;
            }
            with_stream([this](auto& socket) {
                asio::async_write(socket, asio::buffer(mux_request_),
                                  [this](const asio::error_code& write_error, std::size_t) {
                                      if (!connect_timed_out_ && write_error) {
                                          connect_timer_.cancel();
                                          engine_.on_transport_closed(Error::NetworkError);
                                      } else if (!connect_timed_out_) {
                                          read_usbmux_header();
                                      }
                                  });
            });
        };
#ifdef _WIN32
        control_.async_connect(tcp::endpoint(asio::ip::address_v4::loopback(), target.usbmux.port),
                               opened);
#else
        local_.async_connect(asio::local::stream_protocol::endpoint(target.usbmux.path), opened);
#endif
        start_thread();
    }

    void start_viewer_link() {
        acceptor_.async_accept(control_, [this](const asio::error_code& error) {
            if (error) {
                if (error != asio::error::operation_aborted) {
                    engine_.on_transport_closed(Error::NetworkError);
                }
                return;
            }
            asio::error_code ignored;
            acceptor_.close(ignored); // one host per session
            on_control_connected();
        });
        start_thread();
    }

    [[nodiscard]] std::uint16_t control_port() const { return control_port_; }
    [[nodiscard]] std::uint16_t media_port() const { return media_port_; }

    // --- Commands from other threads ---

    template <typename Result, typename Function>
    Result run_sync(Function&& function, Result if_stopped) {
        if (std::this_thread::get_id() == io_thread_id_.load()) {
            return function();
        }
        auto task =
            std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        auto future = task->get_future();
        asio::post(io_, [task] { (*task)(); });
        if (future.wait_for(kCommandTimeout) != std::future_status::ready) {
            return if_stopped;
        }
        return future.get();
    }

    void post(std::function<void()> work) { asio::post(io_, std::move(work)); }

    session::SessionEngine& engine() { return engine_; }
    static TimePoint now() { return Clock::now(); }

    // --- SessionTransport (I/O thread) ---

    void send_control(std::span<const std::uint8_t> frame) override {
        if (socket_closed_) {
            return;
        }
        if (link_) {
            std::vector<std::uint8_t> record;
            if (!protocol::append_link_record(protocol::LinkRecordKind::Control, frame, record)) {
                return; // the engine never builds a frame larger than a record holds
            }
            control_queue_.push_back(std::move(record));
        } else {
            control_queue_.emplace_back(frame.begin(), frame.end());
        }
        write_next();
    }

    void send_datagram(std::span<const std::uint8_t> datagram) override {
        if (link_) {
            if (closed_ || !link_up_ || media_queued_bytes_ + datagram.size() > kLinkMediaBudget) {
                return; // dropped, as on a full UDP socket
            }
            std::vector<std::uint8_t> record;
            if (protocol::append_link_record(protocol::LinkRecordKind::Media, datagram, record)) {
                media_queued_bytes_ += record.size();
                media_queue_.push_back(std::move(record));
                write_next();
            }
            return;
        }
        if (closed_ || !media_peer_) {
            return;
        }
        asio::error_code error;
        // Non-blocking: a full socket buffer drops the datagram, which the protocol treats as loss.
        media_.send_to(asio::buffer(datagram.data(), datagram.size()), *media_peer_, 0, error);
    }

    void set_media_destination_port(std::uint16_t port) override {
        if (link_) {
            return; // media shares the stream
        }
        asio::error_code error;
        const auto host = control_.remote_endpoint(error);
        if (!error) {
            media_peer_ = udp::endpoint(host.address(), port);
        }
    }

    void accept_media_source() override {
        if (!link_) {
            media_peer_ = datagram_source_;
        }
    }

    [[nodiscard]] std::uint16_t local_media_port() const override {
        if (link_) {
            return kLinkMediaPort;
        }
        if (options_.advertised_media_port) {
            return *options_.advertised_media_port;
        }
        asio::error_code error;
        return media_.local_endpoint(error).port();
    }

    void close() override {
        if (closed_) {
            return;
        }
        closed_ = true;
        asio::error_code ignored;
        acceptor_.close(ignored);
        media_.close(ignored);
        timer_.cancel();
        connect_timer_.cancel();
        resolver_.cancel();
        // Let queued control data (Error, Bye) leave before the FIN; media is no longer wanted.
        drop_queued_media();
        if (!writing_ && control_queue_.empty()) {
            close_control_socket();
        }
    }

private:
    /// Runs `operation` on the socket that carries the control stream: the TCP socket, or on a
    /// Mac host's stream link the Unix socket to usbmuxd.
    template <typename Operation>
    void with_stream(Operation&& operation) {
#ifndef _WIN32
        if (local_.is_open()) {
            std::forward<Operation>(operation)(local_);
            return;
        }
#endif
        std::forward<Operation>(operation)(control_);
    }

    void start_connect_timer(Microseconds connect_timeout) {
        connect_timer_.expires_after(connect_timeout);
        connect_timer_.async_wait([this](const asio::error_code& error) {
            if (error || closed_ || connected_) {
                return; // cancelled, or the connection is up
            }
            connect_timed_out_ = true;
            asio::error_code ignored;
            resolver_.cancel();
            control_.close(ignored); // aborts async_connect
#ifndef _WIN32
            local_.close(ignored);
#endif
            engine_.on_transport_closed(Error::TimedOut);
        });
    }

    void read_usbmux_header() {
        with_stream([this](auto& socket) {
            asio::async_read(socket, asio::buffer(mux_header_),
                             [this](const asio::error_code& error, std::size_t) {
                                 if (connect_timed_out_) {
                                     return;
                                 }
                                 const auto size = error ? Expected<std::size_t, Error>::failure(
                                                               Error::NetworkError)
                                                         : usbmux::plist_size(mux_header_);
                                 if (!size) {
                                     usbmux_failed();
                                     return;
                                 }
                                 mux_plist_.assign(size.value(), '\0');
                                 read_usbmux_result();
                             });
        });
    }

    void read_usbmux_result() {
        with_stream([this](auto& socket) {
            asio::async_read(
                socket, asio::buffer(mux_plist_),
                [this](const asio::error_code& error, std::size_t) {
                    if (connect_timed_out_) {
                        return;
                    }
                    const auto message =
                        error ? Expected<usbmux::Message, Error>::failure(Error::NetworkError)
                              : usbmux::parse_message(mux_plist_);
                    const auto* result =
                        message ? std::get_if<usbmux::Result>(&message.value()) : nullptr;
                    if (result == nullptr || result->number != usbmux::kResultOk) {
                        usbmux_failed();
                        return;
                    }
                    // From here on the socket is a byte stream to the device's port.
                    connected_ = true;
                    connect_timer_.cancel();
                    on_control_connected();
                });
        });
    }

    void usbmux_failed() {
        connect_timer_.cancel();
        engine_.on_transport_closed(Error::NetworkError);
    }

    bool open_tcp_acceptor(asio::error_code& error) {
        if (options_.control_port != 0 && open_tcp_acceptor(options_.control_port, error)) {
            return true;
        }
        return open_tcp_acceptor(0, error);
    }

    bool open_tcp_acceptor(std::uint16_t port, asio::error_code& error) {
        for (const auto protocol : {tcp::v6(), tcp::v4()}) {
            error.clear();
            acceptor_.open(protocol, error);
            if (error) {
                continue;
            }
            if (protocol == tcp::v6()) {
                acceptor_.set_option(asio::ip::v6_only(false), error); // also accept IPv4
            }
#ifndef _WIN32
            // The previous session's connection may linger in TIME_WAIT on this port. Windows
            // allows the bind without it, and there the option would let another socket take an
            // active port.
            acceptor_.set_option(tcp::acceptor::reuse_address(true), error);
#endif
            acceptor_.bind(tcp::endpoint(protocol, port), error);
            if (error == asio::error::address_in_use && port != 0) {
                // Another socket has this port. Taking it on IPv4 alone next (which Windows
                // allows beside a dual-stack listener) would split the port; use any instead.
                asio::error_code ignored;
                acceptor_.close(ignored);
                return false;
            }
            if (!error) {
                acceptor_.listen(1, error);
            }
            if (!error) {
                return true;
            }
            asio::error_code ignored;
            acceptor_.close(ignored);
        }
        return false;
    }

    bool open_udp(const udp& protocol, asio::error_code& error) {
        error.clear();
        asio::error_code ignored;
        media_.close(ignored);
        media_.open(protocol, error);
        if (error) {
            return false;
        }
        if (protocol == udp::v6()) {
            media_.set_option(asio::ip::v6_only(false), error);
        }
        media_.bind(udp::endpoint(protocol, 0), error);
        if (error) {
            media_.close(ignored);
            return false;
        }
        media_.non_blocking(true, ignored);
        media_.set_option(asio::socket_base::send_buffer_size(kSocketBufferBytes), ignored);
        media_.set_option(asio::socket_base::receive_buffer_size(kSocketBufferBytes), ignored);
        mark_as_interactive_video(media_); // best effort: an unmarked socket still works
        return true;
    }

    void start_thread() {
        start_timer();
        thread_ = std::thread([this] {
            io_thread_id_.store(std::this_thread::get_id());
            io_.run();
            finished_.set_value();
        });
    }

    void on_control_connected() {
        if (control_.is_open()) {
            asio::error_code ignored;
            control_.set_option(tcp::no_delay(true), ignored);
            mark_as_signaling(control_); // best effort: an unmarked socket still works
        }
        read_control();
        if (link_) {
            link_up_ = true;
        } else {
            receive_datagram();
        }
        engine_.on_connected(now());
    }

    void start_timer() {
        timer_.expires_after(tick_interval_);
        timer_.async_wait([this](const asio::error_code& error) {
            if (error || closed_) {
                return;
            }
            engine_.on_timer(now());
            start_timer();
        });
    }

    void read_control() {
        with_stream([this](auto& socket) {
            socket.async_read_some(asio::buffer(read_buffer_), [this](const asio::error_code& error,
                                                                      std::size_t size) {
                if (error) {
                    if (!closed_) {
                        engine_.on_transport_closed(error == asio::error::eof
                                                        ? Error::ConnectionClosed
                                                        : Error::NetworkError);
                        close();
                    }
                    return;
                }
                if (link_) {
                    read_link_records(std::span(read_buffer_).first(size));
                } else {
                    engine_.on_control_bytes(std::span(read_buffer_).first(size), now());
                }
                if (!closed_) {
                    read_control();
                }
            });
        });
    }

    void read_link_records(std::span<const std::uint8_t> bytes) {
        link_reader_.append(bytes);
        while (!closed_) {
            auto record = link_reader_.next();
            if (!record) {
                engine_.on_transport_closed(record.error());
                close();
                return;
            }
            const auto& next = record.value();
            if (!next.has_value()) {
                return; // more bytes needed
            }
            const auto& [kind, payload] = next.value();
            if (kind == protocol::LinkRecordKind::Control) {
                engine_.on_control_bytes(payload, now());
            } else {
                engine_.on_datagram(payload, now());
            }
        }
    }

    void receive_datagram() {
        media_.async_receive_from(asio::buffer(datagram_buffer_), datagram_source_,
                                  [this](const asio::error_code& error, std::size_t size) {
                                      if (closed_ || error == asio::error::operation_aborted) {
                                          return;
                                      }
                                      // Other errors (e.g. ICMP port unreachable on Windows) are
                                      // not fatal.
                                      if (!error && size <= protocol::kMaxDatagramSize) {
                                          engine_.on_datagram(
                                              std::span(datagram_buffer_).first(size), now());
                                      }
                                      if (!closed_) {
                                          receive_datagram();
                                      }
                                  });
    }

    /// Writes one queued buffer at a time, control before media; partial writes continue from
    /// write_offset_. Pushing to the back of a deque keeps the front being written in place.
    void write_next() {
        if (writing_) {
            return;
        }
        if (!control_queue_.empty()) {
            writing_from_ = &control_queue_;
        } else if (!media_queue_.empty()) {
            writing_from_ = &media_queue_;
        } else {
            if (closed_) {
                close_control_socket();
            }
            return;
        }
        writing_ = true;
        const auto& front = writing_from_->front();
        with_stream([this, &front](auto& socket) {
            socket.async_write_some(
                asio::buffer(front.data() + write_offset_, front.size() - write_offset_),
                [this](const asio::error_code& error, std::size_t size) {
                    writing_ = false;
                    if (error) {
                        control_queue_.clear();
                        drop_queued_media();
                        write_offset_ = 0;
                        if (!closed_) {
                            engine_.on_transport_closed(Error::NetworkError);
                            close();
                        }
                        close_control_socket();
                        return;
                    }
                    write_offset_ += size;
                    if (write_offset_ == writing_from_->front().size()) {
                        if (writing_from_ == &media_queue_) {
                            media_queued_bytes_ -= writing_from_->front().size();
                        }
                        writing_from_->pop_front();
                        write_offset_ = 0;
                    }
                    write_next();
                });
        });
    }

    /// Drops media that has not started on the wire; a record half written must be finished.
    void drop_queued_media() {
        const bool front_started = writing_ && writing_from_ == &media_queue_;
        while (media_queue_.size() > (front_started ? 1U : 0U)) {
            media_queued_bytes_ -= media_queue_.back().size();
            media_queue_.pop_back();
        }
    }

    void close_control_socket() {
        if (socket_closed_) {
            return;
        }
        socket_closed_ = true;
        asio::error_code ignored;
        control_.shutdown(tcp::socket::shutdown_both, ignored);
        control_.close(ignored);
#ifndef _WIN32
        local_.shutdown(asio::socket_base::shutdown_both, ignored);
        local_.close(ignored);
#endif
    }

    void stop() {
        if (!thread_.joinable()) {
            return;
        }
        asio::post(io_, [this] { engine_.close(protocol::ByeReason::ApplicationQuit, now()); });
        work_.reset();
        // Normally the queued Bye is written and the loop runs out of work; never hang on a dead
        // peer.
        if (finished_.get_future().wait_for(kShutdownGrace) != std::future_status::ready) {
            io_.stop();
        }
        thread_.join();
    }

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    tcp::acceptor acceptor_;
    tcp::socket control_;
#ifndef _WIN32
    asio::local::stream_protocol::socket local_; // Mac host's stream link: usbmuxd's Unix socket
#endif
    udp::socket media_;
    tcp::resolver resolver_;
    asio::steady_timer timer_;
    asio::steady_timer connect_timer_; // viewer: until the TCP connection is up
    bool connected_ = false;           // I/O thread only
    bool connect_timed_out_ = false;   // I/O thread only
    std::array<std::uint8_t, kReadBufferBytes> read_buffer_{};
    std::array<std::uint8_t, protocol::kMaxDatagramSize + 1> datagram_buffer_{};
    udp::endpoint datagram_source_;
    std::optional<udp::endpoint> media_peer_;
    std::deque<std::vector<std::uint8_t>> control_queue_;
    std::deque<std::vector<std::uint8_t>> media_queue_; // stream link only
    std::size_t media_queued_bytes_ = 0;
    std::deque<std::vector<std::uint8_t>>* writing_from_ = nullptr;
    bool writing_ = false;
    std::size_t write_offset_ = 0;
    bool link_ = false;    // control and media share the stream (PROTOCOL.md "Stream links")
    bool link_up_ = false; // I/O thread only
    protocol::LinkRecordReader link_reader_;
    std::vector<std::uint8_t> mux_request_;
    std::array<std::uint8_t, usbmux::kHeaderSize> mux_header_{};
    std::string mux_plist_;
    bool closed_ = false;
    bool socket_closed_ = false;
    std::uint16_t control_port_ = 0;
    std::uint16_t media_port_ = 0;
    Microseconds tick_interval_{2'000};
    TransportOptions options_;
    session::SessionEngine engine_;
    std::promise<void> finished_;
    std::atomic<std::thread::id> io_thread_id_;
    std::thread thread_;
};

// --- AsioSession ---------------------------------------------------------------------------------

AsioSession::AsioSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

AsioSession::~AsioSession() = default;

Expected<std::unique_ptr<AsioSession>, Error>
AsioSession::create_host(session::SessionConfig config, crypto::KeyPair identity,
                         crypto::RandomSource& random, session::SessionObserver& observer,
                         TransportOptions options) {
    using Result = Expected<std::unique_ptr<AsioSession>, Error>;
    const auto tick = config.tunables.tick_interval;
    auto impl =
        std::make_unique<Impl>(std::move(config), std::move(identity), random, observer, options);
    impl->set_tick_interval(tick);
    if (const auto listening = impl->listen(); !listening) {
        return Result::failure(listening.error());
    }
    impl->start_host();
    return std::unique_ptr<AsioSession>(new AsioSession(
        std::move(impl))); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

Expected<std::unique_ptr<AsioSession>, Error>
AsioSession::create_viewer(session::SessionConfig config, crypto::KeyPair identity,
                           crypto::RandomSource& random, session::SessionObserver& observer,
                           const std::string& host, std::uint16_t port) {
    const auto tick = config.tunables.tick_interval;
    const auto connect_timeout = config.tunables.connect_timeout;
    auto impl = std::make_unique<Impl>(std::move(config), std::move(identity), random, observer,
                                       TransportOptions{});
    impl->set_tick_interval(tick);
    impl->start_viewer(host, port, connect_timeout);
    return std::unique_ptr<AsioSession>(new AsioSession(
        std::move(impl))); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

Expected<std::unique_ptr<AsioSession>, Error>
AsioSession::create_host_usb(session::SessionConfig config, crypto::KeyPair identity,
                             crypto::RandomSource& random, session::SessionObserver& observer,
                             const UsbTarget& target) {
    const auto tick = config.tunables.tick_interval;
    const auto connect_timeout = config.tunables.connect_timeout;
    auto impl = std::make_unique<Impl>(std::move(config), std::move(identity), random, observer,
                                       TransportOptions{});
    impl->set_tick_interval(tick);
    impl->start_host_usb(target, connect_timeout);
    return std::unique_ptr<AsioSession>(new AsioSession(
        std::move(impl))); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

Expected<std::unique_ptr<AsioSession>, Error>
AsioSession::create_viewer_usb(session::SessionConfig config, crypto::KeyPair identity,
                               crypto::RandomSource& random, session::SessionObserver& observer,
                               std::uint16_t port) {
    using Result = Expected<std::unique_ptr<AsioSession>, Error>;
    const auto tick = config.tunables.tick_interval;
    auto impl = std::make_unique<Impl>(std::move(config), std::move(identity), random, observer,
                                       TransportOptions{});
    impl->set_tick_interval(tick);
    if (const auto listening = impl->listen_on_loopback(port); !listening) {
        return Result::failure(listening.error());
    }
    impl->start_viewer_link();
    return std::unique_ptr<AsioSession>(new AsioSession(
        std::move(impl))); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

std::uint16_t AsioSession::control_port() const {
    return impl_->control_port();
}

std::uint16_t AsioSession::media_port() const {
    return impl_->media_port();
}

Status<Error> AsioSession::confirm_pairing(bool accepted) {
    return impl_->run_sync(
        [this, accepted] { return impl_->engine().confirm_pairing(accepted, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::configure_streams(const std::vector<protocol::StreamConfig>& streams) {
    // Shared so the copy outlives a command that timed out but still runs later.
    auto copy = std::make_shared<const std::vector<protocol::StreamConfig>>(streams);
    return impl_->run_sync(
        [this, copy] { return impl_->engine().configure_streams(*copy, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::request_keyframe(std::uint8_t stream_id) {
    return impl_->run_sync(
        [this, stream_id] { return impl_->engine().request_keyframe(stream_id, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::stop_stream(std::uint8_t stream_id) {
    return impl_->run_sync(
        [this, stream_id] { return impl_->engine().stop_stream(stream_id, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::pause_stream(std::uint8_t stream_id) {
    return impl_->run_sync(
        [this, stream_id] { return impl_->engine().pause_stream(stream_id, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::resume_stream(std::uint8_t stream_id) {
    return impl_->run_sync(
        [this, stream_id] { return impl_->engine().resume_stream(stream_id, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::request_role_swap() {
    return impl_->run_sync([this] { return impl_->engine().request_role_swap(Impl::now()); },
                           Status<Error>::failure(Error::InvalidState));
}

Status<Error> AsioSession::answer_role_swap(session::RoleSwapAnswer answer) {
    return impl_->run_sync(
        [this, answer] { return impl_->engine().answer_role_swap(answer, Impl::now()); },
        Status<Error>::failure(Error::InvalidState));
}

void AsioSession::push_access_unit(const media::AccessUnit& unit) {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(unit.data.begin(), unit.data.end());
    impl_->post([impl = impl_.get(), unit, bytes] {
        media::AccessUnit copy = unit;
        copy.data = *bytes;
        // Frames pushed before streaming starts or while paused are dropped on purpose.
        static_cast<void>(impl->engine().push_access_unit(copy, Impl::now()));
    });
}

void AsioSession::close() {
    impl_->post([impl = impl_.get()] {
        impl->engine().close(protocol::ByeReason::UserDisconnected, Impl::now());
    });
}

void AsioSession::close_for_sleep() {
    impl_->post(
        [impl = impl_.get()] { impl->engine().close(protocol::ByeReason::Sleeping, Impl::now()); });
}

void AsioSession::refuse_busy() {
    impl_->post([impl = impl_.get()] { impl->engine().refuse_busy(Impl::now()); });
}

void AsioSession::push_cursor_shape(const media::CursorShape& shape) {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(shape.bgra.begin(), shape.bgra.end());
    impl_->post([impl = impl_.get(), shape, bytes] {
        media::CursorShape copy = shape;
        copy.bgra = *bytes;
        // Fails only before streaming starts; the next keyframe brings the current shape anyway.
        static_cast<void>(impl->engine().push_cursor_shape(copy, Impl::now()));
    });
}

void AsioSession::push_cursor_position(const protocol::CursorPosition& position) {
    impl_->post([impl = impl_.get(), position] {
        static_cast<void>(impl->engine().push_cursor_position(position, Impl::now()));
    });
}

} // namespace osplay::transport
