#pragma once

// Records of a stream link (PROTOCOL.md "Stream links"): where there is no UDP path, as on a USB
// cable through usbmuxd, the control frames and the media datagrams share one byte stream.

#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osplay::protocol {

enum class LinkRecordKind : std::uint8_t {
    Control = 1, ///< one control frame, its u32 length included
    Media = 2,   ///< one media datagram
};

inline constexpr std::size_t kLinkRecordHeaderSize = 3; // kind, u16 length
/// A control frame: the u32 length, a 4096-byte message and the AEAD tag.
inline constexpr std::size_t kMaxLinkControlRecord = 4 + 4096 + 16;

struct LinkRecord {
    LinkRecordKind kind = LinkRecordKind::Control;
    std::span<std::uint8_t> payload; ///< valid until the next LinkRecordReader::append
};

/// Appends header and payload to `out`; fails for an empty or oversized payload.
[[nodiscard]] Status<Error> append_link_record(LinkRecordKind kind,
                                               std::span<const std::uint8_t> payload,
                                               std::vector<std::uint8_t>& out);

/// Splits an arbitrary chunking of the byte stream into records.
class LinkRecordReader {
public:
    void append(std::span<const std::uint8_t> bytes);
    /// The next complete record, nothing when more bytes are needed, or an error (unknown kind,
    /// empty or oversized payload) that ends the link.
    [[nodiscard]] Expected<std::optional<LinkRecord>, Error> next();

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t consumed_ = 0;
};

} // namespace osplay::protocol
