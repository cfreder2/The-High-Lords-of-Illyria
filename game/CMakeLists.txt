# Game client logic shared by every platform: camera, gestures, battle flow
# and presentation state. Rendering and the platform layer live alongside.
add_library(illyria_game
    src/camera.cpp
    src/gestures.cpp
    src/battle.cpp
    src/battle_command.cpp
    src/battle_orders.cpp
    src/battle_capture.cpp
    src/voice.cpp
    src/sfx.cpp
    src/battle_deaths.cpp
    src/detail_land.cpp
)
target_include_directories(illyria_game PUBLIC include)
target_link_libraries(illyria_game PUBLIC illyria_sim)

# The soldiers' articulated figures (no GL): built into the sprite sheets by
# the renderer, previewed by tools/spritesheet and checked by the tests.
add_library(illyria_figures render/figures.cpp render/closeup.cpp)
target_include_directories(illyria_figures PUBLIC render)
target_link_libraries(illyria_figures PUBLIC illyria_game)
target_compile_options(illyria_figures PRIVATE -Wno-conversion)

if(EMSCRIPTEN)
    add_subdirectory(web)
endif()

# Presentation code mixes ints and floats freely; -Wconversion stays on for
# the simulation, where it matters.
target_compile_options(illyria_game PRIVATE -Wno-conversion)
