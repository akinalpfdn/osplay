include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Match the MSVC runtime used by the rest of the build instead of gtest's static default.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(osplay_core_tests
    capi_protocol_test.cpp
    protocol_codec_test.cpp
    protocol_fixture_test.cpp
    protocol_values_test.cpp
    version_test.cpp)
# Tests reach internal headers; only the C header is public API.
target_include_directories(osplay_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(osplay_core_tests PRIVATE
    OSPLAY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../protocol/fixtures")
target_link_libraries(osplay_core_tests PRIVATE osplay::core osplay::warnings GTest::gtest_main)
set_target_properties(osplay_core_tests PROPERTIES CXX_EXTENSIONS OFF)

include(GoogleTest)
gtest_discover_tests(osplay_core_tests DISCOVERY_MODE PRE_TEST)
