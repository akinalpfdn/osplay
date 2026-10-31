// OsplayAndroidTest: developer tool for Android on the USB cable (TAB-05). Not shipped.
//
//   OsplayAndroidTest [--seconds N]
//
// Watches the USB bus: every Android device plugged in is switched to Osplay's accessory (UsbDk),
// and when one comes back as an accessory a host session starts on it with this PC's stored
// identity, so a device already paired from the tray app connects without a code; a new one is
// paired by accepting the code here. Prints the watcher's log, session states and the virtual
// monitor. Ctrl+C ends the run.
#include <osplay/engine.h>
#include <osplay/osplay.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <format>
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
    int seconds = 600;
};

std::optional<Options> parse(std::span<char*> args) {
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--seconds" && i + 1 < args.size()) {
            const long seconds = std::strtol(args[++i], nullptr, 10);
            options.seconds = static_cast<int>(std::clamp(seconds, 1L, 86'400L));
        } else {
            return std::nullopt;
        }
    }
    return options;
}

const char* state_name(int state) {
    constexpr std::array<const char*, 8> kNames{"?",       "connecting",  "handshaking",
                                                "pairing", "negotiating", "streaming",
                                                "paused",  "closed"};
    return state >= 0 && std::cmp_less(state, kNames.size())
               ? kNames.at(static_cast<std::size_t>(state))
               : "?";
}

/// What the engine reported, shared between engine threads and the main thread.
struct Events {
    std::mutex mutex;
    std::vector<std::string> messages; // printed by the main thread
    std::deque<std::uint32_t> attached;
    std::deque<std::uint32_t> detached;
    std::atomic<bool> pairing{false};
    std::atomic<bool> closed{false};

    void say(std::string message) {
        const std::scoped_lock lock(mutex);
        messages.push_back(std::move(message));
    }
};

osplay_engine_android_callbacks android_callbacks(Events& events) {
    osplay_engine_android_callbacks callbacks{};
    callbacks.context = &events;
    callbacks.on_attached = [](void* context, uint32_t id) {
        auto* e = static_cast<Events*>(context);
        const std::scoped_lock lock(e->mutex);
        e->attached.push_back(id);
    };
    callbacks.on_detached = [](void* context, uint32_t id) {
        auto* e = static_cast<Events*>(context);
        const std::scoped_lock lock(e->mutex);
        e->detached.push_back(id);
    };
    callbacks.on_debugging_on = [](void* context) {
        static_cast<Events*>(context)->say(
            "USB debugging is on: turn it off (Developer options) and plug the cable in again");
    };
    callbacks.on_log = [](void* context, osplay_log_level /*level*/, const char* message) {
        static_cast<Events*>(context)->say(std::format("watcher: {}", message));
    };
    return callbacks;
}

osplay_engine_host_callbacks host_callbacks(Events& events) {
    osplay_engine_host_callbacks callbacks{};
    callbacks.context = &events;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result reason) {
        auto* e = static_cast<Events*>(context);
        e->say(std::format("host: {} (result {})", state_name(state), static_cast<int>(reason)));
        if (state == OSPLAY_SESSION_CLOSED) {
            e->closed = true;
        }
    };
    callbacks.on_pairing_code = [](void* context, uint32_t code, const uint8_t* /*key*/) {
        auto* e = static_cast<Events*>(context);
        e->say(std::format("pairing code {:06}: accepted", code));
        e->pairing = true;
    };
    callbacks.on_peer = [](void* context, osplay_platform /*platform*/, const char* name) {
        static_cast<Events*>(context)->say(std::format("viewer: {}", name));
    };
    callbacks.on_streaming = [](void* context, uint32_t count, int32_t left, int32_t top,
                                int32_t right, int32_t bottom) {
        static_cast<Events*>(context)->say(
            std::format("{} virtual monitor(s), the first at x {}..{}, y {}..{}", count, left,
                        right, top, bottom));
    };
    callbacks.on_failed = [](void* context, osplay_engine_result reason) {
        static_cast<Events*>(context)->say(
            std::format("host failed: engine result {}", static_cast<int>(reason)));
    };
    callbacks.on_log = [](void* context, osplay_log_level level, const char* message) {
        if (level >= OSPLAY_LOG_WARNING) {
            static_cast<Events*>(context)->say(std::format("core: {}", message));
        }
    };
    return callbacks;
}

template <typename T>
std::deque<T> take(std::mutex& mutex, std::deque<T>& queue) {
    const std::scoped_lock lock(mutex);
    return std::exchange(queue, {});
}

int run(const Options& options) {
    SetConsoleCtrlHandler(on_console_control, TRUE);
    std::cout << std::unitbuf; // readable while it runs, also when redirected to a file
    Events events;
    const osplay_engine_android_callbacks android = android_callbacks(events);
    osplay_engine_android_watcher* watcher = nullptr;
    if (const auto result = osplay_engine_android_watcher_start(&android, &watcher);
        result != OSPLAY_ENGINE_OK) {
        std::cerr << "watcher failed: engine result " << static_cast<int>(result) << '\n';
        return 1;
    }
    std::cout << "Plug in an Android device with Osplay and USB debugging off.\n";

    const osplay_engine_host_callbacks callbacks = host_callbacks(events);
    osplay_engine_host_config config{};
    config.device_name_utf8 = "Osplay Android test";
    osplay_engine_host* host = nullptr;
    std::uint32_t host_accessory = 0;

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
    while (!interrupted() && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (const std::string& message : [&] {
                 const std::scoped_lock lock(events.mutex);
                 return std::exchange(events.messages, {});
             }()) {
            std::cout << message << '\n';
        }
        if (events.pairing.exchange(false) && host != nullptr) {
            static_cast<void>(osplay_engine_host_confirm_pairing(host, 1));
        }
        for (const std::uint32_t id : take(events.mutex, events.detached)) {
            std::cout << "accessory " << id << " detached\n";
            if (id == host_accessory) {
                osplay_engine_host_destroy(host);
                host = nullptr;
                host_accessory = 0;
            }
        }
        if (host != nullptr && events.closed.exchange(false)) {
            osplay_engine_host_destroy(host);
            host = nullptr;
            host_accessory = 0;
        }
        for (const std::uint32_t id : take(events.mutex, events.attached)) {
            std::cout << "accessory " << id << " attached\n";
            if (host != nullptr) {
                continue; // one session at a time in this tool
            }
            events.closed = false;
            const auto result =
                osplay_engine_host_start_accessory(&config, &callbacks, watcher, id, &host);
            if (result == OSPLAY_ENGINE_OK) {
                host_accessory = id;
                std::cout << "host session waiting on accessory " << id << '\n';
            } else {
                std::cout << "host start failed: engine result " << static_cast<int>(result)
                          << '\n';
            }
        }
    }
    osplay_engine_host_destroy(host);
    osplay_engine_android_watcher_destroy(watcher);
    return 0;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[]) {
    try {
        const auto options = parse(std::span<char*>(argv, static_cast<std::size_t>(argc)));
        if (!options) {
            std::cerr << "Usage: OsplayAndroidTest [--seconds N]\n";
            return 2;
        }
        return run(*options);
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("error: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("error: unknown exception\n", stderr));
        return 1;
    }
}
