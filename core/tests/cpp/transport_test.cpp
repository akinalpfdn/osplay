// End-to-end over real localhost sockets: AsioSession host and viewer, optionally through a relay
// that records every byte on the wire and can simulate an outage (TCP held back, UDP dropped).
#include "crypto_test_support.hpp"
#include "protocol/control_messages.hpp"
#include "session_simulation.hpp"
#include "transport/asio_session.hpp"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/post.hpp>
#include <asio/write.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
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
    void on_state(SessionState state, std::optional<Error> /*reason*/) override {
        update([&] { state_ = state; });
    }
    void on_peer(const protocol::Hello& /*hello*/) override {}
    TrustDecision on_trust_query(const osplay::crypto::Key& /*key*/) override {
        return TrustDecision::Trusted;
    }
    void on_pairing_code(std::uint32_t /*code*/, const osplay::crypto::Key& /*key*/) override {}
    void on_paired(const osplay::crypto::Key& /*key*/) override {}
    void on_viewer_monitors(const std::vector<protocol::Monitor>& monitors) override {
        update([&] { monitors_ = monitors; });
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
                if (error) {
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

    /// Pushes ~60 fps frames for `duration`, sending a keyframe first and whenever one is
    /// requested.
    void stream_for(milliseconds duration) {
        const auto end = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < end) {
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

TEST_F(RealSessions, StreamsFramesEndToEndOverLoopback) {
    connect(false);
    start_streaming();
    stream_for(seconds(2));
    std::this_thread::sleep_for(milliseconds(200));

    EXPECT_EQ(host_observer.state(), SessionState::Streaming);
    EXPECT_EQ(viewer_observer.frame_count(), next_index) << "a clean link delivers every frame";
    EXPECT_TRUE(viewer_observer.intact());

    viewer.reset(); // sends Bye
    EXPECT_TRUE(host_observer.wait_for_state(SessionState::Closed, seconds(3)));
}

TEST_F(RealSessions, NothingReadableCrossesTheWireAfterTheHandshake) {
    connect(true);
    start_streaming();
    stream_for(seconds(1));
    std::this_thread::sleep_for(milliseconds(200));
    ASSERT_GT(viewer_observer.frame_count(), 30U) << "media really went through the relay";

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
    ASSERT_GT(capture.datagrams.size(), 100U);
    for (const auto& datagram : capture.datagrams) {
        expect_opaque(datagram);
    }
}

TEST_F(RealSessions, SurvivesAThreeSecondNetworkInterruption) {
    connect(true);
    start_streaming();
    stream_for(seconds(1));
    const auto before = viewer_observer.frame_count();
    ASSERT_GT(before, 30U);

    relay->set_outage(true);
    stream_for(seconds(3));
    EXPECT_LE(viewer_observer.frame_count(), before + 2) << "the outage really cut media";
    relay->set_outage(false);
    stream_for(seconds(2));
    std::this_thread::sleep_for(milliseconds(200));

    EXPECT_EQ(host_observer.state(), SessionState::Streaming);
    EXPECT_EQ(viewer_observer.state(), SessionState::Streaming);
    EXPECT_GT(viewer_observer.frame_count(), before + 90) << "streaming resumed after the outage";
    EXPECT_TRUE(viewer_observer.intact());
}

} // namespace
