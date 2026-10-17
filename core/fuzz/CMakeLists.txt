set(OSPLAY_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/fixtures")

# Replays the fixtures through the fuzz entry point in every test run.
if(OSPLAY_CORE_BUILD_TESTS)
    add_executable(osplay_protocol_fuzz_replay protocol_fuzz.cpp replay_main.cpp)
    target_include_directories(osplay_protocol_fuzz_replay PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(osplay_protocol_fuzz_replay PRIVATE osplay::core osplay::warnings)
    set_target_properties(osplay_protocol_fuzz_replay PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME protocol_fuzz_replay COMMAND osplay_protocol_fuzz_replay ${OSPLAY_FIXTURES_DIR})
endif()

# libFuzzer binary; the whole core is instrumented when OSPLAY_CORE_BUILD_FUZZER is on.
if(OSPLAY_CORE_BUILD_FUZZER)
    add_executable(osplay_protocol_fuzzer protocol_fuzz.cpp)
    target_include_directories(osplay_protocol_fuzzer PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(osplay_protocol_fuzzer PRIVATE osplay::core osplay::warnings)
    target_link_options(osplay_protocol_fuzzer PRIVATE -fsanitize=fuzzer)
    set_target_properties(osplay_protocol_fuzzer PROPERTIES CXX_EXTENSIONS OFF)
endif()
