#pragma once

#include "helper_virtual_display_provider.hpp"

namespace osplay::engine {

/// Finds the Virtual Display Driver's monitor through the CCD API and sets its mode with
/// ChangeDisplaySettingsExW, then reads the mode back to confirm it.
class GdiDisplayModeApplier final : public DisplayModeApplier {
public:
    win::Status<DisplayError> apply(const VirtualMonitorConfig& config) override;
};

} // namespace osplay::engine
