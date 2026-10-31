#include "winusb_pipe.hpp"

#include <algorithm>
#include <array>

namespace osplay::engine::usb {
namespace {

/// Android's accessory driver moves at most 16 KiB at a time (transport/aoa.hpp).
constexpr ULONG kMaxTransfer = 16U * 1024U;

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

osplay_result result_for(DWORD error) {
    // An aborted request is a cancel; anything else on a bulk pipe means the cable is gone.
    return error == ERROR_OPERATION_ABORTED ? OSPLAY_ERROR_CONNECTION_CLOSED : OSPLAY_ERROR_NETWORK;
}

} // namespace

std::unique_ptr<WinUsbPipe> WinUsbPipe::open(const std::wstring& path) {
    HANDLE opened =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (opened == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    UniqueHandle file(opened);
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    if (!WinUsb_Initialize(file.get(), &usb)) {
        return nullptr;
    }
    USB_INTERFACE_DESCRIPTOR descriptor{};
    UCHAR in = 0;
    UCHAR out = 0;
    if (WinUsb_QueryInterfaceSettings(usb, 0, &descriptor)) {
        for (UCHAR index = 0; index < descriptor.bNumEndpoints; ++index) {
            WINUSB_PIPE_INFORMATION pipe{};
            if (WinUsb_QueryPipe(usb, 0, index, &pipe) && pipe.PipeType == UsbdPipeTypeBulk) {
                (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId) ? in : out) = pipe.PipeId;
            }
        }
    }
    HANDLE cancelled = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (in == 0 || out == 0 || cancelled == nullptr) {
        if (cancelled != nullptr) {
            CloseHandle(cancelled);
        }
        WinUsb_Free(usb);
        return nullptr;
    }
    return std::unique_ptr<WinUsbPipe>(new WinUsbPipe(file.release(), usb, in, out, cancelled));
}

WinUsbPipe::WinUsbPipe(HANDLE file, WINUSB_INTERFACE_HANDLE usb, UCHAR in, UCHAR out,
                       HANDLE cancelled)
    : file_(file), usb_(usb), in_(in), out_(out), cancelled_(cancelled) {}

WinUsbPipe::~WinUsbPipe() {
    WinUsb_Free(usb_);
    CloseHandle(file_);
    CloseHandle(cancelled_);
}

osplay_result WinUsbPipe::read(std::uint8_t* buffer, std::size_t capacity, std::size_t* out_size) {
    return transfer(in_, buffer, static_cast<ULONG>(std::min<std::size_t>(capacity, kMaxTransfer)),
                    out_size);
}

osplay_result WinUsbPipe::write(const std::uint8_t* data, std::size_t size,
                                std::size_t* out_written) {
    // WinUsb_WritePipe takes a non-const buffer but only reads it.
    return transfer(out_, const_cast<std::uint8_t*>(data), // NOLINT(*-const-cast)
                    static_cast<ULONG>(std::min<std::size_t>(size, kMaxTransfer)), out_written);
}

osplay_result WinUsbPipe::transfer(UCHAR endpoint, std::uint8_t* buffer, ULONG size,
                                   std::size_t* out_moved) {
    if (WaitForSingleObject(cancelled_, 0) == WAIT_OBJECT_0) {
        return OSPLAY_ERROR_CONNECTION_CLOSED;
    }
    const UniqueHandle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!done) {
        return OSPLAY_ERROR_INTERNAL;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = done.get();
    const bool in = USB_ENDPOINT_DIRECTION_IN(endpoint);
    const BOOL started = in ? WinUsb_ReadPipe(usb_, endpoint, buffer, size, nullptr, &overlapped)
                            : WinUsb_WritePipe(usb_, endpoint, buffer, size, nullptr, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        return result_for(GetLastError());
    }
    // Waits for the transfer or a cancel. On a cancel the request is aborted and waited for, since
    // the buffer must outlive it.
    const std::array<HANDLE, 2> events = {done.get(), cancelled_};
    if (WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE) !=
        WAIT_OBJECT_0) {
        WinUsb_AbortPipe(usb_, endpoint);
    }
    ULONG moved = 0;
    if (!WinUsb_GetOverlappedResult(usb_, &overlapped, &moved, TRUE)) {
        return result_for(GetLastError());
    }
    if (WaitForSingleObject(cancelled_, 0) == WAIT_OBJECT_0) {
        return OSPLAY_ERROR_CONNECTION_CLOSED;
    }
    *out_moved = moved;
    return OSPLAY_OK;
}

void WinUsbPipe::cancel() {
    SetEvent(cancelled_);
}

osplay_byte_pipe WinUsbPipe::to_byte_pipe(std::unique_ptr<WinUsbPipe> pipe) {
    osplay_byte_pipe result{};
    result.context = pipe.release();
    result.read = [](void* context, uint8_t* buffer, size_t capacity, size_t* out_size) {
        return static_cast<WinUsbPipe*>(context)->read(buffer, capacity, out_size);
    };
    result.write = [](void* context, const uint8_t* data, size_t size, size_t* out_written) {
        return static_cast<WinUsbPipe*>(context)->write(data, size, out_written);
    };
    result.cancel = [](void* context) { static_cast<WinUsbPipe*>(context)->cancel(); };
    result.release = [](void* context) {
        delete static_cast<WinUsbPipe*>(context); // NOLINT(*-owning-memory): the core's last call
    };
    return result;
}

} // namespace osplay::engine::usb
