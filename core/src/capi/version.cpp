#include <osplay/osplay.h>

extern "C" osplay_version osplay_core_version() {
    return osplay_version{
        .major = OSPLAY_CORE_VERSION_MAJOR,
        .minor = OSPLAY_CORE_VERSION_MINOR,
        .patch = OSPLAY_CORE_VERSION_PATCH,
    };
}
