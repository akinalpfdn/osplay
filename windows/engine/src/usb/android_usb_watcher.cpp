#include "android_usb_watcher.hpp"

#include "engine_tunables.hpp"
#include "usb/android_usb_ids.hpp"

// <initguid.h> first, so the property keys and the interface class below are defined here.
// clang-format off
#include <initguid.h>
#include <combaseapi.h>
#include <devpkey.h>
#include <usbiodef.h>
// clang-format on

#include <algorithm>
#include <cwctype>
#include <vector>

namespace osplay::engine::usb {
namespace {

using namespace std::chrono_literals;

/// A switched device's WinUSB interface appears once Windows has bound WinUSB to it.
constexpr int kInterfaceLookups = 10;
constexpr auto kInterfaceRetryDelay = 500ms;

std::vector<std::wstring> split_multi_sz(const std::vector<wchar_t>& buffer) {
    std::vector<std::wstring> strings;
    const wchar_t* at = buffer.data();
    const wchar_t* end = buffer.data() + buffer.size();
    while (at < end && *at != L'\0') {
        const std::wstring item(at);
        strings.push_back(item);
        at += item.size() + 1; // NOLINT(*-pointer-arithmetic): walking a REG_MULTI_SZ
    }
    return strings;
}

std::wstring upper(std::wstring text) {
    std::ranges::transform(text, text.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}

std::optional<std::wstring> instance_of(const std::wstring& link) {
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    if (CM_Get_Device_Interface_PropertyW(link.c_str(), &DEVPKEY_Device_InstanceId, &type, nullptr,
                                          &size, 0) != CR_BUFFER_SMALL) {
        return std::nullopt;
    }
    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1);
    if (CM_Get_Device_Interface_PropertyW(link.c_str(), &DEVPKEY_Device_InstanceId, &type,
                                          reinterpret_cast<PBYTE>(buffer.data()), &size,
                                          0) != CR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

std::vector<std::wstring> compatible_ids(DEVINST node) {
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_CompatibleIds, &type, nullptr, &size, 0) !=
        CR_BUFFER_SMALL) {
        return {};
    }
    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1);
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_CompatibleIds, &type,
                                 reinterpret_cast<PBYTE>(buffer.data()), &size, 0) != CR_SUCCESS) {
        return {};
    }
    return split_multi_sz(buffer);
}

/// The device's compatible IDs and those of its interfaces (a composite device's children).
std::vector<std::wstring> device_and_interface_ids(DEVINST device) {
    std::vector<std::wstring> ids = compatible_ids(device);
    DEVINST child = 0;
    for (CONFIGRET found = CM_Get_Child(&child, device, 0); found == CR_SUCCESS;
         found = CM_Get_Sibling(&child, child, 0)) {
        const auto more = compatible_ids(child);
        ids.insert(ids.end(), more.begin(), more.end());
    }
    return ids;
}

/// The device interface WinUSB registered for `node`, from the GUID its driver package names.
std::optional<std::wstring> winusb_interface(DEVINST node, const std::wstring& instance) {
    HKEY raw = nullptr;
    if (CM_Open_DevNode_Key(node, KEY_READ, 0, RegDisposition_OpenExisting, &raw,
                            CM_REGISTRY_HARDWARE) != CR_SUCCESS) {
        return std::nullopt;
    }
    std::vector<wchar_t> value(512);
    auto bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    LSTATUS status = RegGetValueW(raw, nullptr, L"DeviceInterfaceGUIDs", RRF_RT_REG_MULTI_SZ,
                                  nullptr, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        status = RegGetValueW(raw, nullptr, L"DeviceInterfaceGUID", RRF_RT_REG_SZ, nullptr,
                              value.data(), &bytes);
    }
    RegCloseKey(raw);
    GUID guid{};
    if (status != ERROR_SUCCESS || FAILED(CLSIDFromString(value.data(), &guid))) {
        return std::nullopt;
    }
    ULONG length = 0;
    if (CM_Get_Device_Interface_List_SizeW(&length, &guid,
                                           const_cast<wchar_t*>(instance.c_str()), // NOLINT
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        length <= 1) {
        return std::nullopt;
    }
    std::vector<wchar_t> list(length);
    if (CM_Get_Device_Interface_ListW(&guid, const_cast<wchar_t*>(instance.c_str()), // NOLINT
                                      list.data(), length,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        list[0] == L'\0') {
        return std::nullopt;
    }
    return std::wstring(list.data());
}

std::vector<std::wstring> present_usb_devices() {
    GUID usb_device = GUID_DEVINTERFACE_USB_DEVICE;
    ULONG length = 0;
    if (CM_Get_Device_Interface_List_SizeW(&length, &usb_device, nullptr,
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
        return {};
    }
    std::vector<wchar_t> list(length);
    if (CM_Get_Device_Interface_ListW(&usb_device, nullptr, list.data(), length,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
        return {};
    }
    return split_multi_sz(list);
}

/// The part of an instance ID after the last backslash: the serial number for a device that has
/// one, the same in its normal and its accessory mode ("RFCW714678P"); otherwise an ID Windows
/// made up from the port, which contains '&'.
std::wstring serial_of(const std::wstring& instance) {
    const auto parts = split_instance_id(instance);
    return parts && parts->instance_id.find(L'&') == std::wstring::npos ? parts->instance_id
                                                                        : std::wstring();
}

std::string narrow(const std::wstring& text) {
    std::string result(text.size(), '\0');
    // Instance IDs are ASCII.
    std::ranges::transform(text, result.begin(), [](wchar_t c) { return static_cast<char>(c); });
    return result;
}

} // namespace

std::unique_ptr<AndroidUsbWatcher> AndroidUsbWatcher::start(Callbacks callbacks) {
    std::unique_ptr<AndroidUsbWatcher> watcher(new AndroidUsbWatcher(std::move(callbacks)));
    watcher->usbdk_ = UsbDk::load();
    if (!watcher->usbdk_) {
        watcher->log(OSPLAY_LOG_WARNING,
                     "UsbDk is not installed; Android devices are not switched");
    }
    watcher->worker_ = std::thread([raw = watcher.get()] { raw->run(); });

    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): Win32 API
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_USB_DEVICE;
    if (CM_Register_Notification(&filter, watcher.get(), &AndroidUsbWatcher::on_notification,
                                 &watcher->notification_) != CR_SUCCESS) {
        watcher->notification_ = nullptr;
        return nullptr; // the destructor stops the worker
    }
    for (std::wstring& link : present_usb_devices()) {
        watcher->post(true, std::move(link), 0ms);
    }
    return watcher;
}

AndroidUsbWatcher::AndroidUsbWatcher(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

AndroidUsbWatcher::~AndroidUsbWatcher() {
    stop();
}

void AndroidUsbWatcher::stop() {
    if (notification_ != nullptr) {
        CM_Unregister_Notification(notification_); // waits for a notification in progress
        notification_ = nullptr;
    }
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::optional<std::wstring> AndroidUsbWatcher::interface_path(std::uint32_t accessory_id) const {
    const std::scoped_lock lock(mutex_);
    for (const auto& [link, accessory] : accessories_) {
        if (accessory.id == accessory_id) {
            return accessory.interface_path;
        }
    }
    return std::nullopt;
}

void AndroidUsbWatcher::accessory_opened(std::uint32_t accessory_id) {
    const std::scoped_lock lock(mutex_);
    for (const auto& [link, accessory] : accessories_) {
        if (accessory.id == accessory_id && !accessory.serial.empty()) {
            switches_.erase(accessory.serial);
        }
    }
}

DWORD CALLBACK AndroidUsbWatcher::on_notification(HCMNOTIFICATION /*notification*/, PVOID context,
                                                  CM_NOTIFY_ACTION action,
                                                  PCM_NOTIFY_EVENT_DATA data, DWORD /*size*/) {
    const bool arrival = action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL;
    if (!arrival && action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
        return ERROR_SUCCESS;
    }
    // Both actions carry an interface; its link is a NUL-terminated array at the end of `data`.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    const wchar_t* link = data->u.DeviceInterface.SymbolicLink;
    static_cast<AndroidUsbWatcher*>(context)->post(
        arrival, link, arrival ? std::chrono::milliseconds(kAccessoryProbeDelayMilliseconds) : 0ms);
    return ERROR_SUCCESS;
}

void AndroidUsbWatcher::post(bool arrived, std::wstring link, std::chrono::milliseconds delay) {
    {
        const std::scoped_lock lock(mutex_);
        events_.push_back(Event{.arrived = arrived,
                                .link = upper(std::move(link)),
                                .due = std::chrono::steady_clock::now() + delay});
    }
    wake_.notify_all();
}

void AndroidUsbWatcher::run() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        if (events_.empty()) {
            wake_.wait(lock);
            continue;
        }
        const auto next = std::ranges::min_element(events_, {}, &Event::due);
        if (next->due > std::chrono::steady_clock::now()) {
            wake_.wait_until(lock, next->due);
            continue;
        }
        const Event event = std::move(*next);
        events_.erase(next);
        lock.unlock();
        if (event.arrived) {
            arrived(event.link);
        } else {
            removed(event.link);
        }
        lock.lock();
    }
}

void AndroidUsbWatcher::arrived(const std::wstring& link) {
    const auto instance = instance_of(link);
    const auto ids = instance ? parse_usb_ids(*instance) : std::nullopt;
    DEVINST node = 0;
    if (!ids ||
        CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(instance->c_str()), // NOLINT(*-const-cast)
                           CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return; // gone already
    }

    switch (accessory_kind(*ids)) {
    case AccessoryKind::AccessoryWithAdb:
        debugging_is_on(); // switched elsewhere, or ADB showed only after the switch
        return;
    case AccessoryKind::Accessory: {
        auto path = winusb_interface(node, *instance);
        if (!path) {
            // WinUSB may not be bound yet; after a while it means the driver package is missing.
            const std::scoped_lock lock(mutex_);
            const int lookups = ++lookups_[link];
            if (lookups < kInterfaceLookups) {
                events_.push_back(
                    Event{.arrived = true,
                          .link = link,
                          .due = std::chrono::steady_clock::now() + kInterfaceRetryDelay});
            }
            if (lookups == kInterfaceLookups) {
                log(OSPLAY_LOG_WARNING,
                    "An Android accessory has no WinUSB driver: reinstall Osplay");
            }
            return;
        }
        std::uint32_t id = 0;
        {
            const std::scoped_lock lock(mutex_);
            lookups_.erase(link);
            id = next_id_++;
            accessories_[link] = Accessory{
                .id = id, .interface_path = std::move(*path), .serial = serial_of(*instance)};
        }
        log(OSPLAY_LOG_INFO, "Android accessory " + std::to_string(id) + " attached");
        if (callbacks_.attached) {
            callbacks_.attached(id);
        }
        return;
    }
    case AccessoryKind::None:
        break;
    }

    switch (cable_action(*ids, device_and_interface_ids(node))) {
    case CableAction::Leave:
        return;
    case CableAction::DebuggingIsOn:
        debugging_is_on();
        return;
    case CableAction::Switch:
        break;
    }
    if (!usbdk_) {
        return;
    }
    const auto parts = split_instance_id(*instance);
    if (!parts) {
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        const std::wstring serial = serial_of(*instance);
        if (switches_[serial.empty() ? *instance : serial]++ >= kAccessorySwitchAttempts) {
            return;
        }
    }
    const osplay_result result = usbdk_->switch_to_accessory(parts->device_id, parts->instance_id);
    if (result == OSPLAY_OK) {
        log(OSPLAY_LOG_INFO, "Switched " + narrow(*instance) + " to Osplay's accessory");
    } else if (result != OSPLAY_ERROR_UNSUPPORTED_VERSION) {
        log(OSPLAY_LOG_WARNING, "Could not switch " + narrow(*instance) + " to the accessory (" +
                                    std::to_string(static_cast<int>(result)) + ")");
    }
}

void AndroidUsbWatcher::removed(const std::wstring& link) {
    std::optional<std::uint32_t> id;
    {
        const std::scoped_lock lock(mutex_);
        lookups_.erase(link);
        if (const auto found = accessories_.find(link); found != accessories_.end()) {
            id = found->second.id;
            accessories_.erase(found);
        }
    }
    if (id) {
        log(OSPLAY_LOG_INFO, "Android accessory " + std::to_string(*id) + " detached");
        if (callbacks_.detached) {
            callbacks_.detached(*id);
        }
    }
}

void AndroidUsbWatcher::debugging_is_on() const {
    log(OSPLAY_LOG_INFO, "An Android device has USB debugging on; the cable cannot reach it");
    if (callbacks_.debugging_on) {
        callbacks_.debugging_on();
    }
}

void AndroidUsbWatcher::log(osplay_log_level level, const std::string& message) const {
    if (callbacks_.log) {
        callbacks_.log(level, message);
    }
}

} // namespace osplay::engine::usb
