#include "usbdk.hpp"

#include "engine_tunables.hpp"

#include <usbspec.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <span>
#include <vector>

namespace osplay::engine::usb {
namespace {

// UsbDkHelper's types, as UsbDk 1.0.22 declares them (UsbDk/UsbDkData.h and
// UsbDkHelper/UsbDkHelper.h, Apache-2.0); the layout must match the driver's.
static_assert(sizeof(void*) == 8, "UsbDk's PVOID64 fields are plain pointers on x64 only");
constexpr std::size_t kUsbDkIdLength = 200; // MAX_DEVICE_ID_LEN in UsbDk's headers

struct UsbDkDeviceId {
    WCHAR device_id[kUsbDkIdLength];   // NOLINT(*-avoid-c-arrays): UsbDk's layout
    WCHAR instance_id[kUsbDkIdLength]; // NOLINT(*-avoid-c-arrays): UsbDk's layout
};

struct UsbDkDeviceInfo {
    UsbDkDeviceId id;
    ULONG64 filter_id;
    ULONG64 port;
    ULONG64 speed;
    USB_DEVICE_DESCRIPTOR device_descriptor;
};

struct UsbDkGenTransferResult {
    ULONG64 bytes_transferred;
    ULONG64 usbd_status;
};

struct UsbDkTransferResult {
    UsbDkGenTransferResult gen_result;
    void* isochronous_results;
};

struct UsbDkTransferRequest {
    ULONG64 endpoint_address;
    void* buffer;
    ULONG64 buffer_length;
    ULONG64 transfer_type;
    ULONG64 isochronous_packets_count;
    void* isochronous_packets;
    UsbDkTransferResult result;
};

// TransferResult, a C enum: FAILURE, SUCCESS, SUCCESS_ASYNC.
constexpr int kTransferSuccess = 1;
constexpr int kTransferSuccessAsync = 2;
constexpr ULONG64 kControlTransferType = 0;
constexpr std::size_t kSetupPacketSize = 8;
constexpr std::uint8_t kDirectionIn = 0x80;

using GetDevicesListFn = BOOL(__cdecl*)(UsbDkDeviceInfo** devices, PULONG count);
using ReleaseDevicesListFn = void(__cdecl*)(UsbDkDeviceInfo* devices);
using StartRedirectFn = HANDLE(__cdecl*)(UsbDkDeviceId* id);
using StopRedirectFn = BOOL(__cdecl*)(HANDLE redirector);
using PipeFn = int(__cdecl*)(HANDLE redirector, UsbDkTransferRequest* request,
                             LPOVERLAPPED overlapped);
using SystemHandleFn = HANDLE(__cdecl*)(HANDLE redirector);

bool same_id(std::span<const WCHAR, kUsbDkIdLength> reported, const std::wstring& wanted) {
    const std::wstring_view left(reported.data(), wcsnlen(reported.data(), reported.size()));
    return std::ranges::equal(
        left, wanted, [](wchar_t a, wchar_t b) { return std::towupper(a) == std::towupper(b); });
}

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

} // namespace

struct UsbDk::Functions {
    GetDevicesListFn get_devices_list = nullptr;
    ReleaseDevicesListFn release_devices_list = nullptr;
    StartRedirectFn start_redirect = nullptr;
    StopRedirectFn stop_redirect = nullptr;
    PipeFn read_pipe = nullptr;
    PipeFn write_pipe = nullptr;
    SystemHandleFn system_handle = nullptr;
};

namespace {

/// One redirected device: the context of the control transfers the core asks for.
struct Redirect {
    const UsbDk::Functions* functions = nullptr;
    HANDLE redirector = INVALID_HANDLE_VALUE;
    HANDLE system = INVALID_HANDLE_VALUE;
};

osplay_result control_transfer(void* context, uint8_t request_type, uint8_t request, uint16_t value,
                               uint16_t index, uint8_t* data, uint16_t length,
                               uint16_t* out_transferred) {
    const auto& redirect = *static_cast<const Redirect*>(context);
    // UsbDk takes the setup packet and the data stage in one buffer, as libusb's backend does.
    std::vector<std::uint8_t> buffer(kSetupPacketSize + length);
    buffer[0] = request_type;
    buffer[1] = request;
    buffer[2] = static_cast<std::uint8_t>(value & 0xFF);
    buffer[3] = static_cast<std::uint8_t>(value >> 8);
    buffer[4] = static_cast<std::uint8_t>(index & 0xFF);
    buffer[5] = static_cast<std::uint8_t>(index >> 8);
    buffer[6] = static_cast<std::uint8_t>(length & 0xFF);
    buffer[7] = static_cast<std::uint8_t>(length >> 8);
    const bool in = (request_type & kDirectionIn) != 0;
    if (!in && length > 0) {
        std::memcpy(buffer.data() + kSetupPacketSize, data, length);
    }

    UsbDkTransferRequest transfer{};
    transfer.buffer = buffer.data();
    transfer.buffer_length = buffer.size();
    transfer.transfer_type = kControlTransferType;
    const UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) {
        return OSPLAY_ERROR_INTERNAL;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.get();

    auto* const pipe = in ? redirect.functions->read_pipe : redirect.functions->write_pipe;
    switch (pipe(redirect.redirector, &transfer, &overlapped)) {
    case kTransferSuccess:
        break;
    case kTransferSuccessAsync:
        if (WaitForSingleObject(event.get(), kAccessoryControlTimeoutMilliseconds) !=
            WAIT_OBJECT_0) {
            CancelIoEx(redirect.system, &overlapped);
            WaitForSingleObject(event.get(), INFINITE); // the buffer must outlive the request
            return OSPLAY_ERROR_TIMED_OUT;
        }
        break;
    default:
        return OSPLAY_ERROR_NETWORK;
    }

    const auto moved = static_cast<std::uint16_t>(
        std::min<ULONG64>(transfer.result.gen_result.bytes_transferred, length));
    if (in && moved > 0) {
        std::memcpy(data, buffer.data() + kSetupPacketSize, moved);
    }
    *out_transferred = in ? moved : length;
    return OSPLAY_OK;
}

} // namespace

std::unique_ptr<UsbDk> UsbDk::load() {
    HMODULE module = LoadLibraryExW(L"UsbDkHelper.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return nullptr;
    }
    auto functions = std::make_unique<Functions>();
    const auto find = [module]<typename Fn>(Fn& target, const char* name) {
        // The cast is the documented way to use GetProcAddress's result.
        target = reinterpret_cast<Fn>(GetProcAddress(module, name)); // NOLINT(*-reinterpret-cast)
        return target != nullptr;
    };
    if (!find(functions->get_devices_list, "UsbDk_GetDevicesList") ||
        !find(functions->release_devices_list, "UsbDk_ReleaseDevicesList") ||
        !find(functions->start_redirect, "UsbDk_StartRedirect") ||
        !find(functions->stop_redirect, "UsbDk_StopRedirect") ||
        !find(functions->read_pipe, "UsbDk_ReadPipe") ||
        !find(functions->write_pipe, "UsbDk_WritePipe") ||
        !find(functions->system_handle, "UsbDk_GetRedirectorSystemHandle")) {
        FreeLibrary(module);
        return nullptr;
    }
    return std::unique_ptr<UsbDk>(new UsbDk(module, std::move(functions)));
}

UsbDk::UsbDk(HMODULE module, std::unique_ptr<Functions> functions)
    : module_(module), functions_(std::move(functions)) {}

UsbDk::~UsbDk() {
    FreeLibrary(module_);
}

osplay_result UsbDk::switch_to_accessory(const std::wstring& device_id,
                                         const std::wstring& instance_id) const {
    UsbDkDeviceInfo* devices = nullptr;
    ULONG count = 0;
    if (!functions_->get_devices_list(&devices, &count)) {
        return OSPLAY_ERROR_NETWORK;
    }
    UsbDkDeviceId wanted{};
    bool found = false;
    for (ULONG i = 0; i < count && !found; ++i) {
        const UsbDkDeviceInfo& device = devices[i]; // NOLINT(*-pointer-arithmetic): UsbDk's array
        if (same_id(device.id.device_id, device_id) &&
            same_id(device.id.instance_id, instance_id)) {
            wanted = device.id;
            found = true;
        }
    }
    functions_->release_devices_list(devices);
    if (!found) {
        return OSPLAY_ERROR_NETWORK;
    }

    Redirect redirect{.functions = functions_.get(),
                      .redirector = functions_->start_redirect(&wanted)};
    if (redirect.redirector == INVALID_HANDLE_VALUE || redirect.redirector == nullptr) {
        return OSPLAY_ERROR_NETWORK;
    }
    redirect.system = functions_->system_handle(redirect.redirector);
    const osplay_result result = osplay_aoa_switch(&redirect, control_transfer);
    // The device leaves the bus after START; stopping returns it to Windows either way.
    functions_->stop_redirect(redirect.redirector);
    return result;
}

} // namespace osplay::engine::usb
