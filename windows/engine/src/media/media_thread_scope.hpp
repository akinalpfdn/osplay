#pragma once

#include <windows.h>

#include <mfapi.h>
#include <objbase.h>

namespace osplay::engine {

/// COM and Media Foundation for the calling thread, for the lifetime of the object.
class MediaThreadScope {
public:
    MediaThreadScope()
        : com_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))),
          media_foundation_(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {}
    ~MediaThreadScope() {
        if (media_foundation_) {
            static_cast<void>(MFShutdown());
        }
        if (com_) {
            CoUninitialize();
        }
    }
    MediaThreadScope(const MediaThreadScope&) = delete;
    MediaThreadScope& operator=(const MediaThreadScope&) = delete;
    MediaThreadScope(MediaThreadScope&&) = delete;
    MediaThreadScope& operator=(MediaThreadScope&&) = delete;

    [[nodiscard]] bool ready() const { return com_ && media_foundation_; }

private:
    bool com_;
    bool media_foundation_;
};

} // namespace osplay::engine
