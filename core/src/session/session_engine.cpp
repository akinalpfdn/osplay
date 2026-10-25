#include "session/session_engine.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace osplay::session {

namespace {

using protocol::ControlMessage;

constexpr std::uint16_t kPeerErrorIncompatibleVersion = 1;
constexpr std::uint16_t kPeerErrorPairingRejected = 2;
constexpr std::uint16_t kPeerErrorNotTrusted = 3;
constexpr std::uint16_t kPeerErrorBusy = 4;
constexpr std::uint16_t kPeerErrorUnexpected = 5;
/// The first protocol minor version that knows RoleSwap.
constexpr std::uint8_t kRoleSwapMinorVersion = 1;
constexpr std::uint64_t kMicrosPerSecond = 1'000'000;
constexpr std::uint64_t kMillihertzMicros = 1'000'000'000; // frame interval = this / refresh_mhz

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

bool requires_encryption(protocol::ControlType type) {
    return type != protocol::ControlType::Hello &&
           type != protocol::ControlType::HandshakeMessage && type != protocol::ControlType::Error;
}

Error reason_for_peer_error(std::uint16_t code) {
    switch (code) {
    case kPeerErrorIncompatibleVersion:
        return Error::UnsupportedVersion;
    case kPeerErrorPairingRejected:
        return Error::PairingRejected;
    case kPeerErrorNotTrusted:
        return Error::NotTrusted;
    case kPeerErrorBusy:
        return Error::PeerBusy;
    default:
        return Error::PeerError;
    }
}

std::uint32_t per_second(std::uint64_t count, Microseconds interval) {
    const auto micros = static_cast<std::uint64_t>(std::max<std::int64_t>(interval.count(), 1));
    return static_cast<std::uint32_t>(count * kMicrosPerSecond / micros);
}

const char* state_name(SessionState state) {
    switch (state) {
    case SessionState::Connecting:
        return "connecting";
    case SessionState::Handshaking:
        return "handshaking";
    case SessionState::AwaitingPairing:
        return "awaiting pairing";
    case SessionState::Negotiating:
        return "negotiating";
    case SessionState::Streaming:
        return "streaming";
    case SessionState::Paused:
        return "paused";
    case SessionState::Closed:
        return "closed";
    }
    return "unknown";
}

std::string describe(const protocol::StreamConfig& stream) {
    return "Stream " + std::to_string(stream.stream_id) + ": " + std::to_string(stream.width) +
           "x" + std::to_string(stream.height) + " at " +
           std::to_string(stream.refresh_millihertz / 1000) + " Hz, " +
           std::to_string(stream.bitrate_kbps) + " kbps";
}

/// Totals are since the stream started; `nacks` only counts on a viewer.
std::string describe(const StreamStats& stream, std::uint32_t rtt_us, std::uint64_t nacks) {
    return "Stream " + std::to_string(stream.stream_id) + ": " +
           std::to_string(stream.frames_per_second) + " fps, " +
           std::to_string(stream.bitrate_kbps) + " kbps, loss " +
           std::to_string(stream.loss_permille / 10) + "." +
           std::to_string(stream.loss_permille % 10) + "%, rtt " + std::to_string(rtt_us / 1000) +
           " ms; total dropped " + std::to_string(stream.frames_dropped) + ", resent " +
           std::to_string(stream.retransmitted_fragments) + ", nacks " + std::to_string(nacks) +
           ", keyframe requests " + std::to_string(stream.keyframe_requests);
}

std::uint16_t loss_permille(std::uint64_t dropped, std::uint64_t completed) {
    const std::uint64_t total = dropped + completed;
    return total == 0 ? std::uint16_t{0} : static_cast<std::uint16_t>(dropped * 1000 / total);
}

} // namespace

// --- Per-stream state ----------------------------------------------------------------------------

struct SessionEngine::HostStream {
    HostStream(const protocol::StreamConfig& stream_config, const SessionTunables& tunables)
        : config(stream_config), cache(tunables.retransmit_frames, tunables.retransmit_bytes),
          bitrate(stream_config.bitrate_kbps, tunables.bitrate) {}

    protocol::StreamConfig config;
    media::Packetizer packetizer;
    media::RetransmitCache cache;
    std::optional<TimePoint> last_forced_keyframe;
    /// The newest frame sent and when; its last fragment is repeated while no newer one follows.
    std::optional<std::uint32_t> last_frame_id;
    TimePoint last_frame_sent;
    std::size_t tail_repeats = 0;
    bool started = false;
    bool paused = false;
    protocol::ReceiverReport last_report{};
    std::uint64_t frames_in_window = 0;
    std::uint64_t bytes_in_window = 0;
    std::uint64_t retransmitted = 0;
    std::uint64_t keyframe_requests = 0;
    BitrateController bitrate;
    /// Fragments sent and resent since the last receiver report.
    std::uint64_t fragments_since_report = 0;
    std::uint64_t resent_since_report = 0;
    /// The current cursor shape, resent after every keyframe; `cursor_shape.bgra` views this.
    std::vector<std::uint8_t> cursor_image;
    std::optional<media::CursorShape> cursor_shape;
};

class SessionEngine::ReassemblyAdapter final : public media::ReassemblySink {
public:
    ReassemblyAdapter(SessionEngine& engine, ViewerStream& stream)
        : engine_(&engine), stream_(&stream) {}

    void on_frame(const media::CompletedFrame& frame) override;
    void on_nack(const protocol::Nack& nack) override;
    void on_keyframe_needed() override;
    void set_now(TimePoint now) { now_ = now; }

private:
    SessionEngine* engine_; // never null; the engine owns the stream that owns this adapter
    ViewerStream* stream_;  // never null
    TimePoint now_;
};

struct SessionEngine::ViewerStream {
    ViewerStream(SessionEngine& engine, const protocol::StreamConfig& stream_config,
                 const SessionTunables& tunables)
        : config(stream_config), adapter(engine, *this),
          reassembler(stream_config.stream_id,
                      media::ReassemblerConfig{
                          .frame_interval = Microseconds{static_cast<std::int64_t>(
                              kMillihertzMicros /
                              std::max<std::uint32_t>(stream_config.refresh_millihertz, 1))},
                          .minimum_rtt = Microseconds{2'000},
                          .max_frames_in_flight = tunables.max_frames_in_flight},
                      adapter),
          keyframe_limiter(tunables.keyframe_request_interval) {}

    protocol::StreamConfig config;
    ReassemblyAdapter adapter;
    media::Reassembler reassembler;
    media::RateLimiter keyframe_limiter;
    bool started = false;
    bool paused = false;
    std::uint64_t frames_in_window = 0;
    std::uint64_t bytes_in_window = 0;
    std::uint64_t keyframe_requests = 0;
    std::uint64_t completed_at_last_report = 0;
    std::uint64_t dropped_at_last_report = 0;
    std::uint16_t last_loss_permille = 0;
    std::uint32_t last_latency_us = 0;
    media::CursorShapeAssembler cursor_assembler;
    std::optional<std::uint64_t> last_cursor_time;
    std::optional<TimePoint> missing_shape_since;
};

void SessionEngine::ReassemblyAdapter::on_frame(const media::CompletedFrame& frame) {
    ++stream_->frames_in_window;
    if (engine_->rtt_) {
        const auto host_now = static_cast<std::int64_t>(micros(now_)) + engine_->clock_offset_us_;
        const std::int64_t latency = host_now - static_cast<std::int64_t>(frame.capture_time_us);
        stream_->last_latency_us = latency > 0 ? static_cast<std::uint32_t>(latency) : 0;
    }
    engine_->observer_.on_access_unit(frame);
}

void SessionEngine::ReassemblyAdapter::on_nack(const protocol::Nack& nack) {
    engine_->send(nack, now_);
}

void SessionEngine::ReassemblyAdapter::on_keyframe_needed() {
    if (!stream_->started || stream_->paused || !stream_->keyframe_limiter.allow(now_)) {
        return;
    }
    ++stream_->keyframe_requests;
    engine_->send(protocol::KeyframeRequest{.stream_id = stream_->config.stream_id,
                                            .reason = protocol::KeyframeReason::FrameLost},
                  now_);
}

// --- Construction --------------------------------------------------------------------------------

SessionEngine::SessionEngine(SessionConfig config, crypto::KeyPair identity,
                             crypto::RandomSource& random, SessionObserver& observer,
                             SessionTransport& transport)
    : config_(std::move(config)), identity_(std::move(identity)), random_(random),
      observer_(observer), transport_(transport) {}

SessionEngine::~SessionEngine() = default;

std::uint64_t SessionEngine::micros(TimePoint now) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<Microseconds>(now.time_since_epoch()).count());
}

void SessionEngine::log(LogLevel level, std::string_view message) {
    observer_.on_log(level, message);
}

void SessionEngine::set_state(SessionState state, std::optional<Error> reason) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    std::string line = std::string("Session ") + state_name(state);
    if (reason) {
        line += " (error " + std::to_string(static_cast<int>(*reason)) + ")";
    }
    log(LogLevel::Info, line);
    observer_.on_state(state, reason);
}

void SessionEngine::abort_session(std::optional<Error> reason) {
    if (state_ == SessionState::Closed) {
        return;
    }
    closing_ = true;
    transport_.close();
    set_state(SessionState::Closed, reason);
}

void SessionEngine::fail(Error reason, std::optional<std::uint16_t> peer_error_code,
                         TimePoint now) {
    if (state_ == SessionState::Closed || closing_) {
        return;
    }
    log(LogLevel::Warning, "Session failed with error " + std::to_string(static_cast<int>(reason)));
    if (peer_error_code) {
        send(protocol::PeerError{.code = *peer_error_code}, now);
    }
    abort_session(reason);
}

// --- Control channel -----------------------------------------------------------------------------

void SessionEngine::send(const ControlMessage& message, TimePoint now) {
    const auto plaintext = protocol::encode_control(message);
    if (!plaintext) {
        log(LogLevel::Error, "Refused to send an invalid control message");
        abort_session(plaintext.error());
        return;
    }
    if (keys_) {
        const auto ciphertext = keys_->control_send.encrypt_with_ad({}, plaintext.value());
        if (!ciphertext) {
            abort_session(ciphertext.error());
            return;
        }
        write_frame(ciphertext.value(), frame_buffer_);
    } else {
        write_frame(plaintext.value(), frame_buffer_);
    }
    transport_.send_control(frame_buffer_);
    last_sent_ = now;
}

void SessionEngine::on_connected(TimePoint now) {
    if (state_ != SessionState::Connecting) {
        return;
    }
    connected_at_ = now;
    last_received_ = now;
    set_state(SessionState::Handshaking);
    send(protocol::Hello{.role = config_.role,
                         .platform = config_.platform,
                         .protocol_minor = protocol::kMinorVersion,
                         .device_name = config_.device_name},
         now);
}

void SessionEngine::on_control_bytes(std::span<const std::uint8_t> bytes, TimePoint now) {
    if (state_ == SessionState::Closed) {
        return;
    }
    frames_.append(bytes);
    while (state_ != SessionState::Closed) {
        const auto frame = frames_.next();
        if (!frame) {
            fail(frame.error(), kPeerErrorUnexpected, now);
            return;
        }
        if (frame.value().empty()) {
            return;
        }
        handle_frame(frame.value(), now);
    }
}

void SessionEngine::handle_frame(std::span<const std::uint8_t> payload, TimePoint now) {
    last_received_ = now;
    std::vector<std::uint8_t> decrypted;
    std::span<const std::uint8_t> plaintext = payload;
    if (keys_) {
        auto opened = keys_->control_receive.decrypt_with_ad({}, payload);
        if (!opened) {
            fail(opened.error(), std::nullopt, now);
            return;
        }
        decrypted = std::move(opened.value());
        plaintext = decrypted;
    }
    const auto message = protocol::decode_control(plaintext);
    if (!message) {
        const bool version = message.error() == Error::UnsupportedVersion;
        fail(message.error(), version ? kPeerErrorIncompatibleVersion : kPeerErrorUnexpected, now);
        return;
    }
    if (!keys_ && requires_encryption(protocol::type_of(message.value()))) {
        fail(Error::UnexpectedMessage, kPeerErrorUnexpected, now);
        return;
    }
    handle_message(message.value(), now);
}

bool SessionEngine::is_expected(protocol::ControlType type) const {
    using protocol::ControlType;
    const bool host = config_.role == protocol::Role::Host;
    const bool negotiated = state_ == SessionState::Negotiating ||
                            state_ == SessionState::Streaming || state_ == SessionState::Paused;
    switch (type) {
    case ControlType::Hello:
        return state_ == SessionState::Handshaking && !peer_hello_received_;
    case ControlType::HandshakeMessage:
        return state_ == SessionState::Handshaking && peer_hello_received_;
    case ControlType::PairingConfirm:
        return state_ == SessionState::AwaitingPairing;
    case ControlType::ViewerMonitors:
        return host && state_ == SessionState::Negotiating && !monitors_received_;
    case ControlType::MediaSetup:
        return !host && state_ == SessionState::Negotiating;
    case ControlType::StreamConfig:
    case ControlType::StreamStart:
        return !host && negotiated;
    case ControlType::KeyframeRequest:
    case ControlType::Nack:
    case ControlType::ReceiverReport:
        return host && negotiated;
    case ControlType::RoleSwap:
        return state_ == SessionState::Streaming || state_ == SessionState::Paused;
    case ControlType::Capabilities:
    case ControlType::StreamPause:
    case ControlType::StreamResume:
    case ControlType::StreamStop:
        return negotiated;
    case ControlType::Ping:
    case ControlType::Pong:
    case ControlType::Heartbeat:
    case ControlType::Error:
    case ControlType::Bye:
        return true; // encryption is checked before dispatch
    }
    return false;
}

void SessionEngine::handle_message(const ControlMessage& message, TimePoint now) {
    if (!is_expected(protocol::type_of(message))) {
        fail(Error::UnexpectedMessage, kPeerErrorUnexpected, now);
        return;
    }
    std::visit(Overloaded{
                   [&](const protocol::Hello& m) { handle_hello(m); },
                   [&](const protocol::HandshakeMessage& m) { handle_handshake(m, now); },
                   [&](const protocol::PairingConfirm& m) { handle_pairing_confirm(m, now); },
                   [&](const protocol::Capabilities& /*m*/) {},
                   [&](const protocol::ViewerMonitors& m) {
                       monitors_received_ = true;
                       observer_.on_viewer_monitors(m.monitors);
                   },
                   [&](const protocol::StreamConfig& m) { handle_stream_config(m); },
                   [&](const protocol::MediaSetup& m) { handle_media_setup(m, now); },
                   [&](const protocol::KeyframeRequest& m) { handle_keyframe_request(m, now); },
                   [&](const protocol::Nack& m) { handle_nack(m, now); },
                   [&](const protocol::ReceiverReport& m) { handle_receiver_report(m); },
                   [&](const protocol::Ping& m) { handle_ping(m, now); },
                   [&](const protocol::Pong& m) { handle_pong(m, now); },
                   [&](const protocol::Heartbeat& /*m*/) {},
                   [&](const protocol::PeerError& m) { handle_peer_error(m); },
                   [&](const protocol::Bye& m) {
                       // A swap the two sides agreed on is an orderly end, not a lost connection;
                       // a peer going to sleep is expected back.
                       std::optional<Error> reason = Error::ConnectionClosed;
                       if (m.reason == protocol::ByeReason::RoleSwap) {
                           reason.reset();
                       } else if (m.reason == protocol::ByeReason::Sleeping) {
                           reason = Error::PeerSleeping;
                       }
                       abort_session(reason);
                   },
                   [&](const protocol::RoleSwap& m) { handle_role_swap(m, now); },
                   [&](const auto& stream_control) { handle_stream_control(stream_control, now); },
               },
               message);
}

void SessionEngine::handle_hello(const protocol::Hello& hello) {
    const protocol::Role expected =
        config_.role == protocol::Role::Host ? protocol::Role::Viewer : protocol::Role::Host;
    if (hello.role != expected) {
        fail(Error::UnexpectedMessage, kPeerErrorUnexpected, last_received_);
        return;
    }
    peer_hello_received_ = true;
    peer_protocol_minor_ = hello.protocol_minor;
    observer_.on_peer(hello);
    const bool initiator = config_.role == protocol::Role::Viewer;
    handshake_.emplace(initiator ? crypto::HandshakeRole::Initiator
                                 : crypto::HandshakeRole::Responder,
                       identity_, std::span<const std::uint8_t>{}, random_);
    if (initiator) {
        const auto message = handshake_->write_message({});
        if (!message) {
            fail(message.error(), std::nullopt, last_received_);
            return;
        }
        send(protocol::HandshakeMessage{.step = 1, .payload = message.value()}, last_received_);
    }
}

void SessionEngine::handle_handshake(const protocol::HandshakeMessage& message, TimePoint now) {
    const bool host = config_.role == protocol::Role::Host;
    const bool expected_step = host ? (message.step == 1 || message.step == 3) : message.step == 2;
    if (!expected_step || !handshake_) {
        fail(Error::HandshakeOutOfOrder, kPeerErrorUnexpected, now);
        return;
    }
    if (const auto payload = handshake_->read_message(message.payload); !payload) {
        fail(payload.error(), kPeerErrorUnexpected, now);
        return;
    }
    if (handshake_->is_complete()) { // host after message 3
        handshake_complete(now);
        return;
    }
    const auto reply = handshake_->write_message({});
    if (!reply) {
        fail(reply.error(), kPeerErrorUnexpected, now);
        return;
    }
    const bool complete = handshake_->is_complete(); // viewer after writing message 3
    send(protocol::HandshakeMessage{.step = static_cast<std::uint8_t>(message.step + 1),
                                    .payload = reply.value()},
         now);
    if (complete && state_ != SessionState::Closed) {
        handshake_complete(now);
    }
}

void SessionEngine::handshake_complete(TimePoint now) {
    if (!handshake_) {
        fail(Error::HandshakeOutOfOrder, std::nullopt, now);
        return;
    }
    auto keys = handshake_->finish();
    handshake_.reset();
    if (!keys) {
        fail(keys.error(), std::nullopt, now);
        return;
    }
    const crypto::SessionKeys& session = keys_.emplace(std::move(keys.value()));
    sealer_ = std::make_unique<crypto::MediaSealer>(session.media_send);
    opener_ = std::make_unique<crypto::MediaOpener>(session.media_receive);
    peer_static_ = session.remote_static;
    const std::uint32_t code = crypto::pairing_code(session.handshake_hash);
    next_ping_ = now;
    next_report_ = now + config_.tunables.report_interval;
    next_stats_ = now + config_.tunables.stats_interval;

    trust_ = observer_.on_trust_query(peer_static_);
    if (trust_ == TrustDecision::Blocked) {
        fail(Error::NotTrusted, kPeerErrorNotTrusted, now);
        return;
    }
    set_state(SessionState::AwaitingPairing);
    if (trust_ == TrustDecision::Trusted) {
        local_decision_made_ = true;
        local_accepted_ = true;
        send(protocol::PairingConfirm{.decision = protocol::PairingDecision::Accepted}, now);
        finish_pairing_if_agreed(now);
    } else {
        observer_.on_pairing_code(code, peer_static_);
    }
}

Status<Error> SessionEngine::confirm_pairing(bool accepted, TimePoint now) {
    if (state_ != SessionState::AwaitingPairing || trust_ != TrustDecision::Unknown ||
        local_decision_made_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    local_decision_made_ = true;
    local_accepted_ = accepted;
    send(protocol::PairingConfirm{.decision = accepted ? protocol::PairingDecision::Accepted
                                                       : protocol::PairingDecision::Rejected},
         now);
    if (!accepted) {
        abort_session(Error::PairingRejected);
        return Status<Error>::ok();
    }
    finish_pairing_if_agreed(now);
    return Status<Error>::ok();
}

void SessionEngine::handle_pairing_confirm(const protocol::PairingConfirm& message, TimePoint now) {
    if (message.decision == protocol::PairingDecision::Rejected) {
        fail(Error::PairingRejected, std::nullopt, now);
        return;
    }
    peer_accepted_ = true;
    finish_pairing_if_agreed(now);
}

void SessionEngine::finish_pairing_if_agreed(TimePoint now) {
    if (!local_accepted_ || !peer_accepted_ || state_ != SessionState::AwaitingPairing) {
        return;
    }
    if (trust_ == TrustDecision::Unknown) {
        observer_.on_paired(peer_static_);
    }
    enter_negotiation(now);
}

void SessionEngine::enter_negotiation(TimePoint now) {
    set_state(SessionState::Negotiating);
    send(config_.capabilities, now);
    if (config_.role == protocol::Role::Viewer) {
        send(protocol::ViewerMonitors{.monitors = config_.monitors}, now);
    }
}

Status<Error> SessionEngine::configure_streams(const std::vector<protocol::StreamConfig>& streams,
                                               TimePoint now) {
    if (config_.role != protocol::Role::Host || state_ != SessionState::Negotiating ||
        !monitors_received_ || !host_streams_.empty()) {
        return Status<Error>::failure(Error::InvalidState);
    }
    std::set<std::uint8_t> ids;
    if (streams.empty() || streams.size() > protocol::kMaxStreams) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    for (const auto& stream : streams) {
        if (!ids.insert(stream.stream_id).second || !protocol::encode_control(stream)) {
            return Status<Error>::failure(Error::InvalidArgument);
        }
    }
    protocol::SessionToken token{};
    if (!random_.fill(token)) {
        return Status<Error>::failure(Error::RandomUnavailable);
    }
    session_token_ = token;
    for (const auto& stream : streams) {
        host_streams_.emplace(stream.stream_id,
                              std::make_unique<HostStream>(stream, config_.tunables));
        log(LogLevel::Info, describe(stream));
    }
    send(protocol::MediaSetup{.udp_port = transport_.local_media_port(), .session_token = token},
         now);
    for (const auto& stream : streams) {
        send(stream, now);
    }
    return Status<Error>::ok();
}

void SessionEngine::handle_media_setup(const protocol::MediaSetup& message, TimePoint now) {
    session_token_ = message.session_token;
    media_peer_bound_ = true;
    transport_.set_media_destination_port(message.udp_port);
    send_udp_hello();
    next_udp_hello_ = now + config_.tunables.udp_hello_interval;
}

void SessionEngine::handle_stream_config(const protocol::StreamConfig& config) {
    viewer_streams_.erase(config.stream_id);
    viewer_streams_.emplace(config.stream_id,
                            std::make_unique<ViewerStream>(*this, config, config_.tunables));
    log(LogLevel::Info, describe(config));
    observer_.on_stream_config(config);
}

void SessionEngine::handle_stream_control(const ControlMessage& message, TimePoint now) {
    const bool host = config_.role == protocol::Role::Host;
    const auto with_stream = [&](std::uint8_t stream_id, auto&& action) {
        if (host) {
            if (const auto it = host_streams_.find(stream_id); it != host_streams_.end()) {
                action(*it->second);
                return;
            }
        } else if (const auto it = viewer_streams_.find(stream_id); it != viewer_streams_.end()) {
            action(*it->second);
            return;
        }
        log(LogLevel::Warning, "Stream control for an unknown stream");
    };

    std::visit(Overloaded{
                   [&](const protocol::StreamStart& m) {
                       with_stream(m.stream_id, [&](auto& stream) { stream.started = true; });
                       if (state_ == SessionState::Negotiating) {
                           set_state(SessionState::Streaming);
                       }
                   },
                   [&](const protocol::StreamPause& m) {
                       with_stream(m.stream_id, [&](auto& stream) { stream.paused = true; });
                   },
                   [&](const protocol::StreamResume& m) {
                       with_stream(m.stream_id, [&](auto& stream) {
                           stream.paused = false;
                           if constexpr (std::is_same_v<std::remove_cvref_t<decltype(stream)>,
                                                        HostStream>) {
                               observer_.on_keyframe_request(m.stream_id);
                           } else {
                               stream.reassembler.reset();
                           }
                       });
                   },
                   [&](const protocol::StreamStop& m) {
                       const std::size_t erased = host ? host_streams_.erase(m.stream_id)
                                                       : viewer_streams_.erase(m.stream_id);
                       if (erased > 0) {
                           observer_.on_stream_stopped(m.stream_id);
                           update_streaming_state();
                       }
                   },
                   [&](const auto& /*other*/) {},
               },
               message);
    static_cast<void>(now);
    update_streaming_state();
}

void SessionEngine::update_streaming_state() {
    if (state_ != SessionState::Streaming && state_ != SessionState::Paused) {
        return;
    }
    const auto active = [](const auto& streams) {
        return std::ranges::any_of(streams,
                                   [](const auto& entry) { return !entry.second->paused; });
    };
    const bool any_active =
        config_.role == protocol::Role::Host ? active(host_streams_) : active(viewer_streams_);
    set_state(any_active ? SessionState::Streaming : SessionState::Paused);
}

Status<Error> SessionEngine::request_keyframe(std::uint8_t stream_id, TimePoint now) {
    const auto it = viewer_streams_.find(stream_id);
    if (config_.role != protocol::Role::Viewer || it == viewer_streams_.end() ||
        (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    ViewerStream& stream = *it->second;
    if (stream.started && !stream.paused && stream.keyframe_limiter.allow(now)) {
        ++stream.keyframe_requests;
        send(protocol::KeyframeRequest{.stream_id = stream_id,
                                       .reason = protocol::KeyframeReason::DecoderError},
             now);
    }
    return Status<Error>::ok();
}

Status<Error> SessionEngine::pause_stream(std::uint8_t stream_id, TimePoint now) {
    const bool host = config_.role == protocol::Role::Host;
    const bool known =
        host ? host_streams_.contains(stream_id) : viewer_streams_.contains(stream_id);
    if (!known || (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (host) {
        host_streams_.at(stream_id)->paused = true;
    } else {
        viewer_streams_.at(stream_id)->paused = true;
    }
    send(protocol::StreamPause{.stream_id = stream_id}, now);
    update_streaming_state();
    return Status<Error>::ok();
}

Status<Error> SessionEngine::stop_stream(std::uint8_t stream_id, TimePoint now) {
    const bool host = config_.role == protocol::Role::Host;
    const bool known =
        host ? host_streams_.contains(stream_id) : viewer_streams_.contains(stream_id);
    if (!known || (state_ != SessionState::Negotiating && state_ != SessionState::Streaming &&
                   state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (host) {
        host_streams_.erase(stream_id);
    } else {
        viewer_streams_.erase(stream_id);
    }
    send(protocol::StreamStop{.stream_id = stream_id}, now);
    update_streaming_state();
    return Status<Error>::ok();
}

Status<Error> SessionEngine::resume_stream(std::uint8_t stream_id, TimePoint now) {
    const bool host = config_.role == protocol::Role::Host;
    const bool known =
        host ? host_streams_.contains(stream_id) : viewer_streams_.contains(stream_id);
    if (!known || (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    send(protocol::StreamResume{.stream_id = stream_id}, now);
    if (host) {
        host_streams_.at(stream_id)->paused = false;
        observer_.on_keyframe_request(stream_id);
    } else {
        auto& stream = *viewer_streams_.at(stream_id);
        stream.paused = false;
        stream.reassembler.reset();
    }
    update_streaming_state();
    return Status<Error>::ok();
}

void SessionEngine::handle_keyframe_request(const protocol::KeyframeRequest& request,
                                            TimePoint now) {
    const auto it = host_streams_.find(request.stream_id);
    if (it == host_streams_.end()) {
        return;
    }
    HostStream& stream = *it->second;
    if (stream.last_forced_keyframe &&
        now - *stream.last_forced_keyframe < config_.tunables.keyframe_coalesce_interval) {
        return;
    }
    stream.last_forced_keyframe = now;
    ++stream.keyframe_requests;
    observer_.on_keyframe_request(request.stream_id);
}

void SessionEngine::handle_nack(const protocol::Nack& nack, TimePoint /*now*/) {
    const auto it = host_streams_.find(nack.stream_id);
    if (it == host_streams_.end()) {
        return;
    }
    HostStream& stream = *it->second;
    const media::AccessUnit* unit = stream.cache.find(nack.frame_id);
    if (unit == nullptr) {
        return; // evicted: the viewer will drop the frame and ask for a keyframe
    }
    for (const auto& range : nack.ranges) {
        media::Packetizer::fragments(nack.frame_id, *unit, range.first_fragment, range.count,
                                     fragments_);
        stream.retransmitted += fragments_.size();
        stream.resent_since_report += fragments_.size();
        send_fragments(fragments_);
    }
}

void SessionEngine::handle_ping(const protocol::Ping& ping, TimePoint now) {
    send(protocol::Pong{.ping_id = ping.ping_id,
                        .ping_send_time_us = ping.send_time_us,
                        .receive_time_us = micros(now),
                        .reply_time_us = micros(now)},
         now);
}

void SessionEngine::handle_pong(const protocol::Pong& pong, TimePoint now) {
    const auto t0 = static_cast<std::int64_t>(pong.ping_send_time_us);
    const auto t1 = static_cast<std::int64_t>(pong.receive_time_us);
    const auto t2 = static_cast<std::int64_t>(pong.reply_time_us);
    const auto t3 = static_cast<std::int64_t>(micros(now));
    const std::int64_t sample = std::max<std::int64_t>((t3 - t0) - (t2 - t1), 0);
    const std::int64_t offset = ((t1 - t0) + (t2 - t3)) / 2;
    rtt_ = rtt_ ? Microseconds{((rtt_->count() * 7) + sample) / 8} : Microseconds{sample};
    clock_offsets_.add(sample, offset);
    clock_offset_us_ = clock_offsets_.offset_us().value_or(offset);
    for (auto& [id, stream] : viewer_streams_) {
        stream->reassembler.set_rtt(*rtt_);
    }
}

void SessionEngine::handle_peer_error(const protocol::PeerError& error) {
    log(LogLevel::Warning, "Peer reported error " + std::to_string(error.code));
    abort_session(reason_for_peer_error(error.code));
}

Status<Error> SessionEngine::request_role_swap(TimePoint now) {
    if ((state_ != SessionState::Streaming && state_ != SessionState::Paused) || closing_ ||
        role_swap_requested_at_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (peer_protocol_minor_ < kRoleSwapMinorVersion) {
        return Status<Error>::failure(Error::UnsupportedVersion); // the peer would not know it
    }
    role_swap_requested_at_ = now;
    send(protocol::RoleSwap{.step = protocol::RoleSwapStep::Request}, now);
    return Status<Error>::ok();
}

Status<Error> SessionEngine::answer_role_swap(RoleSwapAnswer answer, TimePoint now) {
    if (!role_swap_offered_ || closing_ || state_ == SessionState::Closed ||
        answer == RoleSwapAnswer::NoAnswer) {
        return Status<Error>::failure(answer == RoleSwapAnswer::NoAnswer ? Error::InvalidArgument
                                                                         : Error::InvalidState);
    }
    role_swap_offered_ = false;
    if (answer == RoleSwapAnswer::Accepted) {
        send(protocol::RoleSwap{.step = protocol::RoleSwapStep::Accept}, now);
        close(protocol::ByeReason::RoleSwap, now);
        return Status<Error>::ok();
    }
    send(protocol::RoleSwap{.step = protocol::RoleSwapStep::Refuse,
                            .refusal = answer == RoleSwapAnswer::CannotHost
                                           ? protocol::RoleSwapRefusal::CannotHost
                                           : protocol::RoleSwapRefusal::Busy},
         now);
    return Status<Error>::ok();
}

void SessionEngine::handle_role_swap(const protocol::RoleSwap& message, TimePoint now) {
    switch (message.step) {
    case protocol::RoleSwapStep::Request:
        if (role_swap_requested_at_) {
            // Both asked at once: they agree. The host answers for both; the viewer waits for it.
            if (config_.role == protocol::Role::Host) {
                role_swap_requested_at_.reset();
                observer_.on_role_swap_answer(RoleSwapAnswer::Accepted, peer_static_);
                role_swap_offered_ = true;
                static_cast<void>(answer_role_swap(RoleSwapAnswer::Accepted, now));
            }
            return;
        }
        if (role_swap_offered_) {
            fail(Error::UnexpectedMessage, kPeerErrorUnexpected, now);
            return;
        }
        role_swap_offered_ = true;
        observer_.on_role_swap_request(peer_static_);
        return;
    case protocol::RoleSwapStep::Accept:
    case protocol::RoleSwapStep::Refuse: {
        if (!role_swap_requested_at_) {
            fail(Error::UnexpectedMessage, kPeerErrorUnexpected, now);
            return;
        }
        role_swap_requested_at_.reset();
        RoleSwapAnswer answer = RoleSwapAnswer::Accepted;
        if (message.step == protocol::RoleSwapStep::Refuse) {
            answer = message.refusal == protocol::RoleSwapRefusal::CannotHost
                         ? RoleSwapAnswer::CannotHost
                         : RoleSwapAnswer::Busy;
        }
        observer_.on_role_swap_answer(answer, peer_static_);
        return;
    }
    }
}

void SessionEngine::handle_receiver_report(const protocol::ReceiverReport& report) {
    const auto it = host_streams_.find(report.stream_id);
    if (it == host_streams_.end()) {
        return;
    }
    HostStream& stream = *it->second;
    stream.last_report = report;
    const auto target = stream.bitrate.on_report(
        report.loss_permille, stream.fragments_since_report, stream.resent_since_report);
    stream.fragments_since_report = 0;
    stream.resent_since_report = 0;
    if (target) {
        log(LogLevel::Info, "Stream " + std::to_string(report.stream_id) + " bitrate target " +
                                std::to_string(*target) + " kbps");
        observer_.on_bitrate_target(report.stream_id, *target);
    }
}

void SessionEngine::expire_role_swap(TimePoint now) {
    if (role_swap_requested_at_ &&
        now - *role_swap_requested_at_ >= config_.tunables.role_swap_timeout) {
        role_swap_requested_at_.reset();
        observer_.on_role_swap_answer(RoleSwapAnswer::NoAnswer, peer_static_);
    }
}

void SessionEngine::close(protocol::ByeReason reason, TimePoint now) {
    if (state_ == SessionState::Closed || closing_) {
        return;
    }
    if (keys_) {
        send(protocol::Bye{.reason = reason}, now);
    }
    abort_session(std::nullopt);
}

void SessionEngine::on_transport_closed(Error reason) {
    abort_session(reason);
}

// --- Media channel -------------------------------------------------------------------------------

bool SessionEngine::seal_and_send(const protocol::MediaPacket& packet) {
    if (!sealer_) {
        return false;
    }
    const auto size = protocol::encode_media(packet, datagram_);
    if (!size) {
        return false;
    }
    const auto sealed = sealer_->seal(datagram_, size.value());
    if (!sealed) {
        return false;
    }
    transport_.send_datagram(std::span(datagram_).first(sealed.value()));
    return true;
}

Microseconds SessionEngine::rate_limited_window(std::size_t bytes) const {
    std::uint64_t total_kbps = 0;
    for (const auto& [id, stream] : host_streams_) {
        total_kbps += stream->bitrate.target_kbps();
    }
    const double bits_per_second =
        static_cast<double>(total_kbps) * 1000.0 * config_.tunables.pacing_rate_factor;
    if (bits_per_second <= 0) {
        return Microseconds{0};
    }
    return Microseconds{static_cast<std::int64_t>(static_cast<double>(bytes) * 8.0 * 1'000'000.0 /
                                                  bits_per_second)};
}

void SessionEngine::send_paced(TimePoint now) {
    while (!paced_.empty()) {
        PacedFrame& frame = paced_.front();
        if (now < frame.start) {
            return;
        }
        // The first fragment leaves at once, the rest evenly over the window.
        const auto elapsed = std::chrono::duration_cast<Microseconds>(now - frame.start);
        std::uint16_t due = frame.count;
        if (frame.window.count() > 0 && elapsed < frame.window) {
            due = static_cast<std::uint16_t>(std::min<std::int64_t>(
                frame.count,
                1 + (std::int64_t{frame.count} * elapsed.count() / frame.window.count())));
        }
        const auto stream = host_streams_.find(frame.stream_id);
        const media::AccessUnit* unit =
            stream == host_streams_.end() ? nullptr : stream->second->cache.find(frame.frame_id);
        if (unit == nullptr) {
            paced_.pop_front(); // the stream stopped, or the frame was evicted: nothing to send
            continue;
        }
        if (due > frame.next) {
            media::Packetizer::fragments(frame.frame_id, *unit, frame.next,
                                         static_cast<std::uint16_t>(due - frame.next), fragments_);
            send_fragments(fragments_);
            frame.next = due;
        }
        if (frame.next < frame.count) {
            return;
        }
        paced_.pop_front();
    }
}

void SessionEngine::send_fragments(const std::vector<protocol::VideoFragment>& fragments) {
    for (const auto& fragment : fragments) {
        if (!seal_and_send({.sequence = next_sequence_++, .body = fragment})) {
            log(LogLevel::Error, "Could not send a video fragment");
            return;
        }
    }
}

Status<Error> SessionEngine::push_cursor_shape(const media::CursorShape& shape, TimePoint now) {
    static_cast<void>(now);
    const auto it = host_streams_.find(shape.stream_id);
    if (config_.role != protocol::Role::Host || it == host_streams_.end() ||
        (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (const auto valid = media::split_cursor_shape(shape, cursor_fragments_); !valid) {
        return valid;
    }
    HostStream& stream = *it->second;
    stream.cursor_image.assign(shape.bgra.begin(), shape.bgra.end());
    stream.cursor_shape = shape;
    stream.cursor_shape->bgra = stream.cursor_image;
    send_cursor_shape(stream);
    return Status<Error>::ok();
}

Status<Error> SessionEngine::push_cursor_position(const protocol::CursorPosition& position,
                                                  TimePoint now) {
    static_cast<void>(now);
    const auto it = host_streams_.find(position.stream_id);
    if (config_.role != protocol::Role::Host || it == host_streams_.end() ||
        (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    const HostStream& stream = *it->second;
    if (stream.started && !stream.paused &&
        !seal_and_send({.sequence = next_sequence_++, .body = position})) {
        log(LogLevel::Error, "Could not send a cursor position");
    }
    return Status<Error>::ok();
}

void SessionEngine::send_cursor_shape(const HostStream& stream) {
    if (!stream.cursor_shape || !stream.started || stream.paused) {
        return;
    }
    if (!media::split_cursor_shape(*stream.cursor_shape, cursor_fragments_)) {
        return; // validated when it was pushed
    }
    for (const auto& fragment : cursor_fragments_) {
        if (!seal_and_send({.sequence = next_sequence_++, .body = fragment})) {
            log(LogLevel::Error, "Could not send a cursor shape fragment");
            return;
        }
    }
}

void SessionEngine::deliver_cursor_shape(const protocol::CursorShapeFragment& fragment) {
    const auto it = viewer_streams_.find(fragment.stream_id);
    if (it == viewer_streams_.end() || !it->second->started) {
        return;
    }
    if (const auto shape = it->second->cursor_assembler.add(fragment)) {
        observer_.on_cursor_shape(*shape);
    }
}

void SessionEngine::deliver_cursor_position(const protocol::CursorPosition& position,
                                            TimePoint now) {
    const auto it = viewer_streams_.find(position.stream_id);
    if (it == viewer_streams_.end() || !it->second->started) {
        return;
    }
    ViewerStream& stream = *it->second;
    if (stream.last_cursor_time && position.capture_time_us < *stream.last_cursor_time) {
        return; // reordered: a newer position was already shown
    }
    stream.last_cursor_time = position.capture_time_us;
    observer_.on_cursor_position(position);

    // The shape travels without retransmission; when it was lost, a keyframe brings it again.
    if (!position.visible || stream.cursor_assembler.completed_shape() == position.shape_id) {
        stream.missing_shape_since.reset();
        return;
    }
    if (!stream.missing_shape_since) {
        stream.missing_shape_since = now;
    } else if (now - *stream.missing_shape_since >= config_.tunables.missing_cursor_shape_grace) {
        stream.adapter.set_now(now);
        stream.adapter.on_keyframe_needed();
    }
}

void SessionEngine::send_udp_hello() {
    if (session_token_ &&
        !seal_and_send({.sequence = next_sequence_++,
                        .body = protocol::UdpHello{.session_token = *session_token_}})) {
        log(LogLevel::Error, "Could not send UdpHello");
    }
}

Status<Error> SessionEngine::push_access_unit(const media::AccessUnit& unit, TimePoint now) {
    const auto it = host_streams_.find(unit.stream_id);
    if (config_.role != protocol::Role::Host || it == host_streams_.end() ||
        (state_ != SessionState::Streaming && state_ != SessionState::Paused)) {
        return Status<Error>::failure(Error::InvalidState);
    }
    HostStream& stream = *it->second;
    if (!stream.started || stream.paused) {
        return Status<Error>::ok(); // dropped on purpose: nobody is watching
    }
    const auto frame_id = stream.packetizer.packetize(unit, fragments_);
    if (!frame_id) {
        return Status<Error>::failure(frame_id.error());
    }
    stream.cache.store(frame_id.value(), unit);
    stream.fragments_since_report += fragments_.size();
    if (fragments_.size() <= config_.tunables.pacing_minimum_fragments) {
        send_fragments(fragments_);
    } else {
        const auto interval = Microseconds{static_cast<std::int64_t>(
            kMillihertzMicros / std::max<std::uint32_t>(stream.config.refresh_millihertz, 1))};
        // Streams share the queue, so each gets its part of the window; otherwise two streams at
        // half an interval each would fill it and the queue would only grow.
        auto window = std::chrono::duration_cast<Microseconds>(
            interval * config_.tunables.pacing_fraction /
            static_cast<double>(std::max<std::size_t>(host_streams_.size(), 1)));
        const TimePoint start = std::max(now, paced_until_);
        // A large frame (a keyframe of a big monitor) at the window above would leave at hundreds
        // of Mbit/s; rate-limited it takes longer, unless the queue is already long.
        if (start - now < config_.tunables.pacing_max_queue) {
            window = std::max(window, rate_limited_window(unit.data.size()));
        }
        paced_.push_back({.stream_id = unit.stream_id,
                          .frame_id = frame_id.value(),
                          .next = 0,
                          .count = static_cast<std::uint16_t>(fragments_.size()),
                          .start = start,
                          .window = window});
        paced_until_ = start + window;
        send_paced(now);
    }
    stream.last_frame_id = frame_id.value();
    stream.last_frame_sent = now;
    stream.tail_repeats = 0;
    if (unit.keyframe) {
        send_cursor_shape(stream); // a viewer that joined or lost packets gets the cursor back
    }
    ++stream.frames_in_window;
    stream.bytes_in_window += unit.data.size();
    return Status<Error>::ok();
}

void SessionEngine::on_datagram(std::span<std::uint8_t> datagram, TimePoint now) {
    if (state_ == SessionState::Closed || !opener_) {
        ++datagrams_rejected_;
        return;
    }
    const auto plaintext = opener_->open(datagram);
    if (!plaintext) {
        ++datagrams_rejected_;
        return;
    }
    const auto packet = protocol::decode_media(plaintext.value());
    if (!packet) {
        ++datagrams_rejected_;
        return;
    }
    const bool host = config_.role == protocol::Role::Host;
    std::visit(
        Overloaded{
            [&](const protocol::UdpHello& hello) {
                if (!host || !session_token_ || hello.session_token != *session_token_) {
                    ++datagrams_rejected_;
                    return;
                }
                transport_.accept_media_source(); // also follows the viewer to a new address
                media_peer_bound_ = true;
                start_streams_if_ready(now);
            },
            [&](const protocol::VideoFragment& fragment) {
                host ? static_cast<void>(++datagrams_rejected_) : deliver_fragment(fragment, now);
            },
            [&](const protocol::CursorPosition& position) {
                host ? static_cast<void>(++datagrams_rejected_)
                     : deliver_cursor_position(position, now);
            },
            [&](const protocol::CursorShapeFragment& fragment) {
                host ? static_cast<void>(++datagrams_rejected_) : deliver_cursor_shape(fragment);
            },
        },
        packet.value().body);
}

void SessionEngine::start_streams_if_ready(TimePoint now) {
    bool started_any = false;
    for (auto& [id, stream] : host_streams_) {
        if (!stream->started) {
            stream->started = true;
            started_any = true;
            send(protocol::StreamStart{.stream_id = id}, now);
            observer_.on_keyframe_request(id);
        }
    }
    if (started_any && state_ == SessionState::Negotiating) {
        set_state(SessionState::Streaming);
    }
}

void SessionEngine::deliver_fragment(const protocol::VideoFragment& fragment, TimePoint now) {
    last_media_received_ = now;
    const auto it = viewer_streams_.find(fragment.stream_id);
    if (it == viewer_streams_.end() || !it->second->started || it->second->paused) {
        return;
    }
    ViewerStream& stream = *it->second;
    stream.bytes_in_window += fragment.payload.size();
    stream.adapter.set_now(now);
    stream.reassembler.add(fragment, now);
}

// --- Housekeeping --------------------------------------------------------------------------------

void SessionEngine::repeat_tails(TimePoint now) {
    if (config_.role != protocol::Role::Host) {
        return;
    }
    const auto& delays = config_.tunables.tail_repeat_delays;
    for (auto& [id, stream] : host_streams_) {
        const std::optional<std::uint32_t> frame_id = stream->last_frame_id;
        if (!frame_id || !stream->started || stream->paused ||
            stream->tail_repeats >= delays.size() ||
            now - stream->last_frame_sent < delays.at(stream->tail_repeats)) {
            continue;
        }
        ++stream->tail_repeats;
        const media::AccessUnit* unit = stream->cache.find(*frame_id);
        if (unit == nullptr) {
            continue;
        }
        media::Packetizer::fragments(*frame_id, *unit, 0, std::numeric_limits<std::uint16_t>::max(),
                                     fragments_);
        if (!fragments_.empty()) {
            const protocol::VideoFragment last = fragments_.back();
            fragments_.assign(1, last);
            send_fragments(fragments_);
        }
    }
}

void SessionEngine::on_timer(TimePoint now) {
    if (state_ == SessionState::Closed) {
        return;
    }
    const SessionTunables& tunables = config_.tunables;
    if (!keys_) {
        if (state_ == SessionState::Handshaking &&
            now - connected_at_ > tunables.handshake_timeout) {
            fail(Error::TimedOut, std::nullopt, now);
        }
        return;
    }
    if (now - last_received_ > tunables.heartbeat_timeout) {
        fail(Error::TimedOut, std::nullopt, now);
        return;
    }
    if (now >= next_ping_) {
        send(protocol::Ping{.ping_id = next_ping_id_++, .send_time_us = micros(now)}, now);
        next_ping_ = now + tunables.ping_interval;
    }
    if (now - last_sent_ >= tunables.heartbeat_interval) {
        send(protocol::Heartbeat{}, now);
    }

    if (config_.role == protocol::Role::Viewer) {
        if (media_peer_bound_ && now >= next_udp_hello_) {
            send_udp_hello();
            // Fast until media flows, then a slow keepalive for the association.
            const bool flowing =
                last_media_received_ && now - *last_media_received_ < tunables.heartbeat_interval;
            next_udp_hello_ =
                now + (flowing ? tunables.heartbeat_interval : tunables.udp_hello_interval);
        }
        for (auto& [id, stream] : viewer_streams_) {
            if (stream->started && !stream->paused) {
                stream->adapter.set_now(now);
                stream->reassembler.tick(now);
            }
        }
        if (now >= next_report_) {
            send_reports(now);
            next_report_ = now + tunables.report_interval;
        }
    }
    expire_role_swap(now);
    send_paced(now);
    repeat_tails(now);
    if (now >= next_stats_) {
        publish_stats(now);
        next_stats_ = now + tunables.stats_interval;
    }
}

void SessionEngine::send_reports(TimePoint now) {
    for (auto& [id, stream] : viewer_streams_) {
        if (!stream->started) {
            continue;
        }
        const auto& stats = stream->reassembler.stats();
        stream->last_loss_permille =
            loss_permille(stats.frames_dropped - stream->dropped_at_last_report,
                          stats.frames_completed - stream->completed_at_last_report);
        stream->dropped_at_last_report = stats.frames_dropped;
        stream->completed_at_last_report = stats.frames_completed;
        send(protocol::ReceiverReport{.stream_id = id,
                                      .loss_permille = stream->last_loss_permille,
                                      .frames_received =
                                          static_cast<std::uint32_t>(stats.frames_completed),
                                      .frames_dropped =
                                          static_cast<std::uint32_t>(stats.frames_dropped),
                                      .decode_time_us = 0,
                                      .jitter_us = 0},
             now);
    }
}

void SessionEngine::publish_stats(TimePoint now) {
    const Microseconds interval = config_.tunables.stats_interval;
    SessionStats stats{.rtt_us = rtt_ ? static_cast<std::uint32_t>(rtt_->count()) : 0,
                       .clock_offset_us = clock_offset_us_,
                       .datagrams_rejected = datagrams_rejected_,
                       .streams = {}};
    for (auto& [id, stream] : host_streams_) {
        stats.streams.push_back(
            {.stream_id = id,
             .frames_per_second = per_second(stream->frames_in_window, interval),
             .bitrate_kbps = per_second(stream->bytes_in_window * 8, interval) / 1000,
             .frames_dropped = stream->last_report.frames_dropped,
             .retransmitted_fragments = stream->retransmitted,
             .keyframe_requests = stream->keyframe_requests,
             .loss_permille = stream->last_report.loss_permille,
             .latency_us = 0});
        stream->frames_in_window = 0;
        stream->bytes_in_window = 0;
    }
    for (auto& [id, stream] : viewer_streams_) {
        stats.streams.push_back(
            {.stream_id = id,
             .frames_per_second = per_second(stream->frames_in_window, interval),
             .bitrate_kbps = per_second(stream->bytes_in_window * 8, interval) / 1000,
             .frames_dropped = stream->reassembler.stats().frames_dropped,
             .retransmitted_fragments = 0,
             .keyframe_requests = stream->keyframe_requests,
             .loss_permille = stream->last_loss_permille,
             .latency_us = stream->last_latency_us});
        stream->frames_in_window = 0;
        stream->bytes_in_window = 0;
    }
    if (now >= next_stats_log_) {
        next_stats_log_ = now + config_.tunables.stats_log_interval;
        for (const auto& stream : stats.streams) {
            const auto viewer = viewer_streams_.find(stream.stream_id);
            const std::uint64_t nacks = viewer == viewer_streams_.end()
                                            ? 0
                                            : viewer->second->reassembler.stats().nacks_sent;
            log(LogLevel::Info, describe(stream, stats.rtt_us, nacks));
        }
    }
    observer_.on_stats(stats);
}

} // namespace osplay::session
