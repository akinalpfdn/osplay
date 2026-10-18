#pragma once

// Converts Desktop Duplication pointer shapes into the protocol's cursor images (premultiplied
// BGRA, rows top to bottom, at most 256×256). Kept free of Windows headers so it is unit tested on
// every platform; the field meanings follow DXGI_OUTDUPL_POINTER_SHAPE_INFO.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osplay::engine {

enum class PointerShapeType : std::uint8_t {
    Monochrome = 1, ///< DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME: AND mask over XOR mask, 1 bpp
    Color = 2,      ///< DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR: BGRA with straight alpha
    MaskedColor =
        4, ///< DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR: BGR, alpha 0 = copy, 0xFF = XOR
};

struct PointerShapeInfo {
    PointerShapeType type = PointerShapeType::Color;
    std::uint32_t width = 0;
    /// For monochrome shapes this counts both masks, so the cursor is height / 2 rows tall.
    std::uint32_t height = 0;
    std::uint32_t pitch = 0; ///< bytes per row in the source buffer
    std::uint32_t hotspot_x = 0;
    std::uint32_t hotspot_y = 0;
};

struct CursorImage {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t hotspot_x = 0;
    std::uint16_t hotspot_y = 0;
    std::vector<std::uint8_t> bgra; ///< premultiplied, width × height × 4 bytes
};

/// Largest cursor side the protocol carries; bigger shapes are cropped around the hotspot.
inline constexpr std::uint32_t kMaxCursorSide = 256;

/// The image, or nothing if the description does not match the buffer. Pixels that invert the
/// screen (AND 1 / XOR 1, or masked XOR with a colour) cannot be expressed in BGRA and become
/// opaque black with a white outline, so text beams stay visible on any background.
[[nodiscard]] std::optional<CursorImage>
convert_pointer_shape(const PointerShapeInfo& info, std::span<const std::uint8_t> buffer);

} // namespace osplay::engine
