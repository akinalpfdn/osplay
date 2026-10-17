cmake_minimum_required(VERSION 3.28)

project(OsplayWindows
    DESCRIPTION "Osplay native engine and helper service for Windows"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The Osplay Windows native build targets Windows only.")
endif()

enable_testing()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core" core)
add_subdirectory(engine)
add_subdirectory(helper)
