// OsplayVdTool: temporary developer tool that drives the virtual monitor through the engine's
// C API (DEVPLAN Phase 2). Not shipped.
//   OsplayVdTool status
//   OsplayVdTool create <width> <height> <refresh-hz> [gpu name]
#include <osplay/engine.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

const char* describe(osplay_engine_result result) {
    const auto index = static_cast<std::size_t>(result);
    return index < kResultNames.size() ? kResultNames.at(index).data() : "unknown result";
}

int report(const char* step, osplay_engine_result result) {
    std::printf("%s: %s\n", step, describe(result));
    return result == OSPLAY_ENGINE_OK ? 0 : 1;
}

void print_status(osplay_virtual_display* display) {
    osplay_virtual_monitor_status status{};
    if (report("status", osplay_virtual_display_status(display, &status)) != 0) {
        return;
    }
    std::printf("  driver installed: %s\n  monitor present:  %s\n  configured:       %s",
                status.driver_installed ? "yes" : "no", status.monitor_present ? "yes" : "no",
                status.configured ? "yes" : "no");
    if (status.configured != 0) {
        std::printf(" (%ux%u @ %.3f Hz)", status.width, status.height,
                    status.refresh_millihertz / 1000.0);
    }
    std::printf("\n");
}

int usage() {
    std::fprintf(stderr, "Usage:\n  OsplayVdTool status\n  OsplayVdTool create <width> <height> "
                         "<refresh-hz> [gpu name]\n");
    return 2;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return usage();
    }
    const std::string_view command(argv[1]);
    if (command != "status" && command != "create") {
        return usage();
    }
    if (command == "create" && argc != 5 && argc != 6) {
        return usage();
    }

    osplay_virtual_display* display = nullptr;
    if (report("connect", osplay_virtual_display_open(&display)) != 0) {
        return 1;
    }

    int exit_code = 0;
    if (command == "status") {
        print_status(display);
    } else {
        const osplay_virtual_monitor_config config{
            .width = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)),
            .height = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)),
            .refresh_millihertz =
                static_cast<uint32_t>(std::strtod(argv[4], nullptr) * 1000.0 + 0.5),
            .gpu_name_utf8 = argc == 6 ? argv[5] : nullptr,
        };
        exit_code = report("create", osplay_virtual_display_create_monitor(display, &config));
        print_status(display);
        if (exit_code == 0) {
            std::printf(
                "The virtual monitor stays while this tool runs. Press Enter to remove it...\n");
            static_cast<void>(std::getchar());
            exit_code = report("remove", osplay_virtual_display_remove_monitor(display));
        }
    }
    osplay_virtual_display_close(display);
    return exit_code;
}
