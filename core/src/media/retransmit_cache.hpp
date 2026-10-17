#pragma once

#include "media/access_unit.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace osplay::media {

/// The host's copy of recently sent access units, so NACKed fragments can be sent again.
/// Bounded by frame count and bytes; buffers are recycled, so steady state does not allocate.
class RetransmitCache {
public:
    RetransmitCache(std::size_t max_frames, std::size_t max_bytes)
        : max_frames_(max_frames), max_bytes_(max_bytes) {}

    void store(std::uint32_t frame_id, const AccessUnit& unit);
    /// The cached unit (its data stays valid until the next store) or nullptr if evicted.
    [[nodiscard]] const AccessUnit* find(std::uint32_t frame_id) const;
    void clear();

private:
    struct Entry {
        std::uint32_t frame_id = 0;
        AccessUnit unit;
        std::vector<std::uint8_t> bytes;
    };

    std::size_t max_frames_;
    std::size_t max_bytes_;
    std::size_t bytes_ = 0;
    std::deque<Entry> entries_;
    std::vector<std::vector<std::uint8_t>> spare_;
};

} // namespace osplay::media
