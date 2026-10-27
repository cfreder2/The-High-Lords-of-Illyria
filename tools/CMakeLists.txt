# Headless runner: prints the Ford scenario's checksum every second of game
# time. CI diffs native output against WebAssembly output to prove that both
# builds simulate identically.
add_executable(illyria_simcheck simcheck.cpp)
target_link_libraries(illyria_simcheck PRIVATE illyria_sim)
if(EMSCRIPTEN)
    # Flush stdout and propagate the exit code when run under node.
    target_link_options(illyria_simcheck PRIVATE -sEXIT_RUNTIME=1)
endif()
