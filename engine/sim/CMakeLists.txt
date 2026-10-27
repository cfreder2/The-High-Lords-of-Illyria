# Deterministic gameplay simulation. Must not depend on any engine, renderer,
# platform API or floating-point math: every peer has to produce bit-identical
# state from the same inputs (see docs/GAME_DESIGN.md §5).
add_library(illyria_sim
    src/fixed.cpp
    src/ballistics.cpp
    src/world.cpp
    src/scenario.cpp
    src/terrain.cpp
)
target_include_directories(illyria_sim PUBLIC include)
