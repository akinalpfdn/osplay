#include "keystore/dpapi_key_store.hpp"

#include "capi/system_random.hpp"

#include <windows.h>

#include <dpapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace osplay::engine {
namespace {

constexpr const wchar_t* kIdentityFile = L"identity.bin";
constexpr const wchar_t* kPeersFile = L"peers.bin";

/// %APPDATA%\Osplay, created when missing.
std::optional<std::filesystem::path> store_folder() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming))) {
        CoTaskMemFree(roaming);
        return std::nullopt;
    }
    std::filesystem::path folder = std::filesystem::path(roaming) / L"Osplay";
    CoTaskMemFree(roaming);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return error ? std::nullopt : std::optional(folder);
}

/// Nullopt when the file does not exist; an empty vector when it cannot be read.
std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

/// Writes next to the target and replaces it, so a crash never leaves half a file.
bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    return MoveFileExW(temporary.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::optional<std::vector<std::uint8_t>> protect(std::span<const std::uint8_t> plain) {
    std::vector<std::uint8_t> copy(plain.begin(), plain.end()); // DPAPI takes a mutable pointer
    DATA_BLOB input{.cbData = static_cast<DWORD>(copy.size()), .pbData = copy.data()};
    DATA_BLOB output{};
    if (CryptProtectData(&input, L"Osplay", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                         &output) == FALSE) {
        return std::nullopt;
    }
    SecureZeroMemory(copy.data(), copy.size());
    std::vector<std::uint8_t> result(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::optional<std::vector<std::uint8_t>> unprotect(std::span<const std::uint8_t> sealed) {
    std::vector<std::uint8_t> copy(sealed.begin(), sealed.end()); // DPAPI takes a mutable pointer
    DATA_BLOB input{.cbData = static_cast<DWORD>(copy.size()), .pbData = copy.data()};
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                           &output) == FALSE) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> result(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

} // namespace

DpapiKeyStore& DpapiKeyStore::shared() {
    static DpapiKeyStore store;
    return store;
}

win::Expected<DpapiKeyStore::Identity, KeyStoreError> DpapiKeyStore::identity() {
    using Result = win::Expected<Identity, KeyStoreError>;
    const std::scoped_lock lock(mutex_);
    const auto folder = store_folder();
    if (!folder) {
        return Result::failure(KeyStoreError::Unavailable);
    }
    const std::filesystem::path path = *folder / kIdentityFile;
    Identity identity;
    if (const auto sealed = read_file(path)) {
        auto secret = unprotect(*sealed);
        if (!secret || secret->size() != identity.secret_key.size()) {
            return Result::failure(KeyStoreError::Damaged);
        }
        std::ranges::copy(*secret, identity.secret_key.begin());
        SecureZeroMemory(secret->data(), secret->size());
        if (osplay_identity_public_key(identity.secret_key.data(), identity.public_key.data()) !=
            OSPLAY_OK) {
            return Result::failure(KeyStoreError::Damaged);
        }
        return identity;
    }
    const osplay_random_source random{.fill = system_random, .context = nullptr};
    if (osplay_identity_generate(&random, identity.secret_key.data(), identity.public_key.data()) !=
        OSPLAY_OK) {
        return Result::failure(KeyStoreError::RandomUnavailable);
    }
    const auto sealed = protect(identity.secret_key);
    if (!sealed || !write_file(path, *sealed)) {
        return Result::failure(KeyStoreError::Unavailable);
    }
    return identity;
}

win::Expected<PeerList, KeyStoreError> DpapiKeyStore::peers() {
    const std::scoped_lock lock(mutex_);
    return load_peers();
}

win::Expected<PeerList, KeyStoreError> DpapiKeyStore::load_peers() {
    using Result = win::Expected<PeerList, KeyStoreError>;
    const auto folder = store_folder();
    if (!folder) {
        return Result::failure(KeyStoreError::Unavailable);
    }
    const auto sealed = read_file(*folder / kPeersFile);
    if (!sealed) {
        return PeerList{}; // nobody paired yet
    }
    const auto plain = unprotect(*sealed);
    if (!plain) {
        return Result::failure(KeyStoreError::Damaged);
    }
    auto list = PeerList::decode(*plain);
    if (!list) {
        return Result::failure(KeyStoreError::Damaged);
    }
    return std::move(*list);
}

std::optional<KeyStoreError> DpapiKeyStore::save_peers(const PeerList& list) {
    const auto folder = store_folder();
    if (!folder) {
        return KeyStoreError::Unavailable;
    }
    const auto sealed = protect(list.encode());
    if (!sealed || !write_file(*folder / kPeersFile, *sealed)) {
        return KeyStoreError::Unavailable;
    }
    return std::nullopt;
}

} // namespace osplay::engine
