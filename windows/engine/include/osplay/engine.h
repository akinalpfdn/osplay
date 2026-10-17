/*
 * Osplay Windows engine — public C API consumed by the WPF shell through P/Invoke.
 */
#ifndef OSPLAY_ENGINE_H
#define OSPLAY_ENGINE_H

#include <osplay/osplay.h>

#ifdef OSPLAY_ENGINE_BUILDING
#define OSPLAY_ENGINE_API __declspec(dllexport)
#else
#define OSPLAY_ENGINE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the core library linked into the engine. Thread-safe. */
OSPLAY_ENGINE_API osplay_version osplay_engine_core_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSPLAY_ENGINE_H */
