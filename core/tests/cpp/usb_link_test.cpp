// Stream link records (PROTOCOL.md "Stream links"), the usbmuxd messages and the USB watcher
// against a fake usbmuxd.
#include "protocol/link_record.hpp"
#include "transport/aoa.hpp"
#include "transport/usb_watcher.hpp"
#include "transport/usbmux.hpp"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#ifndef _WIN32
#include <asio/local/stream_protocol.hpp>
#endif
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

namespace protocol = osplay::protocol;
namespace usbmux = osplay::transport::usbmux;
using osplay::Error;

// --- Android Open Accessory --------------------------------------------------------------------

TEST(Aoa, ReadsTheProtocolVersionTheDeviceAnswers) {
    namespace aoa = osplay::transport::aoa;
    const std::array<std::uint8_t, 2> two{2, 0};
    ASSERT_TRUE(aoa::parse_protocol(two).has_value());
    EXPECT_EQ(aoa::parse_protocol(two).value(), 2);
    const std::array<std::uint8_t, 1> short_answer{2};
    EXPECT_EQ(aoa::parse_protocol(short_answer).error(), Error::Truncated);
    const std::array<std::uint8_t, 2> none{0, 0};
    EXPECT_EQ(aoa::parse_protocol(none).error(), Error::UnsupportedVersion);
}

// --- Link records -------------------------------------------------------------------------------

TEST(LinkRecords, RoundTripThroughAnyChunking) {
    std::vector<std::uint8_t> stream;
    const std::vector<std::uint8_t> control{1, 2, 3};
    const std::vector<std::uint8_t> media(protocol::kMaxDatagramSize, 0xAB);
    ASSERT_TRUE(protocol::append_link_record(protocol::LinkRecordKind::Control, control, stream));
    ASSERT_TRUE(protocol::append_link_record(protocol::LinkRecordKind::Media, media, stream));
    EXPECT_EQ(stream.size(), 3 + control.size() + 3 + media.size());
    EXPECT_EQ(std::vector(stream.begin(), stream.begin() + 3),
              (std::vector<std::uint8_t>{1, 3, 0})); // kind, u16 little-endian length

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{700}}) {
        protocol::LinkRecordReader reader;
        std::vector<std::pair<protocol::LinkRecordKind, std::vector<std::uint8_t>>> records;
        for (std::size_t offset = 0; offset < stream.size(); offset += chunk) {
            const auto end = std::min(stream.size(), offset + chunk);
            reader.append(std::span(stream).subspan(offset, end - offset));
            for (;;) {
                auto record = reader.next();
                ASSERT_TRUE(record.has_value());
                if (!record.value()) {
                    break;
                }
                records.emplace_back(record.value()->kind,
                                     std::vector<std::uint8_t>(record.value()->payload.begin(),
                                                               record.value()->payload.end()));
            }
        }
        ASSERT_EQ(records.size(), 2U) << "chunk " << chunk;
        EXPECT_EQ(records[0].first, protocol::LinkRecordKind::Control);
        EXPECT_EQ(records[0].second, control);
        EXPECT_EQ(records[1].first, protocol::LinkRecordKind::Media);
        EXPECT_EQ(records[1].second, media);
    }
}

TEST(LinkRecords, RefuseWhatTheChannelCannotCarry) {
    std::vector<std::uint8_t> out;
    EXPECT_FALSE(protocol::append_link_record(protocol::LinkRecordKind::Control, {}, out));
    const std::vector<std::uint8_t> datagram(protocol::kMaxDatagramSize + 1, 0);
    EXPECT_FALSE(protocol::append_link_record(protocol::LinkRecordKind::Media, datagram, out));
    const std::vector<std::uint8_t> frame(protocol::kMaxLinkControlRecord + 1, 0);
    EXPECT_FALSE(protocol::append_link_record(protocol::LinkRecordKind::Control, frame, out));
    EXPECT_TRUE(out.empty());
}

osplay::Expected<std::optional<protocol::LinkRecord>, Error>
read_one(std::vector<std::uint8_t> bytes, protocol::LinkRecordReader& reader) {
    reader.append(bytes);
    return reader.next();
}

TEST(LinkRecords, AMalformedHeaderEndsTheLink) {
    protocol::LinkRecordReader unknown;
    const auto kind = read_one({3, 1, 0, 0}, unknown);
    ASSERT_FALSE(kind.has_value());
    EXPECT_EQ(kind.error(), Error::UnknownType);

    protocol::LinkRecordReader empty;
    const auto zero = read_one({1, 0, 0}, empty);
    ASSERT_FALSE(zero.has_value());
    EXPECT_EQ(zero.error(), Error::TooLarge);

    protocol::LinkRecordReader large;
    const auto media = read_one({2, 0xB1, 0x04}, large); // 1201 bytes
    ASSERT_FALSE(media.has_value());
    EXPECT_EQ(media.error(), Error::TooLarge);

    protocol::LinkRecordReader partial;
    const auto waiting = read_one({1, 5}, partial);
    ASSERT_TRUE(waiting.has_value());
    EXPECT_FALSE(waiting.value()) << "a header split across reads waits for the rest";
}

// --- usbmuxd messages ---------------------------------------------------------------------------

std::string plist_text(const std::vector<std::uint8_t>& message) {
    return {message.begin() + usbmux::kHeaderSize, message.end()};
}

std::array<std::uint8_t, usbmux::kHeaderSize> header_of(const std::vector<std::uint8_t>& message) {
    std::array<std::uint8_t, usbmux::kHeaderSize> header{};
    std::copy_n(message.begin(), header.size(), header.begin());
    return header;
}

TEST(Usbmux, ConnectCarriesTheDeviceAndThePortInNetworkByteOrder) {
    const auto request = usbmux::connect_request(12, 41847, 4);
    const auto size = usbmux::plist_size(header_of(request));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(size.value(), request.size() - usbmux::kHeaderSize);
    EXPECT_EQ(request[12], 4) << "tag";
    const auto text = plist_text(request);
    EXPECT_NE(text.find("<string>Connect</string>"), std::string::npos);
    EXPECT_NE(text.find("<key>DeviceID</key>\n\t<integer>12</integer>"), std::string::npos);
    // 41847 = 0xA377; usbmuxd reads 0x77A3 = 30627 as the port bytes A3 77.
    EXPECT_NE(text.find("<key>PortNumber</key>\n\t<integer>30627</integer>"), std::string::npos);
    EXPECT_TRUE(usbmux::parse_message(text).has_value()) << "the reader takes the writer's output";
}

TEST(Usbmux, ListenIsAPlistMessage) {
    const auto request = usbmux::listen_request(1);
    EXPECT_TRUE(usbmux::plist_size(header_of(request)).has_value());
    EXPECT_NE(plist_text(request).find("<string>Listen</string>"), std::string::npos);
}

TEST(Usbmux, OnlyPlistMessagesOfASaneSizeAreRead) {
    auto header = header_of(usbmux::listen_request(1));
    header[8] = 1; // binary "Result" message, which Osplay never asks for
    EXPECT_FALSE(usbmux::plist_size(header).has_value());
    header = header_of(usbmux::listen_request(1));
    header[3] = 0x10; // 256 MB
    EXPECT_FALSE(usbmux::plist_size(header).has_value());
    header = header_of(usbmux::listen_request(1));
    header[0] = 8; // shorter than the header itself
    header[1] = 0;
    EXPECT_FALSE(usbmux::plist_size(header).has_value());
}

// As macOS 26's usbmuxd sends them.
constexpr std::string_view kResult = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>MessageType</key>
	<string>Result</string>
	<key>Number</key>
	<integer>0</integer>
</dict>
</plist>
)";

std::string attached(int device_id, std::string_view connection) {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
	<key>DeviceID</key>
	<integer>)" +
           std::to_string(device_id) + R"(</integer>
	<key>MessageType</key>
	<string>Attached</string>
	<key>Properties</key>
	<dict>
		<key>ConnectionSpeed</key>
		<integer>480000000</integer>
		<key>ConnectionType</key>
		<string>)" +
           std::string(connection) + R"(</string>
		<key>DeviceID</key>
		<integer>)" +
           std::to_string(device_id) + R"(</integer>
		<key>EscapedFullServiceName</key>
		<string>a &amp; b</string>
		<key>InterfaceIndex</key>
		<array><integer>1</integer><dict/></array>
		<key>NetworkAddress</key>
		<data>EAIAAMCoAWU=</data>
		<key>ProductID</key>
		<integer>4776</integer>
		<key>SerialNumber</key>
		<string>00008027-000A1D2E3F</string>
		<key>Valid</key>
		<true/>
	</dict>
</dict>
</plist>
)";
}

std::string detached(int device_id) {
    return R"(<plist version="1.0"><dict><key>DeviceID</key><integer>)" +
           std::to_string(device_id) +
           R"(</integer><key>MessageType</key><string>Detached</string></dict></plist>)";
}

TEST(Usbmux, ReadsResultsAndDeviceEvents) {
    const auto result = usbmux::parse_message(kResult);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<usbmux::Result>(result.value()).number, usbmux::kResultOk);

    const auto usb = usbmux::parse_message(attached(7, "USB"));
    ASSERT_TRUE(usb.has_value());
    const auto& device = std::get<usbmux::Attached>(usb.value());
    EXPECT_EQ(device.device_id, 7U);
    EXPECT_TRUE(device.usb);
    EXPECT_EQ(device.serial_number, "00008027-000A1D2E3F");

    const auto wifi = usbmux::parse_message(attached(8, "Network"));
    ASSERT_TRUE(wifi.has_value());
    EXPECT_FALSE(std::get<usbmux::Attached>(wifi.value()).usb);

    const auto gone = usbmux::parse_message(detached(7));
    ASSERT_TRUE(gone.has_value());
    EXPECT_EQ(std::get<usbmux::Detached>(gone.value()).device_id, 7U);

    const auto paired = usbmux::parse_message(
        R"(<plist version="1.0"><dict><key>MessageType</key><string>Paired</string></dict></plist>)");
    ASSERT_TRUE(paired.has_value());
    EXPECT_TRUE(std::holds_alternative<usbmux::Ignored>(paired.value()));
}

TEST(Usbmux, RefusesMalformedPlists) {
    for (const std::string& text : {
             std::string(),
             std::string("<plist><dict>"),
             std::string("<plist><dict><key>MessageType</key><string>Result</dict></plist>"),
             std::string("<plist><dict><key>a</key><string>&bogus;</string></dict></plist>"),
             std::string("<plist><dict><key>MessageType</key><string>Result</string><key>"
                         "Number</key><integer>x</integer></dict></plist>"),
             std::string("<plist><dict><key>MessageType</key><string>Detached</string></dict>"
                         "</plist>"),
             std::string("<plist><dict><key>a</key><widget>1</widget></dict></plist>"),
             std::string("<plist><dict/></plist>trailing"),
             std::string("<plist>") + std::string(40, '<') + "</plist>",
         }) {
        EXPECT_FALSE(usbmux::parse_message(text).has_value()) << text;
    }
    std::string deep = "<plist>";
    for (int i = 0; i < 40; ++i) {
        deep += "<array>";
    }
    EXPECT_FALSE(usbmux::parse_message(deep).has_value()) << "nesting is bounded";
}

// --- Watcher ------------------------------------------------------------------------------------

std::vector<std::uint8_t> framed(const std::string& plist) {
    std::vector<std::uint8_t> out;
    for (const std::uint32_t field : {static_cast<std::uint32_t>(16 + plist.size()), 1U, 8U, 1U}) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>(field >> shift));
        }
    }
    out.insert(out.end(), plist.begin(), plist.end());
    return out;
}

/// Accepts watchers one after another; each gets `script` after its Listen request, then the
/// connection is dropped as when usbmuxd restarts.
class ListeningUsbmuxd {
public:
    explicit ListeningUsbmuxd(std::vector<std::string> script)
        : script_(std::move(script)), work_(asio::make_work_guard(io_)),
#ifdef _WIN32
          acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
#else
          acceptor_(io_, asio::local::stream_protocol::endpoint(fresh_path())),
#endif
          client_(io_) {
        accept();
        thread_ = std::thread([this] { io_.run(); });
    }
    ListeningUsbmuxd(const ListeningUsbmuxd&) = delete;
    ListeningUsbmuxd& operator=(const ListeningUsbmuxd&) = delete;
    ListeningUsbmuxd(ListeningUsbmuxd&&) = delete;
    ListeningUsbmuxd& operator=(ListeningUsbmuxd&&) = delete;
    ~ListeningUsbmuxd() {
        asio::post(io_, [this] {
            asio::error_code ignored;
            acceptor_.close(ignored);
            client_.close(ignored);
        });
        work_.reset();
        thread_.join();
#ifndef _WIN32
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
#endif
    }

    [[nodiscard]] osplay::transport::UsbmuxAddress address() const {
#ifdef _WIN32
        return {.port = acceptor_.local_endpoint().port()};
#else
        return {.path = path_};
#endif
    }

private:
#ifndef _WIN32
    std::string fresh_path() {
        path_ = (std::filesystem::temp_directory_path() / "osplay-usbmuxd-listen.sock").string();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        return path_;
    }
#endif

    void accept() {
        acceptor_.async_accept(client_, [this](const asio::error_code& error) {
            if (error) {
                return;
            }
            asio::error_code read_error;
            asio::read(client_, asio::buffer(header_), read_error);
            const auto size = usbmux::plist_size(header_);
            std::string request(size ? size.value() : 0, '\0');
            asio::read(client_, asio::buffer(request), read_error);
            asio::error_code ignored;
            asio::write(client_, asio::buffer(framed(std::string(kResult))), ignored);
            for (const auto& message : script_) {
                asio::write(client_, asio::buffer(framed(message)), ignored);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            client_.close(ignored);
            accept();
        });
    }

    std::vector<std::string> script_;
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
#ifdef _WIN32
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::socket client_;
#else
    std::string path_;
    asio::local::stream_protocol::acceptor acceptor_;
    asio::local::stream_protocol::socket client_;
#endif
    std::array<std::uint8_t, usbmux::kHeaderSize> header_{};
    std::thread thread_;
};

class RecordingUsbObserver final : public osplay::transport::UsbObserver {
public:
    void on_attached(std::uint32_t device_id) override { add("+" + std::to_string(device_id)); }
    void on_detached(std::uint32_t device_id) override { add("-" + std::to_string(device_id)); }

    std::vector<std::string> wait_for(std::size_t count) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(5), [&] { return events_.size() >= count; });
        return events_;
    }

private:
    void add(std::string event) {
        {
            const std::scoped_lock lock(mutex_);
            events_.push_back(std::move(event));
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> events_;
};

TEST(UsbWatcher, ReportsCableDevicesAndForgetsThemWhenUsbmuxdGoesAway) {
    ListeningUsbmuxd usbmuxd(
        {attached(5, "USB"), attached(6, "Network"), detached(5), attached(9, "USB"), detached(6)});
    RecordingUsbObserver observer;
    {
        const osplay::transport::UsbWatcher watcher(observer, usbmuxd.address(),
                                                    osplay::Microseconds{50'000});
        // First connection: +5 -5 +9, then -9 when it drops; the watcher reconnects and hears the
        // same script again.
        const auto events = observer.wait_for(8);
        ASSERT_GE(events.size(), 8U);
        EXPECT_EQ(std::vector(events.begin(), events.begin() + 8),
                  (std::vector<std::string>{"+5", "-5", "+9", "-9", "+5", "-5", "+9", "-9"}));
    }
}

TEST(UsbWatcher, KeepsTryingWhileUsbmuxdIsMissing) {
    RecordingUsbObserver observer;
    osplay::transport::UsbmuxAddress missing;
#ifdef _WIN32
    missing.port = 9; // discard: nothing listens on loopback
#else
    missing.path = (std::filesystem::temp_directory_path() / "osplay-no-usbmuxd.sock").string();
#endif
    const osplay::transport::UsbWatcher watcher(observer, missing, osplay::Microseconds{10'000});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Destroying it while it waits to retry must not hang or call back.
}

} // namespace
