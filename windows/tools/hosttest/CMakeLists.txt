add_executable(osplay_hosttest main.cpp)
target_compile_features(osplay_hosttest PRIVATE cxx_std_20)
target_compile_definitions(osplay_hosttest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_hosttest PRIVATE osplay_engine osplay::core osplay::warnings bcrypt dwmapi user32 gdi32)
set_target_properties(osplay_hosttest PROPERTIES
    OUTPUT_NAME OsplayHostTest
    CXX_EXTENSIONS OFF)
