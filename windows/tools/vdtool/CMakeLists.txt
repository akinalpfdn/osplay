add_executable(osplay_vdtool main.cpp)
target_compile_features(osplay_vdtool PRIVATE cxx_std_20)
target_link_libraries(osplay_vdtool PRIVATE osplay_engine osplay::warnings)
set_target_properties(osplay_vdtool PROPERTIES
    OUTPUT_NAME OsplayVdTool
    CXX_EXTENSIONS OFF)
