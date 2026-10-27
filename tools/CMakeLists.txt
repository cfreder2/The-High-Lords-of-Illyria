# Headless runner: prints the Ford scenario's checksum every second of game
# time. CI diffs native output against WebAssembly output to prove that both
# builds simulate identically.
add_executable(illyria_simcheck simcheck.cpp)
target_link_libraries(illyria_simcheck PRIVATE illyria_sim)
