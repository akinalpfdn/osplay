# Warning policy shared by every native Osplay target: warnings are errors on all compilers.
add_library(osplay_warnings INTERFACE)
add_library(osplay::warnings ALIAS osplay_warnings)

if(MSVC)
    target_compile_options(osplay_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(osplay_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Werror)
endif()
