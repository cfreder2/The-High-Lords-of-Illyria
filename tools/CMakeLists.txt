# Headless runner: prints the Ford scenario's checksum every second of game
# time. CI diffs native output against WebAssembly output to prove that both
# builds simulate identically.
add_executable(illyria_simcheck simcheck.cpp)
target_link_libraries(illyria_simcheck PRIVATE illyria_sim)
if(EMSCRIPTEN)
    # Flush stdout and propagate the exit code when run under node.
    target_link_options(illyria_simcheck PRIVATE -sEXIT_RUNTIME=1)
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
