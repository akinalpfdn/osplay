// OsplayViewerTest: developer tool for the Windows viewer pipeline (DEVPLAN Phase 9). Not shipped.
//
//   OsplayViewerTest --list
//   OsplayViewerTest --loopback [--monitor ID] [--seconds N]
//   OsplayViewerTest --connect HOST PORT [--monitors ID,ID] [--seconds N]
//
// --list prints the monitors the viewer would offer. --loopback starts an engine host session
// (a virtual monitor through the helper service) and views it on one physical monitor. --connect
// views a remote host, such as the Mac's debug "Host for a remote viewer" (until Phase 10 adds
// discovery). Every second it prints, per stream, the frames presented and decode errors.
// Ctrl+Alt+Shift+V hides and shows the viewer windows.
#include <osplay/engine.h>
#include <osplay/osplay.h>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class Mode : std::uint8_t { List, Loopback, Connect };

/// Set by Ctrl+C so the run ends like a timeout: sessions closed and the summary printed.
std::atomic<bool>& interrupted() {
    static std::atomic<bool> flag{false};
    return flag;
}

BOOL WINAPI on_console_control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        interrupted() = true;
        return TRUE;
    }
    return FALSE;
}

struct Options {
    Mode mode = Mode::List;
    int seconds = 60;
    std::string host;
    std::uint16_t port = 0;
    std::vector<std::uint8_t> monitors; // empty: loopback uses the first, connect offers all
};

std::vector<std::uint8_t> parse_ids(std::string_view list) {
    std::vector<std::uint8_t> ids;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const std::string item(list.substr(0, comma));
        ids.push_back(static_cast<std::uint8_t>(std::strtoul(item.c_str(), nullptr, 10)));
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return ids;
}

std::optional<Options> parse(std::span<char*> args) {
    Options options;
    bool chosen = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        const bool has_value = i + 1 < args.size();
        if (arg == "--list") {
            options.mode = Mode::List;
            chosen = true;
        } else if (arg == "--loopback") {
            options.mode = Mode::Loopback;
            chosen = true;
        } else if (arg == "--connect" && i + 2 < args.size()) {
            options.mode = Mode::Connect;
            options.host = args[++i];
            const unsigned long port = std::strtoul(args[++i], nullptr, 10);
            if (port == 0 || port > 65'535) {
                return std::nullopt;
            }
            options.port = static_cast<std::uint16_t>(port);
            chosen = true;
        } else if ((arg == "--monitor" || arg == "--monitors") && has_value) {
            options.monitors = parse_ids(args[++i]);
        } else if (arg == "--seconds" && has_value) {
            const long seconds = std::strtol(args[++i], nullptr, 10);
            options.seconds = static_cast<int>(std::clamp(seconds, 1L, 86'400L));
        } else {
            return std::nullopt;
        }
    }
    return chosen ? std::optional(options) : std::nullopt;
}

osplay_result system_random(void* /*context*/, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length > ULONG_MAX) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
               ? OSPLAY_OK
               : OSPLAY_ERROR_RANDOM_UNAVAILABLE;
}

const char* state_name(int state) {
    constexpr std::array<const char*, 8> kNames{"?",       "connecting",  "handshaking",
                                                "pairing", "negotiating", "streaming",
                                                "paused",  "closed"};
    return state >= 0 && std::cmp_less(state, kNames.size())
               ? kNames.at(static_cast<std::size_t>(state))
               : "?";
}

/// What the viewer reported, shared between engine threads and the main thread.
struct Events {
    std::atomic<int> viewer_state{OSPLAY_SESSION_CONNECTING};
    std::atomic<int> viewer_reason{OSPLAY_OK};
    std::atomic<int> host_state{OSPLAY_SESSION_CONNECTING};
    std::atomic<int> host_failure{OSPLAY_ENGINE_OK};
    std::atomic<bool> visible{true};
    std::mutex mutex;
    std::vector<std::string> messages; // printed by the main thread
    std::map<std::uint8_t, double> latency_ms;

    void say(std::string message) {
        const std::scoped_lock lock(mutex);
        messages.push_back(std::move(message));
    }
};

osplay_engine_viewer_callbacks viewer_callbacks(Events& events) {
    osplay_engine_viewer_callbacks callbacks{};
    callbacks.context = &events;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result reason) {
        auto* e = static_cast<Events*>(context);
        e->viewer_state = state;
        e->viewer_reason = reason;
    };
    callbacks.on_trust_query = [](void* /*context*/, const uint8_t* /*key*/) {
        return OSPLAY_TRUST_TRUSTED; // a developer tool between the developer's own machines
    };
    callbacks.on_visibility = [](void* context, uint8_t visible) {
        auto* e = static_cast<Events*>(context);
        e->visible = visible == 1;
        e->say(visible == 1 ? "shown: the host resumes every stream"
                            : "hidden: the host pauses every stream");
    };
    callbacks.on_stream_failed = [](void* context, uint8_t stream_id, osplay_engine_result reason) {
        static_cast<Events*>(context)->say(std::format("stream {} stopped: engine result {}",
                                                       stream_id, static_cast<int>(reason)));
    };
    callbacks.on_stats = [](void* context, const osplay_session_stats* stats) {
        auto* e = static_cast<Events*>(context);
        const std::scoped_lock lock(e->mutex);
        for (const osplay_stream_stats& stream : std::span(stats->streams, stats->stream_count)) {
            e->latency_ms[stream.stream_id] = stream.latency_us / 1000.0;
        }
    };
    return callbacks;
}

osplay_engine_host_callbacks host_callbacks(Events& events) {
    osplay_engine_host_callbacks callbacks{};
    callbacks.context = &events;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result /*reason*/) {
        static_cast<Events*>(context)->host_state = state;
    };
    callbacks.on_trust_query = [](void* /*context*/, const uint8_t* /*key*/) {
        return OSPLAY_TRUST_TRUSTED;
    };
    callbacks.on_streaming = [](void* context, int32_t left, int32_t top, int32_t right,
                                int32_t bottom) {
        static_cast<Events*>(context)->say(std::format(
            "virtual monitor at x {}..{}, y {}..{}: drag a window there to see it on the viewer",
            left, right, top, bottom));
    };
    callbacks.on_failed = [](void* context, osplay_engine_result reason) {
        static_cast<Events*>(context)->host_failure = reason;
    };
    return callbacks;
}

std::vector<osplay_engine_monitor> list_monitors() {
    std::array<osplay_engine_monitor, OSPLAY_MAX_MONITORS> monitors{};
    std::size_t count = 0;
    if (osplay_engine_list_monitors(monitors.data(), monitors.size(), &count) != OSPLAY_ENGINE_OK) {
        return {};
    }
    return {monitors.begin(), monitors.begin() + static_cast<std::ptrdiff_t>(count)};
}

void print_monitors() {
    const auto monitors = list_monitors();
    if (monitors.empty()) {
        std::cout << "no physical monitors found\n";
    }
    for (const osplay_engine_monitor& m : monitors) {
        std::cout << std::format(
            "monitor {}: {}x{} at {:.3f} Hz, {}x{} mm, {}% scale, x {}..{} y {}..{}  \"{}\"\n",
            m.monitor_id, m.width_px, m.height_px, m.refresh_millihertz / 1000.0,
            m.physical_width_mm, m.physical_height_mm, m.scale_percent, m.left, m.right, m.top,
            m.bottom, static_cast<const char*>(m.name_utf8));
    }
}

/// One line per second: session state, then frames presented and decode errors per stream.
void report(int second, osplay_engine_viewer* viewer, Events& events,
            std::map<std::uint8_t, std::uint64_t>& last_presented) {
    std::array<osplay_engine_stream_figures, OSPLAY_MAX_STREAMS> stats{};
    std::size_t count = 0;
    static_cast<void>(
        osplay_engine_viewer_stream_stats(viewer, stats.data(), stats.size(), &count));
    std::vector<std::string> messages;
    std::map<std::uint8_t, double> latency;
    {
        const std::scoped_lock lock(events.mutex);
        messages.swap(events.messages);
        latency = events.latency_ms;
    }
    for (const std::string& message : messages) {
        std::cout << "  " << message << '\n';
    }
    std::string line = std::format("{:3}s  viewer {}{}", second, state_name(events.viewer_state),
                                   events.visible ? "" : " (hidden)");
    for (const auto& stream : std::span(stats.data(), count)) {
        const std::uint64_t previous = last_presented[stream.stream_id];
        line +=
            std::format("  | stream {}: {:3} fps presented, {} decode errors, latency {:.1f} ms",
                        stream.stream_id, stream.frames_presented - previous, stream.decode_errors,
                        latency[stream.stream_id]);
        last_presented[stream.stream_id] = stream.frames_presented;
    }
    std::cout << line << '\n';
}

int run(const Options& options) {
    if (options.mode == Mode::List) {
        print_monitors();
        return 0;
    }
    std::array<uint8_t, OSPLAY_KEY_SIZE> host_secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> viewer_secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> public_key{};
    const osplay_random_source random{.fill = system_random, .context = nullptr};
    if (osplay_identity_generate(&random, host_secret.data(), public_key.data()) != OSPLAY_OK ||
        osplay_identity_generate(&random, viewer_secret.data(), public_key.data()) != OSPLAY_OK) {
        std::cerr << "could not create identities\n";
        return 1;
    }

    Events events;
    osplay_engine_host* host = nullptr;
    std::string address = options.host;
    std::uint16_t port = options.port;
    std::vector<std::uint8_t> offered = options.monitors;
    if (options.mode == Mode::Loopback) {
        const osplay_engine_host_config host_config{.secret_key = host_secret.data(),
                                                    .device_name_utf8 = "Osplay Loopback Host",
                                                    .dump_directory_utf8 = nullptr};
        const osplay_engine_host_callbacks host_events = host_callbacks(events);
        if (const osplay_engine_result started =
                osplay_engine_host_start(&host_config, &host_events, &host);
            started != OSPLAY_ENGINE_OK) {
            std::cerr << "host start failed: engine result " << static_cast<int>(started) << '\n';
            return 1;
        }
        static_cast<void>(osplay_engine_host_control_port(host, &port));
        address = "127.0.0.1";
        if (offered.empty()) {
            offered.push_back(0); // a Windows host streams to the viewer's first monitor only
        }
    }

    const osplay_engine_viewer_config viewer_config{
        .secret_key = viewer_secret.data(),
        .device_name_utf8 = "Osplay Viewer Test",
        .host_utf8 = address.c_str(),
        .port = port,
        .monitor_ids = offered.empty() ? nullptr : offered.data(),
        .monitor_id_count = offered.size(),
    };
    const osplay_engine_viewer_callbacks viewer_events = viewer_callbacks(events);
    osplay_engine_viewer* viewer = nullptr;
    if (const osplay_engine_result started =
            osplay_engine_viewer_start(&viewer_config, &viewer_events, &viewer);
        started != OSPLAY_ENGINE_OK) {
        std::cerr << "viewer start failed: engine result " << static_cast<int>(started) << '\n';
        osplay_engine_host_destroy(host);
        return 1;
    }
    std::uint8_t hotkey = 0;
    static_cast<void>(osplay_engine_viewer_hotkey_registered(viewer, &hotkey));
    print_monitors();
    std::cout << std::format("Viewing {}:{} for {} s. Ctrl+Alt+Shift+V hides and shows{}.\n",
                             address, port, options.seconds,
                             hotkey == 1 ? "" : " (hotkey unavailable: another program owns it)");

    std::map<std::uint8_t, std::uint64_t> last_presented;
    SetConsoleCtrlHandler(on_console_control, TRUE);
    int second = 0;
    const char* ending = "time is up";
    while (second < options.seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        report(++second, viewer, events, last_presented);
        if (events.viewer_state == OSPLAY_SESSION_CLOSED ||
            events.host_failure != OSPLAY_ENGINE_OK) {
            ending = "the session closed";
            break;
        }
        if (interrupted()) {
            ending = "Ctrl+C";
            break;
        }
    }
    std::array<osplay_engine_stream_figures, OSPLAY_MAX_STREAMS> totals{};
    std::size_t total_count = 0;
    static_cast<void>(
        osplay_engine_viewer_stream_stats(viewer, totals.data(), totals.size(), &total_count));
    std::cout << std::format("ended after {} s ({}): core result {}, host engine result {}\n",
                             second, ending, events.viewer_reason.load(),
                             events.host_failure.load());
    for (const auto& stream : std::span(totals.data(), total_count)) {
        std::cout << std::format("  stream {}: {} frames presented, {} decode errors\n",
                                 stream.stream_id, stream.frames_presented, stream.decode_errors);
    }
    osplay_engine_viewer_destroy(viewer);
    osplay_engine_host_destroy(host);
    return events.host_failure == OSPLAY_ENGINE_OK ? 0 : 1;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[]) {
    try {
        const auto options = parse(std::span<char*>(argv, static_cast<std::size_t>(argc)));
        if (!options) {
            std::cerr << "Usage: OsplayViewerTest --list\n"
                         "       OsplayViewerTest --loopback [--monitor ID] [--seconds N]\n"
                         "       OsplayViewerTest --connect HOST PORT [--monitors ID,ID] "
                         "[--seconds N]\n";
            return 2;
        }
        return run(*options);
    } catch (const std::exception& error) {
        // C stdio cannot throw, unlike iostreams.
        static_cast<void>(std::fputs("error: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("error: unknown exception\n", stderr));
        return 1;
    }
}
