#pragma once

// A deterministic two-peer network for SessionEngine tests: simulated clock, reliable ordered
// control channel (like TCP: delayed during an outage, never lost), lossy datagrams (like UDP:
// dropped at random and during an outage), and a capture of every byte on the wire.

#include "crypto_test_support.hpp"
#include "session/session_engine.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace osplay::test {

using session::SessionEngine;
using session::SessionState;
using session::TrustDecision;
using std::chrono::milliseconds;

/// Marker embedded in every synthetic access unit; must never appear on the wire.
inline constexpr std::array<std::uint8_t, 16> kPlaintextMarker{
    'O', 'S', 'P', 'L', 'A', 'Y', '-', 'P', 'L', 'A', 'I', 'N', 'T', 'E', 'X', 'T'};

inline std::vector<std::uint8_t> synthetic_frame(std::uint32_t index, bool keyframe) {
    std::vector<std::uint8_t> bytes(keyframe ? 30'000 : 5'000);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(index * 31 + i * 7);
    }
    std::memcpy(bytes.data(), &index, sizeof(index));
    std::copy(kPlaintextMarker.begin(), kPlaintextMarker.end(), bytes.begin() + 8);
    return bytes;
}

class SimulatedNetwork;

class RecordingObserver final : public session::SessionObserver {
public:
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> cursor_shapes;
    std::vector<std::uint8_t> stopped_streams;
    std::vector<protocol::CursorPosition> cursor_positions;
    TrustDecision trust = TrustDecision::Trusted;
    std::vector<SessionState> states;
    std::optional<Error> close_reason;
    std::vector<std::uint32_t> pairing_codes;
    std::vector<crypto::Key> paired;
    std::vector<protocol::Monitor> monitors;
    std::vector<protocol::StreamConfig> stream_configs;
    int keyframe_requests = 0;
    std::vector<std::uint32_t> frame_indices; // decoded from delivered access units
    bool frames_intact = true;
    std::optional<session::SessionStats> stats;
    int role_swap_requests = 0;
    std::vector<session::RoleSwapAnswer> role_swap_answers;
    std::optional<crypto::Key> role_swap_peer;

    void on_state(SessionState state, std::optional<Error> reason) override {
        states.push_back(state);
        if (state == SessionState::Closed) {
            close_reason = reason;
        }
    }
    void on_peer(const protocol::Hello& /*hello*/) override {}
    TrustDecision on_trust_query(const crypto::Key& /*key*/) override { return trust; }
    void on_pairing_code(std::uint32_t code, const crypto::Key& /*key*/) override {
        pairing_codes.push_back(code);
    }
    void on_paired(const crypto::Key& key) override { paired.push_back(key); }
    void on_viewer_monitors(const std::vector<protocol::Monitor>& offered) override {
        monitors = offered;
    }
    void on_stream_config(const protocol::StreamConfig& config) override {
        stream_configs.push_back(config);
    }
    void on_keyframe_request(std::uint8_t /*stream_id*/) override { ++keyframe_requests; }
    void on_stream_stopped(std::uint8_t stream_id) override {
        stopped_streams.push_back(stream_id);
    }
    void on_access_unit(const media::CompletedFrame& frame) override {
        std::uint32_t index = 0;
        std::memcpy(&index, frame.data.data(), sizeof(index));
        const auto expected = synthetic_frame(index, frame.keyframe);
        frames_intact = frames_intact && std::ranges::equal(frame.data, expected);
        frame_indices.push_back(index);
    }
    void on_cursor_shape(const media::CursorShape& shape) override {
        cursor_shapes.push_back(
            {shape.shape_id, std::vector<std::uint8_t>(shape.bgra.begin(), shape.bgra.end())});
    }
    void on_cursor_position(const protocol::CursorPosition& position) override {
        cursor_positions.push_back(position);
    }
    void on_stats(const session::SessionStats& latest) override { stats = latest; }
    void on_role_swap_request(const crypto::Key& key) override {
        ++role_swap_requests;
        role_swap_peer = key;
    }
    void on_role_swap_answer(session::RoleSwapAnswer answer, const crypto::Key& key) override {
        role_swap_answers.push_back(answer);
        role_swap_peer = key;
    }
    void on_log(session::LogLevel /*level*/, std::string_view /*message*/) override {}

    [[nodiscard]] SessionState state() const {
        return states.empty() ? SessionState::Connecting : states.back();
    }
};

class SimulatedTransport final : public session::SessionTransport {
public:
    SimulatedTransport(SimulatedNetwork& network, int side) : network_(network), side_(side) {}

    void send_control(std::span<const std::uint8_t> frame) override;
    void send_datagram(std::span<const std::uint8_t> datagram) override;
    void set_media_destination_port(std::uint16_t port) override { media_port = port; }
    void accept_media_source() override { media_source_accepted = true; }
    [[nodiscard]] std::uint16_t local_media_port() const override { return 40'000; }
    void close() override;

    std::optional<std::uint16_t> media_port;
    bool media_source_accepted = false;
    bool closed = false;

private:
    SimulatedNetwork& network_;
    int side_;
};

struct Peer {
    Peer(SimulatedNetwork& network, int side, session::SessionConfig config, std::uint8_t seed)
        : transport(network, side), random(seed),
          engine(std::move(config), key_pair(random), random, observer, transport) {}

    RecordingObserver observer;
    SimulatedTransport transport;
    SeededRandom random;
    SessionEngine engine;
};

class SimulatedNetwork {
public:
    static constexpr int kHost = 0;
    static constexpr int kViewer = 1;

    Microseconds latency{500};
    double media_loss = 0.0;
    std::optional<TimePoint> outage_start;
    std::optional<TimePoint> outage_end;
    TimePoint now{std::chrono::seconds(1000)};

    std::vector<std::vector<std::uint8_t>> wire; // every frame and datagram, in send order
    std::size_t wire_at_handshake = 0;           // wire.size() when the channel became encrypted

    void attach(Peer* host, Peer* viewer) { peers_ = {host, viewer}; }

    [[nodiscard]] bool in_outage(TimePoint at) const {
        return outage_start && outage_end && at >= *outage_start && at < *outage_end;
    }

    void enqueue_control(int from, std::span<const std::uint8_t> bytes) {
        wire.emplace_back(bytes.begin(), bytes.end());
        enqueue_ordered(from, {bytes.begin(), bytes.end()}, false);
    }

    /// Like a TCP FIN: travels behind the data already sent and waits out an outage.
    void close_from(int from) { enqueue_ordered(from, {}, true); }

    void enqueue_ordered(int from, std::vector<std::uint8_t> bytes, bool close) {
        TimePoint at = now + latency;
        if (in_outage(at) || in_outage(now)) {
            at = *outage_end + latency; // TCP holds the data until the path works again
        }
        at = std::max(at, last_control_[static_cast<std::size_t>(from)]); // keep the stream ordered
        last_control_[static_cast<std::size_t>(from)] = at;
        queue_.push_back(
            {.at = at, .to = 1 - from, .control = true, .close = close, .bytes = std::move(bytes)});
    }

    void enqueue_datagram(int from, std::span<const std::uint8_t> bytes) {
        wire.emplace_back(bytes.begin(), bytes.end());
        rng_ = rng_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const double draw = static_cast<double>(rng_ >> 11U) / static_cast<double>(1ULL << 53U);
        if (in_outage(now) || draw < media_loss) {
            return;
        }
        queue_.push_back({.at = now + latency,
                          .to = 1 - from,
                          .control = false,
                          .close = false,
                          .bytes = {bytes.begin(), bytes.end()}});
    }

    /// Advances the clock by one millisecond: delivers what is due, then ticks both engines.
    void step() {
        now += milliseconds(1);
        std::stable_sort(queue_.begin(), queue_.end(),
                         [](const auto& a, const auto& b) { return a.at < b.at; });
        while (!queue_.empty() && queue_.front().at <= now) {
            Delivery delivery = std::move(queue_.front());
            queue_.pop_front();
            Peer* peer = peers_.at(static_cast<std::size_t>(delivery.to));
            if (peer->transport.closed) {
                continue;
            }
            if (delivery.close) {
                peer->engine.on_transport_closed(Error::ConnectionClosed);
            } else if (delivery.control) {
                peer->engine.on_control_bytes(delivery.bytes, now);
            } else {
                peer->engine.on_datagram(delivery.bytes, now);
            }
        }
        for (Peer* peer : peers_) {
            peer->engine.on_timer(now);
        }
    }

private:
    struct Delivery {
        TimePoint at;
        int to = 0;
        bool control = false;
        bool close = false;
        std::vector<std::uint8_t> bytes;
    };

    std::array<Peer*, 2> peers_{};
    std::deque<Delivery> queue_;
    std::array<TimePoint, 2> last_control_{};
    std::uint64_t rng_ = 0x5EED;
};

inline void SimulatedTransport::send_control(std::span<const std::uint8_t> frame) {
    network_.enqueue_control(side_, frame);
}
inline void SimulatedTransport::send_datagram(std::span<const std::uint8_t> datagram) {
    network_.enqueue_datagram(side_, datagram);
}
inline void SimulatedTransport::close() {
    if (!closed) {
        closed = true;
        network_.close_from(side_);
    }
}

} // namespace osplay::test
