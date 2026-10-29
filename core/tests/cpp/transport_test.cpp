// End-to-end over real localhost sockets: AsioSession host and viewer, optionally through a relay
// that records every byte on the wire and can simulate an outage (TCP held back, UDP dropped).
#include "crypto_test_support.hpp"
#include "protocol/control_messages.hpp"
#include "session_simulation.hpp"
#include "transport/asio_session.hpp"
#include "transport/byte_pipe.hpp"
#include "transport/traffic_class.hpp"
#include "transport/usbmux.hpp"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#ifndef _WIN32
#include <asio/local/stream_protocol.hpp>
#endif
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace protocol = osplay::protocol;
namespace session = osplay::session;
using asio::ip::tcp;
using asio::ip::udp;
using osplay::Error;
using osplay::test::kPlaintextMarker;
using session::SessionState;
using session::TrustDecision;
using std::chrono::milliseconds;
using std::chrono::seconds;

// --- Thread-safe observer
// --------------------------------------------------------------------------

class SyncObserver final : public session::SessionObserver {
public:
    void on_state(SessionState new_state, std::optional<Error> reason) override {
        update([&] {
            state_ = new_state;
            reason_ = reason;
        });
    }
    std::optional<Error> close_reason() {
        const std::scoped_lock lock(mutex_);
        return reason_;
    }
    void on_peer(const protocol::Hello& /*hello*/) override {}
    TrustDecision on_trust_query(const osplay::crypto::Key& /*key*/) override {
        return TrustDecision::Trusted;
    }
    void on_pairing_code(std::uint32_t /*code*/, const osplay::crypto::Key& /*key*/) override {}
    void on_paired(const osplay::crypto::Key& /*key*/) override {}
    void on_viewer_monitors(const std::vector<protocol::Monitor>& received) override {
        update([&] { monitors_ = received; });
    }
    void on_stream_config(const protocol::StreamConfig& /*config*/) override {}
    void on_keyframe_request(std::uint8_t /*stream_id*/) override { ++keyframe_requests_; }
    void on_access_unit(const osplay::media::CompletedFrame& frame) override {
        std::uint32_t index = 0;
        std::memcpy(&index, frame.data.data(), sizeof(index));
        const bool intact =
            std::ranges::equal(frame.data, osplay::test::synthetic_frame(index, frame.keyframe));
        update([&] {
            frames_.push_back(index);
            intact_ = intact_ && intact;
        });
    }
    void on_stream_stopped(std::uint8_t /*stream_id*/) override {}
    void on_role_swap_request(const osplay::crypto::Key& /*key*/) override {}
    void on_bitrate_target(std::uint8_t /*stream_id*/, std::uint32_t /*kbps*/) override {}
    void on_role_swap_answer(session::RoleSwapAnswer /*answer*/,
                             const osplay::crypto::Key& /*key*/) override {}
    void on_cursor_shape(const osplay::media::CursorShape& /*shape*/) override {}
    void on_cursor_position(const protocol::CursorPosition& /*position*/) override {}
    void on_stats(const session::SessionStats& /*stats*/) override {}
    void on_log(session::LogLevel /*level*/, std::string_view /*message*/) override {}

    bool wait_for_state(SessionState state, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return state_ == state; });
    }
    bool wait_for_monitors(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return !monitors_.empty(); });
    }
    SessionState state() {
        const std::scoped_lock lock(mutex_);
        return state_;
    }
    bool wait_for_frames(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return frames_.size() >= count; });
    }
    std::size_t frame_count() {
        const std::scoped_lock lock(mutex_);
        return frames_.size();
    }
    bool intact() {
        const std::scoped_lock lock(mutex_);
        return intact_;
    }
    std::vector<protocol::Monitor> monitors() {
        const std::scoped_lock lock(mutex_);
        return monitors_;
    }
    int keyframe_requests() const { return keyframe_requests_.load(); }

private:
    template <typename Change>
    void update(Change&& change) {
        {
            const std::scoped_lock lock(mutex_);
            change();
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    SessionState state_ = SessionState::Connecting;
    std::optional<Error> reason_;
    std::vector<protocol::Monitor> monitors_;
    std::vector<std::uint32_t> frames_;
    bool intact_ = true;
    std::atomic<int> keyframe_requests_{0};
};

// --- Recording relay
// -------------------------------------------------------------------------------

/// Sits between viewer and host on both channels. Bound before the host exists so the host can
/// announce the relay's UDP port; pointed at the host's ports by start().
class Relay {
public:
    Relay()
        : work_(asio::make_work_guard(io_)),
          acceptor_(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0)), viewer_side_(io_),
          host_side_(io_), udp_down_(io_, udp::endpoint(asio::ip::address_v4::loopback(), 0)),
          udp_up_(io_, udp::endpoint(asio::ip::address_v4::loopback(), 0)) {}

    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;
    Relay(Relay&&) = delete;
    Relay& operator=(Relay&&) = delete;

    ~Relay() {
        asio::post(io_, [this] {
            asio::error_code ignored;
            acceptor_.close(ignored);
            viewer_side_.close(ignored);
            host_side_.close(ignored);
            udp_down_.close(ignored);
            udp_up_.close(ignored);
        });
        work_.reset();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t tcp_port() const { return acceptor_.local_endpoint().port(); }
    [[nodiscard]] std::uint16_t udp_port() const { return udp_down_.local_endpoint().port(); }

    void start(std::uint16_t host_tcp_port, std::uint16_t host_udp_port) {
        host_tcp_ = tcp::endpoint(asio::ip::address_v4::loopback(), host_tcp_port);
        host_udp_ = udp::endpoint(asio::ip::address_v4::loopback(), host_udp_port);
        to_host_ = {.from = &viewer_side_, .to = &host_side_, .capture = &capture_.viewer_to_host};
        to_viewer_ = {
            .from = &host_side_, .to = &viewer_side_, .capture = &capture_.host_to_viewer};
        acceptor_.async_accept(viewer_side_, [this](const asio::error_code& error) {
            asio::error_code connect_error;
            if (!error) {
                host_side_.connect(host_tcp_, connect_error);
            }
            if (!error && !connect_error) {
                pump(to_host_);
                pump(to_viewer_);
            }
        });
        forward_datagrams(udp_down_, true);
        forward_datagrams(udp_up_, false);
        thread_ = std::thread([this] { io_.run(); });
    }

    /// While on, TCP bytes are held back (delivered when it ends) and datagrams are dropped.
    void set_outage(bool outage) {
        asio::post(io_, [this, outage] {
            outage_ = outage;
            if (!outage_) {
                flush(to_host_);
                flush(to_viewer_);
            }
        });
    }

    struct Capture {
        std::vector<std::uint8_t> viewer_to_host;
        std::vector<std::uint8_t> host_to_viewer;
        std::vector<std::vector<std::uint8_t>> datagrams;
    };
    Capture capture() {
        const std::scoped_lock lock(capture_mutex_);
        return capture_;
    }

private:
    struct Direction {
        tcp::socket* from = nullptr;
        tcp::socket* to = nullptr;
        std::vector<std::uint8_t>* capture = nullptr;
        std::deque<std::vector<std::uint8_t>> held;
    };

    void pump(Direction& direction) {
        auto buffer = std::make_shared<std::array<std::uint8_t, 16384>>();
        direction.from->async_read_some(
            asio::buffer(*buffer),
            [this, &direction, buffer](const asio::error_code& error, std::size_t size) {
                if (error) {
                    flush(direction);
                    asio::error_code ignored;
                    direction.to->shutdown(tcp::socket::shutdown_send, ignored);
                    return;
                }
                const auto end = buffer->begin() + static_cast<std::ptrdiff_t>(size);
                {
                    const std::scoped_lock lock(capture_mutex_);
                    direction.capture->insert(direction.capture->end(), buffer->begin(), end);
                }
                direction.held.emplace_back(buffer->begin(), end);
                if (!outage_) {
                    flush(direction);
                }
                pump(direction);
            });
    }

    static void flush(Direction& direction) {
        while (!direction.held.empty()) {
            asio::error_code ignored;
            asio::write(*direction.to, asio::buffer(direction.held.front()), ignored);
            direction.held.pop_front();
        }
    }

    void forward_datagrams(udp::socket& socket, bool from_viewer) {
        auto buffer = std::make_shared<std::array<std::uint8_t, 2048>>();
        auto source = std::make_shared<udp::endpoint>();
        socket.async_receive_from(
            asio::buffer(*buffer), *source,
            [this, &socket, from_viewer, buffer, source](const asio::error_code& error,
                                                         std::size_t size) {
                if (error == asio::error::operation_aborted || !socket.is_open()) {
                    return;
                }
                if (error) { // e.g. ICMP port unreachable on Windows: keep relaying
                    forward_datagrams(socket, from_viewer);
                    return;
                }
                {
                    const std::scoped_lock lock(capture_mutex_);
                    capture_.datagrams.emplace_back(
                        buffer->begin(), buffer->begin() + static_cast<std::ptrdiff_t>(size));
                }
                if (from_viewer) {
                    viewer_udp_ = *source;
                }
                asio::error_code ignored;
                if (outage_) {
                    // dropped
                } else if (from_viewer) {
                    udp_up_.send_to(asio::buffer(buffer->data(), size), host_udp_, 0, ignored);
                } else if (viewer_udp_) {
                    udp_down_.send_to(asio::buffer(buffer->data(), size), *viewer_udp_, 0, ignored);
                }
                forward_datagrams(socket, from_viewer);
            });
    }

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    tcp::acceptor acceptor_;
    tcp::socket viewer_side_;
    tcp::socket host_side_;
    udp::socket udp_down_; // faces the viewer; its port is what the host announces
    udp::socket udp_up_;   // faces the host
    tcp::endpoint host_tcp_;
    udp::endpoint host_udp_;
    std::optional<udp::endpoint> viewer_udp_;
    Direction to_host_;
    Direction to_viewer_;
    bool outage_ = false;
    std::mutex capture_mutex_;
    Capture capture_;
    std::thread thread_;
};

// --- Fake usbmuxd
// ---------------------------------------------------------------------------------------

/// Answers one Connect request the way usbmuxd does: Result 0 and then the socket carries the
/// device port's bytes (here a loopback port, where a USB viewer listens), or Result 3 when
/// nothing listens there.
class FakeUsbmuxd {
public:
    FakeUsbmuxd()
        : work_(asio::make_work_guard(io_)),
#ifdef _WIN32
          acceptor_(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
#else
          path_(fresh_socket_path()), acceptor_(io_, asio::local::stream_protocol::endpoint(path_)),
#endif
          client_(io_), device_(io_) {
        acceptor_.async_accept(client_, [this](const asio::error_code& error) {
            if (!error) {
                read_request();
            }
        });
        thread_ = std::thread([this] { io_.run(); });
    }

    FakeUsbmuxd(const FakeUsbmuxd&) = delete;
    FakeUsbmuxd& operator=(const FakeUsbmuxd&) = delete;
    FakeUsbmuxd(FakeUsbmuxd&&) = delete;
    FakeUsbmuxd& operator=(FakeUsbmuxd&&) = delete;

    ~FakeUsbmuxd() {
        asio::post(io_, [this] {
            asio::error_code ignored;
            acceptor_.close(ignored);
            client_.close(ignored);
            device_.close(ignored);
        });
        work_.reset();
        thread_.join();
#ifndef _WIN32
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
#endif
    }

    [[nodiscard]] osplay::transport::UsbmuxAddress address() const {
#ifdef _WIN32
        return {.port = acceptor_.local_endpoint().port()};
#else
        return {.path = path_};
#endif
    }

    /// The DeviceID of the Connect request, once it arrived.
    [[nodiscard]] std::optional<std::uint32_t> requested_device() {
        const std::scoped_lock lock(mutex_);
        return requested_device_;
    }

private:
#ifndef _WIN32
    static std::string fresh_socket_path() {
        static std::atomic<int> instances{0};
        auto path = std::filesystem::temp_directory_path() /
                    ("osplay-usbmuxd-" + std::to_string(++instances) + ".sock");
        std::error_code ignored;
        std::filesystem::remove(path, ignored); // left over from a crashed run
        return path.string();
    }
#endif

    void read_request() {
        asio::async_read(
            client_, asio::buffer(header_), [this](const asio::error_code& error, std::size_t) {
                const auto size = osplay::transport::usbmux::plist_size(header_);
                if (error || !size) {
                    return;
                }
                plist_.assign(size.value(), '\0');
                asio::async_read(client_, asio::buffer(plist_),
                                 [this](const asio::error_code& read_error, std::size_t) {
                                     if (!read_error) {
                                         answer();
                                     }
                                 });
            });
    }

    static std::uint32_t integer_after(const std::string& plist, const std::string& key) {
        const auto at = plist.find("<integer>", plist.find("<key>" + key + "</key>"));
        return static_cast<std::uint32_t>(std::stoul(plist.substr(at + 9)));
    }

    void answer() {
        const auto device = integer_after(plist_, "DeviceID");
        const auto swapped = integer_after(plist_, "PortNumber");
        {
            const std::scoped_lock lock(mutex_);
            requested_device_ = device;
        }
        const auto port = static_cast<std::uint16_t>(((swapped & 0xFFU) << 8U) | (swapped >> 8U));
        asio::error_code connect_error;
        device_.connect(tcp::endpoint(asio::ip::address_v4::loopback(), port), connect_error);
        const int number = connect_error ? 3 : 0;
        const std::string body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<plist version=\"1.0\">\n<dict>\n"
                                 "\t<key>MessageType</key>\n\t<string>Result</string>\n"
                                 "\t<key>Number</key>\n\t<integer>" +
                                 std::to_string(number) + "</integer>\n</dict>\n</plist>\n";
        std::vector<std::uint8_t> reply;
        for (const std::uint32_t field :
             {static_cast<std::uint32_t>(16 + body.size()), 1U, 8U, 1U}) {
            for (unsigned shift = 0; shift < 32; shift += 8) {
                reply.push_back(static_cast<std::uint8_t>(field >> shift));
            }
        }
        reply.insert(reply.end(), body.begin(), body.end());
        asio::error_code ignored;
        asio::write(client_, asio::buffer(reply), ignored);
        if (number == 0) {
            pump(client_, device_);
            pump(device_, client_);
        }
    }

    template <typename From, typename To>
    void pump(From& from, To& to) {
        auto buffer = std::make_shared<std::array<std::uint8_t, 16384>>();
        from.async_read_some(
            asio::buffer(*buffer),
            [this, &from, &to, buffer](const asio::error_code& error, std::size_t size) {
                asio::error_code ignored;
                if (error) {
                    to.shutdown(asio::socket_base::shutdown_send, ignored);
                    return;
                }
                asio::write(to, asio::buffer(buffer->data(), size), ignored);
                pump(from, to);
            });
    }

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
#ifdef _WIN32
    tcp::acceptor acceptor_;
    tcp::socket client_;
#else
    std::string path_;
    asio::local::stream_protocol::acceptor acceptor_;
    asio::local::stream_protocol::socket client_;
#endif
    tcp::socket device_;
    std::array<std::uint8_t, osplay::transport::usbmux::kHeaderSize> header_{};
    std::string plist_;
    std::mutex mutex_;
    std::optional<std::uint32_t> requested_device_;
    std::thread thread_;
};

// --- Fixture
// ---------------------------------------------------------------------------------------

session::SessionConfig config(protocol::Role role) {
    session::SessionConfig config{
        .role = role,
        .platform =
            role == protocol::Role::Host ? protocol::Platform::MacOS : protocol::Platform::Windows,
        .device_name = role == protocol::Role::Host ? "Wire Host" : "Wire Viewer",
        .capabilities = {.codecs = protocol::kCodecH264AnnexB,
                         .max_streams = 2,
                         .flags = 0,
                         .max_width = 7680,
                         .max_height = 4320,
                         .max_refresh_hz = 240},
        .monitors = {},
        .tunables = {}};
    if (role == protocol::Role::Viewer) {
        config.monitors.push_back({.monitor_id = 0,
                                   .width_px = 1920,
                                   .height_px = 1080,
                                   .refresh_millihertz = 60'000,
                                   .physical_width_mm = 527,
                                   .physical_height_mm = 296,
                                   .scale_percent = 100,
                                   .name = "Wire"});
    }
    return config;
}

// --- In-memory Android accessory --------------------------------------------------------------

/// One direction of a cable: chunks as written, an empty one standing for a zero-length packet.
struct CableDirection {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<std::uint8_t>> chunks;
    bool unplugged = false;
};

/// A BytePipe end over two CableDirections. `stubborn` ignores cancel, as an Android accessory
/// read does, until bytes arrive or the cable goes.
class MemoryPipe final : public osplay::transport::BytePipe {
public:
    MemoryPipe(std::shared_ptr<CableDirection> in, std::shared_ptr<CableDirection> out,
               bool stubborn, std::shared_ptr<std::atomic<int>> released)
        : in_(std::move(in)), out_(std::move(out)), stubborn_(stubborn),
          released_(std::move(released)) {}
    ~MemoryPipe() override { ++*released_; }
    MemoryPipe(const MemoryPipe&) = delete;
    MemoryPipe& operator=(const MemoryPipe&) = delete;
    MemoryPipe(MemoryPipe&&) = delete;
    MemoryPipe& operator=(MemoryPipe&&) = delete;

    osplay::Expected<std::size_t, Error> read(std::span<std::uint8_t> buffer) override {
        std::unique_lock lock(in_->mutex);
        in_->changed.wait(lock, [this] {
            return !in_->chunks.empty() || in_->unplugged || (cancelled_ && !stubborn_);
        });
        if (in_->chunks.empty()) {
            return osplay::Expected<std::size_t, Error>::failure(Error::NetworkError);
        }
        auto& front = in_->chunks.front();
        const std::size_t size = std::min(front.size(), buffer.size());
        std::copy_n(front.begin(), size, buffer.begin());
        front.erase(front.begin(), front.begin() + static_cast<std::ptrdiff_t>(size));
        if (front.empty()) {
            in_->chunks.pop_front();
        }
        return size;
    }

    osplay::Expected<std::size_t, Error> write(std::span<const std::uint8_t> data) override {
        const std::scoped_lock lock(out_->mutex);
        if (out_->unplugged) {
            return osplay::Expected<std::size_t, Error>::failure(Error::NetworkError);
        }
        out_->chunks.emplace_back(data.begin(), data.end());
        out_->changed.notify_all();
        return data.size();
    }

    void cancel() override {
        const std::scoped_lock lock(in_->mutex);
        cancelled_ = true;
        in_->changed.notify_all();
    }

private:
    std::shared_ptr<CableDirection> in_;
    std::shared_ptr<CableDirection> out_;
    bool stubborn_;
    bool cancelled_ = false; // guarded by in_->mutex
    std::shared_ptr<std::atomic<int>> released_;
};

/// A cable between a Mac (host end) and an Android device (accessory end).
struct MemoryCable {
    std::shared_ptr<CableDirection> to_accessory = std::make_shared<CableDirection>();
    std::shared_ptr<CableDirection> to_host = std::make_shared<CableDirection>();
    std::shared_ptr<std::atomic<int>> released = std::make_shared<std::atomic<int>>(0);

    std::unique_ptr<osplay::transport::BytePipe> host_end() {
        return std::make_unique<MemoryPipe>(to_host, to_accessory, false, released);
    }
    std::unique_ptr<osplay::transport::BytePipe> accessory_end(bool stubborn = false) {
        return std::make_unique<MemoryPipe>(to_accessory, to_host, stubborn, released);
    }
    /// The host sends a zero-length packet.
    void send_zero_length_packet() {
        const std::scoped_lock lock(to_accessory->mutex);
        to_accessory->chunks.emplace_back();
        to_accessory->changed.notify_all();
    }
    void unplug() {
        for (const auto& direction : {to_accessory, to_host}) {
            const std::scoped_lock lock(direction->mutex);
            direction->unplugged = true;
            direction->changed.notify_all();
        }
    }
};

class RealSessions : public ::testing::Test {
protected:
    /// Starts the host, optionally behind the relay, and connects the viewer.
    void connect(bool through_relay) {
        osplay::transport::TransportOptions options;
        if (through_relay) {
            relay = std::make_unique<Relay>();
            options.advertised_media_port = relay->udp_port();
        }
        auto host_session = osplay::transport::AsioSession::create_host(
            config(protocol::Role::Host), osplay::test::key_pair(host_random), host_random,
            host_observer, options);
        ASSERT_TRUE(host_session.has_value());
        host = std::move(host_session.value());
        std::uint16_t port = host->control_port();
        if (relay) {
            relay->start(host->control_port(), host->media_port());
            port = relay->tcp_port();
        }
        auto viewer_session = osplay::transport::AsioSession::create_viewer(
            config(protocol::Role::Viewer), osplay::test::key_pair(viewer_random), viewer_random,
            viewer_observer, "127.0.0.1", port);
        ASSERT_TRUE(viewer_session.has_value());
        viewer = std::move(viewer_session.value());
    }

    /// The viewer listens as on an iPad; the host reaches it through the fake usbmuxd.
    void connect_usb(FakeUsbmuxd& usbmuxd) {
        auto viewer_session = osplay::transport::AsioSession::create_viewer_usb(
            config(protocol::Role::Viewer), osplay::test::key_pair(viewer_random), viewer_random,
            viewer_observer, 0);
        ASSERT_TRUE(viewer_session.has_value());
        viewer = std::move(viewer_session.value());
        auto host_session = osplay::transport::AsioSession::create_host_usb(
            config(protocol::Role::Host), osplay::test::key_pair(host_random), host_random,
            host_observer,
            {.device_id = 7, .port = viewer->control_port(), .usbmux = usbmuxd.address()});
        ASSERT_TRUE(host_session.has_value());
        host = std::move(host_session.value());
    }

    /// The host waits on its end of the cable; the viewer starts on the accessory end.
    void connect_accessory(std::unique_ptr<osplay::transport::BytePipe> host_end,
                           std::unique_ptr<osplay::transport::BytePipe> accessory_end) {
        auto host_session = osplay::transport::AsioSession::create_host_pipe(
            config(protocol::Role::Host), osplay::test::key_pair(host_random), host_random,
            host_observer, std::move(host_end));
        ASSERT_TRUE(host_session.has_value());
        host = std::move(host_session.value());
        auto viewer_session = osplay::transport::AsioSession::create_viewer_pipe(
            config(protocol::Role::Viewer), osplay::test::key_pair(viewer_random), viewer_random,
            viewer_observer, std::move(accessory_end));
        ASSERT_TRUE(viewer_session.has_value());
        viewer = std::move(viewer_session.value());
    }

    void start_streaming() {
        ASSERT_TRUE(host_observer.wait_for_monitors(seconds(5)));
        const auto monitor = host_observer.monitors().front();
        ASSERT_TRUE(host->configure_streams({{.stream_id = 0,
                                              .monitor_id = monitor.monitor_id,
                                              .width = monitor.width_px,
                                              .height = monitor.height_px,
                                              .refresh_millihertz = monitor.refresh_millihertz,
                                              .codec = protocol::Codec::H264,
                                              .profile = protocol::H264Profile::High,
                                              .cursor_mode = protocol::CursorMode::InVideo,
                                              .bitrate_kbps = 20'000}})
                        .has_value());
        ASSERT_TRUE(viewer_observer.wait_for_state(SessionState::Streaming, seconds(5)));
    }

    /// Pushes ~60 fps frames for `duration`.
    void stream_for(milliseconds duration) {
        const auto end = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < end) {
            push_frame();
        }
    }

    /// Pushes `count` frames at ~60 fps. Counts, not durations, keep slow CI machines comparable.
    void stream_frames(std::uint32_t count) {
        for (std::uint32_t i = 0; i < count; ++i) {
            push_frame();
        }
    }

    /// Pushes frames until the viewer has received `target` of them; false after `limit`.
    bool stream_until(std::size_t target, milliseconds limit) {
        const auto give_up = std::chrono::steady_clock::now() + limit;
        while (viewer_observer.frame_count() < target) {
            if (std::chrono::steady_clock::now() >= give_up) {
                return false;
            }
            push_frame();
        }
        return true;
    }

    /// Sends a keyframe first and whenever one was requested.
    void push_frame() {
        const int requests = host_observer.keyframe_requests();
        const bool keyframe = next_index == 0 || requests > keyframe_requests_seen;
        keyframe_requests_seen = requests;
        const auto bytes = osplay::test::synthetic_frame(next_index++, keyframe);
        host->push_access_unit({.stream_id = 0,
                                .keyframe = keyframe,
                                .codec_config = keyframe,
                                .capture_time_us = 0,
                                .data = bytes});
        std::this_thread::sleep_for(milliseconds(16));
    }

    osplay::test::SeededRandom host_random{11};
    osplay::test::SeededRandom viewer_random{12};
    SyncObserver host_observer;
    SyncObserver viewer_observer;
    std::unique_ptr<Relay> relay;
    std::unique_ptr<osplay::transport::AsioSession> host;
    std::unique_ptr<osplay::transport::AsioSession> viewer;
    int keyframe_requests_seen = 0;
    std::uint32_t next_index = 0;
};

/// Splits a captured TCP byte stream into frame payloads.
std::vector<std::vector<std::uint8_t>> frames_of(const std::vector<std::uint8_t>& stream) {
    std::vector<std::vector<std::uint8_t>> frames;
    std::size_t offset = 0;
    while (offset + 4 <= stream.size()) {
        std::uint32_t length = 0;
        std::memcpy(&length, stream.data() + offset, sizeof(length));
        if (offset + 4 + length > stream.size()) {
            break;
        }
        frames.emplace_back(stream.begin() + static_cast<std::ptrdiff_t>(offset + 4),
                            stream.begin() + static_cast<std::ptrdiff_t>(offset + 4 + length));
        offset += 4 + length;
    }
    return frames;
}

void expect_opaque(const std::vector<std::uint8_t>& bytes) {
    EXPECT_EQ(std::ranges::search(bytes, kPlaintextMarker).size(), 0U)
        << "plaintext frame data on the wire";
    const std::string text(bytes.begin(), bytes.end());
    EXPECT_EQ(text.find("Wire Viewer"), std::string::npos) << "device name after the handshake";
    EXPECT_EQ(text.find("Wire Host"), std::string::npos) << "device name after the handshake";
}

TEST(RealSessionsConnect, ANewHostSessionCanListenOnThePreviousPort) {
    osplay::test::SeededRandom random{6};
    SyncObserver observer;
    auto first = osplay::transport::AsioSession::create_host(
        config(protocol::Role::Host), osplay::test::key_pair(random), random, observer);
    ASSERT_TRUE(first.has_value());
    const std::uint16_t port = first.value()->control_port();
    first.value().reset();

    SyncObserver next_observer;
    auto second = osplay::transport::AsioSession::create_host(
        config(protocol::Role::Host), osplay::test::key_pair(random), random, next_observer,
        osplay::transport::TransportOptions{.advertised_media_port = std::nullopt,
                                            .control_port = port});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second.value()->control_port(), port) << "the advertisement stays right";

    // A taken port falls back to any.
    SyncObserver third_observer;
    auto third = osplay::transport::AsioSession::create_host(
        config(protocol::Role::Host), osplay::test::key_pair(random), random, third_observer,
        osplay::transport::TransportOptions{.advertised_media_port = std::nullopt,
                                            .control_port = port});
    ASSERT_TRUE(third.has_value());
    EXPECT_NE(third.value()->control_port(), port);
}

TEST(RealSessionsConnect, AHostThatNeverAnswersEndsTheViewerInsteadOfHangingIt) {
    // A non-routable address: the SYN goes nowhere (or the network reports it unreachable).
    auto viewer_config = config(protocol::Role::Viewer);
    viewer_config.tunables.connect_timeout = osplay::Microseconds{300'000};
    osplay::test::SeededRandom random{5};
    SyncObserver observer;
    auto viewer = osplay::transport::AsioSession::create_viewer(
        std::move(viewer_config), osplay::test::key_pair(random), random, observer, "10.255.255.1",
        9);
    ASSERT_TRUE(viewer.has_value());
    EXPECT_TRUE(observer.wait_for_state(SessionState::Closed, seconds(3)));
    const auto reason = observer.close_reason();
    EXPECT_TRUE(reason == Error::TimedOut || reason == Error::NetworkError);
}

TEST_F(RealSessions, StreamsFramesEndToEndOverLoopback) {
    connect(false);
    start_streaming();
    stream_frames(120);

    EXPECT_TRUE(viewer_observer.wait_for_frames(next_index, seconds(5)))
        << "a clean link delivers every frame";
    EXPECT_EQ(viewer_observer.frame_count(), next_index);
    EXPECT_EQ(host_observer.state(), SessionState::Streaming);
    EXPECT_TRUE(viewer_observer.intact());

    viewer.reset(); // sends Bye
    EXPECT_TRUE(host_observer.wait_for_state(SessionState::Closed, seconds(3)));
}

TEST_F(RealSessions, NothingReadableCrossesTheWireAfterTheHandshake) {
    connect(true);
    start_streaming();
    ASSERT_TRUE(stream_until(60, seconds(10))) << "media really went through the relay";

    const auto capture = relay->capture();
    const auto to_host = frames_of(capture.viewer_to_host);
    const auto to_viewer = frames_of(capture.host_to_viewer);
    ASSERT_GT(to_host.size(), 2U);
    ASSERT_GT(to_viewer.size(), 1U);
    // Noise XX: messages 1 and 3 travel viewer -> host, message 2 host -> viewer.
    for (std::size_t i = 2; i < to_host.size(); ++i) {
        expect_opaque(to_host[i]);
    }
    for (std::size_t i = 1; i < to_viewer.size(); ++i) {
        expect_opaque(to_viewer[i]);
    }
    ASSERT_GT(capture.datagrams.size(), 60U);
    for (const auto& datagram : capture.datagrams) {
        expect_opaque(datagram);
    }
}

TEST_F(RealSessions, SurvivesAThreeSecondNetworkInterruption) {
    connect(true);
    start_streaming();
    ASSERT_TRUE(stream_until(30, seconds(10)));
    std::this_thread::sleep_for(milliseconds(200)); // let frames in flight land
    const auto before = viewer_observer.frame_count();

    relay->set_outage(true);
    stream_for(seconds(3));
    EXPECT_LE(viewer_observer.frame_count(), before + 3) << "the outage really cut media";
    relay->set_outage(false);
    EXPECT_TRUE(stream_until(before + 60, seconds(10))) << "streaming resumed after the outage";
    EXPECT_EQ(host_observer.state(), SessionState::Streaming);
    EXPECT_EQ(viewer_observer.state(), SessionState::Streaming);
    EXPECT_TRUE(viewer_observer.intact());
}

TEST_F(RealSessions, StreamsFramesOverAUsbLink) {
    FakeUsbmuxd usbmuxd;
    connect_usb(usbmuxd);
    start_streaming();
    stream_frames(120);

    EXPECT_TRUE(viewer_observer.wait_for_frames(next_index, seconds(5)))
        << "media shares the stream, so nothing is lost";
    EXPECT_EQ(viewer_observer.frame_count(), next_index);
    EXPECT_TRUE(viewer_observer.intact());
    EXPECT_EQ(usbmuxd.requested_device(), 7U);

    viewer.reset(); // sends Bye
    EXPECT_TRUE(host_observer.wait_for_state(SessionState::Closed, seconds(3)));
}

TEST_F(RealSessions, StreamsFramesOverAnAndroidAccessory) {
    MemoryCable cable;
    connect_accessory(cable.host_end(), cable.accessory_end());
    start_streaming();
    cable.send_zero_length_packet(); // read again, not the end of the stream
    stream_frames(120);

    EXPECT_TRUE(viewer_observer.wait_for_frames(next_index, seconds(5)));
    EXPECT_EQ(viewer_observer.frame_count(), next_index);
    EXPECT_TRUE(viewer_observer.intact());

    viewer.reset(); // sends Bye
    EXPECT_TRUE(host_observer.wait_for_state(SessionState::Closed, seconds(3)));
    host.reset();
    EXPECT_EQ(*cable.released, 2) << "both ends are released once their threads are done";
}

#ifndef _WIN32
TEST_F(RealSessions, StreamsFramesOverAnAccessoryFileDescriptor) {
    std::array<int, 2> fds{};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);
    connect_accessory(osplay::transport::make_fd_pipe(fds[0]),
                      osplay::transport::make_fd_pipe(fds[1]));
    start_streaming();
    stream_frames(60);

    EXPECT_TRUE(viewer_observer.wait_for_frames(next_index, seconds(5)));
    EXPECT_TRUE(viewer_observer.intact());
}
#endif

TEST(RealSessionsConnect, AnAccessoryHostWaitsForTheViewerWithoutATimeLimit) {
    MemoryCable cable;
    osplay::test::SeededRandom random{10};
    SyncObserver observer;
    auto host_config = config(protocol::Role::Host);
    host_config.tunables.handshake_timeout = osplay::Microseconds{200'000};
    auto host = osplay::transport::AsioSession::create_host_pipe(
        std::move(host_config), osplay::test::key_pair(random), random, observer, cable.host_end());
    ASSERT_TRUE(host.has_value());
    std::this_thread::sleep_for(milliseconds(600));
    EXPECT_EQ(observer.state(), SessionState::Connecting) << "the clock starts with the viewer";
}

TEST(RealSessionsConnect, AnAccessorySessionEndsWhenTheCableGoes) {
    MemoryCable cable;
    osplay::test::SeededRandom host_random{11};
    osplay::test::SeededRandom viewer_random{12};
    SyncObserver host_observer;
    SyncObserver viewer_observer;
    auto host = osplay::transport::AsioSession::create_host_pipe(
        config(protocol::Role::Host), osplay::test::key_pair(host_random), host_random,
        host_observer, cable.host_end());
    auto viewer = osplay::transport::AsioSession::create_viewer_pipe(
        config(protocol::Role::Viewer), osplay::test::key_pair(viewer_random), viewer_random,
        viewer_observer, cable.accessory_end());
    ASSERT_TRUE(host.has_value() && viewer.has_value());
    ASSERT_TRUE(host_observer.wait_for_monitors(seconds(5)));

    cable.unplug();
    EXPECT_TRUE(host_observer.wait_for_state(SessionState::Closed, seconds(3)));
    EXPECT_EQ(host_observer.close_reason(), Error::NetworkError);
    EXPECT_TRUE(viewer_observer.wait_for_state(SessionState::Closed, seconds(3)));
}

TEST(RealSessionsConnect, AViewerStuckInTheAccessoryDoesNotHoldUpItsEnd) {
    MemoryCable cable;
    osplay::test::SeededRandom random{13};
    SyncObserver observer;
    auto viewer = osplay::transport::AsioSession::create_viewer_pipe(
        config(protocol::Role::Viewer), osplay::test::key_pair(random), random, observer,
        cable.accessory_end(true));
    ASSERT_TRUE(viewer.has_value());

    const auto started = std::chrono::steady_clock::now();
    viewer.value().reset(); // its read ignores cancel
    EXPECT_LT(std::chrono::steady_clock::now() - started, seconds(3));
    EXPECT_EQ(*cable.released, 0) << "still in the read";

    cable.send_zero_length_packet(); // what a host sends when it closes its end
    for (int wait = 0; wait < 100 && *cable.released == 0; ++wait) {
        std::this_thread::sleep_for(milliseconds(20));
    }
    EXPECT_EQ(*cable.released, 1);
}

TEST(RealSessionsConnect, AUsbHostEndsWhenNothingListensOnTheDevice) {
    // A port that was just free: usbmuxd answers "connection refused".
    std::uint16_t free_port = 0;
    {
        asio::io_context io;
        const tcp::acceptor probe(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        free_port = probe.local_endpoint().port();
    }
    FakeUsbmuxd usbmuxd;
    osplay::test::SeededRandom random{8};
    SyncObserver observer;
    auto host = osplay::transport::AsioSession::create_host_usb(
        config(protocol::Role::Host), osplay::test::key_pair(random), random, observer,
        {.device_id = 3, .port = free_port, .usbmux = usbmuxd.address()});
    ASSERT_TRUE(host.has_value());
    EXPECT_TRUE(observer.wait_for_state(SessionState::Closed, seconds(3)));
    EXPECT_EQ(observer.close_reason(), Error::NetworkError);
}

TEST(RealSessionsConnect, AUsbHostEndsWhenUsbmuxdIsMissing) {
    osplay::transport::UsbmuxAddress missing;
#ifdef _WIN32
    {
        asio::io_context io;
        const tcp::acceptor probe(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        missing.port = probe.local_endpoint().port();
    }
#else
    missing.path = (std::filesystem::temp_directory_path() / "osplay-no-usbmuxd.sock").string();
#endif
    osplay::test::SeededRandom random{9};
    SyncObserver observer;
    auto host = osplay::transport::AsioSession::create_host_usb(
        config(protocol::Role::Host), osplay::test::key_pair(random), random, observer,
        {.device_id = 1, .port = 40'000, .usbmux = missing});
    ASSERT_TRUE(host.has_value());
    EXPECT_TRUE(observer.wait_for_state(SessionState::Closed, seconds(3)));
    EXPECT_EQ(observer.close_reason(), Error::NetworkError);
}

} // namespace

TEST(TrafficClass, MediaIsInteractiveVideoAndControlIsSignaling) {
    asio::io_context io;
    asio::ip::udp::socket media(io, asio::ip::udp::v6());
    asio::ip::tcp::socket control(io, asio::ip::tcp::v6());
#ifdef __APPLE__
    ASSERT_TRUE(osplay::transport::mark_as_interactive_video(media));
    ASSERT_TRUE(osplay::transport::mark_as_signaling(control));
    int type = -1;
    socklen_t length = sizeof(type);
    ASSERT_EQ(getsockopt(media.native_handle(), SOL_SOCKET, SO_NET_SERVICE_TYPE, &type, &length),
              0);
    EXPECT_EQ(type, NET_SERVICE_TYPE_VI);
    ASSERT_EQ(getsockopt(control.native_handle(), SOL_SOCKET, SO_NET_SERVICE_TYPE, &type, &length),
              0);
    EXPECT_EQ(type, NET_SERVICE_TYPE_SIG);
#else
    EXPECT_FALSE(osplay::transport::mark_as_interactive_video(media)) << "no such option here";
    EXPECT_FALSE(osplay::transport::mark_as_signaling(control));
#endif
}

TEST(TrafficClass, AClosedSocketIsLeftAlone) {
    asio::io_context io;
    asio::ip::udp::socket closed(io);
    EXPECT_FALSE(osplay::transport::mark_as_interactive_video(closed));
}
