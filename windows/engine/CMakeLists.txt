add_library(osplay_engine SHARED
    src/capi/engine_capi.cpp)

target_include_directories(osplay_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(osplay_engine PRIVATE cxx_std_20)
target_compile_definitions(osplay_engine PRIVATE OSPLAY_ENGINE_BUILDING UNICODE _UNICODE NOMINMAX
    WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_engine PRIVATE osplay::core osplay::warnings)
set_target_properties(osplay_engine PROPERTIES
    OUTPUT_NAME OsplayEngine
    CXX_EXTENSIONS OFF)
