// JNI bridge between the Android app (com.akinalpfdn.osplay.core.Native) and the shared core's C
// API (core/include/osplay/osplay.h). Text crosses as UTF-8 byte arrays: JNI's modified UTF-8 would
// change names with characters outside the BMP.
#include <osplay/osplay.h>

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <sys/random.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kLogTag = "OsplayCore";

JavaVM* g_vm = nullptr;
pthread_key_t g_detach_key;

/// The core's I/O thread is attached to the JVM on its first callback and detached when it ends.
JNIEnv* attached_env() {
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return nullptr;
    }
    pthread_setspecific(g_detach_key, env);
    return env;
}

void detach_thread(void* /*env*/) {
    g_vm->DetachCurrentThread();
}

osplay_result fill_random(void* /*context*/, uint8_t* buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        const ssize_t got = getrandom(buffer + done, length - done, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return OSPLAY_ERROR_RANDOM_UNAVAILABLE;
        }
        done += static_cast<size_t>(got);
    }
    return OSPLAY_OK;
}

const osplay_random_source kRandom{.fill = fill_random, .context = nullptr};

jbyteArray to_bytes(JNIEnv* env, const void* data, size_t length) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(length));
    if (array != nullptr && length > 0) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(length),
                                static_cast<const jbyte*>(data));
    }
    return array;
}

jbyteArray to_bytes(JNIEnv* env, const char* text) {
    return to_bytes(env, text, text == nullptr ? 0 : std::strlen(text));
}

std::vector<uint8_t> from_bytes(JNIEnv* env, jbyteArray array) {
    std::vector<uint8_t> bytes;
    if (array == nullptr) {
        return bytes;
    }
    bytes.resize(static_cast<size_t>(env->GetArrayLength(array)));
    env->GetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()),
                            reinterpret_cast<jbyte*>(bytes.data()));
    return bytes;
}

/// A NUL-terminated copy of a UTF-8 byte array.
std::string from_text(JNIEnv* env, jbyteArray array) {
    const std::vector<uint8_t> bytes = from_bytes(env, array);
    return {bytes.begin(), bytes.end()};
}

void clear_exception(JNIEnv* env, const char* callback) {
    if (env->ExceptionCheck()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s threw", callback);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

/// One viewer session and the Kotlin listener its callbacks go to.
struct Viewer {
    jobject listener = nullptr; // global reference
    jmethodID on_state = nullptr;
    jmethodID on_peer = nullptr;
    jmethodID on_trust_query = nullptr;
    jmethodID on_pairing_code = nullptr;
    jmethodID on_paired = nullptr;
    jmethodID on_stream_config = nullptr;
    jmethodID on_access_unit = nullptr;
    jmethodID on_stats = nullptr;
    jmethodID on_log = nullptr;
    jmethodID on_cursor_shape = nullptr;
    jmethodID on_cursor_position = nullptr;
    jmethodID on_stream_stopped = nullptr;
    jmethodID on_role_swap_request = nullptr;
    osplay_session* session = nullptr;
};

Viewer* viewer_of(void* context) {
    return static_cast<Viewer*>(context);
}

Viewer* viewer_of(jlong handle) {
    return reinterpret_cast<Viewer*>(handle);
}

/// Calls a void listener method with local references that are deleted afterwards (the I/O
/// thread stays attached, so its local references would pile up otherwise).
template <typename... Args>
void call_void(Viewer* viewer, jmethodID method, const char* name, Args... args) {
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    env->CallVoidMethod(viewer->listener, method, args...);
    clear_exception(env, name);
}

void on_state(void* context, osplay_session_state state, osplay_result reason) {
    Viewer* viewer = viewer_of(context);
    call_void(viewer, viewer->on_state, "onState", static_cast<jint>(state),
              static_cast<jint>(reason));
}

void on_peer(void* context, const osplay_peer_info* peer) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray name = to_bytes(env, peer->device_name);
    call_void(viewer, viewer->on_peer, "onPeer", static_cast<jint>(peer->platform),
              static_cast<jint>(peer->protocol_minor), name);
    env->DeleteLocalRef(name);
}

osplay_trust on_trust_query(void* context, const uint8_t* key) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return OSPLAY_TRUST_BLOCKED;
    }
    jbyteArray public_key = to_bytes(env, key, OSPLAY_KEY_SIZE);
    const jint decision = env->CallIntMethod(viewer->listener, viewer->on_trust_query, public_key);
    env->DeleteLocalRef(public_key);
    if (env->ExceptionCheck()) {
        clear_exception(env, "onTrustQuery");
        return OSPLAY_TRUST_BLOCKED;
    }
    return static_cast<osplay_trust>(decision);
}

void on_pairing_code(void* context, uint32_t code, const uint8_t* key) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray public_key = to_bytes(env, key, OSPLAY_KEY_SIZE);
    call_void(viewer, viewer->on_pairing_code, "onPairingCode", static_cast<jint>(code),
              public_key);
    env->DeleteLocalRef(public_key);
}

void on_paired(void* context, const uint8_t* key) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray public_key = to_bytes(env, key, OSPLAY_KEY_SIZE);
    call_void(viewer, viewer->on_paired, "onPaired", public_key);
    env->DeleteLocalRef(public_key);
}

void on_stream_config(void* context, const osplay_stream_config* config) {
    Viewer* viewer = viewer_of(context);
    call_void(viewer, viewer->on_stream_config, "onStreamConfig",
              static_cast<jint>(config->stream_id), static_cast<jint>(config->monitor_id),
              static_cast<jint>(config->width), static_cast<jint>(config->height),
              static_cast<jint>(config->refresh_millihertz), static_cast<jint>(config->cursor_mode),
              static_cast<jint>(config->bitrate_kbps));
}

void on_access_unit(void* context, const osplay_access_unit* unit) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray data = to_bytes(env, unit->data, unit->length);
    call_void(viewer, viewer->on_access_unit, "onAccessUnit", static_cast<jint>(unit->stream_id),
              static_cast<jboolean>(unit->keyframe != 0),
              static_cast<jboolean>(unit->codec_config != 0),
              static_cast<jlong>(unit->capture_time_us), data);
    env->DeleteLocalRef(data);
}

void on_stats(void* context, const osplay_session_stats* stats) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    // Per stream: id, frames per second, bitrate, loss (permille), latency (µs).
    constexpr size_t kFields = 5;
    std::vector<jint> fields;
    fields.reserve(stats->stream_count * kFields);
    for (size_t i = 0; i < stats->stream_count; ++i) {
        const osplay_stream_stats& stream = stats->streams[i];
        fields.push_back(static_cast<jint>(stream.stream_id));
        fields.push_back(static_cast<jint>(stream.frames_per_second));
        fields.push_back(static_cast<jint>(stream.bitrate_kbps));
        fields.push_back(static_cast<jint>(stream.loss_permille));
        fields.push_back(static_cast<jint>(stream.latency_us));
    }
    jintArray streams = env->NewIntArray(static_cast<jsize>(fields.size()));
    if (streams == nullptr) {
        return;
    }
    env->SetIntArrayRegion(streams, 0, static_cast<jsize>(fields.size()), fields.data());
    call_void(viewer, viewer->on_stats, "onStats", static_cast<jint>(stats->rtt_us), streams);
    env->DeleteLocalRef(streams);
}

void on_log(void* context, osplay_log_level level, const char* message) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray text = to_bytes(env, message);
    call_void(viewer, viewer->on_log, "onLog", static_cast<jint>(level), text);
    env->DeleteLocalRef(text);
}

void on_cursor_shape(void* context, const osplay_cursor_shape* shape) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray bgra = to_bytes(env, shape->bgra, shape->length);
    call_void(viewer, viewer->on_cursor_shape, "onCursorShape", static_cast<jint>(shape->stream_id),
              static_cast<jint>(shape->shape_id), static_cast<jint>(shape->width),
              static_cast<jint>(shape->height), static_cast<jint>(shape->hotspot_x),
              static_cast<jint>(shape->hotspot_y), bgra);
    env->DeleteLocalRef(bgra);
}

void on_cursor_position(void* context, const osplay_cursor_position* position) {
    Viewer* viewer = viewer_of(context);
    call_void(viewer, viewer->on_cursor_position, "onCursorPosition",
              static_cast<jint>(position->stream_id), static_cast<jboolean>(position->visible != 0),
              static_cast<jint>(position->shape_id), static_cast<jint>(position->x),
              static_cast<jint>(position->y));
}

void on_stream_stopped(void* context, uint8_t stream_id) {
    Viewer* viewer = viewer_of(context);
    call_void(viewer, viewer->on_stream_stopped, "onStreamStopped", static_cast<jint>(stream_id));
}

void on_role_swap_request(void* context, const uint8_t* key) {
    Viewer* viewer = viewer_of(context);
    JNIEnv* env = attached_env();
    if (env == nullptr) {
        return;
    }
    jbyteArray public_key = to_bytes(env, key, OSPLAY_KEY_SIZE);
    call_void(viewer, viewer->on_role_swap_request, "onRoleSwapRequest", public_key);
    env->DeleteLocalRef(public_key);
}

bool find_methods(JNIEnv* env, jobject listener, Viewer& viewer) {
    jclass type = env->GetObjectClass(listener);
    const auto method = [&](const char* name, const char* signature) {
        return env->GetMethodID(type, name, signature);
    };
    viewer.on_state = method("onState", "(II)V");
    viewer.on_peer = method("onPeer", "(II[B)V");
    viewer.on_trust_query = method("onTrustQuery", "([B)I");
    viewer.on_pairing_code = method("onPairingCode", "(I[B)V");
    viewer.on_paired = method("onPaired", "([B)V");
    viewer.on_stream_config = method("onStreamConfig", "(IIIIIII)V");
    viewer.on_access_unit = method("onAccessUnit", "(IZZJ[B)V");
    viewer.on_stats = method("onStats", "(I[I)V");
    viewer.on_log = method("onLog", "(I[B)V");
    viewer.on_cursor_shape = method("onCursorShape", "(IIIIII[B)V");
    viewer.on_cursor_position = method("onCursorPosition", "(IZIII)V");
    viewer.on_stream_stopped = method("onStreamStopped", "(I)V");
    viewer.on_role_swap_request = method("onRoleSwapRequest", "([B)V");
    env->DeleteLocalRef(type);
    const bool found = viewer.on_state != nullptr && viewer.on_peer != nullptr &&
                       viewer.on_trust_query != nullptr && viewer.on_pairing_code != nullptr &&
                       viewer.on_paired != nullptr && viewer.on_stream_config != nullptr &&
                       viewer.on_access_unit != nullptr && viewer.on_stats != nullptr &&
                       viewer.on_log != nullptr && viewer.on_cursor_shape != nullptr &&
                       viewer.on_cursor_position != nullptr &&
                       viewer.on_stream_stopped != nullptr &&
                       viewer.on_role_swap_request != nullptr;
    if (!found) {
        clear_exception(env, "GetMethodID");
    }
    return found;
}

jint result(osplay_result value) {
    return static_cast<jint>(value);
}

} // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    pthread_key_create(&g_detach_key, detach_thread);
    return JNI_VERSION_1_6;
}

JNIEXPORT jintArray JNICALL Java_com_akinalpfdn_osplay_core_Native_coreVersion(JNIEnv* env,
                                                                               jobject /*self*/) {
    const osplay_version version = osplay_core_version();
    const std::array<jint, 3> parts{version.major, version.minor, version.patch};
    jintArray array = env->NewIntArray(parts.size());
    if (array != nullptr) {
        env->SetIntArrayRegion(array, 0, parts.size(), parts.data());
    }
    return array;
}

JNIEXPORT jbyteArray JNICALL
Java_com_akinalpfdn_osplay_core_Native_identityGenerate(JNIEnv* env, jobject /*self*/) {
    std::array<uint8_t, OSPLAY_KEY_SIZE> secret{};
    std::array<uint8_t, OSPLAY_KEY_SIZE> public_key{};
    if (osplay_identity_generate(&kRandom, secret.data(), public_key.data()) != OSPLAY_OK) {
        return nullptr;
    }
    jbyteArray array = to_bytes(env, secret.data(), secret.size());
    secret.fill(0);
    return array;
}

JNIEXPORT jbyteArray JNICALL Java_com_akinalpfdn_osplay_core_Native_identityPublicKey(
    JNIEnv* env, jobject /*self*/, jbyteArray secret_key) {
    std::vector<uint8_t> secret = from_bytes(env, secret_key);
    std::array<uint8_t, OSPLAY_KEY_SIZE> public_key{};
    const bool ok = secret.size() == OSPLAY_KEY_SIZE &&
                    osplay_identity_public_key(secret.data(), public_key.data()) == OSPLAY_OK;
    std::fill(secret.begin(), secret.end(), 0);
    return ok ? to_bytes(env, public_key.data(), public_key.size()) : nullptr;
}

JNIEXPORT jbyteArray JNICALL Java_com_akinalpfdn_osplay_core_Native_identityDeviceId(
    JNIEnv* env, jobject /*self*/, jbyteArray public_key) {
    const std::vector<uint8_t> key = from_bytes(env, public_key);
    std::array<uint8_t, OSPLAY_DEVICE_ID_SIZE> device_id{};
    if (key.size() != OSPLAY_KEY_SIZE ||
        osplay_identity_device_id(key.data(), device_id.data()) != OSPLAY_OK) {
        return nullptr;
    }
    return to_bytes(env, device_id.data(), device_id.size());
}

/// [result, protocol version, protocol minor, platform, roles]; the name and ID are the input's
/// when the result is OSPLAY_OK.
JNIEXPORT jintArray JNICALL Java_com_akinalpfdn_osplay_core_Native_advertisementFromTxt(
    JNIEnv* env, jobject /*self*/, jbyteArray protocol_version, jbyteArray name,
    jbyteArray device_id, jbyteArray roles, jbyteArray os, jbyteArray protocol_minor) {
    osplay_txt_record record{};
    const auto copy = [env](jbyteArray source, char (&target)[OSPLAY_TXT_VALUE_SIZE]) {
        const std::string text = from_text(env, source);
        const size_t length = std::min(text.size(), sizeof(target) - 1);
        std::memcpy(target, text.data(), length);
        target[length] = '\0';
    };
    copy(protocol_version, record.protocol_version);
    copy(name, record.name);
    copy(device_id, record.device_id);
    copy(roles, record.roles);
    copy(os, record.os);
    copy(protocol_minor, record.protocol_minor);
    osplay_advertisement parsed{};
    const osplay_result parse = osplay_advertisement_from_txt(&record, &parsed);
    const std::array<jint, 5> fields{result(parse), static_cast<jint>(parsed.protocol_version),
                                     static_cast<jint>(parsed.protocol_minor),
                                     static_cast<jint>(parsed.platform),
                                     static_cast<jint>(parsed.roles)};
    jintArray array = env->NewIntArray(fields.size());
    if (array != nullptr) {
        env->SetIntArrayRegion(array, 0, fields.size(), fields.data());
    }
    return array;
}

/// monitor: width, height, refresh (mHz), physical width and height (mm), scale (%);
/// capabilities: max streams, max width, max height, max refresh (Hz). With accessory_fd >= 0 the
/// session runs on that USB accessory (the core owns and closes it) instead of host and port. The
/// handle goes into out_handle[0].
JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_viewerCreate(
    JNIEnv* env, jobject /*self*/, jobject listener, jbyteArray device_name, jbyteArray secret_key,
    jintArray monitor, jbyteArray monitor_name, jintArray capabilities, jbyteArray host, jint port,
    jint accessory_fd, jlongArray out_handle) {
    const bool on_cable = accessory_fd >= 0;
    if (listener == nullptr || env->GetArrayLength(monitor) != 6 ||
        env->GetArrayLength(capabilities) != 4 || env->GetArrayLength(out_handle) != 1) {
        if (on_cable) {
            close(accessory_fd);
        }
        return result(OSPLAY_ERROR_INVALID_ARGUMENT);
    }
    std::array<jint, 6> m{};
    env->GetIntArrayRegion(monitor, 0, m.size(), m.data());
    std::array<jint, 4> c{};
    env->GetIntArrayRegion(capabilities, 0, c.size(), c.data());
    const std::string name = from_text(env, device_name);
    const std::string monitor_text = from_text(env, monitor_name);
    const std::string host_text = from_text(env, host);
    std::vector<uint8_t> secret = from_bytes(env, secret_key);
    if (secret.size() != OSPLAY_KEY_SIZE || (!on_cable && (port <= 0 || port > 65535))) {
        if (on_cable) {
            close(accessory_fd);
        }
        return result(OSPLAY_ERROR_INVALID_ARGUMENT);
    }

    auto* viewer = new Viewer();
    viewer->listener = env->NewGlobalRef(listener);
    if (!find_methods(env, listener, *viewer)) {
        env->DeleteGlobalRef(viewer->listener);
        delete viewer;
        if (on_cable) {
            close(accessory_fd);
        }
        return result(OSPLAY_ERROR_INVALID_ARGUMENT);
    }
    const osplay_monitor offered{.monitor_id = 0,
                                 .width_px = static_cast<uint16_t>(m[0]),
                                 .height_px = static_cast<uint16_t>(m[1]),
                                 .refresh_millihertz = static_cast<uint32_t>(m[2]),
                                 .physical_width_mm = static_cast<uint16_t>(m[3]),
                                 .physical_height_mm = static_cast<uint16_t>(m[4]),
                                 .scale_percent = static_cast<uint16_t>(m[5]),
                                 .name = monitor_text.c_str()};
    const osplay_session_config config{
        .platform = OSPLAY_PLATFORM_ANDROID,
        .device_name = name.c_str(),
        .capabilities = {.codecs = OSPLAY_CODEC_H264_ANNEX_B,
                         .max_streams = static_cast<uint8_t>(c[0]),
                         .flags = 0,
                         .max_width = static_cast<uint16_t>(c[1]),
                         .max_height = static_cast<uint16_t>(c[2]),
                         .max_refresh_hz = static_cast<uint16_t>(c[3])},
        .monitors = &offered,
        .monitor_count = 1,
        .control_port = 0};
    const osplay_session_callbacks callbacks{.context = viewer,
                                             .on_state = on_state,
                                             .on_peer = on_peer,
                                             .on_trust_query = on_trust_query,
                                             .on_pairing_code = on_pairing_code,
                                             .on_paired = on_paired,
                                             .on_viewer_monitors = nullptr,
                                             .on_stream_config = on_stream_config,
                                             .on_keyframe_request = nullptr,
                                             .on_access_unit = on_access_unit,
                                             .on_stats = on_stats,
                                             .on_log = on_log,
                                             .on_cursor_shape = on_cursor_shape,
                                             .on_cursor_position = on_cursor_position,
                                             .on_stream_stopped = on_stream_stopped,
                                             .on_role_swap_request = on_role_swap_request,
                                             .on_role_swap_answer = nullptr,
                                             .on_bitrate_target = nullptr};
    const osplay_result created =
        on_cable ? osplay_viewer_session_create_accessory(&config, secret.data(), &kRandom,
                                                          &callbacks, accessory_fd, &viewer->session)
                 : osplay_viewer_session_create(&config, secret.data(), &kRandom, &callbacks,
                                                host_text.c_str(), static_cast<uint16_t>(port),
                                                &viewer->session);
    std::fill(secret.begin(), secret.end(), 0);
    if (created != OSPLAY_OK) {
        env->DeleteGlobalRef(viewer->listener);
        delete viewer;
        return result(created);
    }
    const jlong handle = reinterpret_cast<jlong>(viewer);
    env->SetLongArrayRegion(out_handle, 0, 1, &handle);
    return result(OSPLAY_OK);
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionConfirmPairing(
    JNIEnv* /*env*/, jobject /*self*/, jlong handle, jboolean accepted) {
    return result(osplay_session_confirm_pairing(viewer_of(handle)->session, accepted ? 1 : 0));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionRequestKeyframe(
    JNIEnv* /*env*/, jobject /*self*/, jlong handle, jint stream_id) {
    return result(osplay_session_request_keyframe(viewer_of(handle)->session,
                                                  static_cast<uint8_t>(stream_id)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionPauseStream(JNIEnv* /*env*/,
                                                                                 jobject /*self*/,
                                                                                 jlong handle,
                                                                                 jint stream_id) {
    return result(
        osplay_session_pause_stream(viewer_of(handle)->session, static_cast<uint8_t>(stream_id)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionResumeStream(JNIEnv* /*env*/,
                                                                                  jobject /*self*/,
                                                                                  jlong handle,
                                                                                  jint stream_id) {
    return result(
        osplay_session_resume_stream(viewer_of(handle)->session, static_cast<uint8_t>(stream_id)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionAnswerRoleSwap(
    JNIEnv* /*env*/, jobject /*self*/, jlong handle, jint answer) {
    return result(osplay_session_answer_role_swap(viewer_of(handle)->session,
                                                  static_cast<osplay_role_swap_answer>(answer)));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionClose(JNIEnv* /*env*/,
                                                                           jobject /*self*/,
                                                                           jlong handle) {
    return result(osplay_session_close(viewer_of(handle)->session));
}

JNIEXPORT jint JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionCloseForSleep(JNIEnv* /*env*/,
                                                                                   jobject /*self*/,
                                                                                   jlong handle) {
    return result(osplay_session_close_for_sleep(viewer_of(handle)->session));
}

/// Joins the I/O thread, so it must not run on it (never from a listener callback).
JNIEXPORT void JNICALL Java_com_akinalpfdn_osplay_core_Native_sessionDestroy(JNIEnv* env,
                                                                             jobject /*self*/,
                                                                             jlong handle) {
    Viewer* viewer = viewer_of(handle);
    if (viewer == nullptr) {
        return;
    }
    osplay_session_destroy(viewer->session);
    env->DeleteGlobalRef(viewer->listener);
    delete viewer;
}

JNIEXPORT jlong JNICALL Java_com_akinalpfdn_osplay_core_Native_clockMicroseconds(JNIEnv* /*env*/,
                                                                                 jobject /*self*/) {
    return static_cast<jlong>(osplay_clock_microseconds());
}

} // extern "C"
