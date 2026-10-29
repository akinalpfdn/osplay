#pragma once

// A blocking byte pipe the platform provides: the two bulk endpoints of an Android accessory
// (PROTOCOL.md "Android accessory links"). A session runs a stream link over it on two threads of
// its own, one reading and one writing.

#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace osplay::transport {

class BytePipe {
public:
    BytePipe() = default;
    virtual ~BytePipe() = default;
    BytePipe(const BytePipe&) = delete;
    BytePipe& operator=(const BytePipe&) = delete;
    BytePipe(BytePipe&&) = delete;
    BytePipe& operator=(BytePipe&&) = delete;

    /// Blocks until bytes arrive. Zero bytes (a zero-length packet) is not an error. Fails with
    /// ConnectionClosed at the end of the stream and NetworkError when the cable is gone.
    [[nodiscard]] virtual Expected<std::size_t, Error> read(std::span<std::uint8_t> buffer) = 0;
    /// Blocks until some of `data` is sent; returns how much.
    [[nodiscard]] virtual Expected<std::size_t, Error>
    write(std::span<const std::uint8_t> data) = 0;
    /// Unblocks read and write for good, where the platform can; called once, from any thread,
    /// while they may be running.
    virtual void cancel() = 0;
};

#ifndef _WIN32
/// A pipe over a file descriptor it owns and closes: an Android accessory
/// (UsbManager.openAccessory), or one end of a socket pair in tests. An accessory's read returns 0
/// for a zero-length packet; a socket's 0 ends the stream. cancel shuts a socket down. Android's
/// accessory driver ends a blocked read neither for a pulled cable nor for anything the host can
/// send without data, so cancel interrupts the blocked threads with SIGURG, whose handler
/// (installed once per process) does nothing.
[[nodiscard]] std::unique_ptr<BytePipe> make_fd_pipe(int fd);
#endif

} // namespace osplay::transport
