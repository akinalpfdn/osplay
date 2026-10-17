#include "console_user.hpp"

#include <sddl.h>
#include <wtsapi32.h>

#include <exception>
#include <memory>
#include <vector>

namespace osplay::helper {
namespace {

constexpr const wchar_t* kLocalSystemSid = L"S-1-5-18";

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::optional<std::wstring> token_user_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        return std::nullopt;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &text)) {
        return std::nullopt;
    }
    std::wstring sid(text);
    LocalFree(text);
    return sid;
}

} // namespace

std::optional<std::wstring> console_user_sid() {
    const DWORD session = WTSGetActiveConsoleSessionId();
    if (session == 0xFFFFFFFF) {
        return std::nullopt;
    }
    HANDLE raw_token = nullptr;
    if (!WTSQueryUserToken(session, &raw_token)) {
        return std::nullopt; // nobody logged on at the console
    }
    const UniqueHandle token(raw_token);
    return token_user_sid(token.get());
}

bool pipe_client_is_console_user(HANDLE pipe, const std::wstring& console_sid) {
    ULONG client_session = 0;
    if (!GetNamedPipeClientSessionId(pipe, &client_session) ||
        client_session != WTSGetActiveConsoleSessionId()) {
        return false;
    }
    if (!ImpersonateNamedPipeClient(pipe)) {
        return false;
    }
    HANDLE raw_token = nullptr;
    const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token);
    if (!RevertToSelf()) {
        // Continuing would run as the client; a privileged service must not survive that.
        std::terminate();
    }
    if (!opened) {
        return false;
    }
    const UniqueHandle token(raw_token);
    const auto client_sid = token_user_sid(token.get());
    return client_sid && (*client_sid == console_sid || *client_sid == kLocalSystemSid);
}

} // namespace osplay::helper
