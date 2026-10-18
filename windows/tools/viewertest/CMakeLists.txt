add_executable(osplay_viewertest main.cpp)
target_compile_features(osplay_viewertest PRIVATE cxx_std_20)
target_compile_definitions(osplay_viewertest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_viewertest PRIVATE osplay_engine osplay::core osplay::warnings bcrypt)
set_target_properties(osplay_viewertest PROPERTIES
    OUTPUT_NAME OsplayViewerTest
    CXX_EXTENSIONS OFF)
