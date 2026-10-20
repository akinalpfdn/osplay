#include "session/bitrate_controller.hpp"

#include <algorithm>
#include <cmath>

namespace osplay::session {

namespace {

constexpr std::uint64_t kPerMille = 1'000;

} // namespace

BitrateController::BitrateController(std::uint32_t configured_kbps, const BitrateTunables& tunables)
    : tunables_(tunables), ceiling_(static_cast<double>(configured_kbps)),
      floor_(std::min(ceiling_, std::max(static_cast<double>(tunables.minimum_kbps),
                                         ceiling_ * tunables.floor_fraction))),
      target_(ceiling_), announced_(configured_kbps) {}

std::optional<std::uint32_t> BitrateController::on_report(std::uint16_t loss_permille,
                                                          std::uint64_t fragments_sent,
                                                          std::uint64_t fragments_resent) {
    const std::uint64_t resend_permille =
        fragments_sent == 0 ? 0 : fragments_resent * kPerMille / fragments_sent;
    const bool congested = loss_permille >= tunables_.loss_threshold_permille ||
                           resend_permille >= tunables_.resend_threshold_permille;
    if (congested) {
        clean_reports_ = 0;
        target_ = std::max(floor_, target_ * tunables_.decrease_factor);
    } else if (++clean_reports_ >= tunables_.clean_reports_per_increase) {
        clean_reports_ = 0;
        target_ = std::min(ceiling_, target_ * tunables_.increase_factor);
    }
    const auto target = static_cast<std::uint32_t>(std::lround(target_));
    const auto change =
        static_cast<std::uint64_t>(target > announced_ ? target - announced_ : announced_ - target);
    // Reaching the ceiling or the floor is always announced, so the encoder ends up exactly there.
    const bool at_limit = target_ == ceiling_ || target_ == floor_;
    if (change == 0 || (!at_limit && change * kPerMille < std::uint64_t{announced_} *
                                                              tunables_.minimum_change_permille)) {
        return std::nullopt;
    }
    announced_ = target;
    return target;
}

} // namespace osplay::session
