"""A unit's sprite cells from a rigged Tripo model, by its config (tools/sprites/tripo/units/<unit>.json).

    BPY=python-with-bpy  (python3.13 venv: bpy==5.1.2 pillow numpy scipy)
    $BPY -I tools/sprites/tripo/prep.py DELIVERED_DIR WORK          once per delivery: decimated models/<name>.blend, previews
    $BPY -I tools/sprites/tripo/unit.py build UNIT [--work WORK]     model + items + keyed clips -> WORK/units/UNIT/UNIT.blend, plan.json
    $BPY -I tools/sprites/tripo/unit.py look UNIT                    one frame of every clip, side and front, to check the poses
    $BPY -I tools/sprites/tripo/unit.py render UNIT [--only clip,..] [--views 0,..] [--test] [--art ART]
                                                                     -> ART/UNIT/<clip>/v<V>_f<NN>.png, _team.png, clips.json
    python3 tools/sprites/import.py ART --units UNIT                 packs both sheets, cuts the body parts, installs

tools/sprites/tripo_unit.sh UNIT runs build, render and the import in one go. WORK defaults to $TRIPO_WORK or
~/Downloads/tripo_work (models are large and not in the repository); ART to the repository's art/3d (gitignored).

The model keeps Tripo's Mixamo-named skeleton; the clips are keyed, not downloaded (styles.py, poses.py, keyer.py),
and fitted to the body (fit.py). Items are separate objects in a hand, dropped by physics when he dies. The team
mask is the texture's key blue. Cells are drawn as tools/sprites/import.py wants them: the 'man' canvas, 1024
square at 192 px a metre, the feet on (512, 560), which the importer reduces to the sheet (48) and the close-up (96)."""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'blender'))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import keyer  # noqa: E402
from keyer import B  # noqa: E402
import fit as fitting  # noqa: E402
import styles  # noqa: E402
from sprites import render as R  # noqa: E402

FPS = keyer.FPS
RES, PPM, ANCHOR_Y = 1024, 192.0, 560


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def load_config(unit):
    path = unit if unit.endswith('.json') else os.path.join(HERE, 'units', unit + '.json')
    with open(path) as fh:
        cfg = json.load(fh)
    cfg['work'] = os.path.expanduser(arg('--work') or os.environ.get('TRIPO_WORK') or '~/Downloads/tripo_work')
    cfg['dir'] = os.path.join(cfg['work'], 'units', cfg['unit'])
    cfg['blend'] = os.path.join(cfg['dir'], cfg['unit'] + '.blend')
    cfg['plan'] = os.path.join(cfg['dir'], 'plan.json')
    return cfg


def assign(ob, act):
    d = ob.animation_data or ob.animation_data_create()
    d.action = act
    if act is not None and getattr(d, 'action_slot', None) is None and getattr(act, 'slots', None):
        d.action_slot = act.slots[0]


# ======================================================================================================== build
def build(cfg):
    os.makedirs(cfg['dir'], exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=os.path.join(cfg['work'], 'models', cfg['body'] + '.blend'))
    scene = bpy.context.scene
    arm = next(o for o in scene.objects if o.type == 'ARMATURE')
    body = next(o for o in scene.objects if o.type == 'MESH' and o.parent == arm)
    body.name = 'body'
    root = bpy.data.objects['rig_root']
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.matrix_basis = Matrix()
    root.matrix_world = Matrix()
    bpy.context.view_layer.update()
    # Facing +x by his toes, his height as the config says, his feet on the origin.
    Bn = arm.data.bones
    fwd = sum(((Bn[B + s + 'ToeBase'].head_local - Bn[B + s + 'Foot'].head_local) for s in ('Left', 'Right')), Vector())
    fwd = arm.matrix_world.to_3x3() @ fwd
    pts = fitting.body_points(body)
    k = cfg['height_m'] / float(pts[:, 2].max() - pts[:, 2].min())
    root.matrix_world = Matrix.Rotation(-math.atan2(fwd.y, fwd.x), 4, 'Z') @ Matrix.Scale(k, 4)
    bpy.context.view_layer.update()
    pts = fitting.body_points(body)
    feet = (arm.matrix_world @ Bn[B + 'LeftFoot'].head_local + arm.matrix_world @ Bn[B + 'RightFoot'].head_local) / 2
    root.matrix_world = Matrix.Translation(Vector((-feet.x, -feet.y, -float(pts[:, 2].min())))) @ root.matrix_world
    bpy.context.view_layer.update()
    rig = keyer.Rig(arm)
    fit = fitting.Fit(rig, body)
    print('%s: %d tris, %.2f m tall, hips %.2f m; %s' % (cfg['unit'], sum(len(p.vertices) - 2 for p in body.data.polygons),
                                                          cfg['height_m'], rig.rest_head['Hips'].z, fit.describe()), flush=True)
    # Items: each a separate object in a hand.
    upright = bpy.data.objects.new('upright', None)   # an unturned frame for things that hang
    scene.collection.objects.link(upright)
    for it in cfg.get('items', []):
        with bpy.data.libraries.load(os.path.join(cfg['work'], 'models', it['model'] + '.blend'), link=False) as (src, dst):
            dst.objects = [n for n in src.objects if n.startswith('tripo_node')]
        ob = dst.objects[0]
        scene.collection.objects.link(ob)
        ob.name = it['name']
        ob.parent = None
        # Its mesh upright as delivered (y up in the file), its size as asked, its origin at the grip.
        me = ob.data
        me.transform(Matrix.Rotation(math.pi / 2, 4, 'X') @ Matrix.Scale(0.01, 4))
        co = np.empty(len(me.vertices) * 3)
        me.vertices.foreach_get('co', co)
        co = co.reshape(-1, 3)
        lo, hi = co.min(0), co.max(0)
        s = it['size_m'] / float((hi - lo).max())
        g = it.get('grip', [0.5, 0.5, 0.8])                      # where the hand holds it, as fractions of its box
        grip = Vector([lo[i] + (hi[i] - lo[i]) * g[i] for i in range(3)])
        me.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-grip))
        ob.matrix_world = Matrix()
        side = it.get('hand', 'Right')
        rig.rest()
        kn = rig.knuckles(side)
        fist = sum(kn, Vector()) / 4 + rig.palm(side) * it.get('palm_m', 0.03) + (kn[0] - kn[3]) * 0.0
        ob.parent, ob.parent_type, ob.parent_bone = arm, 'BONE', B + side + 'Hand'
        bpy.context.view_layer.update()
        ob.matrix_world = Matrix.Translation(fist)
        if it.get('hold', 'hang') == 'hang':
            c = ob.constraints.new('COPY_ROTATION')            # it hangs from the fist whatever the hand does
            c.target = upright
        ob['tripo_item'] = json.dumps(it)
        print('item %s: %.2f m, in the %s hand' % (it['name'], it['size_m'], side.lower()), flush=True)
    # The clips.
    P, clips = styles.STYLES[cfg['style']](cfg)
    posed = {}
    for name, pm in P.items():
        posed[name] = fit.pose(pm)
    plan = []
    for name, c in clips.items():
        gait = stride = None
        if 'gait' in c:
            g = dict(c['gait'])
            gait, stride = fit.gait(g)
        act = keyer.bake(rig, name, c['keys'], posed, gait=gait)
        act.name = name
        assign(arm, None)
        last = c['keys'][-1][0]
        entry = {'clip': name, 'action': name, 'kind': c['kind'], 'seconds': c.get('seconds', last)}
        if c['kind'] in ('loop', 'cycle'):
            n = c['frames']
            entry['frames'] = [1 + last * FPS * i / n for i in range(n)]
            entry['loop'] = True
            if stride:
                entry['stride'] = round(stride, 3)
        elif 'times' in c:
            entry['frames'] = [1 + round(t * FPS) for t in c['times']]
        else:
            n = c['frames']
            entry['frames'] = [round(1 + last * FPS * i / (n - 1)) for i in range(n)]
        for key in ('release', 'item_gone', 'drop_at'):
            if key in c:
                entry[key] = c[key]
        plan.append(entry)
        print('%s: %d action frames, %d drawn%s' % (name, int(act.frame_range[1]), len(entry['frames']),
                                                      ', stride %.2f m' % stride if stride else ''), flush=True)
    rig.rest()
    with open(cfg['plan'], 'w') as fh:
        json.dump(plan, fh, indent=1)
    bpy.ops.wm.save_as_mainfile(filepath=cfg['blend'], compress=True)
    print('saved', cfg['blend'], flush=True)


# ======================================================================================================= render
def mask_material(image, hue, name):
    """An emission pass: white where the texture is the key blue."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    if image is None:
        em.inputs['Color'].default_value = (0, 0, 0, 1)
        return m
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = image
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    sep.mode = 'HSV'
    nt.links.new(tex.outputs['Color'], sep.inputs['Color'])

    def math_node(op, a, value):
        n = nt.nodes.new('ShaderNodeMath')
        n.operation = op
        nt.links.new(a, n.inputs[0])
        if isinstance(value, float):
            n.inputs[1].default_value = value
        else:
            nt.links.new(value, n.inputs[1])
        return n.outputs[0]

    off = math_node('ABSOLUTE', math_node('SUBTRACT', sep.outputs[0], hue), 0.0)
    x = math_node('MULTIPLY', math_node('LESS_THAN', off, 0.07), math_node('GREATER_THAN', sep.outputs[1], 0.45))
    x = math_node('MULTIPLY', x, math_node('GREATER_THAN', sep.outputs[2], 0.10))
    nt.links.new(x, em.inputs['Color'])
    return m


def base_image(ob):
    for m in ob.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and any(l.to_socket.name == 'Base Color' for l in n.outputs[0].links):
                    return n.image
    return None


def open_scene(cfg):
    bpy.ops.wm.open_mainfile(filepath=cfg['blend'])
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    c = s.cycles
    c.device = 'CPU'
    if '--cpu' not in sys.argv:
        try:
            prefs = bpy.context.preferences.addons['cycles'].preferences
            prefs.compute_device_type = 'METAL'
            prefs.get_devices()
            for d in prefs.devices:
                d.use = d.type == 'METAL'
            c.device = 'GPU'
        except Exception as e:  # noqa: BLE001
            print('GPU unavailable:', e, flush=True)
    s.render.threads_mode = 'FIXED'
    s.render.threads = int(arg('--threads', '4'))
    c.seed = 0
    c.use_adaptive_sampling = True
    c.adaptive_threshold = 0.03
    c.max_bounces = 4
    s.render.film_transparent = True
    s.render.filter_size = 1.2
    s.view_settings.view_transform = 'Standard'
    s.view_settings.look = 'None'
    s.view_settings.exposure = cfg.get('exposure', 1.0)
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGBA'
    s.render.resolution_x = s.render.resolution_y = RES
    s.render.resolution_percentage = 100
    s.render.use_border = True
    s.render.use_crop_to_border = False
    R._world(s)
    s.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 2.2   # or steel goes black
    studio = R.Studio(s)
    for l, e in zip(studio.lights, (4.5, 1.4, 3.0)):
        l.data.energy = e
    return s, studio


def render(cfg, look=False):
    with open(cfg['plan']) as fh:
        plan = json.load(fh)
    only = set(arg('--only').split(',')) if arg('--only') else None
    views = [int(v) for v in arg('--views').split(',')] if arg('--views') else list(range(5))
    test = '--test' in sys.argv
    art = arg('--art') or os.path.join(ROOT, 'art', '3d')
    if look:
        art, views = os.path.join(cfg['dir'], 'look'), [1, 0] if not arg('--views') else views
    s, studio = open_scene(cfg)
    c = s.cycles
    root = bpy.data.objects['rig_root']
    arm = next(o for o in s.objects if o.type == 'ARMATURE')
    body = bpy.data.objects['body']
    items = [o for o in s.objects if o.type == 'MESH' and 'tripo_item' in o]
    base = root.matrix_world.copy()
    hue = cfg.get('team_hue', 0.62)
    masks = {body.name: mask_material(base_image(body), hue, 'team_mask')}
    black = mask_material(None, hue, 'mask_black')
    for it in items:
        masks[it.name] = mask_material(base_image(it), hue, 'team_' + it.name) if json.loads(it['tripo_item']).get('team') else black

    # Each item's falling twin, and the ground it falls on.
    bpy.ops.rigidbody.world_add()
    rbw = s.rigidbody_world
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, 0))
    ground = bpy.context.active_object
    ground.hide_render = True
    bpy.ops.rigidbody.object_add(type='PASSIVE')
    ground.rigid_body.friction = 0.9
    twins = {}
    for it in items:
        o = it.copy()
        o.data = it.data
        for con in list(o.constraints):
            o.constraints.remove(con)
        o.parent = None
        o.name = 'free_' + it.name
        s.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'
        bpy.ops.object.select_all(action='DESELECT')
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
        bpy.ops.rigidbody.object_add(type='ACTIVE')
        rb = o.rigid_body
        rb.collision_shape = 'CONVEX_HULL'
        rb.mass, rb.friction, rb.restitution = 1.5, 0.9, 0.15
        rb.linear_damping, rb.angular_damping = 0.1, 0.4
        o.hide_render = True
        masks[o.name] = masks[it.name]
        twins[it.name] = o
    drawn = [body] + items + list(twins.values())
    saved = {o.name: [sl.material for sl in o.material_slots] for o in drawn}

    def masked(on):
        for o in drawn:
            for i, sl in enumerate(o.material_slots):
                sl.material = masks[o.name] if on else saved[o.name][i]

    def hips():
        return arm.matrix_world @ arm.pose.bones[B + 'Hips'].head

    def place(corr):
        root.matrix_world = Matrix.Translation(Vector((-corr.x, -corr.y, 0))) @ base
        bpy.context.view_layer.update()

    def goto(f):
        whole = int(math.floor(f + 1e-6))
        s.frame_set(whole, subframe=max(0.0, f - whole))

    def border(v):
        """The render border round what is drawn in this view, on the canvas."""
        right, up, _ = R.view(v)
        pts = np.concatenate([fitting.body_points(o) for o in drawn if not o.hide_render])
        u, w = pts @ np.array(right), pts @ np.array(up)
        cv = (ANCHOR_Y - RES / 2) / PPM
        x0, x1 = RES / 2 + u.min() * PPM - 8, RES / 2 + u.max() * PPM + 8
        y0, y1 = RES / 2 + (w.min() - cv) * PPM - 8, RES / 2 + (w.max() - cv) * PPM + 8
        if x0 < 0 or y0 < 0 or x1 > RES or y1 > RES:
            print('  WARNING: view %d leaves the canvas (%.0f..%.0f, %.0f..%.0f)' % (v, x0, x1, y0, y1), flush=True)
        clamp = lambda a: min(1.0, max(0.0, a / RES))
        s.render.border_min_x, s.render.border_max_x = clamp(x0), clamp(x1)
        s.render.border_min_y, s.render.border_max_y = clamp(y0), clamp(y1)

    def shoot(d, j):
        for v in views:
            studio.aim(v, 0.0, (ANCHOR_Y - RES / 2) / PPM, RES / PPM)
            border(v)
            c.samples, c.use_denoising = int(arg('--samples', '32')), True
            s.render.filepath = '%s/v%d_f%02d.png' % (d, v, j)
            bpy.ops.render.render(write_still=True)
            if look:
                continue
            masked(True)
            c.samples, c.use_denoising = 8, False
            s.render.filepath = '%s/v%d_f%02d_team.png' % (d, v, j)
            bpy.ops.render.render(write_still=True)
            masked(False)

    clips = []
    for e in plan:
        clip, kind, frames = e['clip'], e['kind'], e['frames']
        if only and clip not in only:
            continue
        act = bpy.data.actions[e['action']]
        assign(arm, act)
        a, b = act.frame_range
        if test:
            frames = frames[:1]
        if look:
            frames = frames[::2] if len(frames) > 4 else frames
        d = os.path.join(art, cfg['unit'], clip)
        os.makedirs(d, exist_ok=True)
        root.matrix_world = base
        bpy.context.view_layer.update()
        first = frames[0]
        goto(first)
        h0 = hips()
        release = None
        gone_at = 1 + e['item_gone'] * FPS if 'item_gone' in e else None
        if kind == 'death' and items:
            rbw.enabled = True
            stand = h0.z
            held = {it.name: {} for it in items}
            for f in range(int(a), int(b) + 1):
                goto(f)
                place(h0)
                for it in items:
                    held[it.name][f] = it.matrix_world.copy()
                if release is None and ((e.get('drop_at') and f >= e['drop_at']) or (f > a + 3 and hips().z < 0.8 * stand)):
                    release = f
            release = release or int(a + (b - a) // 3)
            for it in items:
                free = twins[it.name]
                free.animation_data_clear()
                for f in range(int(a), release + 1):
                    # Still for its last moments in the hand: let go, it drops; it is not flung by the arm's swing.
                    loc, rot, sca = held[it.name][release if f >= release - 2 else f].decompose()
                    free.location, free.rotation_quaternion, free.scale = loc, rot, sca
                    free.keyframe_insert('location', frame=f)
                    free.keyframe_insert('rotation_quaternion', frame=f)
                    free.keyframe_insert('scale', frame=f)
                    free.rigid_body.kinematic = f < release
                    free.rigid_body.keyframe_insert('kinematic', frame=f)
            rbw.point_cache.frame_start, rbw.point_cache.frame_end = int(a), int(b)
            s.frame_start, s.frame_end = int(a), int(b)
            steps = [float(f) for f in range(int(a), int(b) + 1)]   # physics must be stepped through
        else:
            rbw.enabled = False
            steps = frames
        for f in steps:
            goto(f)
            place(h0)
            dropped = release is not None and f >= release
            for it in items:
                it.hide_render = dropped or (gone_at is not None and f >= gone_at - 1e-6 and f < b - 0.5)
                twins[it.name].hide_render = not dropped
            hit = [k for k, x in enumerate(frames) if abs(f - x) < 1e-6]
            if hit:
                shoot(d, hit[0])
                print('%s f%02d (action frame %.2f)%s' % (clip, hit[0], f, ' item on the ground' if dropped else ''), flush=True)
        for it in items:
            it.hide_render = False
            twins[it.name].hide_render = True
        entry = {'name': clip, 'frames': len(frames)}
        if kind in ('loop', 'cycle'):
            entry['seconds'] = round(e['seconds'], 4)
            entry['loop'] = True
            if 'stride' in e:
                entry['stride'] = e['stride']
        else:
            times = [round((f - 1) / FPS, 4) for f in frames]
            entry['seconds'] = round(max(e['seconds'], times[-1] + 1 / FPS), 4)   # the last frame shows a 24th at least
            entry['times'] = times
        if 'release' in e:
            entry['release'] = round(e['release'], 4)
        clips.append(entry)
    if look:
        sheet_look(os.path.join(art, cfg['unit']), plan, views)
    elif not only and not test:
        with open(os.path.join(art, cfg['unit'], 'clips.json'), 'w') as fh:
            json.dump({'clips': clips}, fh, indent=1)
        print('wrote clips.json', flush=True)
    else:
        print(json.dumps(clips), flush=True)


def sheet_look(d, plan, views):
    """The look frames on one page: a row per clip and view."""
    from PIL import Image, ImageDraw
    rows = []
    for e in plan:
        for v in views:
            files = sorted(f for f in os.listdir(os.path.join(d, e['clip'])) if f.startswith('v%d_' % v) and '_team' not in f)
            if files:
                rows.append((e['clip'] + ' v%d' % v, [os.path.join(d, e['clip'], f) for f in files]))
    if not rows:
        return
    cw, ch = 300, 300
    box = (512 - 250, 560 - 420, 512 + 250, 560 + 80)
    page = Image.new('RGB', (cw * max(len(r[1]) for r in rows), ch * len(rows)), (150, 150, 150))
    dr = ImageDraw.Draw(page)
    for y, (label, files) in enumerate(rows):
        for x, f in enumerate(files):
            im = Image.open(f).convert('RGBA').crop(box).resize((cw, ch))
            page.paste(im, (x * cw, y * ch), im)
        dr.text((4, y * ch + 4), label, fill=(255, 255, 0))
    page.save(os.path.join(d, 'look.png'))
    print('look:', os.path.join(d, 'look.png'), flush=True)


if __name__ == '__main__':
    cmd, unit = sys.argv[1], sys.argv[2]
    cfg = load_config(unit)
    if cmd == 'build':
        build(cfg)
    elif cmd == 'look':
        render(cfg, look=True)
    elif cmd == 'render':
        render(cfg)
    else:
        sys.exit(__doc__)
