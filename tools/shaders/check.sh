#!/bin/sh
# Compiles every renderer shader with glslangValidator (GLSL ES 3.00).
#   tools/shaders/check.sh build/tools/illyria_shaderdump
set -e
dump="$1"
dir=$(mktemp -d)
"$dump" "$dir"
status=0
for f in "$dir"/*.vert "$dir"/*.frag; do
    glslangValidator "$f" >"$dir/log" 2>&1 || { echo "== $(basename "$f")"; grep -E "ERROR|error" "$dir/log"; status=1; }
done
[ $status -eq 0 ] && echo "all shaders compile"
exit $status
