# Headless runner: prints the Ford scenario's checksum every second of game
# time. CI diffs native output against WebAssembly output to prove that both
# builds simulate identically.
add_executable(illyria_simcheck simcheck.cpp)
target_link_libraries(illyria_simcheck PRIVATE illyria_sim)
if(EMSCRIPTEN)
    # Flush stdout and propagate the exit code when run under node.
    target_link_options(illyria_simcheck PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
endif()

# Prints docs/UNITS.md from the locked unit sheet.
add_executable(illyria_unitsheet unitsheet.cpp)
target_link_libraries(illyria_unitsheet PRIVATE illyria_sim)

# Renders the continent top-down for docs/images/continent-map.png:
#   illyria_continentmap map.ppm map.json && python3 tools/continentmap/label.py map.ppm map.json out.png
add_executable(illyria_continentmap continentmap/continentmap.cpp)
target_link_libraries(illyria_continentmap PRIVATE illyria_sim)

# Dumps the renderer's shaders for offline checking (tools/shaders/check.sh).
add_executable(illyria_shaderdump shaders/shaderdump.cpp)

# Bakes the eroded relief, streams and pools into engine/sim/data (run by hand
# after changing the continent's design; the output is committed):
#   illyria_terrainbake engine/sim/data/illyria_relief.bin [preview.ppm]
add_executable(illyria_terrainbake terrainbake/bake.cpp)
target_link_libraries(illyria_terrainbake PRIVATE illyria_sim)
target_compile_options(illyria_terrainbake PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-conversion -Wno-sign-conversion -Wno-double-promotion>)

# Draws the soldiers' sprite sheets in software, for looking at the figures:
#   illyria_spritesheet out.ppm [type]   (no type: one row of samples per type)
add_executable(illyria_spritesheet spritesheet/preview.cpp)
target_link_libraries(illyria_spritesheet PRIVATE illyria_figures)
target_compile_options(illyria_spritesheet PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-conversion -Wno-sign-conversion -Wno-double-promotion>)

# Times the Conquest tick at 1.8k, 3.4k and 7k men in one World (prints ms a tick and the checksum):
#   illyria_tickbench [ticks] [men ...]
add_executable(illyria_tickbench tickbench.cpp)
target_link_libraries(illyria_tickbench PRIVATE illyria_sim)
if(EMSCRIPTEN)
    target_link_options(illyria_tickbench PRIVATE -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
endif()

# Audits the generated castles and the roads to them (see tools/terrain_audit.h):
#   illyria_terrainaudit [seed] [-v]
add_executable(illyria_terrainaudit terrainaudit.cpp)
target_link_libraries(illyria_terrainaudit PRIVATE illyria_sim)
target_include_directories(illyria_terrainaudit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Clumping in melee on open ground: spacing, men in reach, attackers on one target, the line's width, once a second,
# and the mirror match's win rate:
#   illyria_clumpcheck [scenario|all] [seed]       illyria_clumpcheck mirror [seeds]
add_executable(illyria_clumpcheck clumpcheck.cpp)
target_link_libraries(illyria_clumpcheck PRIVATE illyria_sim)
