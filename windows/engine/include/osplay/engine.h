/*
 * Osplay Windows engine — public C API consumed by the WPF shell through P/Invoke.
 *
 * Every function returns an osplay_engine_result; none throws or produces user-facing text.
 * Unless stated otherwise, functions block and may be called from any thread, but calls on the
 * same handle must not run concurrently.
 */
#ifndef OSPLAY_ENGINE_H
#define OSPLAY_ENGINE_H

#include <osplay/osplay.h>

#include <stdint.h>

#ifdef OSPLAY_ENGINE_BUILDING
#define OSPLAY_ENGINE_API __declspec(dllexport)
#else
#define OSPLAY_ENGINE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Stable values: the shell maps each one to a localized message. Never renumber. */
typedef enum osplay_engine_result {
    OSPLAY_ENGINE_OK = 0,
    OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT = 1,
    OSPLAY_ENGINE_ERROR_HELPER_UNAVAILABLE = 2,
    OSPLAY_ENGINE_ERROR_HELPER_ACCESS_DENIED = 3,
    OSPLAY_ENGINE_ERROR_HELPER_NOT_TRUSTED = 4,
    OSPLAY_ENGINE_ERROR_PROTOCOL_MISMATCH = 5,
    OSPLAY_ENGINE_ERROR_CONNECTION_LOST = 6,
    OSPLAY_ENGINE_ERROR_VDD_NOT_INSTALLED = 7,
    OSPLAY_ENGINE_ERROR_DEVICE_CONTROL_FAILED = 8,
    OSPLAY_ENGINE_ERROR_CONFIG_WRITE_FAILED = 9,
    OSPLAY_ENGINE_ERROR_REJECTED = 10,
    OSPLAY_ENGINE_ERROR_INTERNAL = 11,
    OSPLAY_ENGINE_ERROR_MONITOR_NOT_FOUND = 12,
    OSPLAY_ENGINE_ERROR_MODE_CHANGE_FAILED = 13
} osplay_engine_result;

/* Version of the core library linked into the engine. Thread-safe. */
OSPLAY_ENGINE_API osplay_version osplay_engine_core_version(void);

/* --- Virtual display (Windows host) --------------------------------------------------------- */

typedef struct osplay_virtual_display osplay_virtual_display; /* opaque */

typedef struct osplay_virtual_monitor_config {
    uint32_t width;              /* pixels, 640...7680 */
    uint32_t height;             /* pixels, 480...4320 */
    uint32_t refresh_millihertz; /* 24000...240000 */
    const char* gpu_name_utf8;   /* DXGI adapter description, or NULL to let the driver choose */
} osplay_virtual_monitor_config;

typedef struct osplay_virtual_monitor_status {
    uint8_t driver_installed;
    uint8_t monitor_present;
    uint8_t configured;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_millihertz;
} osplay_virtual_monitor_status;

/* Connects to the OsplayHelper service. The monitor lives at most as long as the handle. */
OSPLAY_ENGINE_API osplay_engine_result
osplay_virtual_display_open(osplay_virtual_display** out_display);

/* Adds the virtual monitor and sets exactly this mode, replacing any previous configuration.
 * Blocks until the monitor has joined the desktop (a few seconds at most). If the mode cannot be
 * set, the monitor is removed again and the error is returned. */
OSPLAY_ENGINE_API osplay_engine_result osplay_virtual_display_create_monitor(
    osplay_virtual_display* display, const osplay_virtual_monitor_config* config);

OSPLAY_ENGINE_API osplay_engine_result
osplay_virtual_display_remove_monitor(osplay_virtual_display* display);

OSPLAY_ENGINE_API osplay_engine_result osplay_virtual_display_status(
    osplay_virtual_display* display, osplay_virtual_monitor_status* out_status);

/* Removes the monitor (if present) and releases the handle. Accepts NULL. */
OSPLAY_ENGINE_API void osplay_virtual_display_close(osplay_virtual_display* display);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_ENGINE_H */
