#include "media/cursor.hpp"

#include "protocol/constants.hpp"

#include <algorithm>

namespace osplay::media {

using protocol::kMaxCursorDimension;
using protocol::kMaxCursorShapePayload;

std::uint16_t cursor_fragment_count(std::uint16_t width, std::uint16_t height) {
    const std::size_t bytes = std::size_t{width} * height * 4;
    return static_cast<std::uint16_t>((bytes + kMaxCursorShapePayload - 1) /
                                      kMaxCursorShapePayload);
}

Status<Error> split_cursor_shape(const CursorShape& shape,
                                 std::vector<protocol::CursorShapeFragment>& out) {
    out.clear();
    if (shape.width == 0 || shape.height == 0 || shape.width > kMaxCursorDimension ||
        shape.height > kMaxCursorDimension || shape.hotspot_x >= shape.width ||
        shape.hotspot_y >= shape.height ||
        shape.bgra.size() != std::size_t{shape.width} * shape.height * 4) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    const std::uint16_t count = cursor_fragment_count(shape.width, shape.height);
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t offset = std::size_t{index} * kMaxCursorShapePayload;
        out.push_back({.stream_id = shape.stream_id,
                       .shape_id = shape.shape_id,
                       .width = shape.width,
                       .height = shape.height,
                       .hotspot_x = shape.hotspot_x,
                       .hotspot_y = shape.hotspot_y,
                       .fragment_index = index,
                       .fragment_count = count,
                       .payload = shape.bgra.subspan(
                           offset, std::min(kMaxCursorShapePayload, shape.bgra.size() - offset))});
    }
    return Status<Error>::ok();
}

std::optional<CursorShape>
CursorShapeAssembler::add(const protocol::CursorShapeFragment& fragment) {
    if (completed_shape_ == fragment.shape_id) {
        return std::nullopt; // a repeat of the shape already delivered
    }
    const bool same_shape = current_ && current_->shape_id == fragment.shape_id;
    if (same_shape && (current_->width != fragment.width || current_->height != fragment.height ||
                       current_->fragment_count != fragment.fragment_count)) {
        return std::nullopt; // inconsistent with the fragments already received
    }
    const std::size_t size = std::size_t{fragment.width} * fragment.height * 4;
    if (fragment.fragment_count != cursor_fragment_count(fragment.width, fragment.height) ||
        fragment.fragment_index >= fragment.fragment_count) {
        return std::nullopt;
    }
    const std::size_t offset = std::size_t{fragment.fragment_index} * kMaxCursorShapePayload;
    if (fragment.payload.size() != std::min(kMaxCursorShapePayload, size - offset)) {
        return std::nullopt;
    }
    if (!same_shape) {
        current_ = fragment;
        current_->payload = {};
        image_.assign(size, 0);
        have_.assign(fragment.fragment_count, false);
        received_ = 0;
    }
    if (have_[fragment.fragment_index]) {
        return std::nullopt;
    }
    have_[fragment.fragment_index] = true;
    ++received_;
    std::ranges::copy(fragment.payload, image_.begin() + static_cast<std::ptrdiff_t>(offset));
    if (received_ != current_->fragment_count) {
        return std::nullopt;
    }
    completed_shape_ = current_->shape_id;
    const CursorShape shape{.stream_id = current_->stream_id,
                            .shape_id = current_->shape_id,
                            .width = current_->width,
                            .height = current_->height,
                            .hotspot_x = current_->hotspot_x,
                            .hotspot_y = current_->hotspot_y,
                            .bgra = image_};
    current_.reset();
    return shape;
}

} // namespace osplay::media
