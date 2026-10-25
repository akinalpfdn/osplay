#pragma once

#include <osplay/win/helper_request_handler.hpp>

namespace osplay::helper {

/// Writes vdd_settings.xml where the driver looks for it: the directory in the driver's VDDPATH
/// registry value, or C:\VirtualDisplayDriver. Replaces the file atomically.
class VddSettingsFile final : public win::helper::SettingsStore {
public:
    explicit VddSettingsFile(win::helper::HelperLog& log) : log_(log) {}

    bool write(const std::string& contents) override;
    /// Deletes the file, and its directory when nothing else is in it (uninstall).
    bool remove();

private:
    win::helper::HelperLog& log_;
};

} // namespace osplay::helper
