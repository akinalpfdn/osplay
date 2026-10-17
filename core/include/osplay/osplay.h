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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_OSPLAY_H */
