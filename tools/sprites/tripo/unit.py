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
sys.path.insert(0, os.path.join(ROOT, 'tools', 'sprites'))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import keyer  # noqa: E402
from keyer import B  # noqa: E402
import fit as fitting  # noqa: E402
import styles  # noqa: E402
from sprites import render as R  # noqa: E402

FPS = keyer.FPS
import layout  # noqa: E402  (tools/sprites: the size classes' canvases)


def canvas_of(cfg):
    """The unit's canvas (w, h), pixels a metre and anchor, its class's times the config's "scale"."""
    c = layout.unit_class(cfg['unit'])
    k = cfg.get('scale', 1)
    return int(c['canvas'][0] * k), int(c['canvas'][1] * k), c['ppm'] * k, c['anchor'][0] * k, c['anchor'][1] * k


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
def attach_item(it, cfg, scene, arm, rig, upright):
    """An item of the config into a hand (hang, grip) or onto the forearm (strap), a separate object."""
    with bpy.data.libraries.load(os.path.join(cfg['work'], 'models', it['model'] + '.blend'), link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n.startswith('tripo_node')]
    ob = dst.objects[0]
    scene.collection.objects.link(ob)
    ob.name = it['name']
    ob.parent = None
    # Its mesh upright as delivered (y up in the file), its size as asked.
    me = ob.data
    me.transform(Matrix.Rotation(math.pi / 2, 4, 'X') @ Matrix.Scale(0.01, 4))
    if it.get('tip') == 'down':                              # swords are delivered point down: the point is +z here
        me.transform(Matrix.Rotation(math.pi, 4, 'X'))
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(0), co.max(0)
    s = it['size_m'] / float((hi - lo).max())
    hold = it.get('hold', 'hang')
    side = it.get('hand', 'Right')
    rig.rest()
    if hold == 'strap':
        # A shield, strapped across the forearm (keyer.strap_shield): its mesh about its own middle.
        mid = Vector((lo + hi) / 2)
        me.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-mid))
        if it.get('front') == 'back':
            me.transform(Matrix.Rotation(math.pi, 4, 'Z'))
        ob.matrix_world = Matrix()
        ob.parent, ob.parent_type, ob.parent_bone = arm, 'BONE', B + 'LeftForeArm'
        bpy.context.view_layer.update()
        rig.shield = ob
        rig.strap_shield()
    else:
        g = it.get('grip', [0.5, 0.5, 0.8])                  # where the hand holds it, as fractions of its box
        grip = Vector([lo[i] + (hi[i] - lo[i]) * g[i] for i in range(3)])
        me.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-grip))
        ob.matrix_world = Matrix()
        ob.parent, ob.parent_type, ob.parent_bone = arm, 'BONE', B + side + 'Hand'
        bpy.context.view_layer.update()
        fist = rig.fist(side)
        if hold == 'hang':
            ob.matrix_world = Matrix.Translation(fist)
            c = ob.constraints.new('COPY_ROTATION')        # it hangs from the fist whatever the hand does
            c.target = upright
        else:
            # Gripped: its length along the knuckles, the point out past the forefinger, its broad side (a
            # blade's edge) where the fingers point.
            kn = rig.knuckles(side)
            along = (kn[0] - kn[3]).normalized()
            fingers = (rig.head(side + 'HandMiddle1') - rig.head(side + 'Hand')).normalized()
            edge = (fingers - along * along.dot(fingers)).normalized()
            flat = along.cross(edge)
            wide = 0 if (hi[0] - lo[0]) >= (hi[1] - lo[1]) else 1   # the mesh's broad horizontal axis
            cols = [None, None, along]
            cols[wide] = edge
            cols[1 - wide] = flat if wide == 1 else -flat
            frame = Matrix(cols).transposed()
            if frame.determinant() < 0:
                cols[1 - wide] = -cols[1 - wide]
                frame = Matrix(cols).transposed()
            ob.matrix_world = Matrix.Translation(fist) @ frame.to_4x4()
            if side == 'Right':
                rig.sword = ob
                hand = rig.rot('RightHand').inverted()
                rig.blade_local, rig.flat_local = hand @ along, hand @ flat
    bpy.context.view_layer.update()
    ob['tripo_item'] = json.dumps(it)
    print('item %s: %.2f m, in the %s hand' % (it['name'], it['size_m'], side.lower()), flush=True)


def swing_items(scene, e):
    """In a clip marked `swing` (a throw) a thing that hangs from the fist goes with the hand instead: swung, it
    stands out from the hand like more of the arm; hanging plumb it would lie through the arm that swings it."""
    for o in scene.objects:
        if o.type == 'MESH' and 'tripo_item' in o:
            for con in o.constraints:
                con.influence = 0.0 if e.get('swing') else 1.0


def clear_weapons(cfg, scene, rig, arm, bodies, plan, ref_bone=None, stand_arm=None):
    """Every frame of every clip checked for a weapon or shield through the body (clearance.py), turned clear where
    it is not; the table written beside the blend and printed; the build fails if anything is left."""
    import clearance
    items = [o for o in scene.objects if o.type == 'MESH' and 'tripo_item' in o]
    if rig is None:
        scene['tripo_gate'] = 'passed'      # no figure of its own to check (a mount alone)
        return
    prefix = arm.get('clip_prefix', '')
    main = stand_arm or arm
    bone = ref_bone or B + 'Hips'
    rows, wrists = [], []
    for e in plan:
        act = bpy.data.actions.get(prefix + e['action'])
        if act is None:
            continue
        a, b = (int(x) for x in act.frame_range)
        until = {}
        swing_items(scene, e)
        if 'item_gone' in e:
            for it in items:
                if json.loads(it['tripo_item']).get('hold', 'hang') != 'strap':
                    until[it.name] = int(e['item_gone'] * FPS)
        if e['kind'] == 'death':
            # As the render lets go of what he holds: his hips a fifth below standing, or the clip's drop_at.
            for o in [o for o in scene.objects if o.type == 'ARMATURE']:
                assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
            scene.frame_set(a)
            stand = (main.matrix_world @ main.pose.bones[bone].head).z
            release = None
            for f in range(a, b + 1):
                scene.frame_set(f)
                if (e.get('drop_at') and f >= e['drop_at']) or (f > a + 3 and (main.matrix_world @ main.pose.bones[bone].head).z < 0.8 * stand):
                    release = f
                    break
            release = release or int(a + (b - a) // 3)
            for it in items:
                if json.loads(it['tripo_item']).get('drop', json.loads(it['tripo_item']).get('hold') != 'strap'):
                    until[it.name] = release - 1
        else:
            for o in [o for o in scene.objects if o.type == 'ARMATURE']:
                assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
        # His own arms out of his own body first (limbs.py), then what he holds (which puts a second hand back).
        import limbs
        own = next((o for o in bodies if o.name == 'body'), bodies[0])
        rows += limbs.settle(rig, own, act, scene, fix='--no-fix' not in sys.argv)
        for o in [o for o in scene.objects if o.type == 'ARMATURE']:
            assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
        if items:
            rows += clearance.settle(rig, bodies, items, act, scene, fix='--no-fix' not in sys.argv, until=until)
            if '--no-fix' not in sys.argv:
                # And his arms once more as the weapon's turning left them; then both as they end, for the table.
                for o in [o for o in scene.objects if o.type == 'ARMATURE']:
                    assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
                limbs.settle(rig, own, act, scene, fix=True)
                for o in [o for o in scene.objects if o.type == 'ARMATURE']:
                    assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
                final = limbs.settle(rig, own, act, scene, fix=False)
                for o in [o for o in scene.objects if o.type == 'ARMATURE']:
                    assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
                final += clearance.settle(rig, bodies, items, act, scene, fix=False, until=until)
                for r in rows:
                    for fr in final:
                        if r['clip'] == fr['clip'] and r['item'] == fr['item']:
                            r.update(after_cm=fr['before_cm'], after_frame=fr['before_frame'], frames_left=fr['frames_in'],
                                     where=fr['where'] if fr['frames_in'] else '')
        for o in [o for o in scene.objects if o.type == 'ARMATURE']:
            assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
        w = clearance.wrist_report(rig, act, scene)
        wrists.append((e['clip'], w))
    swing_items(scene, {})
    for o in [o for o in scene.objects if o.type == 'ARMATURE']:
        assign(o, None)
        for pb in o.pose.bones:
            pb.matrix_basis = Matrix()
    lines = ['%s: an arm in his own body, a weapon through him: every frame of every clip (cm under the skin; tolerance in its column)' % cfg['unit'],
             '%-12s %-9s %5s | %7s %6s %7s | %7s %6s %7s | %s' % ('clip', 'item', 'tol', 'before', 'frame', 'frames', 'after', 'frame', 'frames', 'turned  (still in)')]
    failed = []
    for r in rows:
        lines.append('%-12s %-9s %5.1f | %7.1f %6d %3d/%-3d | %7.1f %6d %3d/%-3d | %d  %s' % (
            r['clip'].split('|')[-1], r['item'], r['tolerance_cm'], r['before_cm'], r['before_frame'], r['frames_in'], r['frames'],
            r['after_cm'], r['after_frame'], r['frames_left'], r['frames'], r['turned'], r.get('where', '')))
        if r['frames_left']:
            failed.append(r)
    lines.append('')
    lines.append('wrists, worst bend from straight on the forearm (degrees, frame): ' + '; '.join(
        '%s L %.0f@%d R %.0f@%d' % (c, w['Left'][0], w['Left'][1], w['Right'][0], w['Right'][1]) for c, w in wrists))
    text = '\n'.join(lines)
    with open(os.path.join(cfg['dir'], 'penetration.txt'), 'w') as fh:
        fh.write(text + '\n')
    print(text, flush=True)
    # The verdict goes into the blend itself: render() draws nothing from a blend that failed, whoever built it and
    # with whatever flags (an --allow-penetration build for looking, or another run's, left in the same work folder).
    bpy.context.scene['tripo_gate'] = ('failed: %s' % ', '.join(sorted({'%s %s' % (r['clip'], r['item']) for r in failed}))) if failed else 'passed'
    if failed and '--allow-penetration' not in sys.argv and not os.environ.get('TRIPO_ALLOW_PENETRATION'):
        bpy.ops.wm.save_as_mainfile(filepath=cfg['blend'], compress=True)
        sys.exit('%s: %d clip(s) still have an arm or a weapon through the body: not built for drawing' % (cfg['unit'], len(failed)))


def thin(entry, n, seconds):
    """A clip drawn in n frames instead (the config's "frames": a big body's sheet holds fewer pictures at a
    density worth having): a loop resampled evenly, any other clip keeping its first, its last and its release."""
    if not n or n >= len(entry['frames']):
        return
    if entry['kind'] in ('loop', 'cycle'):
        entry['frames'] = [1 + seconds * FPS * i / n for i in range(n)]
        return
    fr = entry['frames']
    keep = {0, len(fr) - 1}
    if 'release' in entry:
        keep.add(min(range(len(fr)), key=lambda i: abs((fr[i] - 1) / FPS - entry['release'])))
    for i in range(n):
        if len(keep) >= n:
            break
        keep.add(round(i * (len(fr) - 1) / (n - 1)))
    entry['frames'] = [fr[i] for i in sorted(keep)]


def build(cfg):
    os.makedirs(cfg['dir'], exist_ok=True)
    if cfg.get('mount'):
        return build_mounted(cfg)
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
    if cfg.get('cape'):
        fitting.free_cloth(rig, body, cfg['cape'])
    fitting.trunk_off_arms(rig, body)
    fit = fitting.Fit(rig, body)
    print('%s: %d tris, %.2f m tall, hips %.2f m; %s' % (cfg['unit'], sum(len(p.vertices) - 2 for p in body.data.polygons),
                                                          cfg['height_m'], rig.rest_head['Hips'].z, fit.describe()), flush=True)
    # Items: each a separate object in a hand.
    upright = bpy.data.objects.new('upright', None)   # an unturned frame for things that hang
    scene.collection.objects.link(upright)
    for it in cfg.get('items', []):
        attach_item(it, cfg, scene, arm, rig, upright)
    # The clips.
    P, clips = styles.STYLES[cfg['style']](cfg)
    fit.options(cfg)
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
        for key in ('release', 'item_gone', 'drop_at', 'swing'):
            if key in c:
                entry[key] = c[key]
        thin(entry, cfg.get('frames', {}).get(name), last)
        plan.append(entry)
        print('%s: %d action frames, %d drawn%s' % (name, int(act.frame_range[1]), len(entry['frames']),
                                                      ', stride %.2f m' % stride if stride else ''), flush=True)
    rig.rest()
    with open(cfg['plan'], 'w') as fh:
        json.dump(plan, fh, indent=1)
    clear_weapons(cfg, scene, rig, arm, [body], plan)
    rig.W = arm.matrix_world.copy()
    rig.Wi = rig.W.inverted()
    bpy.ops.wm.save_as_mainfile(filepath=cfg['blend'], compress=True)
    print('saved', cfg['blend'], flush=True)


def build_mounted(cfg):
    """A rider on a horse (cavalry.py): two models, two rigs, two sets of actions, one scene."""
    import cavalry
    m = cfg['mount']
    bpy.ops.wm.open_mainfile(filepath=os.path.join(cfg['work'], 'models', m['body'] + '.blend'))
    scene = bpy.context.scene
    harm = next(o for o in scene.objects if o.type == 'ARMATURE')
    hbody = next(o for o in scene.objects if o.type == 'MESH' and o.parent == harm)
    hbody.name = 'mount'
    root = bpy.data.objects['rig_root']
    for pb in harm.pose.bones:
        pb.matrix_basis = Matrix()
    root.matrix_world = Matrix()
    bpy.context.view_layer.update()
    # Facing +x by where its head is, its length as the config says, its hooves on the ground about the origin.
    pts = fitting.body_points(hbody)
    high = pts[pts[:, 2] > pts[:, 2].min() + 0.9 * (pts[:, 2].max() - pts[:, 2].min())]
    d = high.mean(0) - pts.mean(0)
    turn = Matrix.Rotation(-math.atan2(d[1], d[0]), 4, 'Z')
    root.matrix_world = turn
    bpy.context.view_layer.update()
    pts = fitting.body_points(hbody)
    k = m['length_m'] / float(pts[:, 0].max() - pts[:, 0].min())
    root.matrix_world = Matrix.Scale(k, 4) @ turn
    bpy.context.view_layer.update()
    pts = fitting.body_points(hbody)
    low = pts[pts[:, 2] < pts[:, 2].min() + 0.05 * m['length_m']]
    root.matrix_world = Matrix.Translation(Vector((-float(low[:, 0].mean()), -float(low[:, 1].mean()), -float(pts[:, 2].min())))) @ root.matrix_world
    bpy.context.view_layer.update()
    H = cavalry.Horse(harm, hbody).read()
    print('%s: horse %s, %.2f m long; %s' % (cfg['unit'], m['body'], m['length_m'], H.describe()), flush=True)
    for line in H.repair():
        print('  repaired', line, flush=True)
    clips = cavalry.horse_clips()
    seconds = {}
    for name, (sec, fn) in clips.items():
        H.bake(name, sec, fn)
        seconds[name] = sec
    H.rest()
    seat, half = cavalry.seat_of(H, m.get('seat', 0.60))
    mids = {n: (H.rest_head[n] + H.rest_tail[n]) / 2 for n in H.spine}
    seat_bone = min(mids, key=lambda n: (mids[n] - seat).length)
    print('  the saddle at x %.2f, %.2f m up, the barrel %.2f m wide there; rides on %s' % (seat.x, seat.z, 2 * half, seat_bone), flush=True)
    harm['main'], harm['ref_bone'], harm['clip_prefix'] = 1, seat_bone, ''
    harm['gib_mount_head'] = json.dumps(H.neck[1:])
    harm['gib_mount_leg'] = json.dumps(next(l['bones'][1:] for l in H.legs if l['front']))
    plan = []
    rig = None
    if cfg.get('body'):
        # The rider: his own file's armature and skin, stood at the origin to be measured and posed, then set on the back.
        with bpy.data.libraries.load(os.path.join(cfg['work'], 'models', cfg['body'] + '.blend'), link=False) as (src, dst):
            dst.objects = [n for n in src.objects if n == 'Armature' or n.startswith('tripo_node')]
        rarm = next(o for o in dst.objects if o.type == 'ARMATURE')
        rbody = next(o for o in dst.objects if o.type == 'MESH')
        for o in (rarm, rbody):
            scene.collection.objects.link(o)
        rarm.name, rbody.name = 'rider_rig', 'body'
        rarm.parent = None
        bpy.context.view_layer.update()
        Bn = rarm.data.bones
        fwd = rarm.matrix_world.to_3x3() @ sum(((Bn[B + sd + 'ToeBase'].head_local - Bn[B + sd + 'Foot'].head_local) for sd in ('Left', 'Right')), Vector())
        rp = fitting.body_points(rbody)
        rk = cfg['rider_height_m'] / float(rp[:, 2].max() - rp[:, 2].min())
        rarm.matrix_world = Matrix.Scale(rk, 4) @ Matrix.Rotation(-math.atan2(fwd.y, fwd.x), 4, 'Z') @ rarm.matrix_world
        bpy.context.view_layer.update()
        rp = fitting.body_points(rbody)
        feet = (rarm.matrix_world @ Bn[B + 'LeftFoot'].head_local + rarm.matrix_world @ Bn[B + 'RightFoot'].head_local) / 2
        rarm.matrix_world = Matrix.Translation(Vector((-feet.x, -feet.y, -float(rp[:, 2].min())))) @ rarm.matrix_world
        bpy.context.view_layer.update()
        for pb in rarm.pose.bones:
            pb.rotation_mode = 'QUATERNION'
        rig = keyer.Rig(rarm)
        fitting.trunk_off_arms(rig, rbody)
        upright = bpy.data.objects.new('upright', None)
        scene.collection.objects.link(upright)
        for it in cfg.get('items', []):
            attach_item(it, cfg, scene, rarm, rig, upright)
        P, rclips = cavalry.rider_clips(seconds)
        snaps = {n: cavalry.seated(rig, seat, half, pp) for n, pp in P.items()}
        for name, keys in rclips.items():
            cavalry.bake_snaps(rig, 'rider|' + name, keys, snaps)
        rig.apply(snaps['SIT'])
        world = rarm.matrix_world.copy()
        rarm.parent, rarm.parent_type, rarm.parent_bone = harm, 'BONE', seat_bone
        bpy.context.view_layer.update()
        rarm.matrix_world = world
        rarm['clip_prefix'] = 'rider|'
        rig.rest()
    for name, sec in seconds.items():
        kind = cavalry.KINDS.get(name, 'death')
        n = cavalry.FRAMES[name]
        e = {'clip': name, 'action': name, 'kind': kind, 'seconds': sec}
        if kind in ('loop', 'cycle'):
            e['frames'] = [1 + sec * FPS * i / n for i in range(n)]
            e['loop'] = True
            if name in cavalry.STRIDES:
                e['stride'] = round(cavalry.STRIDES[name] * m['length_m'] / 2.4, 3)
        else:
            e['frames'] = sorted({round(1 + sec * FPS * i / (n - 1)) for i in range(n)})
        if name == 'die_burn':
            e['drop_at'] = 4
        thin(e, cfg.get('frames', {}).get(name), sec)
        plan.append(e)
    with open(cfg['plan'], 'w') as fh:
        json.dump(plan, fh, indent=1)
    if rig is not None:
        clear_weapons(cfg, scene, rig, rarm, [rbody, hbody], plan, ref_bone=seat_bone, stand_arm=harm)
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
    s.render.resolution_x, s.render.resolution_y = canvas_of(cfg)[:2]
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
    gate = s.get('tripo_gate', 'never checked')
    if gate != 'passed' and not look and '--allow-penetration' not in sys.argv:
        sys.exit('%s: the build did not pass the arm and weapon check (%s): not drawn' % (cfg['unit'], gate))
    c = s.cycles
    W, H, PPM, AX, AY = canvas_of(cfg)
    root = bpy.data.objects['rig_root']
    rigs = [o for o in s.objects if o.type == 'ARMATURE']
    arm = next((o for o in rigs if o.get('main')), rigs[0])
    ref_bone = arm.get('ref_bone', B + 'Hips')
    bodies = [o for o in s.objects if o.type == 'MESH' and o.name in ('body', 'mount')]
    if arg('--part'):
        # One of the pair alone (rider or mount), for sheets of their own.
        for o in bodies:
            o.hide_render = o.name != {'rider': 'body', 'mount': 'mount'}[arg('--part')]
    items = [o for o in s.objects if o.type == 'MESH' and 'tripo_item' in o]
    base = root.matrix_world.copy()
    hue = cfg.get('team_hue', 0.62)
    masks = {o.name: mask_material(base_image(o), hue, 'team_' + o.name) for o in bodies}
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
        rb.linear_damping, rb.angular_damping = 0.55, 0.6   # dropped, not flung: a fast hand must not send it across the field
        o.hide_render = True
        masks[o.name] = masks[it.name]
        twins[it.name] = o
    drawn = bodies + items + list(twins.values())
    droppers = [it for it in items if json.loads(it['tripo_item']).get('drop', json.loads(it['tripo_item']).get('hold') != 'strap')]
    saved = {o.name: [sl.material for sl in o.material_slots] for o in drawn}

    def masked(on):
        for o in drawn:
            for i, sl in enumerate(o.material_slots):
                sl.material = masks[o.name] if on else saved[o.name][i]

    def hips():
        return arm.matrix_world @ arm.pose.bones[ref_bone].head

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
        x0, x1 = AX + u.min() * PPM - 8, AX + u.max() * PPM + 8
        y0, y1 = (H - AY) + w.min() * PPM - 8, (H - AY) + w.max() * PPM + 8
        if x0 < 0 or y0 < 0 or x1 > W or y1 > H:
            print('  WARNING: view %d leaves the canvas (%.0f..%.0f, %.0f..%.0f)' % (v, x0, x1, y0, y1), flush=True)
        s.render.border_min_x, s.render.border_max_x = min(1.0, max(0.0, x0 / W)), min(1.0, max(0.0, x1 / W))
        s.render.border_min_y, s.render.border_max_y = min(1.0, max(0.0, y0 / H)), min(1.0, max(0.0, y1 / H))

    def shoot(d, j):
        for v in views:
            studio.aim(v, (W / 2 - AX) / PPM, (AY - H / 2) / PPM, max(W, H) / PPM)
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
        swing_items(s, e)
        for o in rigs:
            assign(o, bpy.data.actions.get(o.get('clip_prefix', '') + e['action']))
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
                if release is None and droppers and ((e.get('drop_at') and f >= e['drop_at']) or (f > a + 3 and hips().z < 0.8 * stand)):
                    release = f
            release = release or int(a + (b - a) // 3)
            for it in droppers:
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
                falls = it in droppers
                it.hide_render = (dropped and falls) or (falls and gone_at is not None and f >= gone_at - 1e-6 and f < b - 0.5)
                twins[it.name].hide_render = not (dropped and falls)
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
        sheet_look(os.path.join(art, cfg['unit']), plan, views, (W, H, PPM, AX, AY), cfg['height_m'])
    elif not only and not test:
        if not arg('--part'):
            render_gibs(cfg, s, studio, rigs, bodies, items, masks, art, bodies + items + list(twins.values()))
        with open(os.path.join(art, cfg['unit'], 'clips.json'), 'w') as fh:
            json.dump(dict({'clips': clips}, **({'scale': cfg['scale']} if cfg.get('scale', 1) != 1 else {})), fh, indent=1)
        print('wrote clips.json', flush=True)
    else:
        print(json.dumps(clips), flush=True)


# The body parts a death may throw (docs/SPRITE_SPEC.md, gib parts): each drawn from the model itself, not cut out of
# the idle picture (the importer's cut goes by a standing man's proportions, and a spear held upright or a cape
# spoils them). Which bones' skin makes which part:
GIB_BONES = {'head': ('Head', 'Neck'), 'torso': ('Spine', 'Hips'), 'arm': ('RightShoulder', 'RightArm', 'RightForeArm', 'RightHand'),
             'leg': ('LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToe')}


def render_gibs(cfg, s, studio, rigs, bodies, items, masks, art, hidden):
    import bmesh
    W, H, PPM, AX, AY = canvas_of(cfg)
    k = cfg.get('scale', 1)
    side = int(layout.GIB_CANVAS[0] * k)
    parts = layout.UNITS[cfg['unit']][6]
    d = os.path.join(art, cfg['unit'], 'gibs')
    os.makedirs(d, exist_ok=True)
    for o in rigs:
        assign(o, None)
        for pb in o.pose.bones:
            pb.matrix_basis = Matrix()
    s.frame_set(1)
    bpy.context.view_layer.update()
    for o in hidden:
        o.hide_render = True
    s.render.resolution_x = s.render.resolution_y = side
    s.render.use_border = False
    c = s.cycles
    dg = bpy.context.evaluated_depsgraph_get()

    def piece(src, names):
        """A copy of a skin as it stands at rest, only what the named bones hold (None: all of it)."""
        me = bpy.data.meshes.new_from_object(src.evaluated_get(dg))
        ob = bpy.data.objects.new('gib', me)
        s.collection.objects.link(ob)
        ob.matrix_world = src.matrix_world.copy()
        if names is not None:
            want = {g.index for g in src.vertex_groups if g.name in names or any(g.name.split(':')[-1].startswith(n) for n in names if ':' not in n and '_' not in n)}
            keep = [sum(g.weight for g in v.groups if g.group in want) > 0.5 for v in src.data.vertices]
            bm = bmesh.new()
            bm.from_mesh(me)
            bm.verts.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index < len(keep) and not keep[v.index]], context='VERTS')
            bm.to_mesh(me)
            bm.free()
        return ob

    def lay(ob):
        """Lying on its side, its middle on the origin; shrunk if it would not fit the canvas."""
        pts = fitting.body_points(ob)
        if not len(pts):
            return False
        mid = Vector((pts.min(0) + pts.max(0)) / 2)
        ext = pts.max(0) - pts.min(0)
        turn = Matrix.Rotation(math.radians(82), 4, 'Y') if ext[2] >= max(ext[0], ext[1]) else Matrix()
        fit = min(1.0, 0.9 * side / PPM / float(max(ext)))
        ob.matrix_world = Matrix.Translation(Vector((0, 0, 0))) @ Matrix.Scale(fit, 4) @ turn @ Matrix.Translation(-mid) @ ob.matrix_world
        return True

    made = []
    for part in parts:
        if part in GIB_BONES and any(o.name == 'body' for o in bodies):
            src, names = next(o for o in bodies if o.name == 'body'), GIB_BONES[part]
        elif part.startswith('mount_') and any(o.name == 'mount' for o in bodies):
            src = next(o for o in bodies if o.name == 'mount')
            names = json.loads(src.parent.get('gib_' + part, '[]'))
            if not names:
                continue
        elif part == 'weapon':
            src, names = next((o for o in items if json.loads(o['tripo_item']).get('hold') != 'strap'), None), None
        elif part == 'shield':
            src, names = next((o for o in items if json.loads(o['tripo_item']).get('hold') == 'strap'), None), None
        else:
            continue          # gobbets ('chunk') stay the importer's
        if src is None:
            continue
        src.hide_render = False
        ob = piece(src, names)
        src.hide_render = True
        if not lay(ob):
            bpy.data.objects.remove(ob)
            continue
        mat = masks.get(src.name)
        for variant, v in (('a', 1), ('b', 3)):
            studio.aim(v, 0.0, 0.0, side / PPM)
            c.samples, c.use_denoising = int(arg('--samples', '32')), True
            s.render.filepath = '%s/%s_%s.png' % (d, part, variant)
            bpy.ops.render.render(write_still=True)
            saved = [sl.material for sl in ob.material_slots]
            for sl in ob.material_slots:
                sl.material = mat
            c.samples, c.use_denoising = 8, False
            s.render.filepath = '%s/%s_%s_team.png' % (d, part, variant)
            bpy.ops.render.render(write_still=True)
            for sl, m in zip(ob.material_slots, saved):
                sl.material = m
        bpy.data.objects.remove(ob)
        made.append(part)
    print('gib parts drawn from the model:', ', '.join(made), flush=True)


def sheet_look(d, plan, views, canvas, tall):
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
    W, H, PPM, AX, AY = canvas
    r = tall * 0.8 * PPM
    box = (int(AX - r), int(AY - 1.5 * r), int(AX + r), int(AY + 0.5 * r))
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
