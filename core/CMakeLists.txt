cmake_minimum_required(VERSION 3.28)

# The version lives in the public header so C, Swift and C# consumers see the same numbers.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/osplay/osplay.h" _osplay_version_lines
     REGEX "^#define OSPLAY_CORE_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_line IN LISTS _osplay_version_lines)
    string(REGEX MATCH "OSPLAY_CORE_VERSION_([A-Z]+) ([0-9]+)" _ "${_line}")
    set(_osplay_version_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()

project(OsplayCore
    VERSION "${_osplay_version_MAJOR}.${_osplay_version_MINOR}.${_osplay_version_PATCH}"
    DESCRIPTION "Shared protocol, crypto and transport library for Osplay"
    LANGUAGES C CXX)

option(OSPLAY_CORE_BUILD_TESTS "Build the core unit tests" ${PROJECT_IS_TOP_LEVEL})
option(OSPLAY_CORE_BUILD_FUZZER "Build the libFuzzer protocol fuzzer (Clang only)" OFF)

if(OSPLAY_CORE_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "OSPLAY_CORE_BUILD_FUZZER requires Clang with libFuzzer for C and C++")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(NOT TARGET osplay::warnings)
    include(cmake/OsplayWarnings.cmake)
endif()

# Third-party C, built without Osplay's warning policy (docs/THIRD_PARTY.md).
add_library(osplay_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(osplay_monocypher PUBLIC third_party/monocypher)

add_library(osplay_core STATIC
    src/capi/clock_capi.cpp
    src/capi/discovery_capi.cpp
    src/capi/identity_capi.cpp
    src/capi/protocol_capi.cpp
    src/capi/session_capi.cpp
    src/capi/version.cpp
    src/crypto/media_crypto.cpp
    src/crypto/noise.cpp
    src/crypto/primitives.cpp
    src/media/cursor.cpp
    src/media/packetizer.cpp
    src/media/reassembler.cpp
    src/media/retransmit_cache.cpp
    src/session/bitrate_controller.cpp
    src/session/control_framer.cpp
    src/session/session_engine.cpp
    src/transport/asio_session.cpp
    src/transport/traffic_class.cpp
    src/protocol/advertisement.cpp
    src/protocol/control_codec.cpp
    src/protocol/media_codec.cpp
    src/protocol/text.cpp)
add_library(osplay::core ALIAS osplay_core)

target_include_directories(osplay_core
    PUBLIC include
    PRIVATE src)
target_compile_features(osplay_core PUBLIC cxx_std_20)
set_target_properties(osplay_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osplay_core PRIVATE osplay_monocypher osplay::warnings)

# Asio is header-only; SYSTEM keeps its headers out of Osplay's warning policy. The tests use it
# too (a recording relay), so it is an interface target rather than settings on osplay_core.
find_package(Threads REQUIRED)
add_library(osplay_asio INTERFACE)
target_include_directories(osplay_asio SYSTEM INTERFACE third_party/asio/include)
target_compile_definitions(osplay_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(osplay_asio INTERFACE Threads::Threads)
if(WIN32)
    target_compile_definitions(osplay_asio INTERFACE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(osplay_asio INTERFACE ws2_32 mswsock)
endif()
target_link_libraries(osplay_core PRIVATE osplay_asio)

if(OSPLAY_CORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/cpp)
endif()

add_subdirectory(fuzz)
