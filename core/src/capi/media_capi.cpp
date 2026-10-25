#include "media/stream_size.hpp"

#include <osplay/osplay.h>

extern "C" osplay_result osplay_fit_stream_size(uint32_t monitor_width, uint32_t monitor_height,
                                                uint32_t max_width, uint32_t max_height,
                                                uint32_t* out_width, uint32_t* out_height) {
    if (out_width == nullptr || out_height == nullptr || monitor_width == 0 ||
        monitor_height == 0) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    const osplay::media::StreamSize size =
        osplay::media::fit_stream_size(monitor_width, monitor_height, max_width, max_height);
    *out_width = size.width;
    *out_height = size.height;
    return OSPLAY_OK;
}
