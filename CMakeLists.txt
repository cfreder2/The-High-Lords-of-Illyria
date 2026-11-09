cmake_minimum_required(VERSION 3.20)
project(Illyria LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ILLYRIA_BUILD_TESTS "Build unit tests" ON)
option(ILLYRIA_DEV_HOOKS "Compile the developer hooks for screenshots (the desktop --paths-demo); off in anything shipped" OFF)
option(ILLYRIA_BUILD_APP "Build the desktop game (illyria_app; fetches SDL and libwebp, which are C)" OFF)
if(ILLYRIA_BUILD_APP AND NOT EMSCRIPTEN)
    enable_language(C)
endif()

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "The simulation core needs __int128; build Windows with clang-cl or MinGW.")
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)

add_subdirectory(engine/sim)
add_subdirectory(game)
add_subdirectory(tools)

if(ILLYRIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
