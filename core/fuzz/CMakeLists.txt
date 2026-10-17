set(OSPLAY_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/fixtures")

# Two fuzz targets: the protocol decoders are fast, the cryptographic parsers do X25519 per input,
# so they run as separate binaries to keep the decoders' iteration rate high.
set(OSPLAY_FUZZ_TARGETS protocol crypto)

foreach(target IN LISTS OSPLAY_FUZZ_TARGETS)
    # Replays the fixtures through each entry point in every test run.
    if(OSPLAY_CORE_BUILD_TESTS)
        add_executable(osplay_${target}_fuzz_replay ${target}_fuzz.cpp replay_main.cpp)
        target_include_directories(osplay_${target}_fuzz_replay PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(osplay_${target}_fuzz_replay PRIVATE osplay::core osplay::warnings)
        set_target_properties(osplay_${target}_fuzz_replay PROPERTIES CXX_EXTENSIONS OFF)
        add_test(NAME ${target}_fuzz_replay COMMAND osplay_${target}_fuzz_replay ${OSPLAY_FIXTURES_DIR})
    endif()

    # libFuzzer binaries; the whole core is instrumented when OSPLAY_CORE_BUILD_FUZZER is on.
    if(OSPLAY_CORE_BUILD_FUZZER)
        add_executable(osplay_${target}_fuzzer ${target}_fuzz.cpp)
        target_include_directories(osplay_${target}_fuzzer PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(osplay_${target}_fuzzer PRIVATE osplay::core osplay::warnings)
        target_link_options(osplay_${target}_fuzzer PRIVATE -fsanitize=fuzzer)
        set_target_properties(osplay_${target}_fuzzer PROPERTIES CXX_EXTENSIONS OFF)
    endif()
endforeach()
