// OsplayHostTest: developer tool for the Windows host pipeline (DEVPLAN Phase 8). Not shipped.
// It starts an engine host session, connects an in-process core viewer that offers one monitor,
// fills the virtual monitor with a moving pattern and prints, once per second, what arrives.
//
//   OsplayHostTest [--seconds N] [--size WIDTHxHEIGHT] [--hz RATE] [--dump DIRECTORY]
//   [--no-pattern]
//
// With --dump, the stream is written as .h264 and every cursor shape as a .bmp.
#include <osplay/engine.h>
#include <osplay/osplay.h>

#include <windows.h>

#include <bcrypt.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Options {
    int seconds = 30;
    std::uint16_t width = 2560;
    std::uint16_t height = 1664;
    std::uint32_t refresh_millihertz = 60'000;
    std::string dump_directory;
    bool pattern = true;
};

std::optional<Options> parse(std::span<char*> args) {
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        const bool has_value = i + 1 < args.size();
        if (arg == "--no-pattern") {
            options.pattern = false;
        } else if (arg == "--seconds" && has_value) {
            const long seconds = std::strtol(args[++i], nullptr, 10);
            options.seconds = static_cast<int>(std::clamp(seconds, 1L, 86'400L));
        } else if (arg == "--hz" && has_value) {
            options.refresh_millihertz =
                static_cast<std::uint32_t>(std::strtod(args[++i], nullptr) * 1000.0);
        } else if (arg == "--dump" && has_value) {
            options.dump_directory = args[++i];
        } else if (arg == "--size" && has_value) {
            const std::string_view size(args[++i]);
            const auto x = size.find('x');
            if (x == std::string_view::npos) {
                return std::nullopt;
            }
            options.width = static_cast<std::uint16_t>(
                std::strtoul(std::string(size.substr(0, x)).c_str(), nullptr, 10));
            options.height = static_cast<std::uint16_t>(
                std::strtoul(std::string(size.substr(x + 1)).c_str(), nullptr, 10));
        } else {
            return std::nullopt;
        }
    }
    return options;
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

double seconds_now() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

/// Writes a premultiplied BGRA cursor as a 32-bit top-down BMP.
void write_bmp(const std::filesystem::path& path, const osplay_cursor_shape& shape) {
    const auto pixels = static_cast<std::uint32_t>(shape.length);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42; // "BM"
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + pixels;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = shape.width;
    info.biHeight = -static_cast<LONG>(shape.height); // negative: rows top to bottom
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(shape.bgra),
              static_cast<std::streamsize>(shape.length));
}

/// What the in-process viewer measured, shared between the core's thread and the main thread.
struct Measurements {
    std::mutex mutex;
    std::vector<double> arrivals; // seconds, since the last report
    std::uint64_t bytes = 0;
    std::uint64_t frames_total = 0;
    std::uint32_t cursor_shapes = 0;
    std::uint32_t cursor_positions = 0;
    std::string last_shape;
    double latency_ms = 0;
    std::string dump_directory;
    std::atomic<int> viewer_state{OSPLAY_SESSION_CONNECTING};
    std::atomic<int> host_state{OSPLAY_SESSION_CONNECTING};
    std::atomic<int> host_failure{OSPLAY_ENGINE_OK};
    std::atomic<bool> streaming{false};
    std::mutex rect_mutex;
    std::optional<RECT> monitor;
};

osplay_session_callbacks viewer_callbacks(Measurements& measurements) {
    osplay_session_callbacks callbacks{};
    callbacks.context = &measurements;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result /*reason*/) {
        static_cast<Measurements*>(context)->viewer_state = state;
    };
    callbacks.on_trust_query = [](void* /*context*/, const uint8_t* /*key*/) {
        return OSPLAY_TRUST_TRUSTED;
    };
    callbacks.on_access_unit = [](void* context, const osplay_access_unit* unit) {
        auto* m = static_cast<Measurements*>(context);
        const double now = seconds_now();
        const std::scoped_lock lock(m->mutex);
        m->arrivals.push_back(now);
        m->bytes += unit->length;
        ++m->frames_total;
    };
    callbacks.on_cursor_shape = [](void* context, const osplay_cursor_shape* shape) {
        auto* m = static_cast<Measurements*>(context);
        const std::scoped_lock lock(m->mutex);
        ++m->cursor_shapes;
        m->last_shape = std::format("{}x{} hotspot {},{}", shape->width, shape->height,
                                    shape->hotspot_x, shape->hotspot_y);
        if (!m->dump_directory.empty()) {
            write_bmp(std::filesystem::path(m->dump_directory) /
                          std::format("cursor-{:03}.bmp", m->cursor_shapes),
                      *shape);
        }
    };
    callbacks.on_cursor_position = [](void* context, const osplay_cursor_position* /*position*/) {
        auto* m = static_cast<Measurements*>(context);
        const std::scoped_lock lock(m->mutex);
        ++m->cursor_positions;
    };
    callbacks.on_stats = [](void* context, const osplay_session_stats* stats) {
        auto* m = static_cast<Measurements*>(context);
        if (stats->stream_count > 0 && stats->streams[0].latency_us > 0) {
            const std::scoped_lock lock(m->mutex);
            m->latency_ms = stats->streams[0].latency_us / 1000.0;
        }
    };
    return callbacks;
}

osplay_engine_host_callbacks host_callbacks(Measurements& measurements) {
    osplay_engine_host_callbacks callbacks{};
    callbacks.context = &measurements;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result /*reason*/) {
        static_cast<Measurements*>(context)->host_state = state;
    };
    callbacks.on_trust_query = [](void* /*context*/, const uint8_t* /*key*/) {
        return OSPLAY_TRUST_TRUSTED;
    };
    callbacks.on_streaming = [](void* context, uint32_t /*display_count*/, int32_t left,
                                int32_t top, int32_t right, int32_t bottom) {
        auto* m = static_cast<Measurements*>(context);
        {
            const std::scoped_lock lock(m->rect_mutex);
            m->monitor = RECT{.left = left, .top = top, .right = right, .bottom = bottom};
        }
        m->streaming = true;
    };
    callbacks.on_failed = [](void* context, osplay_engine_result reason) {
        static_cast<Measurements*>(context)->host_failure = reason;
    };
    return callbacks;
}

/// A borderless window covering the virtual monitor with a bar that moves every composition frame.
class PatternWindow {
public:
    explicit PatternWindow(RECT rect) : thread_([this, rect] { run(rect); }) {}
    ~PatternWindow() {
        stopping_ = true;
        try {
            thread_.join();
        } catch (...) {
            std::terminate(); // the pattern thread cannot be left running
        }
    }
    PatternWindow(const PatternWindow&) = delete;
    PatternWindow& operator=(const PatternWindow&) = delete;
    PatternWindow(PatternWindow&&) = delete;
    PatternWindow& operator=(PatternWindow&&) = delete;

private:
    void run(RECT rect) {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = instance;
        window_class.lpszClassName = L"OsplayHostTestPattern";
        RegisterClassW(&window_class);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, window_class.lpszClassName,
                                      L"Osplay pattern", WS_POPUP, rect.left, rect.top, width,
                                      height, nullptr, nullptr, instance, nullptr);
        if (window == nullptr) {
            return;
        }
        ShowWindow(window, SW_SHOWNOACTIVATE);
        auto* black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        auto* white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        const int bar = std::max(width / 12, 1);
        int x = 0;
        while (!stopping_) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                DispatchMessageW(&message);
            }
            HDC dc = GetDC(window);
            const RECT background{.left = 0, .top = 0, .right = width, .bottom = height};
            FillRect(dc, &background, black);
            const RECT moving{.left = x, .top = 0, .right = x + bar, .bottom = height};
            FillRect(dc, &moving, white);
            ReleaseDC(window, dc);
            x = (x + std::max(width / 120, 1)) % std::max(width - bar, 1);
            // One step per composition frame, like an animation would.
            static_cast<void>(DwmFlush());
        }
        DestroyWindow(window);
    }

    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

const char* state_name(int state) {
    constexpr std::array<const char*, 8> kNames{"?",       "connecting",  "handshaking",
                                                "pairing", "negotiating", "streaming",
                                                "paused",  "closed"};
    return state >= 0 && std::cmp_less(state, kNames.size())
               ? kNames.at(static_cast<std::size_t>(state))
               : "?";
}

/// Prints one line of pacing figures and resets the per-second counters.
void report(int second, Measurements& m) {
    std::vector<double> arrivals;
    std::uint64_t bytes = 0;
    double latency = 0;
    std::uint32_t shapes = 0;
    std::uint32_t positions = 0;
    std::string last_shape;
    {
        const std::scoped_lock lock(m.mutex);
        // Keep the last arrival so the next interval is measured from it.
        arrivals = m.arrivals;
        if (!m.arrivals.empty()) {
            m.arrivals.erase(m.arrivals.begin(), m.arrivals.end() - 1);
        }
        bytes = std::exchange(m.bytes, 0);
        latency = m.latency_ms;
        shapes = m.cursor_shapes;
        positions = m.cursor_positions;
        last_shape = m.last_shape;
    }
    std::vector<double> intervals;
    for (std::size_t i = 1; i < arrivals.size(); ++i) {
        intervals.push_back((arrivals[i] - arrivals[i - 1]) * 1000.0);
    }
    std::ranges::sort(intervals);
    const auto percentile = [&](double fraction) {
        if (intervals.empty()) {
            return 0.0;
        }
        const auto index =
            static_cast<std::size_t>(fraction * static_cast<double>(intervals.size() - 1));
        return intervals.at(index);
    };
    std::cout << std::format(
        "{:3}s  {:5.1f} fps  p99 {:5.1f} ms  max {:5.1f} ms  {:5.1f} Mbit/s  latency {:5.1f} ms"
        "  cursor shapes {} positions {} ({})  host {} viewer {}\n",
        second, static_cast<double>(intervals.size()), percentile(0.99),
        intervals.empty() ? 0.0 : intervals.back(), static_cast<double>(bytes) * 8 / 1e6, latency,
        shapes, positions, last_shape, state_name(m.host_state), state_name(m.viewer_state));
}

int run(const Options& options) {
    std::array<uint8_t, OSPLAY_KEY_SIZE> host_secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> viewer_secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> public_key{};
    const osplay_random_source random{.fill = system_random, .context = nullptr};
    if (osplay_identity_generate(&random, host_secret.data(), public_key.data()) != OSPLAY_OK ||
        osplay_identity_generate(&random, viewer_secret.data(), public_key.data()) != OSPLAY_OK) {
        std::cerr << "could not create identities\n";
        return 1;
    }

    Measurements measurements;
    measurements.dump_directory = options.dump_directory;
    if (!options.dump_directory.empty()) {
        std::filesystem::create_directories(options.dump_directory);
    }
    const osplay_engine_host_config host_config{
        .secret_key = host_secret.data(),
        .device_name_utf8 = "Osplay Host Test",
        .dump_directory_utf8 =
            options.dump_directory.empty() ? nullptr : options.dump_directory.c_str(),
    };
    const osplay_engine_host_callbacks host_events = host_callbacks(measurements);
    osplay_engine_host* host = nullptr;
    if (const osplay_engine_result started =
            osplay_engine_host_start(&host_config, &host_events, &host);
        started != OSPLAY_ENGINE_OK) {
        std::cerr << "host start failed: engine result " << static_cast<int>(started) << '\n';
        return 1;
    }
    std::uint16_t port = 0;
    static_cast<void>(osplay_engine_host_control_port(host, &port));

    const osplay_monitor monitor{.monitor_id = 0,
                                 .width_px = options.width,
                                 .height_px = options.height,
                                 .refresh_millihertz = options.refresh_millihertz,
                                 .physical_width_mm = 302,
                                 .physical_height_mm = 196,
                                 .scale_percent = 200,
                                 .name = "Osplay Host Test"};
    const osplay_session_config viewer_config{
        .platform = OSPLAY_PLATFORM_MACOS,
        .device_name = "Osplay Host Test Viewer",
        .capabilities = {.codecs = OSPLAY_CODEC_H264_ANNEX_B,
                         .max_streams = 1,
                         .flags = 0,
                         .max_width = 7680,
                         .max_height = 4320,
                         .max_refresh_hz = 240},
        .monitors = &monitor,
        .monitor_count = 1,
        .control_port = 0,
    };
    const osplay_session_callbacks viewer_events = viewer_callbacks(measurements);
    osplay_session* viewer = nullptr;
    if (osplay_viewer_session_create(&viewer_config, viewer_secret.data(), &random, &viewer_events,
                                     "127.0.0.1", port, &viewer) != OSPLAY_OK) {
        std::cerr << "viewer start failed\n";
        osplay_engine_host_destroy(host);
        return 1;
    }

    std::cout << std::format(
        "Streaming a {}x{} at {:.3f} Hz virtual monitor for {} s. Move the mouse over it,\n"
        "hover text and window edges, try UAC, lock/unlock and a resolution change.\n",
        options.width, options.height, options.refresh_millihertz / 1000.0, options.seconds);
    std::optional<PatternWindow> pattern;
    for (int second = 1; second <= options.seconds; ++second) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (options.pattern && !pattern && measurements.streaming) {
            const std::scoped_lock lock(measurements.rect_mutex);
            if (measurements.monitor) {
                const RECT& r = *measurements.monitor;
                std::cout << std::format(
                    "Virtual monitor at x {}..{}, y {}..{} on the desktop: move the mouse past "
                    "the screen edge on that side to reach it.\n",
                    r.left, r.right, r.top, r.bottom);
                pattern.emplace(r);
            }
        }
        report(second, measurements);
        if (measurements.host_failure != OSPLAY_ENGINE_OK ||
            measurements.host_state == OSPLAY_SESSION_CLOSED) {
            std::cout << "host ended: engine result " << measurements.host_failure.load() << '\n';
            break;
        }
    }
    pattern.reset();
    osplay_session_destroy(viewer);
    osplay_engine_host_destroy(host);
    {
        const std::scoped_lock lock(measurements.mutex);
        std::cout << std::format("{} frames, {} cursor shapes received.\n",
                                 measurements.frames_total, measurements.cursor_shapes);
    }
    return measurements.host_failure == OSPLAY_ENGINE_OK ? 0 : 1;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[]) {
    try {
        const auto options = parse(std::span<char*>(argv, static_cast<std::size_t>(argc)));
        if (!options) {
            std::cerr << "Usage: OsplayHostTest [--seconds N] [--size WIDTHxHEIGHT] [--hz RATE] "
                         "[--dump DIRECTORY] [--no-pattern]\n";
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
