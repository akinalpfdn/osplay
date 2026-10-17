#include "media/retransmit_cache.hpp"

#include <algorithm>
#include <utility>

namespace osplay::media {

void RetransmitCache::store(std::uint32_t frame_id, const AccessUnit& unit) {
    if (unit.data.size() > max_bytes_) {
        clear(); // a single frame larger than the budget cannot be retransmitted
        return;
    }
    while (!entries_.empty() &&
           (entries_.size() >= max_frames_ || bytes_ + unit.data.size() > max_bytes_)) {
        bytes_ -= entries_.front().bytes.size();
        spare_.push_back(std::move(entries_.front().bytes));
        entries_.pop_front();
    }
    Entry entry;
    if (!spare_.empty()) {
        entry.bytes = std::move(spare_.back());
        spare_.pop_back();
    }
    entry.bytes.assign(unit.data.begin(), unit.data.end());
    entry.frame_id = frame_id;
    entry.unit = unit;
    entry.unit.data = entry.bytes;
    bytes_ += entry.bytes.size();
    entries_.push_back(std::move(entry));
}

const AccessUnit* RetransmitCache::find(std::uint32_t frame_id) const {
    const auto it = std::ranges::find_if(
        entries_, [frame_id](const Entry& entry) { return entry.frame_id == frame_id; });
    return it == entries_.end() ? nullptr : &it->unit;
}

void RetransmitCache::clear() {
    for (Entry& entry : entries_) {
        spare_.push_back(std::move(entry.bytes));
    }
    entries_.clear();
    bytes_ = 0;
}

} // namespace osplay::media
