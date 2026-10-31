#pragma once

// The bulk endpoints of an Android accessory (18D1:2D00, interface 0) through WinUSB, which
// Osplay's installer binds to that ID (TAB-05): the byte pipe a host session runs on.

#include <osplay/osplay.h>

#include <windows.h>

#include <winusb.h>

#include <memory>
#include <string>

namespace osplay::engine::usb {

class WinUsbPipe {
public:
    /// Opens the WinUSB device interface at `path`; null when it is gone or not WinUSB.
    [[nodiscard]] static std::unique_ptr<WinUsbPipe> open(const std::wstring& path);

    ~WinUsbPipe();
    WinUsbPipe(const WinUsbPipe&) = delete;
    WinUsbPipe& operator=(const WinUsbPipe&) = delete;
    WinUsbPipe(WinUsbPipe&&) = delete;
    WinUsbPipe& operator=(WinUsbPipe&&) = delete;

    osplay_result read(std::uint8_t* buffer, std::size_t capacity, std::size_t* out_size);
    osplay_result write(const std::uint8_t* data, std::size_t size, std::size_t* out_written);
    /// Unblocks read and write for good; from any thread.
    void cancel();

    /// Hands the pipe to the core: its release callback deletes it.
    [[nodiscard]] static osplay_byte_pipe to_byte_pipe(std::unique_ptr<WinUsbPipe> pipe);

private:
    WinUsbPipe(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR in, UCHAR out, HANDLE cancelled);

    osplay_result transfer(UCHAR endpoint, std::uint8_t* buffer, ULONG size,
                           std::size_t* out_moved);

    HANDLE file_;
    WINUSB_INTERFACE_HANDLE usb_;
    UCHAR in_;
    UCHAR out_;
    HANDLE cancelled_; // manual-reset event, set by cancel()
};

} // namespace osplay::engine::usb
