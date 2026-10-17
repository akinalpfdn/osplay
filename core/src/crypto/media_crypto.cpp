#include "crypto/media_crypto.hpp"

#include "protocol/constants.hpp"

#include <limits>

namespace osplay::crypto {
namespace {

using protocol::kMaxDatagramSize;
using protocol::kMaxMediaPlaintextSize;
using protocol::kMediaHeaderSize;
constexpr std::size_t kSequenceOffset = 4;

std::uint64_t sequence_of(std::span<const std::uint8_t> datagram) {
    std::uint64_t sequence = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        sequence |= std::uint64_t{datagram[kSequenceOffset + i]} << (8U * i);
    }
    return sequence;
}

} // namespace

bool ReplayWindow::is_new(std::uint64_t sequence) const {
    if (!highest_ || sequence > *highest_) {
        return true;
    }
    const std::uint64_t age = *highest_ - sequence;
    return age < kSize && !seen_.test(static_cast<std::size_t>(age));
}

void ReplayWindow::accept(std::uint64_t sequence) {
    if (!highest_ || sequence > *highest_) {
        const std::uint64_t shift = highest_ ? sequence - *highest_ : kSize;
        if (shift >= kSize) {
            seen_.reset();
        } else {
            seen_ <<= static_cast<std::size_t>(shift);
        }
        seen_.set(0);
        highest_ = sequence;
        return;
    }
    const std::uint64_t age = *highest_ - sequence;
    if (age < kSize) {
        seen_.set(static_cast<std::size_t>(age));
    }
}

MediaSealer::~MediaSealer() {
    wipe(key_);
}

Expected<std::size_t, Error> MediaSealer::seal(std::span<std::uint8_t> datagram,
                                               std::size_t packet_size) {
    using Result = Expected<std::size_t, Error>;
    if (packet_size < kMediaHeaderSize || packet_size > kMaxMediaPlaintextSize) {
        return Result::failure(Error::InvalidArgument);
    }
    if (datagram.size() < packet_size + kTagSize) {
        return Result::failure(Error::BufferTooSmall);
    }
    const std::uint64_t sequence = sequence_of(datagram);
    if (last_sequence_ && sequence <= *last_sequence_) {
        return Result::failure(Error::InvalidArgument);
    }
    if (sequence == std::numeric_limits<std::uint64_t>::max()) {
        return Result::failure(Error::NonceExhausted);
    }

    const auto header = datagram.first(kMediaHeaderSize);
    const std::size_t body_size = packet_size - kMediaHeaderSize;
    aead_encrypt(key_, sequence, header, datagram.subspan(kMediaHeaderSize, body_size),
                 datagram.subspan(kMediaHeaderSize, body_size + kTagSize));
    last_sequence_ = sequence;
    return packet_size + kTagSize;
}

MediaOpener::~MediaOpener() {
    wipe(key_);
}

Expected<std::span<const std::uint8_t>, Error> MediaOpener::open(std::span<std::uint8_t> datagram) {
    using Result = Expected<std::span<const std::uint8_t>, Error>;
    if (datagram.size() < kMediaHeaderSize + kTagSize) {
        return Result::failure(Error::Truncated);
    }
    if (datagram.size() > kMaxDatagramSize) {
        return Result::failure(Error::TooLarge);
    }
    const std::uint64_t sequence = sequence_of(datagram);
    if (!window_.is_new(sequence)) {
        return Result::failure(Error::Replayed);
    }

    const auto header = datagram.first(kMediaHeaderSize);
    const std::size_t sealed_size = datagram.size() - kMediaHeaderSize;
    if (!aead_decrypt(key_, sequence, header, datagram.subspan(kMediaHeaderSize, sealed_size),
                      datagram.subspan(kMediaHeaderSize, sealed_size - kTagSize))) {
        return Result::failure(Error::DecryptionFailed);
    }
    window_.accept(sequence);
    return std::span<const std::uint8_t>(datagram.first(datagram.size() - kTagSize));
}

} // namespace osplay::crypto
