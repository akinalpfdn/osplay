#pragma once

#include "helper_virtual_display_provider.hpp"

namespace osplay::engine {

/// Finds the Virtual Display Driver's monitors through the CCD API, sets each one's mode with
/// ChangeDisplaySettingsExW (all at once), then reads every mode back to confirm it.
class GdiDisplayModeApplier final : public DisplayModeApplier {
public:
    win::Expected<std::vector<std::wstring>, DisplayError>
    apply(std::span<const VirtualMonitorConfig> configs) override;
};

} // namespace osplay::engine
