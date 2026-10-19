#pragma once

// Between DNS-SD key/value lists and the core's TXT record, and the service instance names this
// engine uses. Portable and unit tested.

#include <osplay/osplay.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace osplay::engine {

using TxtPairs = std::vector<std::pair<std::string, std::string>>;

/// The five keys in a fixed order, ready for registration.
[[nodiscard]] TxtPairs txt_pairs(const osplay_txt_record& record);

/// A peer's record: known keys copied (cut to the value size), unknown ones ignored, missing ones
/// left empty for the core to reject.
[[nodiscard]] osplay_txt_record txt_record(const TxtPairs& pairs);

/// The instance label: the device ID in hex, unique per device and stable across renames; the UI
/// shows the TXT name instead.
[[nodiscard]] std::string instance_label(const osplay_txt_record& record);

/// "label._osplay._tcp.local" → "label"; nullopt for anything else.
[[nodiscard]] std::optional<std::string> label_of(const std::string& instance_name);

} // namespace osplay::engine
