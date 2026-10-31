#include "transport/usbmux.hpp"

#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <utility>

namespace osplay::transport::usbmux {

namespace {

constexpr std::uint32_t kVersionPlist = 1;
constexpr std::uint32_t kMessagePlist = 8;
constexpr int kMaxDepth = 16;

std::vector<std::uint8_t> message(const std::string& body, std::uint32_t tag) {
    const std::string plist = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                                          "<plist version=\"1.0\">\n<dict>\n"
                                          "\t<key>ClientVersionString</key>\n"
                                          "\t<string>osplay</string>\n"
                                          "\t<key>ProgName</key>\n"
                                          "\t<string>Osplay</string>\n"
                                          "\t<key>kLibUSBMuxVersion</key>\n"
                                          "\t<integer>3</integer>\n") +
                              body + "</dict>\n</plist>\n";
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + plist.size());
    for (const std::uint32_t field : {static_cast<std::uint32_t>(kHeaderSize + plist.size()),
                                      kVersionPlist, kMessagePlist, tag}) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>(field >> shift));
        }
    }
    out.insert(out.end(), plist.begin(), plist.end());
    return out;
}

/// Reads an XML property list into "path" → text pairs, e.g. "Properties.SerialNumber". Arrays are
/// checked but not kept: usbmuxd's events carry what Osplay needs in dictionaries.
class PlistReader {
public:
    explicit PlistReader(std::string_view text) : text_(text) {}

    bool read(std::vector<std::pair<std::string, std::string>>& out) {
        out_ = &out;
        skip_prolog();
        std::string name;
        bool empty = false;
        if (!open_tag(name, empty) || name != "plist" || empty) {
            return false;
        }
        if (!value("", true, 0)) {
            return false;
        }
        skip_space();
        if (!close_tag("plist")) {
            return false;
        }
        skip_space();
        return pos_ == text_.size();
    }

private:
    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    /// The XML declaration, the doctype and comments before the root element.
    void skip_prolog() {
        for (;;) {
            skip_space();
            const auto rest = text_.substr(pos_);
            std::string_view end;
            if (rest.starts_with("<?")) {
                end = "?>";
            } else if (rest.starts_with("<!--")) {
                end = "-->";
            } else if (rest.starts_with("<!")) {
                end = ">";
            } else {
                return;
            }
            const auto found = rest.find(end);
            if (found == std::string_view::npos) {
                pos_ = text_.size();
                return;
            }
            pos_ += found + end.size();
        }
    }

    /// `<name ...>` or `<name/>`; attributes are skipped.
    bool open_tag(std::string& name, bool& empty) {
        skip_space();
        if (pos_ >= text_.size() || text_[pos_] != '<') {
            return false;
        }
        const auto end = text_.find('>', pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        auto inside = text_.substr(pos_ + 1, end - pos_ - 1);
        pos_ = end + 1;
        empty = inside.ends_with('/');
        if (empty) {
            inside.remove_suffix(1);
        }
        const auto name_end = inside.find_first_of(" \t\r\n");
        name = std::string(inside.substr(0, name_end));
        return !name.empty() && name.front() != '/';
    }

    bool close_tag(std::string_view name) {
        skip_space();
        const auto rest = text_.substr(pos_);
        if (!rest.starts_with("</") || rest.size() < name.size() + 3 ||
            rest.substr(2, name.size()) != name || rest[2 + name.size()] != '>') {
            return false;
        }
        pos_ += name.size() + 3;
        return true;
    }

    bool peek_close() {
        skip_space();
        return text_.substr(pos_).starts_with("</");
    }

    /// Character data up to the next tag, with the five predefined entities decoded.
    bool characters(std::string& out) {
        const auto end = text_.find('<', pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        const auto raw = text_.substr(pos_, end - pos_);
        pos_ = end;
        static constexpr std::array<std::pair<std::string_view, char>, 5> kEntities{
            {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}}};
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] != '&') {
                out.push_back(raw[i++]);
                continue;
            }
            bool decoded = false;
            for (const auto& [entity, character] : kEntities) {
                if (raw.substr(i).starts_with(entity)) {
                    out.push_back(character);
                    i += entity.size();
                    decoded = true;
                    break;
                }
            }
            if (!decoded) {
                return false;
            }
        }
        return true;
    }

    // NOLINTNEXTLINE(misc-no-recursion): nesting is bounded by kMaxDepth
    bool value(const std::string& path, bool keep, int depth) {
        if (depth > kMaxDepth) {
            return false;
        }
        std::string name;
        bool empty = false;
        if (!open_tag(name, empty)) {
            return false;
        }
        if (name == "dict") {
            return empty || dictionary(path, keep, depth);
        }
        if (name == "array") {
            while (!empty && !peek_close()) {
                if (!value(path, false, depth + 1)) {
                    return false;
                }
            }
            return empty || close_tag("array");
        }
        if (name == "true" || name == "false") {
            store(path, keep, name);
            return empty || close_tag(name);
        }
        if (name != "string" && name != "integer" && name != "real" && name != "date" &&
            name != "data") {
            return false;
        }
        std::string text;
        if (!empty && (!characters(text) || !close_tag(name))) {
            return false;
        }
        store(path, keep, std::move(text));
        return true;
    }

    // NOLINTNEXTLINE(misc-no-recursion): see value()
    bool dictionary(const std::string& path, bool keep, int depth) {
        while (!peek_close()) {
            std::string name;
            bool empty = false;
            std::string key;
            if (!open_tag(name, empty) || name != "key" || empty || !characters(key) ||
                !close_tag("key")) {
                return false;
            }
            std::string child = path;
            if (!child.empty()) {
                child += '.';
            }
            child += key;
            if (!value(child, keep, depth + 1)) {
                return false;
            }
        }
        return close_tag("dict");
    }

    void store(const std::string& path, bool keep, std::string text) {
        if (keep && !path.empty()) {
            out_->emplace_back(path, std::move(text));
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::vector<std::pair<std::string, std::string>>* out_ = nullptr;
};

std::optional<std::string_view>
lookup(const std::vector<std::pair<std::string, std::string>>& entries, std::string_view key) {
    for (const auto& [path, text] : entries) {
        if (path == key) {
            return text;
        }
    }
    return std::nullopt;
}

template <typename Integer>
std::optional<Integer> integer(std::optional<std::string_view> text) {
    Integer number{};
    if (!text || text->empty()) {
        return std::nullopt;
    }
    const auto* const begin = text->data();
    const auto* const end = begin + text->size();
    const auto [last, error] = std::from_chars(begin, end, number);
    if (error != std::errc{} || last != end) {
        return std::nullopt;
    }
    return number;
}

} // namespace

std::vector<std::uint8_t> listen_request(std::uint32_t tag) {
    return message("\t<key>MessageType</key>\n\t<string>Listen</string>\n", tag);
}

std::vector<std::uint8_t> connect_request(std::uint32_t device_id, std::uint16_t port,
                                          std::uint32_t tag) {
    // usbmuxd reads the port in network byte order.
    const auto swapped = static_cast<std::uint16_t>((port << 8U) | (port >> 8U));
    return message("\t<key>MessageType</key>\n\t<string>Connect</string>\n"
                   "\t<key>DeviceID</key>\n\t<integer>" +
                       std::to_string(device_id) +
                       "</integer>\n"
                       "\t<key>PortNumber</key>\n\t<integer>" +
                       std::to_string(swapped) + "</integer>\n",
                   tag);
}

Expected<std::size_t, Error> plist_size(std::span<const std::uint8_t, kHeaderSize> header) {
    const auto field = [header](std::size_t index) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= std::uint32_t{header[(index * 4) + i]} << (8U * i);
        }
        return value;
    };
    const std::uint32_t length = field(0);
    const std::uint32_t version = field(1);
    const std::uint32_t type = field(2);
    if (version != kVersionPlist || type != kMessagePlist) {
        return Expected<std::size_t, Error>::failure(Error::UnknownType);
    }
    if (length < kHeaderSize || length - kHeaderSize > kMaxPlistSize) {
        return Expected<std::size_t, Error>::failure(Error::TooLarge);
    }
    return std::size_t{length - kHeaderSize};
}

Expected<Message, Error> parse_message(std::string_view plist) {
    using Parsed = Expected<Message, Error>;
    std::vector<std::pair<std::string, std::string>> entries;
    if (!PlistReader(plist).read(entries)) {
        return Parsed::failure(Error::UnexpectedMessage);
    }
    const auto type = lookup(entries, "MessageType");
    if (type == "Result") {
        const auto number = integer<std::int64_t>(lookup(entries, "Number"));
        return number ? Parsed(Result{.number = *number}) : Parsed::failure(Error::ValueOutOfRange);
    }
    if (type == "Attached" || type == "Detached") {
        const auto device = integer<std::uint32_t>(lookup(entries, "DeviceID"));
        if (!device) {
            return Parsed::failure(Error::ValueOutOfRange);
        }
        if (type == "Detached") {
            return Detached{.device_id = *device};
        }
        return Attached{.device_id = *device,
                        .usb = lookup(entries, "Properties.ConnectionType") == "USB",
                        .serial_number =
                            std::string(lookup(entries, "Properties.SerialNumber").value_or(""))};
    }
    return Ignored{};
}

} // namespace osplay::transport::usbmux
