# Windows-only helpers shared by the engine and the helper service.
add_library(osplay_win_platform STATIC
    src/vdd_device_lookup.cpp)
add_library(osplay::win_platform ALIAS osplay_win_platform)

target_include_directories(osplay_win_platform PUBLIC include)
target_compile_features(osplay_win_platform PUBLIC cxx_std_20)
target_compile_definitions(osplay_win_platform PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(osplay_win_platform PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(osplay_win_platform PUBLIC setupapi PRIVATE osplay::warnings)
