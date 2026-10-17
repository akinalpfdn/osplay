add_executable(osplay_helper
    src/main.cpp)

target_compile_features(osplay_helper PRIVATE cxx_std_20)
target_compile_definitions(osplay_helper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_helper PRIVATE osplay::warnings)
set_target_properties(osplay_helper PROPERTIES
    OUTPUT_NAME OsplayHelper
    CXX_EXTENSIONS OFF)
