// The session C API end to end on loopback: first pairing, stream setup and frame delivery, as a
// platform (Swift or C#) would drive it.
#include "session_simulation.hpp"

#include <osplay/osplay.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

osplay_result fill_random(void* /*context*/, uint8_t* buffer, size_t length) {
    static std::mutex mutex;
    static std::mt19937_64 generator(7); // deterministic test randomness, not for production
    const std::scoped_lock lock(mutex);
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(generator());
    }
    return OSPLAY_OK;
}

const osplay_random_source kRandom{.fill = fill_random, .context = nullptr};

/// What one side's callbacks observed; filled on the session's I/O thread.
struct Side {
    std::mutex mutex;
    std::condition_variable changed;
    osplay_session_state state = OSPLAY_SESSION_CONNECTING;
    std::vector<uint32_t> codes;
    std::vector<std::array<uint8_t, OSPLAY_KEY_SIZE>> paired;
    std::vector<osplay_monitor> monitors;
    std::vector<std::string> monitor_names;
    std::string peer_name;
    std::vector<osplay_stream_config> stream_configs;
    std::vector<uint32_t> frames;
    bool intact = true;
    int keyframe_requests = 0;
    std::vector<uint8_t> cursor_image;
    uint32_t cursor_shape_id = 0;
    std::vector<osplay_cursor_position> cursor_positions;

    size_t frame_count() {
        const std::scoped_lock lock(mutex);
        return frames.size();
    }
    int keyframe_request_count() {
        const std::scoped_lock lock(mutex);
        return keyframe_requests;
    }

    template <typename Predicate>
    bool wait(Predicate predicate) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, seconds(5), [&] { return predicate(*this); });
    }

    template <typename Change>
    static void update(void* context, Change change) {
        auto& side = *static_cast<Side*>(context);
        {
            const std::scoped_lock lock(side.mutex);
            change(side);
        }
        side.changed.notify_all();
    }

    osplay_session_callbacks callbacks() {
        osplay_session_callbacks result{};
        result.context = this;
        result.on_state = [](void* context, osplay_session_state new_state,
                             osplay_result /*reason*/) {
            update(context, [new_state](Side& side) { side.state = new_state; });
        };
        result.on_peer = [](void* context, const osplay_peer_info* peer) {
            update(context,
                   [name = std::string(peer->device_name)](Side& side) { side.peer_name = name; });
        };
        result.on_trust_query = [](void* /*context*/, const uint8_t* /*key*/) {
            return OSPLAY_TRUST_UNKNOWN;
        };
        result.on_pairing_code = [](void* context, uint32_t code, const uint8_t* /*key*/) {
            update(context, [code](Side& side) { side.codes.push_back(code); });
        };
        result.on_paired = [](void* context, const uint8_t* key) {
            std::array<uint8_t, OSPLAY_KEY_SIZE> copy{};
            std::memcpy(copy.data(), key, copy.size());
            update(context, [copy](Side& side) { side.paired.push_back(copy); });
        };
        result.on_viewer_monitors = [](void* context, const osplay_monitor* received,
                                       size_t count) {
            update(context, [received, count](Side& side) {
                for (size_t i = 0; i < count; ++i) {
                    side.monitor_names.emplace_back(received[i].name);
                    side.monitors.push_back(received[i]);
                    side.monitors.back().name = nullptr; // the callback's string is gone afterwards
                }
            });
        };
        result.on_stream_config = [](void* context, const osplay_stream_config* config) {
            update(context, [copy = *config](Side& side) { side.stream_configs.push_back(copy); });
        };
        result.on_cursor_shape = [](void* context, const osplay_cursor_shape* shape) {
            update(context, [id = shape->shape_id,
                             image = std::vector<uint8_t>(
                                 shape->bgra, shape->bgra + shape->length)](Side& side) {
                side.cursor_shape_id = id;
                side.cursor_image = image;
            });
        };
        result.on_cursor_position = [](void* context, const osplay_cursor_position* position) {
            update(context,
                   [copy = *position](Side& side) { side.cursor_positions.push_back(copy); });
        };
        result.on_keyframe_request = [](void* context, uint8_t /*stream_id*/) {
            update(context, [](Side& side) { ++side.keyframe_requests; });
        };
        result.on_access_unit = [](void* context, const osplay_access_unit* unit) {
            uint32_t index = 0;
            std::memcpy(&index, unit->data, sizeof(index));
            const auto expected = osplay::test::synthetic_frame(index, unit->keyframe == 1);
            const bool matches = unit->length == expected.size() &&
                                 std::memcmp(unit->data, expected.data(), expected.size()) == 0;
            update(context, [index, matches](Side& side) {
                side.frames.push_back(index);
                side.intact = side.intact && matches;
            });
        };
        return result;
    }
};

std::array<uint8_t, OSPLAY_KEY_SIZE> new_secret() {
    std::array<uint8_t, OSPLAY_KEY_SIZE> secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> public_key{};
    EXPECT_EQ(osplay_identity_generate(&kRandom, secret.data(), public_key.data()), OSPLAY_OK);
    return secret;
}

constexpr osplay_capabilities kCapabilities{.codecs = OSPLAY_CODEC_H264_ANNEX_B,
                                            .max_streams = 2,
                                            .flags = 0,
                                            .max_width = 7680,
                                            .max_height = 4320,
                                            .max_refresh_hz = 240};

TEST(CapiSession, PairsConfiguresAndStreamsOverLoopback) {
    Side host_side;
    Side viewer_side;
    const auto host_secret = new_secret();
    const auto viewer_secret = new_secret();
    const auto host_callbacks = host_side.callbacks();
    const auto viewer_callbacks = viewer_side.callbacks();

    const osplay_session_config host_config{.platform = OSPLAY_PLATFORM_MACOS,
                                            .device_name = "C Host",
                                            .capabilities = kCapabilities,
                                            .monitors = nullptr,
                                            .monitor_count = 0};
    osplay_session* host = nullptr;
    ASSERT_EQ(osplay_host_session_create(&host_config, host_secret.data(), &kRandom,
                                         &host_callbacks, &host),
              OSPLAY_OK);
    uint16_t port = 0;
    ASSERT_EQ(osplay_session_control_port(host, &port), OSPLAY_OK);

    const osplay_monitor monitor{.monitor_id = 0,
                                 .width_px = 2560,
                                 .height_px = 1440,
                                 .refresh_millihertz = 144'000,
                                 .physical_width_mm = 597,
                                 .physical_height_mm = 336,
                                 .scale_percent = 100,
                                 .name = "Studio Display"};
    const osplay_session_config viewer_config{.platform = OSPLAY_PLATFORM_WINDOWS,
                                              .device_name = "C Viewer",
                                              .capabilities = kCapabilities,
                                              .monitors = &monitor,
                                              .monitor_count = 1};
    osplay_session* viewer = nullptr;
    ASSERT_EQ(osplay_viewer_session_create(&viewer_config, viewer_secret.data(), &kRandom,
                                           &viewer_callbacks, "127.0.0.1", port, &viewer),
              OSPLAY_OK);

    ASSERT_TRUE(host_side.wait([](Side& side) { return !side.codes.empty(); }));
    ASSERT_TRUE(viewer_side.wait([](Side& side) { return !side.codes.empty(); }));
    EXPECT_EQ(host_side.codes, viewer_side.codes);
    EXPECT_LT(host_side.codes.front(), 1'000'000U);
    EXPECT_EQ(host_side.peer_name, "C Viewer");
    EXPECT_EQ(viewer_side.peer_name, "C Host");

    ASSERT_EQ(osplay_session_confirm_pairing(host, 1), OSPLAY_OK);
    ASSERT_EQ(osplay_session_confirm_pairing(viewer, 1), OSPLAY_OK);
    ASSERT_TRUE(host_side.wait([](Side& side) { return !side.monitors.empty(); }));
    EXPECT_EQ(host_side.paired.size(), 1U);
    EXPECT_EQ(host_side.monitor_names, std::vector<std::string>{"Studio Display"});
    EXPECT_EQ(host_side.monitors.front().refresh_millihertz, 144'000U);

    const osplay_stream_config stream{.stream_id = 0,
                                      .monitor_id = 0,
                                      .width = 2560,
                                      .height = 1440,
                                      .refresh_millihertz = 144'000,
                                      .profile = OSPLAY_H264_HIGH,
                                      .cursor_mode = OSPLAY_CURSOR_IN_VIDEO,
                                      .bitrate_kbps = 30'000};
    ASSERT_EQ(osplay_session_configure_streams(host, &stream, 1), OSPLAY_OK);
    ASSERT_TRUE(
        viewer_side.wait([](Side& side) { return side.state == OSPLAY_SESSION_STREAMING; }));
    ASSERT_EQ(viewer_side.stream_configs.size(), 1U);
    EXPECT_EQ(viewer_side.stream_configs.front().bitrate_kbps, 30'000U);

    // Push until 30 frames arrived, answering keyframe requests like an encoder would.
    constexpr size_t kFrames = 30;
    int requests_seen = 0;
    const auto give_up = std::chrono::steady_clock::now() + seconds(10);
    for (uint32_t index = 0; viewer_side.frame_count() < kFrames; ++index) {
        ASSERT_LT(std::chrono::steady_clock::now(), give_up) << "frames stopped arriving";
        const int requests = host_side.keyframe_request_count();
        const bool keyframe = index == 0 || requests > requests_seen;
        requests_seen = requests;
        const auto bytes = osplay::test::synthetic_frame(index, keyframe);
        const osplay_access_unit unit{.stream_id = 0,
                                      .keyframe = static_cast<uint8_t>(keyframe),
                                      .codec_config = static_cast<uint8_t>(keyframe),
                                      .frame_id = 0,
                                      .capture_time_us = 0,
                                      .data = bytes.data(),
                                      .length = bytes.size()};
        ASSERT_EQ(osplay_session_push_access_unit(host, &unit), OSPLAY_OK);
        std::this_thread::sleep_for(milliseconds(16));
    }
    EXPECT_TRUE(viewer_side.intact);

    std::vector<uint8_t> cursor(std::size_t{24} * 24 * 4);
    for (std::size_t i = 0; i < cursor.size(); ++i) {
        cursor[i] = static_cast<uint8_t>(i);
    }
    const osplay_cursor_shape shape{.stream_id = 0,
                                    .shape_id = 11,
                                    .width = 24,
                                    .height = 24,
                                    .hotspot_x = 2,
                                    .hotspot_y = 2,
                                    .bgra = cursor.data(),
                                    .length = cursor.size()};
    ASSERT_EQ(osplay_session_push_cursor_shape(host, &shape), OSPLAY_OK);
    const osplay_cursor_position position{
        .stream_id = 0, .visible = 1, .shape_id = 11, .x = 640, .y = 360, .capture_time_us = 5};
    ASSERT_EQ(osplay_session_push_cursor_position(host, &position), OSPLAY_OK);
    ASSERT_TRUE(viewer_side.wait(
        [](Side& side) { return side.cursor_shape_id == 11 && !side.cursor_positions.empty(); }));
    EXPECT_EQ(viewer_side.cursor_image, cursor);
    EXPECT_EQ(viewer_side.cursor_positions.front().x, 640);
    osplay_cursor_shape bad = shape;
    bad.length = 10;
    EXPECT_EQ(osplay_session_push_cursor_shape(host, &bad), OSPLAY_ERROR_INVALID_ARGUMENT);

    osplay_session_destroy(viewer);
    EXPECT_TRUE(host_side.wait([](Side& side) { return side.state == OSPLAY_SESSION_CLOSED; }));
    osplay_session_destroy(host);
}

TEST(CapiSession, RejectsInvalidArguments) {
    Side side;
    const auto callbacks = side.callbacks();
    const auto secret = new_secret();
    osplay_session* session = reinterpret_cast<osplay_session*>(&side); // must be overwritten

    osplay_session_config config{.platform = OSPLAY_PLATFORM_MACOS,
                                 .device_name = "Host",
                                 .capabilities = kCapabilities,
                                 .monitors = nullptr,
                                 .monitor_count = 0};
    EXPECT_EQ(osplay_host_session_create(&config, secret.data(), &kRandom, nullptr, &session),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(session, nullptr);

    osplay_session_callbacks missing_trust = callbacks;
    missing_trust.on_trust_query = nullptr;
    EXPECT_EQ(
        osplay_host_session_create(&config, secret.data(), &kRandom, &missing_trust, &session),
        OSPLAY_ERROR_INVALID_ARGUMENT);

    config.platform = static_cast<osplay_platform>(9);
    EXPECT_EQ(osplay_host_session_create(&config, secret.data(), &kRandom, &callbacks, &session),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    config.platform = OSPLAY_PLATFORM_MACOS;

    config.device_name = "bad\nname";
    EXPECT_EQ(osplay_host_session_create(&config, secret.data(), &kRandom, &callbacks, &session),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    const std::string long_name(OSPLAY_MAX_STRING_BYTES + 1, 'x');
    config.device_name = long_name.c_str();
    EXPECT_EQ(osplay_host_session_create(&config, secret.data(), &kRandom, &callbacks, &session),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    config.device_name = "Viewer";

    EXPECT_EQ(osplay_viewer_session_create(&config, secret.data(), &kRandom, &callbacks,
                                           "127.0.0.1", 1, &session),
              OSPLAY_ERROR_INVALID_ARGUMENT)
        << "a viewer must offer at least one monitor";
    const std::array<uint8_t, OSPLAY_KEY_SIZE> zero_secret{};
    EXPECT_EQ(
        osplay_host_session_create(&config, zero_secret.data(), &kRandom, &callbacks, &session),
        OSPLAY_ERROR_INVALID_KEY);

    EXPECT_EQ(osplay_session_confirm_pairing(nullptr, 1), OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osplay_session_push_access_unit(nullptr, nullptr), OSPLAY_ERROR_INVALID_ARGUMENT);
    osplay_session_destroy(nullptr);
}

TEST(CapiSession, CommandsInTheWrongStateFail) {
    Side side;
    const auto callbacks = side.callbacks();
    const auto secret = new_secret();
    const osplay_session_config config{.platform = OSPLAY_PLATFORM_MACOS,
                                       .device_name = "Lonely Host",
                                       .capabilities = kCapabilities,
                                       .monitors = nullptr,
                                       .monitor_count = 0};
    osplay_session* host = nullptr;
    ASSERT_EQ(osplay_host_session_create(&config, secret.data(), &kRandom, &callbacks, &host),
              OSPLAY_OK);

    const osplay_stream_config stream{.stream_id = 0,
                                      .monitor_id = 0,
                                      .width = 1920,
                                      .height = 1080,
                                      .refresh_millihertz = 60'000,
                                      .profile = OSPLAY_H264_HIGH,
                                      .cursor_mode = OSPLAY_CURSOR_IN_VIDEO,
                                      .bitrate_kbps = 10'000};
    EXPECT_EQ(osplay_session_configure_streams(host, &stream, 1), OSPLAY_ERROR_INVALID_STATE);
    osplay_stream_config bad_profile = stream;
    bad_profile.profile = static_cast<osplay_h264_profile>(1);
    EXPECT_EQ(osplay_session_configure_streams(host, &bad_profile, 1),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osplay_session_confirm_pairing(host, 2), OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osplay_session_confirm_pairing(host, 1), OSPLAY_ERROR_INVALID_STATE);
    osplay_session_destroy(host);
}

} // namespace
