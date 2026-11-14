# The desktop game: a window (SDL 3), OpenGL 4.1 core, the mixer and the files
# on disk, around the same game loop and renderer the browser build runs
# (game/app/platform.h is the seam). macOS gets an .app bundle; Linux and Windows
# a program with an assets/ folder beside it.
include(FetchContent)

# SDL: an installed SDL3 if there is one, else the pinned release, built static.
find_package(SDL3 3.2 QUIET CONFIG)
if(NOT SDL3_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.20
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# libwebp decodes the sprite sheets and model textures (they are .webp, the browser's own format).
find_package(WebP QUIET)
if(NOT TARGET WebP::webp AND NOT TARGET webp)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libwebp
        GIT_REPOSITORY https://github.com/webmproject/libwebp.git
        GIT_TAG v1.6.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libwebp)
endif()
if(TARGET WebP::webp)
    set(ILLYRIA_WEBP WebP::webp)
else()
    set(ILLYRIA_WEBP webp)
endif()

# OpenGL 4.1 core functions (glad, generated: gl:core=4.1, no extensions).
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(illyria_runtime PUBLIC glad)

set(ILLYRIA_APP_SOURCES
    platform_sdl.cpp
    input.cpp
    overlay.cpp
    hud.cpp
    audio.cpp
    assets.cpp
)

if(APPLE)
    add_executable(illyria_app MACOSX_BUNDLE ${ILLYRIA_APP_SOURCES})
    set(ILLYRIA_ASSET_DIR "$<TARGET_BUNDLE_CONTENT_DIR:illyria_app>/Resources")
    set_target_properties(illyria_app PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "High Lords of Illyria"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.cfreder2.illyria"
        MACOSX_BUNDLE_BUNDLE_VERSION "${ILLYRIA_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${ILLYRIA_VERSION}")
    target_link_libraries(illyria_app PRIVATE "-framework OpenGL")
else()
    add_executable(illyria_app ${ILLYRIA_APP_SOURCES})
    set(ILLYRIA_ASSET_DIR "$<TARGET_FILE_DIR:illyria_app>/assets")
endif()
set_target_properties(illyria_app PROPERTIES OUTPUT_NAME "Illyria")
target_link_libraries(illyria_app PRIVATE illyria_runtime SDL3::SDL3 ${ILLYRIA_WEBP})
target_compile_options(illyria_app PRIVATE -Wno-conversion)
if(ILLYRIA_DEV_HOOKS)
    target_compile_definitions(illyria_app PRIVATE ILLYRIA_DEV_HOOKS)
    # --play (docs/PLAYTEST.md): the scripted player, in the developer builds only.
    target_sources(illyria_app PRIVATE playrun.cpp playscript.cpp)
endif()
# Run from the source tree without copying: the folders the browser build serves.
target_compile_definitions(illyria_app PRIVATE ILLYRIA_VERSION="${ILLYRIA_VERSION}" ILLYRIA_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/../web")

# The same files the browser fetches: sprite sheets, models, sound effects (game/web/CMakeLists.txt).
add_custom_command(TARGET illyria_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ILLYRIA_ASSET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../web/sprites" "${ILLYRIA_ASSET_DIR}/sprites"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../web/models" "${ILLYRIA_ASSET_DIR}/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../web/sfx" "${ILLYRIA_ASSET_DIR}/sfx"
)
