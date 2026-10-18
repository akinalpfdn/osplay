# Platform-independent engine logic, linked into the DLL and its tests.
add_library(osplay_engine_logic STATIC
    src/display/helper_virtual_display_provider.cpp
    src/host/host_orchestrator.cpp
    src/host/stream_dump.cpp
    src/media/h264_stream.cpp
    src/media/pointer_shape.cpp)
target_include_directories(osplay_engine_logic PUBLIC src)
target_compile_features(osplay_engine_logic PUBLIC cxx_std_20)
set_target_properties(osplay_engine_logic PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osplay_engine_logic PUBLIC osplay::win_shared osplay::core PRIVATE osplay::warnings)

if(WIN32)
    add_library(osplay_engine SHARED
        src/capi/engine_capi.cpp
        src/capi/host_capi.cpp
        src/capture/duplication_pipeline.cpp
        src/capture/dxgi_output.cpp
        src/display/gdi_display_mode_applier.cpp
        src/display/pipe_helper_channel.cpp
        src/display/vdd_monitor.cpp
        src/encode/mf_h264_encoder.cpp
        src/encode/nv12_converter.cpp)

    target_include_directories(osplay_engine
        PUBLIC include
        PRIVATE src)
    target_compile_features(osplay_engine PRIVATE cxx_std_20)
    target_compile_definitions(osplay_engine PRIVATE OSPLAY_ENGINE_BUILDING UNICODE _UNICODE NOMINMAX
        WIN32_LEAN_AND_MEAN)
    target_link_libraries(osplay_engine
        PUBLIC osplay::core
        PRIVATE osplay_engine_logic osplay::win_platform osplay::warnings user32 ole32 bcrypt d3d11 dxgi
            mfplat mfuuid propsys winmm)
    set_target_properties(osplay_engine PROPERTIES
        OUTPUT_NAME OsplayEngine
        CXX_EXTENSIONS OFF)
endif()

if(OSPLAY_CORE_BUILD_TESTS)
    add_executable(osplay_engine_tests
        tests/frame_pacer_test.cpp
        tests/h264_stream_test.cpp
        tests/helper_virtual_display_provider_test.cpp
        tests/host_orchestrator_test.cpp
        tests/pointer_shape_test.cpp)
    target_link_libraries(osplay_engine_tests PRIVATE osplay_engine_logic osplay::warnings GTest::gtest_main)
    set_target_properties(osplay_engine_tests PROPERTIES CXX_EXTENSIONS OFF)
    include(GoogleTest)
    gtest_discover_tests(osplay_engine_tests DISCOVERY_MODE PRE_TEST)
endif()
