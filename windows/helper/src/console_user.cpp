#include "console_user.hpp"

#include <sddl.h>
#include <wtsapi32.h>

#include <exception>
#include <memory>
#include <string>
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

std::string narrow(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t c : text) {
        result.push_back(static_cast<char>(c)); // SIDs are ASCII
    }
    return result;
}

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

ClientCheck check_pipe_client(HANDLE pipe, const std::wstring& console_sid) {
    ULONG client_session = 0;
    if (!GetNamedPipeClientSessionId(pipe, &client_session)) {
        return {.accepted = false, .reason = "client session unknown"};
    }
    if (client_session != WTSGetActiveConsoleSessionId()) {
        return {.accepted = false,
                .reason = "client in session " + std::to_string(client_session) +
                          ", not the console session"};
    }
    if (!ImpersonateNamedPipeClient(pipe)) {
        return {.accepted = false, .reason = "cannot identify the client"};
    }
    HANDLE raw_token = nullptr;
    const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token);
    if (!RevertToSelf()) {
        // Continuing would run as the client; a privileged service must not survive that.
        std::terminate();
    }
    if (!opened) {
        // Anonymous connections (the .NET default) cannot be identified.
        return {.accepted = false,
                .reason =
                    "client did not allow identification (connect with SECURITY_IDENTIFICATION)"};
    }
    const UniqueHandle token(raw_token);
    const auto client_sid = token_user_sid(token.get());
    if (!client_sid) {
        return {.accepted = false, .reason = "client user unknown"};
    }
    if (*client_sid != console_sid && *client_sid != kLocalSystemSid) {
        return {.accepted = false,
                .reason = "client user " + narrow(*client_sid) + " is not the console user"};
    }
    return {.accepted = true, .reason = {}};
}

} // namespace osplay::helper
