add_executable(osplay_androidtest main.cpp)
target_compile_features(osplay_androidtest PRIVATE cxx_std_20)
target_compile_definitions(osplay_androidtest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_androidtest PRIVATE osplay_engine osplay::core osplay::warnings)
set_target_properties(osplay_androidtest PROPERTIES
    OUTPUT_NAME OsplayAndroidTest
    CXX_EXTENSIONS OFF)
