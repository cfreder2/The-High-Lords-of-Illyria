#!/bin/sh
# One unit from its Tripo model to the game's sheets (tools/sprites/tripo/unit.py has the steps and the config):
#
#   tools/sprites/tripo_unit.sh UNIT [BPY_PYTHON]
#
# UNIT names tools/sprites/tripo/units/UNIT.json. BPY_PYTHON is a Python 3.13 with bpy==5.1.2, pillow, numpy and
# scipy (default $BPY, else ~/Downloads/tripo_work/venv/bin/python). The delivered models must have been through
# tools/sprites/tripo/prep.py once (WORK/models/*.blend; WORK is $TRIPO_WORK or ~/Downloads/tripo_work).
# Builds the scene, renders every cell (about 4 minutes for a man-sized unit on the GPU), writes contact sheets
# to WORK/units/UNIT/contact, paints the burning death, packs both sheets with the body parts, and removes the
# rendered cells (KEEP_ART=1 keeps them, in WORK/art).
set -e
UNIT="$1"
PY="${2:-${BPY:-$HOME/Downloads/tripo_work/venv/bin/python}}"
WORK="${TRIPO_WORK:-$HOME/Downloads/tripo_work}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ART="$WORK/art"
[ -n "$UNIT" ] || { echo "usage: $0 UNIT [BPY_PYTHON]"; exit 2; }
FREE=$(df -k / | tail -1 | awk '{print int($4 / 1024)}')
[ "$FREE" -gt 1500 ] || { echo "only $FREE MB free: a render needs about 400 MB"; exit 1; }
"$PY" -I "$HERE/tripo/unit.py" build "$UNIT" --work "$WORK"
rm -rf "$ART/$UNIT"
"$PY" -I "$HERE/tripo/unit.py" render "$UNIT" --work "$WORK" --art "$ART" > "$WORK/units/$UNIT/render.log" 2>&1 || { tail -20 "$WORK/units/$UNIT/render.log"; exit 1; }
grep WARNING "$WORK/units/$UNIT/render.log" | sort | uniq -c || true
"$PY" "$HERE/tripo/contact.py" "$ART/$UNIT" "$WORK/units/$UNIT/contact"
PPM=$("$PY" -c "import sys, json; sys.path.insert(0, '$HERE'); import layout; print(layout.unit_class('$UNIT')['ppm'] * json.load(open('$ART/$UNIT/clips.json')).get('scale', 1))")
"$PY" "$HERE/tripo/burn_paint.py" "$ART/$UNIT/die_burn" --ppm "$PPM" > /dev/null
rm -rf "$ART/$UNIT/die_burn/unburnt"
"$PY" "$HERE/import.py" "$ART" --units "$UNIT"
"$PY" "$HERE/tripo/colours.py" "$HERE/../../game/web/sprites" "$UNIT" "$WORK/units/$UNIT/contact/${UNIT}_both_sides.png"
[ -n "$KEEP_ART" ] || rm -rf "$ART/$UNIT"
echo "$UNIT installed in game/web/sprites (a built app copies them on its next link; or copy them into its Resources/sprites)"
