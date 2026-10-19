#pragma once

// Shared by the engine's C API files: error mapping and the exception barrier.

#include "../display/virtual_display_provider.hpp"
#include "../host/host_orchestrator.hpp"
#include "../keystore/dpapi_key_store.hpp"

#include <osplay/engine.h>

namespace osplay::engine {

inline osplay_engine_result to_result(DisplayError error) {
    switch (error) {
    case DisplayError::InvalidArgument:
        return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
    case DisplayError::HelperUnavailable:
        return OSPLAY_ENGINE_ERROR_HELPER_UNAVAILABLE;
    case DisplayError::HelperBusy:
        return OSPLAY_ENGINE_ERROR_HELPER_BUSY;
    case DisplayError::HelperAccessDenied:
        return OSPLAY_ENGINE_ERROR_HELPER_ACCESS_DENIED;
    case DisplayError::HelperNotTrusted:
        return OSPLAY_ENGINE_ERROR_HELPER_NOT_TRUSTED;
    case DisplayError::ProtocolMismatch:
        return OSPLAY_ENGINE_ERROR_PROTOCOL_MISMATCH;
    case DisplayError::ConnectionLost:
        return OSPLAY_ENGINE_ERROR_CONNECTION_LOST;
    case DisplayError::VddNotInstalled:
        return OSPLAY_ENGINE_ERROR_VDD_NOT_INSTALLED;
    case DisplayError::DeviceControlFailed:
        return OSPLAY_ENGINE_ERROR_DEVICE_CONTROL_FAILED;
    case DisplayError::ConfigWriteFailed:
        return OSPLAY_ENGINE_ERROR_CONFIG_WRITE_FAILED;
    case DisplayError::Rejected:
        return OSPLAY_ENGINE_ERROR_REJECTED;
    case DisplayError::MonitorNotFound:
        return OSPLAY_ENGINE_ERROR_MONITOR_NOT_FOUND;
    case DisplayError::ModeChangeFailed:
        return OSPLAY_ENGINE_ERROR_MODE_CHANGE_FAILED;
    }
    return OSPLAY_ENGINE_ERROR_INTERNAL;
}

inline osplay_engine_result to_result(PipelineError error) {
    switch (error) {
    case PipelineError::MonitorNotFound:
        return OSPLAY_ENGINE_ERROR_MONITOR_NOT_FOUND;
    case PipelineError::CaptureFailed:
        return OSPLAY_ENGINE_ERROR_CAPTURE_FAILED;
    case PipelineError::EncoderUnavailable:
        return OSPLAY_ENGINE_ERROR_ENCODER_UNAVAILABLE;
    case PipelineError::EncoderFailed:
        return OSPLAY_ENGINE_ERROR_ENCODER_FAILED;
    case PipelineError::DecoderUnavailable:
        return OSPLAY_ENGINE_ERROR_DECODER_UNAVAILABLE;
    case PipelineError::PresentationFailed:
        return OSPLAY_ENGINE_ERROR_PRESENTATION_FAILED;
    }
    return OSPLAY_ENGINE_ERROR_INTERNAL;
}

inline osplay_engine_result to_result(KeyStoreError error) {
    return error == KeyStoreError::Damaged ? OSPLAY_ENGINE_ERROR_KEY_STORE_DAMAGED
                                           : OSPLAY_ENGINE_ERROR_KEY_STORE_UNAVAILABLE;
}

/// No C++ exception may cross the C boundary.
template <typename Body>
inline osplay_engine_result guarded(const Body& body) noexcept {
    try {
        return body();
    } catch (...) {
        return OSPLAY_ENGINE_ERROR_INTERNAL;
    }
}

} // namespace osplay::engine
