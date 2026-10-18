#pragma once

// The session protocol without I/O (docs/PROTOCOL.md): bytes, datagrams, time and commands go in;
// bytes, datagrams and observer callbacks come out. The Asio transport drives it on one thread;
// tests drive it with a simulated network and clock.
//
// Lifecycle: Connecting -> Handshaking -> AwaitingPairing -> Negotiating -> Streaming <-> Paused
// -> Closed. Every call must come from the same thread.

#include "crypto/media_crypto.hpp"
#include "crypto/noise.hpp"
#include "media/cursor.hpp"
#include "media/packetizer.hpp"
#include "media/rate_limiter.hpp"
#include "media/reassembler.hpp"
#include "media/retransmit_cache.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"
#include "session/control_framer.hpp"
#include "session/session_config.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace osplay::session {

enum class SessionState : std::uint8_t {
    Connecting = 1,
    Handshaking = 2,
    AwaitingPairing = 3,
    Negotiating = 4,
    Streaming = 5,
    Paused = 6,
    Closed = 7,
};

enum class TrustDecision : std::uint8_t { Trusted = 1, Unknown = 2, Blocked = 3 };
enum class LogLevel : std::uint8_t { Debug = 1, Info = 2, Warning = 3, Error = 4 };

struct StreamStats {
    std::uint8_t stream_id = 0;
    std::uint32_t frames_per_second = 0;
    std::uint32_t bitrate_kbps = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t retransmitted_fragments = 0;
    std::uint64_t keyframe_requests = 0;
    std::uint16_t loss_permille = 0;
    /// Viewer: capture to delivery, using the measured clock offset. 0 if unknown.
    std::uint32_t latency_us = 0;
};

struct SessionStats {
    std::uint32_t rtt_us = 0;
    std::int64_t clock_offset_us = 0; // peer clock minus local clock
    std::uint64_t datagrams_rejected = 0;
    std::vector<StreamStats> streams;
};

/// Callbacks from the engine. They run on the engine's thread and must not block or call back
/// into the engine synchronously, except where stated.
class SessionObserver {
public:
    virtual ~SessionObserver() = default;
    virtual void on_state(SessionState state, std::optional<Error> reason) = 0;
    virtual void on_peer(const protocol::Hello& hello) = 0;
    /// Decide quickly from in-memory data: the engine waits for the answer.
    [[nodiscard]] virtual TrustDecision on_trust_query(const crypto::Key& peer_static_key) = 0;
    /// First pairing: show the code; the user's answer goes to SessionEngine::confirm_pairing.
    virtual void on_pairing_code(std::uint32_t code, const crypto::Key& peer_static_key) = 0;
    /// Both users confirmed: store the key as trusted.
    virtual void on_paired(const crypto::Key& peer_static_key) = 0;
    virtual void on_viewer_monitors(const std::vector<protocol::Monitor>& monitors) = 0; // host
    virtual void on_stream_config(const protocol::StreamConfig& config) = 0;             // viewer
    virtual void on_keyframe_request(std::uint8_t stream_id) = 0;                        // host
    virtual void on_access_unit(const media::CompletedFrame& frame) = 0;                 // viewer
    /// Viewer: a complete cursor image; the bytes are valid only during the call.
    virtual void on_cursor_shape(const media::CursorShape& shape) = 0;
    /// Viewer: the newest cursor position (older ones are dropped).
    virtual void on_cursor_position(const protocol::CursorPosition& position) = 0;
    virtual void on_stats(const SessionStats& stats) = 0;
    virtual void on_log(LogLevel level, std::string_view message) = 0;
};

/// Where the engine's output goes.
class SessionTransport {
public:
    virtual ~SessionTransport() = default;
    virtual void send_control(std::span<const std::uint8_t> frame) = 0;
    virtual void send_datagram(std::span<const std::uint8_t> datagram) = 0;
    /// Viewer: send media to this UDP port on the host's address.
    virtual void set_media_destination_port(std::uint16_t port) = 0;
    /// Host: the datagram being processed came from the viewer; send media to its source.
    virtual void accept_media_source() = 0;
    /// Host: the local UDP port announced in MediaSetup.
    [[nodiscard]] virtual std::uint16_t local_media_port() const = 0;
    virtual void close() = 0;
};

class SessionEngine {
public:
    SessionEngine(SessionConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
                  SessionObserver& observer, SessionTransport& transport);
    ~SessionEngine();
    SessionEngine(const SessionEngine&) = delete;
    SessionEngine& operator=(const SessionEngine&) = delete;
    SessionEngine(SessionEngine&&) = delete;
    SessionEngine& operator=(SessionEngine&&) = delete;

    // --- From the transport ---
    /// The TCP connection is up; both sides send Hello.
    void on_connected(TimePoint now);
    void on_control_bytes(std::span<const std::uint8_t> bytes, TimePoint now);
    /// Decrypts in place.
    void on_datagram(std::span<std::uint8_t> datagram, TimePoint now);
    void on_timer(TimePoint now);
    void on_transport_closed(Error reason);

    // --- Commands from the platform ---
    [[nodiscard]] Status<Error> confirm_pairing(bool accepted, TimePoint now);
    /// Host, after on_viewer_monitors: one config per stream to start.
    [[nodiscard]] Status<Error>
    configure_streams(const std::vector<protocol::StreamConfig>& streams, TimePoint now);
    /// Host: sends one encoded frame.
    [[nodiscard]] Status<Error> push_access_unit(const media::AccessUnit& unit, TimePoint now);
    /// Host: a new cursor image for a stream; it is sent now and again after every keyframe.
    [[nodiscard]] Status<Error> push_cursor_shape(const media::CursorShape& shape, TimePoint now);
    /// Host: where the cursor is; positions are not retransmitted, the next one replaces it.
    [[nodiscard]] Status<Error> push_cursor_position(const protocol::CursorPosition& position,
                                                     TimePoint now);
    /// Viewer: the decoder failed; asks the host for a keyframe (rate-limited per stream).
    [[nodiscard]] Status<Error> request_keyframe(std::uint8_t stream_id, TimePoint now);
    [[nodiscard]] Status<Error> pause_stream(std::uint8_t stream_id, TimePoint now);
    [[nodiscard]] Status<Error> resume_stream(std::uint8_t stream_id, TimePoint now);
    /// Sends Bye (when the channel is encrypted) and closes.
    void close(protocol::ByeReason reason, TimePoint now);

    [[nodiscard]] SessionState state() const { return state_; }

private:
    struct HostStream;
    struct ViewerStream;
    class ReassemblyAdapter;

    // Control channel
    void handle_frame(std::span<const std::uint8_t> payload, TimePoint now);
    void handle_message(const protocol::ControlMessage& message, TimePoint now);
    [[nodiscard]] bool is_expected(protocol::ControlType type) const;
    void handle_hello(const protocol::Hello& hello);
    void handle_handshake(const protocol::HandshakeMessage& message, TimePoint now);
    void handle_pairing_confirm(const protocol::PairingConfirm& message, TimePoint now);
    void handle_stream_control(const protocol::ControlMessage& message, TimePoint now);
    void update_streaming_state();
    void handle_media_setup(const protocol::MediaSetup& message, TimePoint now);
    void handle_stream_config(const protocol::StreamConfig& config);
    void handle_nack(const protocol::Nack& nack, TimePoint now);
    void handle_keyframe_request(const protocol::KeyframeRequest& request, TimePoint now);
    void handle_ping(const protocol::Ping& ping, TimePoint now);
    void handle_pong(const protocol::Pong& pong, TimePoint now);
    void handle_peer_error(const protocol::PeerError& error);
    void send(const protocol::ControlMessage& message, TimePoint now);
    void handshake_complete(TimePoint now);
    void finish_pairing_if_agreed(TimePoint now);
    void enter_negotiation(TimePoint now);
    void start_streams_if_ready(TimePoint now);

    // Media channel
    void send_cursor_shape(const HostStream& stream);
    void deliver_cursor_shape(const protocol::CursorShapeFragment& fragment);
    void deliver_cursor_position(const protocol::CursorPosition& position, TimePoint now);
    void send_fragments(const std::vector<protocol::VideoFragment>& fragments);
    void send_udp_hello();
    [[nodiscard]] bool seal_and_send(const protocol::MediaPacket& packet);
    void deliver_fragment(const protocol::VideoFragment& fragment, TimePoint now);

    // Housekeeping
    void send_reports(TimePoint now);
    void publish_stats(TimePoint now);
    void fail(Error reason, std::optional<std::uint16_t> peer_error_code, TimePoint now);
    /// Closes without sending anything; the only way out when sending itself failed.
    void abort_session(std::optional<Error> reason);
    void set_state(SessionState state, std::optional<Error> reason = std::nullopt);
    void log(LogLevel level, std::string_view message);
    [[nodiscard]] static std::uint64_t micros(TimePoint now);

    SessionConfig config_;
    crypto::KeyPair identity_;
    crypto::RandomSource& random_;
    SessionObserver& observer_;
    SessionTransport& transport_;

    SessionState state_ = SessionState::Connecting;
    FrameDecoder frames_;
    std::vector<std::uint8_t> frame_buffer_;
    std::optional<crypto::HandshakeState> handshake_;
    std::optional<crypto::SessionKeys> keys_;
    std::unique_ptr<crypto::MediaSealer> sealer_;
    std::unique_ptr<crypto::MediaOpener> opener_;
    crypto::Key peer_static_{};
    bool peer_hello_received_ = false;
    bool closing_ = false;

    TrustDecision trust_ = TrustDecision::Unknown;
    bool local_decision_made_ = false;
    bool local_accepted_ = false;
    bool peer_accepted_ = false;

    bool monitors_received_ = false; // host
    std::optional<protocol::SessionToken> session_token_;
    bool media_peer_bound_ = false; // host: UdpHello received; viewer: MediaSetup received
    std::uint64_t next_sequence_ = 0;
    std::array<std::uint8_t, protocol::kMaxDatagramSize> datagram_{};
    std::vector<protocol::VideoFragment> fragments_;
    std::vector<protocol::CursorShapeFragment> cursor_fragments_;

    std::map<std::uint8_t, std::unique_ptr<HostStream>> host_streams_;
    std::map<std::uint8_t, std::unique_ptr<ViewerStream>> viewer_streams_;

    TimePoint connected_at_;
    TimePoint last_sent_;
    TimePoint last_received_;
    TimePoint next_ping_;
    TimePoint next_report_;
    TimePoint next_stats_;
    TimePoint next_udp_hello_;
    std::optional<TimePoint> last_media_received_;
    std::uint32_t next_ping_id_ = 0;
    std::optional<Microseconds> rtt_;
    std::int64_t clock_offset_us_ = 0;
    std::uint64_t datagrams_rejected_ = 0;
};

} // namespace osplay::session
