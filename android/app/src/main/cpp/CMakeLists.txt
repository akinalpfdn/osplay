cmake_minimum_required(VERSION 3.28)
project(OsplayAndroid LANGUAGES C CXX)

# The shared core, built for the Android ABI; the JNI bridge is the only code on top of it.
set(OSPLAY_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core core)

add_library(osplay_jni SHARED osplay_jni.cpp)
target_compile_features(osplay_jni PRIVATE cxx_std_20)
target_compile_options(osplay_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(osplay_jni PRIVATE osplay::core android log)
