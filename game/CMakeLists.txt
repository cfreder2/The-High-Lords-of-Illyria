# Game client logic shared by every platform: camera, gestures, battle flow
# and presentation state. Rendering and the platform layer live alongside.
add_library(illyria_game
    src/camera.cpp
    src/gestures.cpp
    src/battle.cpp
    src/battle_command.cpp
    src/battle_conquest.cpp
    src/battle_orders.cpp
    src/path_lines.cpp
    src/battle_capture.cpp
    src/battle_modes.cpp
    src/flag_poles.cpp
    src/mode.cpp
    src/mode_places.cpp
    src/mode_horde.cpp
    src/mode_ctf.cpp
    src/mode_sandbox.cpp
    src/mode_setup.cpp
    src/voice.cpp
    src/sfx.cpp
    src/battle_deaths.cpp
    src/minimap.cpp
    src/palette.cpp
    src/detail_land.cpp
    src/far_forest.cpp
    src/river_ribbons.cpp
    src/underwater.cpp
    src/underwater_audio.cpp
)
target_include_directories(illyria_game PUBLIC include)
target_link_libraries(illyria_game PUBLIC illyria_sim)

# The soldiers' articulated figures (no GL): built into the sprite sheets by
# the renderer, previewed by tools/spritesheet and checked by the tests; and
# the renderer's other pure parts (close-up sheets, sheets' own clips, what a
# soldier is seen doing).
add_library(illyria_figures render/figures.cpp render/closeup.cpp render/sprite_clips.cpp render/unit_deeds.cpp)
target_include_directories(illyria_figures PUBLIC render)
target_link_libraries(illyria_figures PUBLIC illyria_game)
target_compile_options(illyria_figures PRIVATE -Wno-conversion)

# The game loop, renderer and asset loaders every platform runs. app/platform.h is the one seam to the
# machine: the browser's side is web/, the desktop's native/.
set(ILLYRIA_VERSION "0.0.0-dev" CACHE STRING "Version shown in the game")
if(EMSCRIPTEN OR ILLYRIA_BUILD_APP)
    add_library(illyria_runtime OBJECT
        app/app.cpp
        app/sheets.cpp
        app/models.cpp
        render/renderer.cpp
        render/renderer_detail.cpp
        render/renderer_sheets.cpp
        render/renderer_orders.cpp
        render/renderer_flags.cpp
        render/renderer_map.cpp
        render/renderer_gibs.cpp
        render/renderer_models.cpp
        render/renderer_trees.cpp
        render/renderer_post.cpp
        render/meshes.cpp
    )
    target_link_libraries(illyria_runtime PUBLIC illyria_game illyria_figures)
    target_compile_definitions(illyria_runtime PRIVATE ILLYRIA_VERSION="${ILLYRIA_VERSION}")
    target_compile_options(illyria_runtime PRIVATE -Wno-conversion)
    if(ILLYRIA_DEV_HOOKS)
        target_compile_definitions(illyria_runtime PRIVATE ILLYRIA_DEV_HOOKS)
    endif()
endif()
if(EMSCRIPTEN)
    add_subdirectory(web)
elseif(ILLYRIA_BUILD_APP)
    add_subdirectory(native)
endif()

# Presentation code mixes ints and floats freely; -Wconversion stays on for
# the simulation, where it matters.
target_compile_options(illyria_game PRIVATE -Wno-conversion)
