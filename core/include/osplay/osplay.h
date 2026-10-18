/*
 * Osplay core — public C API.
 *
 * This is the only public header of the shared core library. It is consumed by C++ (Windows
 * engine), Swift (through the OsplayCore clang module) and C# (through P/Invoke), so it stays
 * plain C: opaque handles, POD structs and fixed-width integers only.
 */
#ifndef OSPLAY_OSPLAY_H
#define OSPLAY_OSPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single source of truth for the core version; CMake parses these lines. */
#define OSPLAY_CORE_VERSION_MAJOR 0
#define OSPLAY_CORE_VERSION_MINOR 1
#define OSPLAY_CORE_VERSION_PATCH 0

typedef struct osplay_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} osplay_version;

/* Wire protocol constants (docs/PROTOCOL.md). */
#define OSPLAY_PROTOCOL_VERSION 1
#define OSPLAY_MAX_CONTROL_MESSAGE_SIZE 4096
#define OSPLAY_MAX_MEDIA_PLAINTEXT_SIZE 1184

/*
 * Result of every core call. Values are stable and documented in docs/PROTOCOL.md ("Error codes");
 * new codes are only ever appended. Platforms map them to localized messages; core never produces
 * user-facing text.
 */
/*
 * Plain C enums: this header is consumed from C, Swift and C#. Each ends with a *_MAX_ENUM value
 * so the C++ side can receive any 32-bit value from a binding without undefined behaviour; it is
 * never a valid argument or result.
 */
/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osplay_result {
    OSPLAY_OK = 0,
    OSPLAY_ERROR_INVALID_ARGUMENT = 1,
    OSPLAY_ERROR_BUFFER_TOO_SMALL = 2,
    OSPLAY_ERROR_TRUNCATED = 3,
    OSPLAY_ERROR_BAD_MAGIC = 4,
    OSPLAY_ERROR_UNSUPPORTED_VERSION = 5,
    OSPLAY_ERROR_UNKNOWN_TYPE = 6,
    OSPLAY_ERROR_TRAILING_BYTES = 7,
    OSPLAY_ERROR_VALUE_OUT_OF_RANGE = 8,
    OSPLAY_ERROR_INVALID_STRING = 9,
    OSPLAY_ERROR_TOO_LARGE = 10,
    OSPLAY_ERROR_DECRYPTION_FAILED = 11,
    OSPLAY_ERROR_HANDSHAKE_OUT_OF_ORDER = 12,
    OSPLAY_ERROR_REPLAYED = 13,
    OSPLAY_ERROR_NONCE_EXHAUSTED = 14,
    OSPLAY_ERROR_INVALID_KEY = 15,
    OSPLAY_ERROR_RANDOM_UNAVAILABLE = 16,
    OSPLAY_ERROR_TIMED_OUT = 17,
    OSPLAY_ERROR_CONNECTION_CLOSED = 18,
    OSPLAY_ERROR_PEER_ERROR = 19,
    OSPLAY_ERROR_UNEXPECTED_MESSAGE = 20,
    OSPLAY_ERROR_NOT_TRUSTED = 21,
    OSPLAY_ERROR_PAIRING_REJECTED = 22,
    OSPLAY_ERROR_PEER_BUSY = 23,
    OSPLAY_ERROR_INVALID_STATE = 24,
    OSPLAY_ERROR_NETWORK = 25,
    OSPLAY_ERROR_INTERNAL = 26,
    OSPLAY_RESULT_MAX_ENUM = 0x7FFFFFFF
} osplay_result;

typedef enum osplay_wire_kind {
    OSPLAY_WIRE_CONTROL_MESSAGE = 1, /* one control message, without the TCP length prefix */
    OSPLAY_WIRE_MEDIA_PACKET = 2,    /* one plaintext media packet, without the AEAD tag */
    OSPLAY_WIRE_KIND_MAX_ENUM = 0x7FFFFFFF
} osplay_wire_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

#define OSPLAY_KEY_SIZE 32
#define OSPLAY_DEVICE_ID_SIZE 16

/*
 * Cryptographically secure random bytes from the platform (SecRandomCopyBytes on macOS,
 * BCryptGenRandom on Windows). `fill` writes exactly `length` bytes and returns OSPLAY_OK, or
 * returns any other code to report failure. It may be called from any thread.
 */
typedef struct osplay_random_source {
    osplay_result (*fill)(void* context, uint8_t* buffer, size_t length);
    void* context;
} osplay_random_source;

/*
 * Version of the core library that is actually linked. Comparing it with the
 * OSPLAY_CORE_VERSION_* macros seen at compile time detects header/library mismatches.
 * Thread-safe; may be called from any thread.
 */
osplay_version osplay_core_version(void);

/*
 * The core's monotonic clock in microseconds: the time base of osplay_access_unit.capture_time_us
 * and of the session's clock offset. It keeps counting while the computer sleeps, unlike some
 * platform media clocks (e.g. CMClock's host time on macOS), so platforms convert capture times
 * to it: capture_time_us = osplay_clock_microseconds() - (media clock now - capture time).
 * Thread-safe.
 */
uint64_t osplay_clock_microseconds(void);

/*
 * Protocol diagnostics. Both functions are pure and thread-safe; `data` may be NULL only when
 * `length` is 0.
 *
 * osplay_protocol_validate decodes one message or packet with every check the session applies and
 * writes its type (PROTOCOL.md type value) to `out_type`.
 */
osplay_result osplay_protocol_validate(osplay_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out_type);

/*
 * Device identity: a static X25519 key pair. Platforms store the 32-byte secret (Keychain, DPAPI);
 * core never persists it. All buffers are OSPLAY_KEY_SIZE bytes unless stated otherwise.
 *
 * osplay_identity_generate creates a new key pair from `random`.
 */
osplay_result osplay_identity_generate(const osplay_random_source* random, uint8_t* secret_key,
                                       uint8_t* public_key);

/* Validates a stored secret (an all-zero secret is rejected) and derives its public key. */
osplay_result osplay_identity_public_key(const uint8_t* secret_key, uint8_t* public_key);

/* The device ID advertised over Bonjour (OSPLAY_DEVICE_ID_SIZE bytes, see PROTOCOL.md). */
osplay_result osplay_identity_device_id(const uint8_t* public_key, uint8_t* device_id);

/*
 * Decodes and re-encodes. Encoding is canonical, so a valid input comes back byte for byte.
 * Writes the encoded size to `out_length`; fails with OSPLAY_ERROR_BUFFER_TOO_SMALL if `capacity`
 * is not enough.
 */
osplay_result osplay_protocol_reencode(osplay_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out, size_t capacity, size_t* out_length);

/*
 * Sessions (docs/PROTOCOL.md "Session flow"). A session is one host and one viewer over a TCP
 * control connection and a UDP media socket, both driven by one I/O thread owned by the session.
 *
 * Threading: every callback runs on that I/O thread, must return quickly and must not call
 * osplay_session_destroy. The command functions may be called from any thread except a callback;
 * they wait (at most 5 s) until the I/O thread has executed them. Pointers passed to callbacks are
 * valid only for the duration of the call; copy what you keep.
 */
/* NOLINTBEGIN(cppcoreguidelines-use-enum-class,performance-enum-size) */
typedef enum osplay_session_state {
    OSPLAY_SESSION_CONNECTING = 1,
    OSPLAY_SESSION_HANDSHAKING = 2,
    OSPLAY_SESSION_AWAITING_PAIRING = 3,
    OSPLAY_SESSION_NEGOTIATING = 4,
    OSPLAY_SESSION_STREAMING = 5,
    OSPLAY_SESSION_PAUSED = 6,
    OSPLAY_SESSION_CLOSED = 7,
    OSPLAY_SESSION_STATE_MAX_ENUM = 0x7FFFFFFF
} osplay_session_state;

typedef enum osplay_trust {
    OSPLAY_TRUST_TRUSTED = 1, /* known key: no pairing code */
    OSPLAY_TRUST_UNKNOWN = 2, /* first contact: show the pairing code */
    OSPLAY_TRUST_BLOCKED = 3, /* refuse the session */
    OSPLAY_TRUST_MAX_ENUM = 0x7FFFFFFF
} osplay_trust;

typedef enum osplay_log_level {
    OSPLAY_LOG_DEBUG = 1,
    OSPLAY_LOG_INFO = 2,
    OSPLAY_LOG_WARNING = 3,
    OSPLAY_LOG_ERROR = 4,
    OSPLAY_LOG_LEVEL_MAX_ENUM = 0x7FFFFFFF
} osplay_log_level;

typedef enum osplay_platform {
    OSPLAY_PLATFORM_MACOS = 1,
    OSPLAY_PLATFORM_WINDOWS = 2,
    OSPLAY_PLATFORM_MAX_ENUM = 0x7FFFFFFF
} osplay_platform;

typedef enum osplay_h264_profile {
    OSPLAY_H264_BASELINE = 66,
    OSPLAY_H264_MAIN = 77,
    OSPLAY_H264_HIGH = 100,
    OSPLAY_H264_PROFILE_MAX_ENUM = 0x7FFFFFFF
} osplay_h264_profile;

typedef enum osplay_cursor_mode {
    OSPLAY_CURSOR_IN_VIDEO = 0,
    OSPLAY_CURSOR_SEPARATE = 1,
    OSPLAY_CURSOR_MODE_MAX_ENUM = 0x7FFFFFFF
} osplay_cursor_mode;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

#define OSPLAY_CODEC_H264_ANNEX_B 1u /* osplay_capabilities.codecs bit */
#define OSPLAY_MAX_STRING_BYTES 64   /* UTF-8 bytes in a device or monitor name */
#define OSPLAY_MAX_MONITORS 4
#define OSPLAY_MAX_STREAMS 4

/* Strings are NUL-terminated UTF-8 without control characters. */
typedef struct osplay_monitor {
    uint8_t monitor_id;
    uint16_t width_px;
    uint16_t height_px;
    uint32_t refresh_millihertz;
    uint16_t physical_width_mm;
    uint16_t physical_height_mm;
    uint16_t scale_percent;
    const char* name;
} osplay_monitor;

typedef struct osplay_capabilities {
    uint32_t codecs; /* OSPLAY_CODEC_* bits */
    uint8_t max_streams;
    uint8_t flags; /* 0 in protocol v1 */
    uint16_t max_width;
    uint16_t max_height;
    uint16_t max_refresh_hz;
} osplay_capabilities;

typedef struct osplay_session_config {
    osplay_platform platform;
    const char* device_name;
    osplay_capabilities capabilities;
    const osplay_monitor* monitors; /* viewer: the monitors it offers; host: NULL */
    size_t monitor_count;
} osplay_session_config;

typedef struct osplay_stream_config {
    uint8_t stream_id;
    uint8_t monitor_id;
    uint16_t width;
    uint16_t height;
    uint32_t refresh_millihertz;
    osplay_h264_profile profile; /* the codec is H.264 Annex B in protocol v1 */
    osplay_cursor_mode cursor_mode;
    uint32_t bitrate_kbps;
} osplay_stream_config;

/* One encoded frame. `frame_id` is set by the viewer callback and ignored when pushing. */
typedef struct osplay_access_unit {
    uint8_t stream_id;
    uint8_t keyframe;     /* 0 or 1 */
    uint8_t codec_config; /* 1 if the unit carries SPS/PPS; requires keyframe */
    uint32_t frame_id;
    uint64_t capture_time_us; /* host's osplay_clock_microseconds() time base */
    const uint8_t* data;
    size_t length;
} osplay_access_unit;

/* A cursor image: premultiplied BGRA, rows top to bottom, no padding (width × height × 4 bytes). */
typedef struct osplay_cursor_shape {
    uint8_t stream_id;
    uint32_t shape_id; /* changes whenever the image changes */
    uint16_t width;    /* 1–256 */
    uint16_t height;   /* 1–256 */
    uint16_t hotspot_x;
    uint16_t hotspot_y;
    const uint8_t* bgra;
    size_t length;
} osplay_cursor_shape;

typedef struct osplay_cursor_position {
    uint8_t stream_id;
    uint8_t visible; /* 0 or 1 */
    uint32_t shape_id;
    int32_t x; /* hotspot in stream pixels; may lie outside the stream */
    int32_t y;
    uint64_t capture_time_us; /* osplay_clock_microseconds() time base */
} osplay_cursor_position;

typedef struct osplay_stream_stats {
    uint8_t stream_id;
    uint16_t loss_permille;
    uint32_t frames_per_second;
    uint32_t bitrate_kbps;
    uint32_t latency_us; /* viewer: capture to delivery; 0 if unknown */
    uint64_t frames_dropped;
    uint64_t retransmitted_fragments;
    uint64_t keyframe_requests;
} osplay_stream_stats;

typedef struct osplay_session_stats {
    uint32_t rtt_us;
    int64_t clock_offset_us; /* peer clock minus local clock */
    uint64_t datagrams_rejected;
    const osplay_stream_stats* streams;
    size_t stream_count;
} osplay_session_stats;

typedef struct osplay_peer_info {
    osplay_platform platform;
    uint8_t protocol_minor;
    const char* device_name;
} osplay_peer_info;

/*
 * `on_state` and `on_trust_query` are required; any other member may be NULL. Keys are
 * OSPLAY_KEY_SIZE bytes.
 */
typedef struct osplay_session_callbacks {
    void* context;
    /* `reason` is OSPLAY_OK unless the session closed because of an error. */
    void (*on_state)(void* context, osplay_session_state state, osplay_result reason);
    void (*on_peer)(void* context, const osplay_peer_info* peer);
    /* Answer from memory: the I/O thread waits for it. */
    osplay_trust (*on_trust_query)(void* context, const uint8_t* peer_public_key);
    /* First pairing: show `code` (6 digits), then call osplay_session_confirm_pairing. */
    void (*on_pairing_code)(void* context, uint32_t code, const uint8_t* peer_public_key);
    /* Both users accepted: store the key as trusted. */
    void (*on_paired)(void* context, const uint8_t* peer_public_key);
    /* Host: the viewer's monitors; answer with osplay_session_configure_streams. */
    void (*on_viewer_monitors)(void* context, const osplay_monitor* monitors, size_t count);
    /* Viewer: a stream the host is about to send. */
    void (*on_stream_config)(void* context, const osplay_stream_config* config);
    /* Host: encode a keyframe for this stream next. */
    void (*on_keyframe_request)(void* context, uint8_t stream_id);
    /* Viewer: a complete frame, in order. */
    void (*on_access_unit)(void* context, const osplay_access_unit* unit);
    void (*on_stats)(void* context, const osplay_session_stats* stats);
    void (*on_log)(void* context, osplay_log_level level, const char* message);
    /* Viewer: a complete cursor image, for streams with separate cursor mode. */
    void (*on_cursor_shape)(void* context, const osplay_cursor_shape* shape);
    /* Viewer: the newest cursor position; older ones are dropped. */
    void (*on_cursor_position)(void* context, const osplay_cursor_position* position);
} osplay_session_callbacks;

typedef struct osplay_session osplay_session;

/*
 * Creates a host session listening for one viewer on an ephemeral TCP port (IPv4 and IPv6).
 * `secret_key` is the device identity. `random` and `callbacks` are copied; their contexts must
 * outlive the session.
 */
osplay_result osplay_host_session_create(const osplay_session_config* config,
                                         const uint8_t* secret_key,
                                         const osplay_random_source* random,
                                         const osplay_session_callbacks* callbacks,
                                         osplay_session** out_session);

/* Creates a viewer session connecting to `host` (name or address) on `port`. */
osplay_result osplay_viewer_session_create(const osplay_session_config* config,
                                           const uint8_t* secret_key,
                                           const osplay_random_source* random,
                                           const osplay_session_callbacks* callbacks,
                                           const char* host, uint16_t port,
                                           osplay_session** out_session);

/* Sends Bye when possible, closes the sockets and joins the I/O thread. NULL is ignored. */
void osplay_session_destroy(osplay_session* session);

/* Host: the TCP port to advertise over Bonjour. */
osplay_result osplay_session_control_port(const osplay_session* session, uint16_t* out_port);

/* Answers on_pairing_code with the local user's decision (`accepted` is 0 or 1). */
osplay_result osplay_session_confirm_pairing(osplay_session* session, uint8_t accepted);

/* Host: starts one stream per config, once, after on_viewer_monitors. */
osplay_result osplay_session_configure_streams(osplay_session* session,
                                               const osplay_stream_config* streams, size_t count);

/*
 * Host: queues one encoded frame and returns without waiting for the network. The data is
 * copied. Frames for a paused stream, or pushed before streaming starts, are dropped.
 */
osplay_result osplay_session_push_access_unit(osplay_session* session,
                                              const osplay_access_unit* unit);

/*
 * Host: a new cursor image (copied); it is sent now and again after every keyframe, so viewers
 * that join late or lose packets recover it.
 */
osplay_result osplay_session_push_cursor_shape(osplay_session* session,
                                               const osplay_cursor_shape* shape);

/* Host: where the cursor is; send at most at the stream's refresh rate. Not retransmitted. */
osplay_result osplay_session_push_cursor_position(osplay_session* session,
                                                  const osplay_cursor_position* position);

osplay_result osplay_session_pause_stream(osplay_session* session, uint8_t stream_id);
osplay_result osplay_session_resume_stream(osplay_session* session, uint8_t stream_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_OSPLAY_H */
