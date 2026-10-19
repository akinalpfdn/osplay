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

#include <stddef.h>
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
/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size): plain C for P/Invoke */
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
    OSPLAY_ENGINE_ERROR_MODE_CHANGE_FAILED = 13,
    OSPLAY_ENGINE_ERROR_HELPER_BUSY = 14,
    OSPLAY_ENGINE_ERROR_SESSION_FAILED = 15,      /* the core refused or ended the session */
    OSPLAY_ENGINE_ERROR_CAPTURE_FAILED = 16,      /* Desktop Duplication could not run */
    OSPLAY_ENGINE_ERROR_ENCODER_UNAVAILABLE = 17, /* no hardware H.264 encoder for the stream */
    OSPLAY_ENGINE_ERROR_ENCODER_FAILED = 18,
    OSPLAY_ENGINE_ERROR_DECODER_UNAVAILABLE = 19, /* no hardware H.264 decoder for the stream */
    OSPLAY_ENGINE_ERROR_PRESENTATION_FAILED = 20, /* the viewer window could not be shown */
    OSPLAY_ENGINE_RESULT_MAX_ENUM = 0x7FFFFFFF
} osplay_engine_result;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

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

/* --- Host session (Windows shares its screen) ------------------------------------------------ */

/*
 * A host session: listens for one viewer, adds the virtual monitor in the mode of the viewer's
 * first monitor, captures it with Desktop Duplication, encodes it with the hardware H.264 encoder
 * and sends the pointer as cursor packets. It needs the OsplayHelper service.
 */
typedef struct osplay_engine_host osplay_engine_host; /* opaque */

typedef struct osplay_engine_host_config {
    const uint8_t* secret_key;       /* the device identity, OSPLAY_KEY_SIZE bytes */
    const char* device_name_utf8;    /* shown to the viewer */
    const char* dump_directory_utf8; /* debug: also write the stream as .h264 here, or NULL */
} osplay_engine_host_config;

/*
 * Callbacks run on engine threads and must return quickly; they must not call
 * osplay_engine_host_destroy. `on_state` and `on_trust_query` are required.
 */
typedef struct osplay_engine_host_callbacks {
    void* context;
    /* `reason` is OSPLAY_OK unless the core closed the session because of an error. */
    void (*on_state)(void* context, osplay_session_state state, osplay_result reason);
    osplay_trust (*on_trust_query)(void* context, const uint8_t* peer_public_key);
    void (*on_pairing_code)(void* context, uint32_t code, const uint8_t* peer_public_key);
    void (*on_paired)(void* context, const uint8_t* peer_public_key);
    /* The virtual monitor is streaming; its place on the desktop in desktop coordinates. */
    void (*on_streaming)(void* context, int32_t left, int32_t top, int32_t right, int32_t bottom);
    /* The engine ended the session; a closed state follows. */
    void (*on_failed)(void* context, osplay_engine_result reason);
    void (*on_stats)(void* context, const osplay_session_stats* stats);
    void (*on_log)(void* context, osplay_log_level level, const char* message);
} osplay_engine_host_callbacks;

/* Connects to the helper service and starts listening on an ephemeral TCP port. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_host_start(
    const osplay_engine_host_config* config, const osplay_engine_host_callbacks* callbacks,
    osplay_engine_host** out_host);

/* The TCP port to advertise. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_host_control_port(osplay_engine_host* host,
                                                                       uint16_t* out_port);

/* Answers on_pairing_code (`accepted` is 0 or 1). */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_host_confirm_pairing(osplay_engine_host* host,
                                                                          uint8_t accepted);

/* Ends the session with Bye without waiting; on_state reports CLOSED. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_host_stop(osplay_engine_host* host);

/* Stops everything, removes the virtual monitor and releases the handle. Accepts NULL. */
OSPLAY_ENGINE_API void osplay_engine_host_destroy(osplay_engine_host* host);

/* --- Viewer session (Windows shows a remote screen) ------------------------------------------- */

#define OSPLAY_ENGINE_MONITOR_NAME_SIZE 65 /* 64 bytes of UTF-8 and the NUL */

/* A physical monitor this PC can offer (never the Virtual Display Driver's). */
typedef struct osplay_engine_monitor {
    uint8_t monitor_id; /* by desktop position, left to right; valid until the layout changes */
    uint16_t width_px;
    uint16_t height_px;
    uint32_t refresh_millihertz;
    uint16_t physical_width_mm; /* 0 when unknown */
    uint16_t physical_height_mm;
    uint16_t scale_percent;
    int32_t left; /* place on the desktop, physical pixels */
    int32_t top;
    int32_t right;
    int32_t bottom;
    char name_utf8[OSPLAY_ENGINE_MONITOR_NAME_SIZE]; /* the monitor's own name, may be empty */
} osplay_engine_monitor;

/* Fills up to `capacity` monitors; `capacity` must be at least OSPLAY_MAX_MONITORS. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_list_monitors(
    osplay_engine_monitor* out_monitors, size_t capacity, size_t* out_count);

/*
 * A viewer session: connects to a host, offers monitors, and shows each stream full screen on the
 * monitor the host chose (hardware decoding, flip-model presentation). The global hotkey hides and
 * shows every stream; hidden streams are paused on the host. A monitor that disappears stops only
 * its own stream.
 */
typedef struct osplay_engine_viewer osplay_engine_viewer; /* opaque */

typedef struct osplay_engine_viewer_config {
    const uint8_t* secret_key;    /* the device identity, OSPLAY_KEY_SIZE bytes */
    const char* device_name_utf8; /* shown to the host */
    const char* host_utf8;        /* the host's address */
    uint16_t port;                /* the host's control port */
    const uint8_t* monitor_ids;   /* ids from osplay_engine_list_monitors to offer, or NULL: all */
    size_t monitor_id_count;
} osplay_engine_viewer_config;

/* Callbacks run on engine threads and must return quickly; they must not call
 * osplay_engine_viewer_destroy. `on_state` and `on_trust_query` are required. */
typedef struct osplay_engine_viewer_callbacks {
    void* context;
    void (*on_state)(void* context, osplay_session_state state, osplay_result reason);
    osplay_trust (*on_trust_query)(void* context, const uint8_t* peer_public_key);
    void (*on_pairing_code)(void* context, uint32_t code, const uint8_t* peer_public_key);
    void (*on_paired)(void* context, const uint8_t* peer_public_key);
    /* The streams were hidden (0) or shown (1), by the hotkey or set_visible. */
    void (*on_visibility)(void* context, uint8_t visible);
    /* One stream could not be shown and was stopped; the session continues. */
    void (*on_stream_failed)(void* context, uint8_t stream_id, osplay_engine_result reason);
    void (*on_stats)(void* context, const osplay_session_stats* stats);
    void (*on_log)(void* context, osplay_log_level level, const char* message);
} osplay_engine_viewer_callbacks;

typedef struct osplay_engine_stream_figures {
    uint8_t stream_id;
    uint64_t frames_presented;
    uint64_t decode_errors;
} osplay_engine_stream_figures;

/* Starts connecting. MONITOR_NOT_FOUND when none of the requested monitors is attached. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_viewer_start(
    const osplay_engine_viewer_config* config, const osplay_engine_viewer_callbacks* callbacks,
    osplay_engine_viewer** out_viewer);

/* Whether the hide/show hotkey could be registered (another program may own it). */
OSPLAY_ENGINE_API osplay_engine_result
osplay_engine_viewer_hotkey_registered(osplay_engine_viewer* viewer, uint8_t* out_registered);

/* Answers on_pairing_code (`accepted` is 0 or 1). */
OSPLAY_ENGINE_API osplay_engine_result
osplay_engine_viewer_confirm_pairing(osplay_engine_viewer* viewer, uint8_t accepted);

/* Hides (0) or shows (1) every stream, like the hotkey. */
OSPLAY_ENGINE_API osplay_engine_result
osplay_engine_viewer_set_visible(osplay_engine_viewer* viewer, uint8_t visible);

/* Presentation figures of every running stream; `capacity` must be at least OSPLAY_MAX_STREAMS. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_viewer_stream_stats(
    osplay_engine_viewer* viewer, osplay_engine_stream_figures* out_stats, size_t capacity,
    size_t* out_count);

/* Ends the session with Bye without waiting; on_state reports CLOSED. */
OSPLAY_ENGINE_API osplay_engine_result osplay_engine_viewer_stop(osplay_engine_viewer* viewer);

/* Stops everything, closes the windows and releases the handle. Accepts NULL. */
OSPLAY_ENGINE_API void osplay_engine_viewer_destroy(osplay_engine_viewer* viewer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_ENGINE_H */
