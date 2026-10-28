#!/bin/bash
# Renders every unit type's sprite sheet with Blender and installs them into
# game/web/sprites (see tools/blender/README.md).
#
#   tools/blender/build_all.sh [python] [work dir] [unit ...]
#
# python: a Python with the bpy module (default: $BPY_PYTHON or python3);
# work dir: where the cells are rendered (default build/sprites); units:
# names from tools/blender/sprites (default: all). Each type renders in its own
# process, so one failure does not stop the rest; a log per type is left in
# the work dir.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
py="${1:-${BPY_PYTHON:-python3}}"
work="${2:-$here/../../build/sprites}"
shift $(( $# > 2 ? 2 : $# ))
mkdir -p "$work"
units=("$@")
if [ ${#units[@]} -eq 0 ]; then
    mapfile -t units < <("$py" "$here/render_sheets.py" list 2>/dev/null | grep '^unit ' | cut -d' ' -f2)
fi
failed=()
for u in "${units[@]}"; do
    start=$(date +%s)
    if "$py" "$here/render_sheets.py" sheet "$u" "$work" > "$work/$u.log" 2>&1; then
        "$py" "$here/render_sheets.py" install "$u" "$work" >> "$work/$u.log" 2>&1
        echo "$u: $(( $(date +%s) - start )) s, $(grep -o '[0-9]* KB webp' "$work/$u.log" | tail -1)"
    else
        echo "$u: FAILED (see $work/$u.log)"
        failed+=("$u")
    fi
done
[ ${#failed[@]} -eq 0 ] || { echo "failed: ${failed[*]}"; exit 1; }
