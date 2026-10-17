#include "pipe_helper_channel.hpp"

#include <osplay/win/helper_protocol.hpp>

namespace osplay::engine {
namespace {

constexpr DWORD kBusyWaitMilliseconds = 2000;

DisplayError error_for_open_failure(DWORD error) {
    switch (error) {
    case ERROR_ACCESS_DENIED:
        return DisplayError::HelperAccessDenied;
    default:
        return DisplayError::HelperUnavailable;
    }
}

} // namespace

win::Expected<std::unique_ptr<PipeHelperChannel>, DisplayError> PipeHelperChannel::connect() {
    using Result = win::Expected<std::unique_ptr<PipeHelperChannel>, DisplayError>;

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 2 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        // SECURITY_IDENTIFICATION lets the helper check who we are without acting as us.
        pipe = CreateFileW(win::helper::kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY ||
                !WaitNamedPipeW(win::helper::kPipeName, kBusyWaitMilliseconds)) {
                return Result::failure(error_for_open_failure(GetLastError()));
            }
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        return Result::failure(DisplayError::HelperUnavailable);
    }

    auto channel = std::make_unique<PipeHelperChannel>(ConstructionToken{}, pipe);

    ULONG server_session = 1;
    if (!GetNamedPipeServerSessionId(pipe, &server_session) || server_session != 0) {
        return Result::failure(DisplayError::HelperNotTrusted);
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        return Result::failure(DisplayError::HelperUnavailable);
    }
    return channel;
}

PipeHelperChannel::~PipeHelperChannel() {
    CloseHandle(pipe_);
}

win::Expected<win::helper::Bytes, DisplayError>
PipeHelperChannel::transact(std::span<const std::uint8_t> request) {
    using Result = win::Expected<win::helper::Bytes, DisplayError>;
    win::helper::Bytes response(win::helper::kMaxMessageSize);
    DWORD received = 0;
    // TransactNamedPipe only reads the input buffer; its signature is simply not const-correct.
    if (!TransactNamedPipe(pipe_, const_cast<std::uint8_t*>(request.data()),
                           static_cast<DWORD>(request.size()), response.data(),
                           static_cast<DWORD>(response.size()), &received, nullptr)) {
        const DWORD error = GetLastError();
        return Result::failure(error == ERROR_MORE_DATA ? DisplayError::ProtocolMismatch
                                                        : DisplayError::ConnectionLost);
    }
    response.resize(received);
    return response;
}

} // namespace osplay::engine
