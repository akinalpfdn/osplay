#include "media/pointer_shape.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using osplay::engine::convert_pointer_shape;
using osplay::engine::CursorImage;
using osplay::engine::PointerShapeInfo;
using osplay::engine::PointerShapeType;
using osplay::engine::scale_cursor_image;

std::vector<std::uint8_t> pixel_at(const osplay::engine::CursorImage& image, int x, int y) {
    const auto offset =
        (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
    return {image.bgra.begin() + static_cast<std::ptrdiff_t>(offset),
            image.bgra.begin() + static_cast<std::ptrdiff_t>(offset + 4)};
}

TEST(PointerShape, MonochromeMasksBecomeBlackWhiteTransparentAndOutlinedInversion) {
    // 8×2 cursor: AND rows then XOR rows, one byte per row (pitch 1).
    // Row 0: pixel 0 black (AND0 XOR0), 1 white (AND0 XOR1), 2 transparent (AND1 XOR0),
    //        3 inverted (AND1 XOR1), the rest transparent.
    const std::vector<std::uint8_t> buffer{
        0b0011'1111,
        0xFF, // AND rows 0 and 1
        0b0101'0000,
        0x00, // XOR rows 0 and 1
    };
    const auto image = convert_pointer_shape({.type = PointerShapeType::Monochrome,
                                              .width = 8,
                                              .height = 4,
                                              .pitch = 1,
                                              .hotspot_x = 1,
                                              .hotspot_y = 0},
                                             buffer);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->width, 8);
    EXPECT_EQ(image->height, 2);
    EXPECT_EQ(image->hotspot_x, 1);
    EXPECT_EQ(pixel_at(*image, 0, 0), (std::vector<std::uint8_t>{0, 0, 0, 0xFF}));
    EXPECT_EQ(pixel_at(*image, 1, 0), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(pixel_at(*image, 3, 0), (std::vector<std::uint8_t>{0, 0, 0, 0xFF}))
        << "inverted → black";
    EXPECT_EQ(pixel_at(*image, 4, 0), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}))
        << "transparent neighbour of an inverted pixel → white outline";
    EXPECT_EQ(pixel_at(*image, 6, 0), (std::vector<std::uint8_t>{0, 0, 0, 0})) << "transparent";
    EXPECT_EQ(pixel_at(*image, 3, 1), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}))
        << "outline below";
}

TEST(PointerShape, ColorShapesArePremultiplied) {
    // 2×1, pitch 12 (row padding is skipped).
    const std::vector<std::uint8_t> buffer{200, 100, 50, 128, 10, 20, 30, 0, 0, 0, 0, 0};
    const auto image = convert_pointer_shape({.type = PointerShapeType::Color,
                                              .width = 2,
                                              .height = 1,
                                              .pitch = 12,
                                              .hotspot_x = 0,
                                              .hotspot_y = 0},
                                             buffer);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(pixel_at(*image, 0, 0), (std::vector<std::uint8_t>{100, 50, 25, 128}));
    EXPECT_EQ(pixel_at(*image, 1, 0), (std::vector<std::uint8_t>{0, 0, 0, 0}));
}

TEST(PointerShape, MaskedColorCopiesHidesOrInverts) {
    // 3×1: copy (mask 0), transparent (mask FF, black), invert (mask FF, colour).
    const std::vector<std::uint8_t> buffer{1, 2, 3, 0x00, 0, 0, 0, 0xFF, 9, 9, 9, 0xFF};
    const auto image = convert_pointer_shape({.type = PointerShapeType::MaskedColor,
                                              .width = 3,
                                              .height = 1,
                                              .pitch = 12,
                                              .hotspot_x = 0,
                                              .hotspot_y = 0},
                                             buffer);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(pixel_at(*image, 0, 0), (std::vector<std::uint8_t>{1, 2, 3, 0xFF}));
    EXPECT_EQ(pixel_at(*image, 1, 0), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF}))
        << "outline";
    EXPECT_EQ(pixel_at(*image, 2, 0), (std::vector<std::uint8_t>{0, 0, 0, 0xFF}));
}

TEST(PointerShape, LargeShapesAreCroppedAroundTheHotspot) {
    const std::uint32_t side = 300;
    const std::vector<std::uint8_t> buffer(std::size_t{side} * side * 4, 0xFF);
    const auto image = convert_pointer_shape({.type = PointerShapeType::Color,
                                              .width = side,
                                              .height = side,
                                              .pitch = side * 4,
                                              .hotspot_x = 290,
                                              .hotspot_y = 10},
                                             buffer);
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->width, 256);
    EXPECT_EQ(image->height, 256);
    EXPECT_EQ(image->hotspot_x, 290 - (300 - 256));
    EXPECT_EQ(image->hotspot_y, 10);
}

TEST(PointerShape, RejectsBuffersThatDoNotMatchTheDescription) {
    const std::vector<std::uint8_t> small(10);
    EXPECT_FALSE(convert_pointer_shape({.type = PointerShapeType::Color,
                                        .width = 4,
                                        .height = 4,
                                        .pitch = 16,
                                        .hotspot_x = 0,
                                        .hotspot_y = 0},
                                       small)
                     .has_value());
    EXPECT_FALSE(convert_pointer_shape({.type = PointerShapeType::Monochrome,
                                        .width = 8,
                                        .height = 3,
                                        .pitch = 1,
                                        .hotspot_x = 0,
                                        .hotspot_y = 0},
                                       std::vector<std::uint8_t>(3))
                     .has_value())
        << "monochrome needs an even height";
}

} // namespace

TEST(PointerShape, AHalfSizeStreamGetsAHalfSizeCursor) {
    CursorImage image{.width = 4, .height = 2, .hotspot_x = 3, .hotspot_y = 1, .bgra = {}};
    image.bgra.assign(std::size_t{4} * 2 * 4, 0);
    for (std::size_t pixel = 0; pixel < 8; ++pixel) {
        const std::uint8_t value = pixel % 2 == 0 ? 200 : 100; // alternating columns
        std::fill_n(image.bgra.begin() + static_cast<std::ptrdiff_t>(pixel * 4), 4, value);
    }
    const CursorImage scaled = scale_cursor_image(image, 0.5);
    EXPECT_EQ(scaled.width, 2);
    EXPECT_EQ(scaled.height, 1);
    EXPECT_EQ(scaled.hotspot_x, 1) << "2 rounded from 1.5, kept inside the image";
    EXPECT_EQ(scaled.hotspot_y, 0) << "0.5 rounds to 1, kept inside the one-row image";
    ASSERT_EQ(scaled.bgra.size(), 8U);
    EXPECT_EQ(scaled.bgra[0], 150) << "each pixel averages the two it covers";
    EXPECT_EQ(scaled.bgra[7], 150);
}

TEST(PointerShape, AFullSizeStreamKeepsTheCursorAsItIs) {
    CursorImage image{.width = 3, .height = 3, .hotspot_x = 1, .hotspot_y = 1, .bgra = {}};
    image.bgra.assign(std::size_t{3} * 3 * 4, 7);
    const CursorImage same = scale_cursor_image(image, 1.0);
    EXPECT_EQ(same.width, 3);
    EXPECT_EQ(same.bgra, image.bgra);
    EXPECT_EQ(scale_cursor_image(image, 0.0).width, 3) << "a nonsensical factor changes nothing";
}
