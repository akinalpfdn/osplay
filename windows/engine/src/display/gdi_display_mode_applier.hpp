#pragma once

#include "virtual_display_hub.hpp"

namespace osplay::engine {

/// Finds the Virtual Display Driver's monitors through the CCD API, sets each new one's mode with
/// ChangeDisplaySettingsExW (all at once), then reads every mode back to confirm it. Monitors of
/// other sessions keep their modes.
class GdiDisplayModeApplier final : public DisplayModeApplier {
public:
    win::Expected<std::vector<std::wstring>, DisplayError> current_monitors() override;
    win::Expected<std::vector<std::wstring>, DisplayError>
    apply(std::span<const VirtualMonitorConfig> configs,
          std::span<const std::wstring> existing) override;
};

} // namespace osplay::engine
