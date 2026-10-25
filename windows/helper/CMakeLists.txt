add_executable(osplay_helper
    src/main.cpp
    src/console_user.cpp
    src/file_log.cpp
    src/pipe_server.cpp
    src/service_host.cpp
    src/service_installer.cpp
    src/vdd_device.cpp
    src/vdd_settings_file.cpp
    src/vdd_setup.cpp)

target_compile_features(osplay_helper PRIVATE cxx_std_20)
target_compile_definitions(osplay_helper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(osplay_helper PRIVATE
    osplay::win_shared osplay::win_platform osplay::warnings
    advapi32 cfgmgr32 newdev ole32 setupapi shell32 wtsapi32)
set_target_properties(osplay_helper PROPERTIES
    OUTPUT_NAME OsplayHelper
    CXX_EXTENSIONS OFF)
