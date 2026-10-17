// OsplayVdTool: temporary developer tool that drives the virtual monitor through the engine's
// C API (DEVPLAN Phase 2). Not shipped.
//   OsplayVdTool status
//   OsplayVdTool create <width> <height> <refresh-hz> [gpu name]
#include <osplay/engine.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 12> kResultNames{
    "ok",
    "invalid argument",
    "helper service not running (install it with OsplayHelper.exe --install)",
    "access denied by the helper (not the console user?)",
    "pipe server is not the helper service",
    "protocol mismatch (engine and helper versions differ)",
    "connection to the helper lost",
    "Virtual Display Driver not installed",
    "could not enable or disable the driver device",
    "could not write the driver settings",
    "rejected by the helper",
    "internal error",
};

std::string_view describe(osplay_engine_result result) {
    const auto index = static_cast<std::size_t>(result);
    return index < kResultNames.size() ? kResultNames.at(index) : "unknown result";
}

bool report(std::string_view step, osplay_engine_result result) {
    std::cout << step << ": " << describe(result) << '\n';
    return result == OSPLAY_ENGINE_OK;
}

std::string_view yes_no(std::uint8_t flag) {
    return flag != 0 ? "yes" : "no";
}

void print_status(osplay_virtual_display* display) {
    osplay_virtual_monitor_status status{};
    if (!report("status", osplay_virtual_display_status(display, &status))) {
        return;
    }
    std::cout << "  driver installed: " << yes_no(status.driver_installed) << '\n'
              << "  monitor present:  " << yes_no(status.monitor_present) << '\n'
              << "  configured:       " << yes_no(status.configured);
    if (status.configured != 0) {
        std::cout << std::format(" ({}x{} @ {:.3f} Hz)", status.width, status.height,
                                 status.refresh_millihertz / 1000.0);
    }
    std::cout << '\n';
}

int usage() {
    std::cerr << "Usage:\n  OsplayVdTool status\n"
                 "  OsplayVdTool create <width> <height> <refresh-hz> [gpu name]\n";
    return 2;
}

int run(std::span<char*> args) {
    if (args.size() < 2) {
        return usage();
    }
    const std::string_view command(args[1]);
    const bool create = command == "create";
    if ((command != "status" && !create) || (create && args.size() != 5 && args.size() != 6)) {
        return usage();
    }

    osplay_virtual_display* display = nullptr;
    if (!report("connect", osplay_virtual_display_open(&display))) {
        return 1;
    }

    bool ok = true;
    if (!create) {
        print_status(display);
    } else {
        const osplay_virtual_monitor_config config{
            .width = static_cast<std::uint32_t>(std::strtoul(args[2], nullptr, 10)),
            .height = static_cast<std::uint32_t>(std::strtoul(args[3], nullptr, 10)),
            .refresh_millihertz =
                static_cast<std::uint32_t>(std::lround(std::strtod(args[4], nullptr) * 1000.0)),
            .gpu_name_utf8 = args.size() == 6 ? args[5] : nullptr,
        };
        ok = report("create", osplay_virtual_display_create_monitor(display, &config));
        print_status(display);
        if (ok) {
            std::cout
                << "The virtual monitor stays while this tool runs. Press Enter to remove it...\n";
            std::string ignored;
            std::getline(std::cin, ignored);
            ok = report("remove", osplay_virtual_display_remove_monitor(display));
        }
    }
    osplay_virtual_display_close(display);
    return ok ? 0 : 1;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(*-avoid-c-arrays)
int main(int argc, char* argv[]) {
    try {
        return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
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
