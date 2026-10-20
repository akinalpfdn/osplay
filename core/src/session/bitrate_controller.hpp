#pragma once

// Host-side bitrate adaptation from receiver reports (DEVPLAN Phase 13): back off quickly when the
// viewer loses frames or the host resends many fragments, creep back up while the link is clean,
// never above the configured bitrate or below a floor. Pure logic; the engine feeds it once per
// report and tells the platform's encoder about changes.

#include <cstdint>
#include <optional>

namespace osplay::session {

struct BitrateTunables {
    double decrease_factor = 0.75;
    double increase_factor = 1.08;
    /// The target never drops below this share of the configured bitrate, nor below the minimum.
    double floor_fraction = 0.25;
    std::uint32_t minimum_kbps = 1'000;
    /// Frames the viewer reports lost, per mille of the interval's frames.
    std::uint16_t loss_threshold_permille = 10;
    /// Fragments the host resent, per mille of the fragments it sent in the interval.
    std::uint16_t resend_threshold_permille = 50;
    /// Clean reports in a row before each step up.
    int clean_reports_per_increase = 3;
    /// Smaller changes are not worth reconfiguring the encoder for.
    std::uint16_t minimum_change_permille = 50;
};

class BitrateController {
public:
    BitrateController(std::uint32_t configured_kbps, const BitrateTunables& tunables);

    /// One report interval. Returns the new target when it moved enough to tell the encoder.
    [[nodiscard]] std::optional<std::uint32_t> on_report(std::uint16_t loss_permille,
                                                         std::uint64_t fragments_sent,
                                                         std::uint64_t fragments_resent);

    [[nodiscard]] std::uint32_t target_kbps() const { return announced_; }

private:
    BitrateTunables tunables_;
    double ceiling_;
    double floor_;
    double target_;
    std::uint32_t announced_;
    int clean_reports_ = 0;
};

} // namespace osplay::session
