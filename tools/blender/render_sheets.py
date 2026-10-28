"""Pre-rendered sprite sheets for The High Lords of Illyria, made with Blender.

Each unit type is modelled, rigged and animated from scratch by the
scripts in sprites/ (no downloaded models), then rendered with Cycles from the
game's five views into a sheet the renderer can load in place of its runtime
bake (game/render/renderer.cpp, Renderer::bakeSheet; format in sprites/pack.py).

Install Blender as a Python module (Blender 5.1 for Python 3.13), in a
virtualenv of its own:

    python3.13 -m venv /tmp/bpyvenv
    /tmp/bpyvenv/bin/pip install bpy==5.1.2 pillow

Run headless (no Blender executable needed):

    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py list
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py sheet heavy_infantry build/sprites
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py sheet all build/sprites
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py look archons out.png [view,frame ...] [ppm]
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py pack archons build/sprites
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py install all build/sprites
    /tmp/bpyvenv/bin/python tools/blender/render_sheets.py blend archons out.blend

"sheet" writes <out>/<unit>/raw/ (every cell's picture and team mask at twice
the sheet's resolution), then <out>/<unit>/<unit>.png and .json (the packed
sheet, sprites/pack.py) and <unit>_color.png / <unit>_mask.png (the cells laid
out in rows: views down, the 26 frames across). "look" renders chosen frames
large, for working on a model; "pack" packs already rendered cells again;
"install" copies packed sheets into game/web/sprites/ (the site's sprites/,
with index.json naming each UnitType's sheet); "blend" saves the scene to
open in Blender.

Output is deterministic: fixed seeds everywhere (Cycles' seed, the models'
own generator), so the same Blender version gives the same pixels.
"list" prints every unit's name and UnitType number (build_all.sh renders them all).
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

from sprites import pack, render, units  # noqa: E402
from sprites import beasts, beasts2, roster, roster2  # noqa: E402,F401  (register the rest of the roster in units.TYPES)


def build(name):
    render.reset()
    number, f = units.TYPES[name]
    t = time.time()
    f()
    print('%s: built and keyed in %.1f s' % (name, time.time() - t))
    return number


def main(argv):
    if argv[:1] == ['list']:
        for n, (number, f) in sorted(units.TYPES.items(), key=lambda kv: kv[1][0]):
            print('unit %s %d' % (n, number))
        return 0
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, name = argv[0], argv[1]
    if cmd == 'sheet':
        out = argv[2]
        ppm = float(argv[3]) if len(argv) > 3 else None  # None: chosen per type (render.choose_ppm)
        names = sorted(units.TYPES) if name == 'all' else [name]
        for n in names:
            number = build(n)
            d = os.path.join(out, n)
            meta = render.render_cells(d, ppm=ppm, ss=2)
            pack.pack(d, n, number, meta)
        return 0
    if cmd == 'pack':  # again, from the cells already rendered into <out>/<unit>/raw
        import json
        d = os.path.join(argv[2], name)
        with open(os.path.join(d, 'raw', 'lattice.json')) as fh:
            meta = json.load(fh)
        pack.pack(d, name, units.TYPES[name][0], meta)
        return 0
    if cmd == 'install':  # the packed sheets into the game's site: <dest>/<unit>.webp, .json, index.json
        import json
        import shutil
        src, dest = argv[2], argv[3] if len(argv) > 3 else os.path.join(os.path.dirname(__file__), '..', '..', 'game', 'web', 'sprites')
        os.makedirs(dest, exist_ok=True)
        index_path = os.path.join(dest, 'index.json')
        index = json.load(open(index_path)) if os.path.exists(index_path) else {}
        for n in (sorted(units.TYPES) if name == 'all' else [name]):
            d = os.path.join(src, n)
            if not os.path.exists(os.path.join(d, n + '.webp')):
                continue
            for ext in ('.webp', '.json'):
                shutil.copyfile(os.path.join(d, n + ext), os.path.join(dest, n + ext))
            index[str(units.TYPES[n][0])] = n
        with open(index_path, 'w') as fh:
            json.dump(dict(sorted(index.items(), key=lambda kv: int(kv[0]))), fh, indent=1)
        print('installed', index)
        return 0
    if cmd == 'look':
        build(name)
        out = argv[2]
        cells = [tuple(int(x) for x in a.split(',')) for a in argv[3:] if ',' in a]
        rest = [a for a in argv[3:] if ',' not in a]
        ppm = float(rest[0]) if rest else 120.0
        cells = cells or [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0)]
        paths = []
        for k, (v, f) in enumerate(cells):
            p = out + '.%d.png' % k
            render.preview(p, v=v, frame=f + 1, ppm=ppm)
            paths.append(p)
        pack.tile(paths, out)
        for p in paths:
            os.remove(p)
        return 0
    if cmd == 'blend':
        build(name)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(argv[2]))
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
