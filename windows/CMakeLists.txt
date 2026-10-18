cmake_minimum_required(VERSION 3.28)

project(OsplayWindows
    DESCRIPTION "Osplay native engine and helper service for Windows"
    LANGUAGES CXX)

enable_testing()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core" core)
add_subdirectory(shared)
if(WIN32)
    add_subdirectory(platform)
endif()
add_subdirectory(engine)

if(WIN32)
    add_subdirectory(helper)
    add_subdirectory(tools/vdtool)
    add_subdirectory(tools/hosttest)

    # Put every executable and the engine DLL in one folder so tools find OsplayEngine.dll.
    foreach(target osplay_engine osplay_helper osplay_vdtool osplay_hosttest)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()
else()
    # Other hosts build the platform-independent libraries and tests only (the "portable" preset),
    # so protocol and logic changes are tested and linted on every platform.
    message(STATUS "Non-Windows host: building portable Windows libraries and tests only")
endif()
