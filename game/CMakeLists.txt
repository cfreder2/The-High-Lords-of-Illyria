# Game client logic shared by every platform: camera, gestures, battle flow
# and presentation state. Rendering and the platform layer live alongside.
add_library(illyria_game
    src/camera.cpp
    src/gestures.cpp
    src/battle.cpp
)
target_include_directories(illyria_game PUBLIC include)
target_link_libraries(illyria_game PUBLIC illyria_sim)

if(EMSCRIPTEN)
    add_subdirectory(web)
endif()

# Presentation code mixes ints and floats freely; -Wconversion stays on for
# the simulation, where it matters.
target_compile_options(illyria_game PRIVATE -Wno-conversion)
