# Platform-independent pieces of the Windows build (protocol and file formats), kept free of
# Windows headers so they can be unit tested anywhere.
add_library(osplay_win_shared STATIC
    src/helper_protocol.cpp
    src/helper_request_handler.cpp
    src/vdd_settings.cpp)
add_library(osplay::win_shared ALIAS osplay_win_shared)

target_include_directories(osplay_win_shared PUBLIC include)
target_compile_features(osplay_win_shared PUBLIC cxx_std_20)
set_target_properties(osplay_win_shared PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osplay_win_shared PRIVATE osplay::warnings)

if(OSPLAY_CORE_BUILD_TESTS)
    add_executable(osplay_win_shared_tests
        tests/helper_protocol_test.cpp
        tests/helper_request_handler_test.cpp
        tests/vdd_settings_test.cpp)
    target_link_libraries(osplay_win_shared_tests PRIVATE osplay::win_shared osplay::warnings GTest::gtest_main)
    set_target_properties(osplay_win_shared_tests PROPERTIES CXX_EXTENSIONS OFF)
    include(GoogleTest)
    gtest_discover_tests(osplay_win_shared_tests DISCOVERY_MODE PRE_TEST)
endif()
