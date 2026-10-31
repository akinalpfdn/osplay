include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Match the MSVC runtime used by the rest of the build: the DLL runtime by default, the static one
# when the build asks for it (the Windows release preset).
if(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
else()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(osplay_core_tests
    bitrate_controller_test.cpp
    capi_discovery_test.cpp
    capi_identity_test.cpp
    capi_protocol_test.cpp
    capi_session_test.cpp
    clock_offset_test.cpp
    crypto_primitives_test.cpp
    media_crypto_test.cpp
    media_test.cpp
    noise_handshake_test.cpp
    noise_vectors_test.cpp
    protocol_codec_test.cpp
    protocol_fixture_test.cpp
    protocol_values_test.cpp
    session_engine_test.cpp
    transport_test.cpp
    usb_link_test.cpp
    version_test.cpp)
# Tests reach internal headers; only the C header is public API.
target_include_directories(osplay_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(osplay_core_tests PRIVATE
    OSPLAY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/fixtures"
    OSPLAY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
target_link_libraries(osplay_core_tests PRIVATE osplay::core osplay_asio osplay::warnings GTest::gtest_main)
set_target_properties(osplay_core_tests PROPERTIES CXX_EXTENSIONS OFF)

include(GoogleTest)
gtest_discover_tests(osplay_core_tests DISCOVERY_MODE PRE_TEST)
