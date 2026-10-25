#include "media/pointer_shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace osplay::engine {
namespace {

enum class Pixel : std::uint8_t { Transparent, Opaque, Invert };

/// An intermediate image: colours for opaque pixels plus which pixels invert the screen.
struct Canvas {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bgra; // premultiplied
    std::vector<bool> invert;

    void set(std::uint32_t x, std::uint32_t y, std::uint8_t b, std::uint8_t g, std::uint8_t r,
             std::uint8_t a) {
        const std::size_t offset = ((std::size_t{y} * width) + x) * 4;
        bgra[offset] = b;
        bgra[offset + 1] = g;
        bgra[offset + 2] = r;
        bgra[offset + 3] = a;
    }
};

bool buffer_holds(const PointerShapeInfo& info, std::size_t bytes_per_row_needed,
                  std::span<const std::uint8_t> buffer) {
    return info.width > 0 && info.height > 0 && info.pitch >= bytes_per_row_needed &&
           buffer.size() >= std::size_t{info.pitch} * info.height;
}

std::optional<Canvas> from_monochrome(const PointerShapeInfo& info,
                                      std::span<const std::uint8_t> buffer) {
    if (info.height % 2 != 0 || !buffer_holds(info, (std::size_t{info.width} + 7) / 8, buffer)) {
        return std::nullopt;
    }
    const std::uint32_t rows = info.height / 2;
    Canvas canvas{.width = info.width,
                  .height = rows,
                  .bgra = std::vector<std::uint8_t>(std::size_t{info.width} * rows * 4, 0),
                  .invert = std::vector<bool>(std::size_t{info.width} * rows, false)};
    const auto bit = [&](std::uint32_t row, std::uint32_t x) {
        const std::uint8_t byte = buffer[(std::size_t{row} * info.pitch) + (x / 8)];
        return (byte >> (7U - (x % 8U))) & 1U;
    };
    for (std::uint32_t y = 0; y < rows; ++y) {
        for (std::uint32_t x = 0; x < info.width; ++x) {
            const unsigned and_bit = bit(y, x);
            const unsigned xor_bit = bit(y + rows, x);
            if (and_bit == 0) {
                const std::uint8_t level = xor_bit != 0 ? 0xFF : 0x00;
                canvas.set(x, y, level, level, level, 0xFF);
            } else if (xor_bit != 0) {
                canvas.invert[(std::size_t{y} * info.width) + x] = true;
            }
        }
    }
    return canvas;
}

std::optional<Canvas> from_color(const PointerShapeInfo& info, std::span<const std::uint8_t> buffer,
                                 bool masked) {
    if (!buffer_holds(info, std::size_t{info.width} * 4, buffer)) {
        return std::nullopt;
    }
    Canvas canvas{.width = info.width,
                  .height = info.height,
                  .bgra = std::vector<std::uint8_t>(std::size_t{info.width} * info.height * 4, 0),
                  .invert = std::vector<bool>(std::size_t{info.width} * info.height, false)};
    for (std::uint32_t y = 0; y < info.height; ++y) {
        for (std::uint32_t x = 0; x < info.width; ++x) {
            const std::size_t source = (std::size_t{y} * info.pitch) + (std::size_t{x} * 4);
            const std::uint8_t b = buffer[source];
            const std::uint8_t g = buffer[source + 1];
            const std::uint8_t r = buffer[source + 2];
            const std::uint8_t a = buffer[source + 3];
            if (!masked) {
                const auto premultiply = [a](std::uint8_t channel) {
                    return static_cast<std::uint8_t>(((unsigned{channel} * a) + 127U) / 255U);
                };
                canvas.set(x, y, premultiply(b), premultiply(g), premultiply(r), a);
            } else if (a == 0) {
                canvas.set(x, y, b, g, r, 0xFF); // mask 0: the colour replaces the screen
            } else if ((b | g | r) != 0) {
                canvas.invert[(std::size_t{y} * info.width) + x] = true; // XOR with a colour
            }
        }
    }
    return canvas;
}

/// Inverting pixels become black; transparent neighbours of black get a white outline.
void resolve_inversion(Canvas& canvas) {
    const auto inverted = [&](std::int64_t x, std::int64_t y) {
        return x >= 0 && y >= 0 && x < std::int64_t{canvas.width} &&
               y < std::int64_t{canvas.height} &&
               canvas.invert[(static_cast<std::size_t>(y) * canvas.width) +
                             static_cast<std::size_t>(x)];
    };
    const auto transparent = [&](std::uint32_t x, std::uint32_t y) {
        return canvas.bgra[(((std::size_t{y} * canvas.width) + x) * 4) + 3] == 0 &&
               !canvas.invert[(std::size_t{y} * canvas.width) + x];
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> outline;
    for (std::uint32_t y = 0; y < canvas.height; ++y) {
        for (std::uint32_t x = 0; x < canvas.width; ++x) {
            if (!transparent(x, y)) {
                continue;
            }
            const auto sx = static_cast<std::int64_t>(x);
            const auto sy = static_cast<std::int64_t>(y);
            if (inverted(sx - 1, sy) || inverted(sx + 1, sy) || inverted(sx, sy - 1) ||
                inverted(sx, sy + 1)) {
                outline.emplace_back(x, y);
            }
        }
    }
    for (std::uint32_t y = 0; y < canvas.height; ++y) {
        for (std::uint32_t x = 0; x < canvas.width; ++x) {
            if (canvas.invert[(std::size_t{y} * canvas.width) + x]) {
                canvas.set(x, y, 0, 0, 0, 0xFF);
            }
        }
    }
    for (const auto& [x, y] : outline) {
        canvas.set(x, y, 0xFF, 0xFF, 0xFF, 0xFF);
    }
}

/// Crops to kMaxCursorSide around the hotspot.
CursorImage crop(const Canvas& canvas, std::uint32_t hotspot_x, std::uint32_t hotspot_y) {
    const std::uint32_t width = std::min(canvas.width, kMaxCursorSide);
    const std::uint32_t height = std::min(canvas.height, kMaxCursorSide);
    const std::uint32_t left =
        std::min(hotspot_x > width / 2 ? hotspot_x - (width / 2) : 0U, canvas.width - width);
    const std::uint32_t top =
        std::min(hotspot_y > height / 2 ? hotspot_y - (height / 2) : 0U, canvas.height - height);
    CursorImage image{.width = static_cast<std::uint16_t>(width),
                      .height = static_cast<std::uint16_t>(height),
                      .hotspot_x = static_cast<std::uint16_t>(
                          std::min(hotspot_x > left ? hotspot_x - left : 0U, width - 1)),
                      .hotspot_y = static_cast<std::uint16_t>(
                          std::min(hotspot_y > top ? hotspot_y - top : 0U, height - 1)),
                      .bgra = std::vector<std::uint8_t>(std::size_t{width} * height * 4)};
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto source =
            canvas.bgra.begin() +
            static_cast<std::ptrdiff_t>(((std::size_t{top + y} * canvas.width) + left) * 4);
        std::copy_n(source, std::size_t{width} * 4,
                    image.bgra.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * width * 4));
    }
    return image;
}

} // namespace

std::optional<CursorImage> convert_pointer_shape(const PointerShapeInfo& info,
                                                 std::span<const std::uint8_t> buffer) {
    std::optional<Canvas> canvas;
    switch (info.type) {
    case PointerShapeType::Monochrome:
        canvas = from_monochrome(info, buffer);
        break;
    case PointerShapeType::Color:
        canvas = from_color(info, buffer, false);
        break;
    case PointerShapeType::MaskedColor:
        canvas = from_color(info, buffer, true);
        break;
    }
    if (!canvas) {
        return std::nullopt;
    }
    resolve_inversion(*canvas);
    return crop(*canvas, info.hotspot_x, info.hotspot_y);
}

CursorImage scale_cursor_image(const CursorImage& image, double factor) {
    if (!(factor > 0.0) || factor >= 1.0 || image.width == 0 || image.height == 0) {
        return image;
    }
    const auto scaled_side = [factor](std::uint16_t side) {
        return static_cast<std::uint16_t>(
            std::max(1L, std::lround(static_cast<double>(side) * factor)));
    };
    CursorImage scaled{.width = scaled_side(image.width),
                       .height = scaled_side(image.height),
                       .hotspot_x = 0,
                       .hotspot_y = 0,
                       .bgra = {}};
    scaled.hotspot_x = static_cast<std::uint16_t>(
        std::min<long>(std::lround(image.hotspot_x * factor), scaled.width - 1));
    scaled.hotspot_y = static_cast<std::uint16_t>(
        std::min<long>(std::lround(image.hotspot_y * factor), scaled.height - 1));
    scaled.bgra.resize(std::size_t{scaled.width} * scaled.height * 4);
    // Each target pixel averages the source pixels it covers; premultiplied channels average right.
    for (std::uint32_t y = 0; y < scaled.height; ++y) {
        const std::uint32_t y0 = y * image.height / scaled.height;
        const std::uint32_t y1 = std::max(y0 + 1, (y + 1) * image.height / scaled.height);
        for (std::uint32_t x = 0; x < scaled.width; ++x) {
            const std::uint32_t x0 = x * image.width / scaled.width;
            const std::uint32_t x1 = std::max(x0 + 1, (x + 1) * image.width / scaled.width);
            std::array<std::uint32_t, 4> sum{};
            for (std::uint32_t sy = y0; sy < y1; ++sy) {
                for (std::uint32_t sx = x0; sx < x1; ++sx) {
                    const std::size_t at = ((std::size_t{sy} * image.width) + sx) * 4;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        sum.at(channel) += image.bgra.at(at + channel);
                    }
                }
            }
            const std::uint32_t count = (y1 - y0) * (x1 - x0);
            const std::size_t out = ((std::size_t{y} * scaled.width) + x) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                scaled.bgra.at(out + channel) =
                    static_cast<std::uint8_t>((sum.at(channel) + (count / 2)) / count);
            }
        }
    }
    return scaled;
}

} // namespace osplay::engine
