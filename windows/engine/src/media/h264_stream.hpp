#pragma once

// H.264 Annex B helpers for the encoder's output (portable, unit tested everywhere): every
// keyframe must start with SPS and PPS (docs/PROTOCOL.md), but hardware encoders do not always
// repeat them.

#include <cstdint>
#include <span>
#include <vector>

namespace osplay::engine {

/// NAL unit types (ITU-T H.264 table 7-1) the engine cares about.
inline constexpr std::uint8_t kNalIdr = 5;
inline constexpr std::uint8_t kNalSps = 7;
inline constexpr std::uint8_t kNalPps = 8;

/// The NAL units of an Annex B buffer, without start codes. Empty if it is not Annex B.
[[nodiscard]] std::vector<std::span<const std::uint8_t>>
nal_units(std::span<const std::uint8_t> annexb);

/// Remembers the latest SPS and PPS and puts them in front of keyframes that lack them.
class ParameterSetCache {
public:
    /// Seeds the cache, e.g. from the encoder's sequence header (Annex B SPS + PPS).
    void remember(std::span<const std::uint8_t> annexb);

    /// The access unit as it goes on the wire: keyframes (IDR) start with SPS and PPS. Returns
    /// whether the unit is a keyframe through `keyframe`.
    [[nodiscard]] std::vector<std::uint8_t> prepare(std::span<const std::uint8_t> annexb,
                                                    bool& keyframe);

private:
    std::vector<std::uint8_t> sps_;
    std::vector<std::uint8_t> pps_;
};

} // namespace osplay::engine
