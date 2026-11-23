add_executable(osplay_audiotest main.cpp)
target_compile_features(osplay_audiotest PRIVATE cxx_std_20)
target_compile_definitions(osplay_audiotest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_audiotest PRIVATE osplay::warnings ole32)
set_target_properties(osplay_audiotest PROPERTIES
    OUTPUT_NAME OsplayAudioTest
    CXX_EXTENSIONS OFF)
