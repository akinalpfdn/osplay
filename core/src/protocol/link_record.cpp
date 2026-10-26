#include "protocol/link_record.hpp"

#include "protocol/constants.hpp"

#include <algorithm>
#include <optional>

namespace osplay::protocol {

namespace {

std::size_t max_payload(LinkRecordKind kind) {
    return kind == LinkRecordKind::Control ? kMaxLinkControlRecord : kMaxDatagramSize;
}

bool known(std::uint8_t kind) {
    return kind == static_cast<std::uint8_t>(LinkRecordKind::Control) ||
           kind == static_cast<std::uint8_t>(LinkRecordKind::Media);
}

} // namespace

Status<Error> append_link_record(LinkRecordKind kind, std::span<const std::uint8_t> payload,
                                 std::vector<std::uint8_t>& out) {
    if (payload.empty() || payload.size() > max_payload(kind)) {
        return Status<Error>::failure(Error::TooLarge);
    }
    const auto length = static_cast<std::uint16_t>(payload.size());
    out.push_back(static_cast<std::uint8_t>(kind));
    out.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(length >> 8U));
    out.insert(out.end(), payload.begin(), payload.end());
    return Status<Error>::ok();
}

void LinkRecordReader::append(std::span<const std::uint8_t> bytes) {
    if (consumed_ > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

Expected<std::optional<LinkRecord>, Error> LinkRecordReader::next() {
    using Result = Expected<std::optional<LinkRecord>, Error>;
    const std::size_t available = buffer_.size() - consumed_;
    if (available == 0) {
        return std::optional<LinkRecord>{};
    }
    const std::uint8_t kind = buffer_[consumed_];
    if (!known(kind)) {
        return Result::failure(Error::UnknownType);
    }
    if (available < kLinkRecordHeaderSize) {
        return std::optional<LinkRecord>{};
    }
    const std::size_t length =
        std::size_t{buffer_[consumed_ + 1]} | (std::size_t{buffer_[consumed_ + 2]} << 8U);
    const auto record_kind = static_cast<LinkRecordKind>(kind);
    if (length == 0 || length > max_payload(record_kind)) {
        return Result::failure(Error::TooLarge);
    }
    if (available < kLinkRecordHeaderSize + length) {
        return std::optional<LinkRecord>{};
    }
    const auto payload = std::span(buffer_).subspan(consumed_ + kLinkRecordHeaderSize, length);
    consumed_ += kLinkRecordHeaderSize + length;
    return std::optional<LinkRecord>{LinkRecord{.kind = record_kind, .payload = payload}};
}

} // namespace osplay::protocol
