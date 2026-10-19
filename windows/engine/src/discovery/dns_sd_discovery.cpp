#include "discovery/dns_sd_discovery.hpp"

#include "discovery/txt_mapping.hpp"

#include <windows.h>

#include <windns.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <format>
#include <map>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace osplay::engine {
namespace {

constexpr const wchar_t* kBrowseName = L"_osplay._tcp.local";

std::string utf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.pop_back(); // the NUL
    return result;
}

std::wstring wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}

/// IPv4 first: every Osplay core listens on both families, and IPv4 avoids scope IDs.
std::string address_of(const DNS_SERVICE_INSTANCE& instance) {
    if (instance.ip4Address != nullptr) {
        std::array<std::uint8_t, 4> bytes{};
        std::memcpy(bytes.data(), instance.ip4Address, bytes.size()); // network order
        return std::format("{}.{}.{}.{}", bytes[0], bytes[1], bytes[2], bytes[3]);
    }
    if (instance.ip6Address != nullptr) {
        std::array<std::uint8_t, 16> bytes{};
        std::memcpy(bytes.data(), instance.ip6Address, bytes.size());
        std::string text;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            text += std::format("{}{:x}", i == 0 ? "" : ":", (bytes.at(i) << 8U) | bytes.at(i + 1));
        }
        return text;
    }
    return utf8(instance.pszHostName); // Windows resolves .local names itself
}

} // namespace

// --- Browser -----------------------------------------------------------------------------------

struct DnsSdBrowser::State {
    struct Resolve {
        State* state = nullptr;
        std::wstring instance;
        DNS_SERVICE_CANCEL cancel{};
    };

    std::mutex mutex;
    std::condition_variable idle;
    bool closed = false;
    int running = 0; ///< shell callbacks in progress; the destructor waits for none
    Callbacks callbacks;
    DNS_SERVICE_CANCEL browse_cancel{};
    /// By instance name; shared so a request that completes at once cannot free it under us.
    std::map<std::wstring, std::shared_ptr<Resolve>> resolving;
    std::map<std::wstring, std::string> found; ///< instance name → label

    static VOID WINAPI on_browse(DWORD status, PVOID context, PDNS_RECORD records) {
        auto* state = static_cast<State*>(context);
        if (status == ERROR_SUCCESS) {
            for (const DNS_RECORD* record = records; record != nullptr; record = record->pNext) {
                if (record->wType != DNS_TYPE_PTR) {
                    continue;
                }
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): wType says it is a PTR
                const wchar_t* instance = record->Data.PTR.pNameHost;
                if (instance != nullptr) {
                    state->on_pointer(instance, record->dwTtl == 0);
                }
            }
        }
        if (records != nullptr) {
            DnsRecordListFree(records, DnsFreeRecordList);
        }
    }

    static VOID WINAPI on_resolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
        auto* resolve = static_cast<Resolve*>(context);
        resolve->state->finish(*resolve, status == ERROR_SUCCESS ? instance : nullptr);
        if (instance != nullptr) {
            DnsServiceFreeInstance(instance);
        }
    }

    void on_pointer(const wchar_t* instance_name, bool goodbye) {
        std::function<void(const std::string&)> lost;
        std::string label;
        std::shared_ptr<Resolve> resolve;
        {
            const std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const std::wstring name(instance_name);
            if (goodbye) {
                if (const auto it = found.find(name); it != found.end()) {
                    label = it->second;
                    found.erase(it);
                    lost = callbacks.on_lost;
                    ++running;
                }
            } else if (!resolving.contains(name)) {
                resolve = std::make_shared<Resolve>();
                resolve->state = this;
                resolve->instance = name;
                resolving.emplace(name, resolve);
            }
        }
        if (lost) {
            lost(label);
            done_calling();
        }
        if (resolve) {
            start_resolve(resolve); // outside the lock: it may complete before returning
        }
    }

    void start_resolve(const std::shared_ptr<Resolve>& resolve) {
        DNS_SERVICE_RESOLVE_REQUEST request{};
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.InterfaceIndex = 0;
        request.QueryName = resolve->instance.data();
        request.pResolveCompletionCallback = on_resolved;
        request.pQueryContext = resolve.get();
        if (DnsServiceResolve(&request, &resolve->cancel) != DNS_REQUEST_PENDING) {
            const std::scoped_lock lock(mutex);
            if (!closed) {
                resolving.erase(resolve->instance);
            }
        }
    }

    void finish(Resolve& resolve, const DNS_SERVICE_INSTANCE* instance) {
        std::function<void(const DiscoveredDevice&)> found_callback;
        DiscoveredDevice device;
        {
            const std::scoped_lock lock(mutex);
            if (closed) {
                return; // the resolve object stays with the abandoned state
            }
            const std::wstring name = resolve.instance; // copied: erasing may free `resolve`
            resolving.erase(name);
            if (instance == nullptr) {
                return;
            }
            TxtPairs pairs;
            for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
                const std::span keys(instance->keys, instance->dwPropertyCount);
                const std::span values(instance->values, instance->dwPropertyCount);
                pairs.emplace_back(utf8(keys[i]), utf8(values[i]));
            }
            const osplay_txt_record record = txt_record(pairs);
            const auto label = label_of(utf8(name.c_str()));
            if (!label ||
                osplay_advertisement_from_txt(&record, &device.advertisement) != OSPLAY_OK) {
                return; // not an Osplay record we understand
            }
            device.label = *label;
            device.address = address_of(*instance);
            device.port = instance->wPort;
            if (device.address.empty()) {
                return;
            }
            found[name] = device.label;
            found_callback = callbacks.on_found;
            ++running;
        }
        found_callback(device);
        done_calling();
    }

    void done_calling() {
        {
            const std::scoped_lock lock(mutex);
            --running;
        }
        idle.notify_all();
    }
};

win::Expected<std::unique_ptr<DnsSdBrowser>, DiscoveryError>
DnsSdBrowser::start(Callbacks callbacks) {
    using Result = win::Expected<std::unique_ptr<DnsSdBrowser>, DiscoveryError>;
    auto state = std::make_unique<State>();
    state->callbacks = std::move(callbacks);
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = kBrowseName;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): Win32 API
    request.pBrowseCallback = State::on_browse;
    request.pQueryContext = state.get();
    if (DnsServiceBrowse(&request, &state->browse_cancel) != DNS_REQUEST_PENDING) {
        return Result::failure(DiscoveryError::Unavailable);
    }
    return std::unique_ptr<DnsSdBrowser>(new DnsSdBrowser(
        state.release())); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

DnsSdBrowser::~DnsSdBrowser() {
    try {
        stop();
    } catch (...) {
        std::terminate(); // locking failed: callbacks could outlive the caller's context
    }
}

void DnsSdBrowser::stop() {
    // DNS-SD may still call back after a cancel, so the state is closed and left alive for the
    // rest of the process instead of freed (a few hundred bytes per browser).
    std::vector<std::shared_ptr<State::Resolve>> pending;
    {
        std::unique_lock lock(state_->mutex);
        state_->closed = true;
        for (const auto& [name, resolve] : state_->resolving) {
            pending.push_back(resolve);
        }
        // The C API promises no callback after destruction (callbacks must not destroy).
        state_->idle.wait(lock, [this] { return state_->running == 0; });
    }
    // Outside the lock: a cancel may call back at once, and the callback takes the lock.
    DnsServiceBrowseCancel(&state_->browse_cancel);
    for (const auto& resolve : pending) {
        DnsServiceResolveCancel(&resolve->cancel);
    }
}

// --- Advertiser --------------------------------------------------------------------------------

struct DnsSdAdvertiser::State {
    DNS_SERVICE_REGISTER_REQUEST request{};
    PDNS_SERVICE_INSTANCE instance = nullptr;
    DNS_SERVICE_CANCEL cancel{};

    static VOID WINAPI on_registered(DWORD /*status*/, PVOID /*context*/,
                                     PDNS_SERVICE_INSTANCE instance) {
        if (instance != nullptr) {
            DnsServiceFreeInstance(instance);
        }
    }
};

win::Expected<std::unique_ptr<DnsSdAdvertiser>, DiscoveryError>
DnsSdAdvertiser::start(const osplay_advertisement& advertisement, std::uint16_t port) {
    using Result = win::Expected<std::unique_ptr<DnsSdAdvertiser>, DiscoveryError>;
    osplay_txt_record record{};
    if (osplay_advertisement_to_txt(&advertisement, &record) != OSPLAY_OK) {
        return Result::failure(DiscoveryError::InvalidAdvertisement);
    }
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 64> computer{};
    auto computer_length = static_cast<DWORD>(computer.size());
    if (GetComputerNameExW(ComputerNameDnsHostname, computer.data(), &computer_length) == FALSE) {
        return Result::failure(DiscoveryError::Unavailable);
    }
    const std::wstring host = std::wstring(computer.data(), computer_length) + L".local";
    const std::wstring service = wide(instance_label(record)) + L"._osplay._tcp.local";
    std::vector<std::wstring> keys;
    std::vector<std::wstring> values;
    for (const auto& [key, value] : txt_pairs(record)) {
        keys.push_back(wide(key));
        values.push_back(wide(value));
    }
    std::vector<PCWSTR> key_pointers;
    std::vector<PCWSTR> value_pointers;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        key_pointers.push_back(keys[i].c_str());
        value_pointers.push_back(values[i].c_str());
    }
    auto state = std::make_unique<State>();
    // The system answers address queries for its own host name.
    state->instance = DnsServiceConstructInstance(service.c_str(), host.c_str(), nullptr, nullptr,
                                                  port, 0, 0, static_cast<DWORD>(keys.size()),
                                                  key_pointers.data(), value_pointers.data());
    if (state->instance == nullptr) {
        return Result::failure(DiscoveryError::Unavailable);
    }
    // Owned by the request from here on; DNS-SD keeps using it until deregistration completes.
    state->request.Version = DNS_QUERY_REQUEST_VERSION1;
    state->request.InterfaceIndex = 0;
    state->request.pServiceInstance = state->instance;
    state->request.pRegisterCompletionCallback = State::on_registered;
    state->request.pQueryContext = state.get();
    state->request.hCredentials = nullptr;
    state->request.unicastEnabled = FALSE;
    if (DnsServiceRegister(&state->request, &state->cancel) != DNS_REQUEST_PENDING) {
        DnsServiceFreeInstance(state->instance);
        return Result::failure(DiscoveryError::Unavailable);
    }
    return std::unique_ptr<DnsSdAdvertiser>(new DnsSdAdvertiser(
        state.release())); // NOLINT(cppcoreguidelines-owning-memory): private constructor
}

DnsSdAdvertiser::~DnsSdAdvertiser() {
    // Deregistration completes asynchronously through the same request, so the state stays alive
    // for the rest of the process (see DnsSdBrowser).
    DnsServiceDeRegister(&state_->request, nullptr);
}

} // namespace osplay::engine
