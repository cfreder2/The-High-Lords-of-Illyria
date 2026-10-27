# Deterministic core (`4daefce`)

`tests.txt` is the unit-test run and `simcheck-trace.txt` is the "Ford" benchmark:
six archers volley ten footmen through a crosswind. It prints a checksum of the
whole simulation every second of game time. CI diffs this trace between Linux
GCC, Linux Clang, macOS ARM and WebAssembly; any difference fails the build.

Rebuilt from the commit for this devlog (native GCC, Release).
