#pragma once

// Runs a SessionEngine over real sockets: a TCP control connection, a UDP media socket and a
// timer, all on one I/O thread owned by the session (DEVPLAN: "core owns one I/O thread").
// Observer callbacks run on that thread. Commands may come from any thread; they are executed on
// the I/O thread in order.

#include "crypto/noise.hpp"
#include "media/access_unit.hpp"
#include "protocol/control_messages.hpp"
#include "session/session_config.hpp"
#include "session/session_engine.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osplay::transport {

struct TransportOptions {
    /// Host: UDP port to announce instead of the socket's own, when a forwarder sits in between
    /// (port forwarding; the wire-inspection tests use it to route media through a relay).
    std::optional<std::uint16_t> advertised_media_port;
};

class AsioSession {
public:
    /// Host: listens for one viewer on an ephemeral TCP port (both IPv4 and IPv6).
    [[nodiscard]] static Expected<std::unique_ptr<AsioSession>, Error>
    create_host(session::SessionConfig config, crypto::KeyPair identity,
                crypto::RandomSource& random, session::SessionObserver& observer,
                TransportOptions options = {});

    /// Viewer: connects to the host's control port.
    [[nodiscard]] static Expected<std::unique_ptr<AsioSession>, Error>
    create_viewer(session::SessionConfig config, crypto::KeyPair identity,
                  crypto::RandomSource& random, session::SessionObserver& observer,
                  const std::string& host, std::uint16_t port);

    /// Sends Bye if possible, closes the sockets and joins the I/O thread. Must not be called from
    /// an observer callback.
    ~AsioSession();
    AsioSession(const AsioSession&) = delete;
    AsioSession& operator=(const AsioSession&) = delete;
    AsioSession(AsioSession&&) = delete;
    AsioSession& operator=(AsioSession&&) = delete;

    /// Host: the TCP port viewers connect to (advertised over Bonjour).
    [[nodiscard]] std::uint16_t control_port() const;

    /// Host: the UDP port the media socket is bound to (before any advertised_media_port override).
    [[nodiscard]] std::uint16_t media_port() const;

    // Commands: block until the I/O thread has executed them, so results are immediate. Must not
    // be called from an observer callback.
    [[nodiscard]] Status<Error> confirm_pairing(bool accepted);
    [[nodiscard]] Status<Error>
    configure_streams(const std::vector<protocol::StreamConfig>& streams);
    [[nodiscard]] Status<Error> pause_stream(std::uint8_t stream_id);
    [[nodiscard]] Status<Error> resume_stream(std::uint8_t stream_id);

    /// Copies the access unit and queues it; returns without waiting for the network.
    void push_access_unit(const media::AccessUnit& unit);

    class Impl;

private:
    explicit AsioSession(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace osplay::transport
