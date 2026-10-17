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
/* Plain C enums: this header is consumed from C, Swift and C#. */
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
    OSPLAY_ERROR_TOO_LARGE = 10
} osplay_result;

typedef enum osplay_wire_kind {
    OSPLAY_WIRE_CONTROL_MESSAGE = 1, /* one control message, without the TCP length prefix */
    OSPLAY_WIRE_MEDIA_PACKET = 2     /* one plaintext media packet, without the AEAD tag */
} osplay_wire_kind;
/* NOLINTEND(cppcoreguidelines-use-enum-class,performance-enum-size) */

/*
 * Version of the core library that is actually linked. Comparing it with the
 * OSPLAY_CORE_VERSION_* macros seen at compile time detects header/library mismatches.
 * Thread-safe; may be called from any thread.
 */
osplay_version osplay_core_version(void);

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
 * Decodes and re-encodes. Encoding is canonical, so a valid input comes back byte for byte.
 * Writes the encoded size to `out_length`; fails with OSPLAY_ERROR_BUFFER_TOO_SMALL if `capacity`
 * is not enough.
 */
osplay_result osplay_protocol_reencode(osplay_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out, size_t capacity, size_t* out_length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_OSPLAY_H */
