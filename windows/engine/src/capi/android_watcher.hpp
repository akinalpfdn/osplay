#pragma once

#include "../usb/android_usb_watcher.hpp"

#include <memory>

/// Shared with the host sessions started on its accessories, which tell it when one is opened.
struct osplay_engine_android_watcher {
    std::shared_ptr<osplay::engine::usb::AndroidUsbWatcher> watcher;
};
