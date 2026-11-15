"""A rider on a horse, from two Tripo models kept apart: the horse on its own (Tripo's quadruped) rig with its own
actions, the rider on his (Mixamo-named) rig with his, the rider's armature a child of the horse's back. One scene
renders them together (the game has one unit); rendered apart (hide either) they give the separate sheets a rule
for unhorsing would need.

Tripo's quadruped rigs are not alike and not reliable: bones are misnamed (a tail called a hind limb), a leg may
have a bone too few or a spare chain, and nothing tells front from hind. So the horse's skeleton is read by its
shape, not its names: chains off the middle line that reach the ground are legs (front or hind by where they
hang), the middle line's bones ahead of the forelegs and rising are the neck and head, those behind the hind legs
the tail. A leg is posed as three stiff lengths (by how far down the chain a bone lies), so a leg of four bones
and one of six move alike; a bone that runs on through the ground is cut at the ground first.

Clips: idle, walk (four beats), run (a gallop), attack (the rider's thrust over a gathered horse), flinch,
celebrate (a rear), and the deaths (the horse goes down on its side, three ways)."""
import math

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

import keyer
from keyer import B, lean
import fit as fitting

FPS = keyer.FPS
X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))


def ease(u):
    return u * u * (3 - 2 * u)


class Horse:
    def __init__(self, arm, body):
        self.arm, self.body, self.P = arm, body, arm.pose.bones
        for pb in self.P:
            pb.rotation_mode = 'QUATERNION'
        self.refresh()

    def refresh(self):
        self.W = self.arm.matrix_world.copy()
        self.Wi = self.W.inverted()
        self.k = self.W.to_scale()[0]
        self.rest()
        self.rest_rot = {pb.name: (self.W @ pb.matrix).to_quaternion() for pb in self.P}
        self.rest_head = {pb.name: self.W @ pb.head for pb in self.P}
        self.rest_tail = {pb.name: self.W @ pb.tail for pb in self.P}

    def rest(self):
        for pb in self.P:
            pb.matrix_basis = Matrix()
        bpy.context.view_layer.update()

    def head(self, n):
        return self.W @ self.P[n].head

    def tail(self, n):
        return self.W @ self.P[n].tail

    def rot(self, n):
        return (self.W @ self.P[n].matrix).to_quaternion()

    def set_rot(self, n, q, pos=None):
        pb = self.P[n]
        h = pos if pos is not None else self.head(n)
        pb.matrix = self.Wi @ (Matrix.Translation(h) @ q.to_matrix().to_4x4() @ Matrix.Scale(self.k, 4))
        bpy.context.view_layer.update()

    # ----------------------------------------------------------------------------------------------- the skeleton, read
    def read(self):
        """Finds the root, the legs (each a chain, top to hoof), the neck and head, and the tail."""
        bones = self.arm.data.bones
        self.root = next(b.name for b in bones if b.parent is None)
        zs = [self.rest_head[b.name].z for b in bones] + [self.rest_tail[b.name].z for b in bones]
        xs = [self.rest_head[b.name].x for b in bones]
        self.length = max(xs) - min(xs)
        L = self.length
        mid = lambda n: abs(self.rest_head[n].y) < 0.035 * L and abs(self.rest_tail[n].y) < 0.035 * L

        def chain_from(b):
            out = [b.name]
            while len(b.children) >= 1:
                # follow the child that goes on downward (a leg) or simply the only one
                b = min(b.children, key=lambda c: self.rest_tail[c.name].z) if len(b.children) > 1 else b.children[0]
                out.append(b.name)
            return out

        self.legs = []
        seen = set()
        for b in bones:
            if b.name == self.root or mid(b.name) or b.name in seen:
                continue
            if b.parent is not None and not mid(b.parent.name) and b.parent.name != self.root:
                continue
            ch = chain_from(b)
            end = self.rest_tail[ch[-1]]
            if end.z < 0.08 * L:
                top = self.rest_head[ch[0]]
                self.legs.append({'bones': ch, 'x': (top.x + end.x) / 2, 'side': 'L' if top.y + end.y > 0 else 'R'})
                seen.update(ch)
        cut = (max(l['x'] for l in self.legs) + min(l['x'] for l in self.legs)) / 2
        for l in self.legs:
            l['front'] = l['x'] > cut
            l['name'] = ('F' if l['front'] else 'H') + l['side']
        front_x = min(self.rest_head[l['bones'][0]].x for l in self.legs if l['front'])
        hind_x = max(self.rest_head[l['bones'][0]].x for l in self.legs if not l['front'])
        # The neck: middle bones ahead of the forelegs' tops, in order; the head is the one with most skin.
        legbones = {n for l in self.legs for n in l['bones']}
        middle = [b.name for b in bones if mid(b.name) and b.name != self.root and b.name not in legbones]
        ahead = [n for n in middle if self.rest_head[n].x > front_x - 0.02 * L and self.rest_tail[n].z > self.rest_head[n].z - 0.03 * L
                 and self.rest_tail[n].x > front_x]
        depth = lambda n: len(bones[n].parent_recursive)
        self.neck = sorted(ahead, key=depth)
        self.tailbones = sorted([n for n in middle if self.rest_head[n].x < hind_x - 0.02 * L and self.rest_tail[n].x < hind_x], key=depth)
        self.spine = sorted([n for n in middle if n not in self.neck and n not in self.tailbones], key=depth)
        self.seat_bone = None
        return self

    def describe(self):
        return 'legs %s; neck %d bones, tail %d, spine %d' % (
            ', '.join('%s %d bones' % (l['name'], len(l['bones'])) for l in self.legs), len(self.neck), len(self.tailbones), len(self.spine))

    def repair(self):
        """A leg's last bone that runs on through the ground is cut where the hoof begins (a twentieth of the
        horse's length above the ground): the lower part a new hoof bone with the skin below the cut."""
        fixed = []
        L = self.length
        for l in self.legs:
            last = l['bones'][-1]
            h, t = self.rest_head[last], self.rest_tail[last]
            if t.z < -0.02 * L and h.z > 0.07 * L:
                cut_z = 0.045 * L
                u = (h.z - cut_z) / (h.z - t.z)
                bpy.context.view_layer.objects.active = self.arm
                bpy.ops.object.mode_set(mode='EDIT')
                eb = self.arm.data.edit_bones
                b = eb[last]
                head, tail = b.head.copy(), b.tail.copy()
                cut = head + (tail - head) * u
                ground = head + (tail - head) * ((h.z - 0.0) / (h.z - t.z))
                nb = eb.new(last + '_hoof')
                nb.head, nb.tail, nb.roll, nb.parent, nb.use_connect = cut, ground, b.roll, b, True
                b.tail = cut
                bpy.ops.object.mode_set(mode='OBJECT')
                grp = self.body.vertex_groups.get(last)
                new = self.body.vertex_groups.new(name=last + '_hoof')
                mw = self.body.matrix_world
                gi = grp.index
                for v in self.body.data.vertices:
                    for g in v.groups:
                        if g.group == gi and g.weight > 0 and (mw @ v.co).z < cut_z:
                            w = g.weight
                            grp.remove([v.index])
                            new.add([v.index], w, 'ADD')
                            break
                l['bones'].append(last + '_hoof')
                fixed.append('%s: %s cut at the hoof' % (l['name'], last))
        # Skin low on a leg that the root holds (Tripo gives the root the soles) stays behind when the leg moves and
        # is drawn out into a blade: it goes to the nearest leg bone.
        if fixed:
            self.P = self.arm.pose.bones
            for pb in self.P:
                pb.rotation_mode = 'QUATERNION'
            self.refresh()
        # Skin on a leg that another leg's bone (or the root) holds stays behind when its own leg moves and is drawn
        # out into a blade. Tripo's weights do this at the hooves: each such vertex goes to the nearest bone of the
        # leg it is on.
        mw = self.body.matrix_world
        vg = self.body.vertex_groups
        owner = {vg[n].index: i for i, l in enumerate(self.legs) for n in l['bones'] if vg.get(n)}
        if vg.get(self.root):
            owner[vg[self.root].index] = -1
        segs = [[(n, np.array(self.rest_head[n]), np.array(self.rest_tail[n])) for n in l['bones']] for l in self.legs]

        def nearest(pw, chain):
            best, bd = None, 1e9
            for n, a, b in chain:
                ab = b - a
                t = min(1.0, max(0.0, float((pw - a) @ ab / (ab @ ab))))
                dd = float(np.linalg.norm(pw - (a + t * ab)))
                if dd < bd:
                    best, bd = n, dd
            return best, bd

        moved = 0
        for v in self.body.data.vertices:
            held = [(g.group, g.weight) for g in v.groups if g.group in owner and g.weight > 1e-4]
            if not held:
                continue
            pw = np.array(mw @ v.co)
            if pw[2] > 0.30 * L:
                continue
            near = [nearest(pw, ch) for ch in segs]
            leg = min(range(len(segs)), key=lambda i: near[i][1])
            for gi, w in held:
                if owner[gi] != leg and (owner[gi] < 0 or near[owner[gi]][1] > 2.0 * near[leg][1]):
                    vg[gi].remove([v.index])
                    (vg.get(near[leg][0]) or vg.new(name=near[leg][0])).add([v.index], w, 'ADD')
                    moved += 1
        if moved:
            fixed.append('%d weights low on the legs taken from the wrong bone to the leg they are on' % moved)
        if fixed:
            self.P = self.arm.pose.bones
            for pb in self.P:
                pb.rotation_mode = 'QUATERNION'
            self.refresh()
        return fixed

    # ----------------------------------------------------------------------------------------------- posing
    def roles(self, leg):
        """Each bone of a leg: 0 upper, 1 middle, 2 lower, by how far down the chain its middle lies."""
        if 'roles' not in leg:
            lens = [(self.rest_tail[n] - self.rest_head[n]).length for n in leg['bones']]
            total, run, out = sum(lens), 0.0, []
            for ln in lens:
                at = (run + ln / 2) / total
                out.append(0 if at < (0.50 if leg['front'] else 0.42) else 1 if at < (0.80 if leg['front'] else 0.72) else 2)
                run += ln
            leg['roles'] = out
        return leg['roles']

    def pose(self, p):
        """p: dz, dx (m), pitch (deg, + nose up), roll (deg, + onto its right side), yaw; neck, head (deg, + up);
        tail (deg swish); legs: name -> (swing deg, + forward; fold 0..1); plumb: leg names that hang true whatever
        the body does; ground: 'hooves' (default), 'hind', 'body' (lowest point of the skin), or None."""
        self.rest()
        r = self.root
        body = Quaternion(X, math.radians(p.get('roll', 0))) @ Quaternion(Y, math.radians(-p.get('pitch', 0))) @ Quaternion(Z, math.radians(p.get('yaw', 0)))
        pivot = Vector(p.get('pivot', self.rest_head[r]))
        pos = pivot + body @ (self.rest_head[r] - pivot) + Vector((p.get('dx', 0), p.get('dy', 0), p.get('dz', 0)))
        self.set_rot(r, body @ self.rest_rot[r], pos)
        if self.neck:
            self.set_rot(self.neck[0], Quaternion(body @ Y, math.radians(-p.get('neck', 0))) @ self.rot(self.neck[0]))
            if len(self.neck) > 2:
                hd = self.neck[-1]
                self.set_rot(hd, Quaternion(body @ Y, math.radians(-p.get('head', 0))) @ self.rot(hd))
        for i, n in enumerate(self.tailbones[:3]):
            self.set_rot(n, Quaternion(body @ Z, math.radians(p.get('tail', 0) * (0.5 + 0.25 * i))) @ self.rot(n))
        for leg in self.legs:
            swing, fold = p.get('legs', {}).get(leg['name'], (0.0, 0.0))
            f = fold * 75.0
            if leg['front']:
                ang = (swing + 0.15 * f, swing - f, swing - 1.7 * f + 0.5 * f)
            else:
                ang = (0.75 * swing + 0.35 * f, 0.75 * swing - 0.65 * f, swing + 0.45 * f)
            frame = Quaternion() if leg['name'] in p.get('plumb', ()) else body
            for n, role in zip(leg['bones'], self.roles(leg)):
                self.set_rot(n, frame @ Quaternion(Y, math.radians(ang[role])) @ self.rest_rot[n])
        g = p.get('ground', 'hooves')
        if g:
            if g == 'body':
                low = float(np.percentile(fitting.body_points(self.body)[:, 2], 0.5))
            else:
                low = min(self.tail(l['bones'][-1]).z for l in self.legs if g != 'hind' or not l['front'])
            shift = Vector((0, 0, -low))
            if g == 'hind':
                hind = [l for l in self.legs if not l['front']]
                shift.x = sum(self.rest_tail[l['bones'][-1]].x - self.tail(l['bones'][-1]).x for l in hind) / len(hind)
            self.set_rot(r, self.rot(r), self.head(r) + shift)

    def key(self, frame):
        for pb in self.P:
            pb.keyframe_insert('location', frame=frame)
            pb.keyframe_insert('rotation_quaternion', frame=frame)

    def bake(self, name, seconds, fn):
        """fn(u) -> pose dict, u 0..1 through the clip. Every frame keyed."""
        ad = self.arm.animation_data or self.arm.animation_data_create()
        act = bpy.data.actions.new(name)
        act.use_fake_user = True
        ad.action = act
        n = int(round(seconds * FPS))
        prev = {}
        for f in range(n + 1):
            self.pose(fn(f / n))
            for pb in self.P:
                q = pb.rotation_quaternion
                if pb.name in prev and prev[pb.name].dot(q) < 0:
                    pb.rotation_quaternion = -q
                prev[pb.name] = pb.rotation_quaternion.copy()
            self.key(f + 1)
        act.name = name
        ad.action = None
        self.rest()
        return act


# ---------------------------------------------------------------------------------------------------- the horse's clips
def stride(ph, duty, reach, fold):
    """A leg through its cycle: swept back while it stands (duty of the cycle), folded and brought forward after."""
    ph %= 1.0
    if ph < duty:
        return reach * (1 - 2 * ph / duty), 0.0
    s = (ph - duty) / (1 - duty)
    return reach * (-1 + 2 * ease(s)), fold * math.sin(math.pi * s)


def gait(u, duty, reach, fold, phases):
    return {name: stride(u - at, duty, reach * (1.0 if name[0] == 'F' else 0.9), fold) for name, at in phases.items()}


WALK = {'HL': 0.0, 'FL': 0.25, 'HR': 0.5, 'FR': 0.75}          # four beats, a hind foot then the fore of its side
GALLOP = {'HL': 0.0, 'HR': 0.12, 'FL': 0.42, 'FR': 0.54}       # hind pair, then fore pair, then all in the air


def horse_clips():
    two = 2 * math.pi

    def idle(u):
        return dict(neck=3 * math.sin(two * u) - 2, head=4 * math.sin(two * 2 * u + 1), tail=14 * math.sin(two * 3 * u),
                    dz=0.004 * math.sin(two * 2 * u), legs={'HR': (0.0, 0.10 * max(0.0, math.sin(two * u)))})

    def walk(u):
        return dict(legs=gait(u, 0.62, 17, 0.45, WALK), neck=-4 + 4 * math.sin(two * 2 * u), head=-3 * math.sin(two * 2 * u),
                    pitch=0.8 * math.sin(two * 2 * u + 0.5), roll=1.2 * math.sin(two * u), tail=8 * math.sin(two * u))

    def run(u):
        return dict(legs=gait(u, 0.30, 33, 0.95, GALLOP), pitch=5.5 * math.sin(two * (u - 0.08)), neck=-14 - 7 * math.sin(two * (u - 0.08)),
                    head=6 + 5 * math.sin(two * u), tail=-10 + 6 * math.sin(two * u), dz=0.0)

    def rear(u):
        a = math.sin(math.pi * ease(u))                    # up and down again
        return dict(pitch=46 * a, plumb=('HL', 'HR'), ground='hind', neck=-18 * a, head=14 * a, tail=-20 * a,
                    legs={'FL': (30 * a, 0.9 * a), 'FR': (38 * a, 0.75 * a * (0.8 + 0.2 * math.sin(two * 3 * u))),
                          'HL': (-8 * a, 0.0), 'HR': (-6 * a, 0.0)})

    def gather(u):                                          # under the rider's thrust: a surge and a check
        a = math.sin(math.pi * min(1.0, u * 1.3)) ** 2
        return dict(pitch=-3 * a, neck=-10 * a, head=8 * a, dx=0.10 * a, tail=12 * math.sin(two * 2 * u),
                    legs={'FL': (-6 * a, 0.0), 'FR': (10 * a, 0.25 * a), 'HL': (-8 * a, 0.0), 'HR': (-8 * a, 0.0)})

    def flinch(u):
        a = math.sin(math.pi * u)
        return dict(pitch=6 * a, neck=14 * a, head=-10 * a, dx=-0.08 * a, tail=-25 * a, plumb=('HL', 'HR'),
                    legs={'FL': (8 * a, 0.3 * a), 'FR': (-4 * a, 0.0)})

    def fall(u, how):
        """Down on its right side. how: 'side' (struck: staggers and goes over), 'rear' (rears and topples),
        'fore' (the forelegs give way first)."""
        if how == 'rear':
            up = math.sin(math.pi * min(1.0, u / 0.55)) if u < 0.55 else 0.0
            over = ease(max(0.0, (u - 0.35) / 0.45)) if u < 0.8 else 1.0
            p = dict(pitch=38 * up, plumb=('HL', 'HR') if over < 0.05 else (), legs={'FL': (30 * up, 0.8 * up), 'FR': (36 * up, 0.7 * up)})
        elif how == 'fore':
            down = ease(min(1.0, u / 0.4))
            over = ease(max(0.0, (u - 0.3) / 0.45)) if u < 0.75 else 1.0
            p = dict(pitch=-16 * down * (1 - over), legs={'FL': (-10 * down, 0.95 * down), 'FR': (-6 * down, 0.9 * down)}, neck=-25 * down)
        else:
            over = ease(min(1.0, max(0.0, (u - 0.12) / 0.5)))
            st = math.sin(math.pi * min(1.0, u / 0.25))
            p = dict(dy=-0.10 * st, legs={'FL': (6 * st, 0.2 * st), 'HL': (-6 * st, 0.15 * st)})
        settle = ease(max(0.0, (u - 0.7) / 0.3))
        legs = dict(p.get('legs', {}))
        for n in ('FL', 'FR', 'HL', 'HR'):
            s, f = legs.get(n, (0.0, 0.0))
            # Lying: the legs stretch and sag a little toward the ground.
            legs[n] = (s * (1 - settle) + (10 if n[0] == 'F' else -10) * settle, f * (1 - settle) + 0.12 * settle)
        p.update(roll=88 * over, legs=legs, ground='body' if over > 0.02 else p.get('ground', 'hooves' if how != 'rear' else 'hind'),
                 neck=p.get('neck', 0) - 22 * over, head=10 * over, tail=-15 * over)
        if how == 'rear' and over <= 0.02:
            p['ground'] = 'hind'
        return p

    return {
        'idle': (4.0, idle), 'walk': (1.1, walk), 'run': (0.52, run), 'attack': (1.0, gather), 'flinch': (0.6, flinch),
        'die_back': (1.6, lambda u: fall(u, 'rear')), 'die_front': (1.5, lambda u: fall(u, 'fore')),
        'die_crumple': (1.5, lambda u: fall(u, 'side')), 'die_burn': (2.25, lambda u: fall(min(1.0, u * 1.1), 'rear')),
        'celebrate': (2.4, rear),
    }


STRIDES = {'walk': 1.75, 'run': 5.6}       # metres a cycle, for a horse 2.4 m long


# ---------------------------------------------------------------------------------------------------- the rider
def seat_of(horse, at=0.60):
    """Where the rider sits: the low of the saddle's top line between the legs, and the horse's half width there."""
    pts = fitting.body_points(horse.body)
    fx = min(horse.rest_head[l['bones'][0]].x for l in horse.legs if l['front'])
    hx = max(horse.rest_head[l['bones'][0]].x for l in horse.legs if not l['front'])
    # Just behind the withers: six tenths of the way from the hind legs' tops to the forelegs' (the low of the top
    # line is no guide: behind a saddle's cantle the croup is lower than its seat).
    x = hx + at * (fx - hx)
    band = pts[(np.abs(pts[:, 0] - x) < 0.03 * horse.length) & (np.abs(pts[:, 1]) < 0.05 * horse.length)]
    z = float(band[:, 2].max())
    girth = pts[(np.abs(pts[:, 0] - x) < 0.08 * horse.length) & (pts[:, 2] > z * 0.55) & (pts[:, 2] < z * 0.9)]
    half = float(np.abs(girth[:, 1]).max()) if len(girth) else 0.3
    return Vector((x, 0.0, z)), half


def seated(rig, seat, half, p):
    """The rider's pose in the saddle, in the world as the horse stands at rest. p: chest, head (keyer.lean), rise (m
    out of the saddle), sword_hand / sword_elbow / shield_hand / shield_elbow relative to his hips' joint, blade, edge,
    shield_face, fist."""
    rig.rest()
    k = rig.rest_head['Hips'].z / 1.04
    hips = seat + Vector((0.0, 0.0, 0.09 * k + p.get('rise', 0.0)))
    hr = p.get('hips_rot', lean(pitch=-4))
    rig.set_rot('Hips', hr @ rig.rest_rot['Hips'], hips)
    chest = p.get('chest', lean(pitch=2))
    for n, w in (('Spine', 0.34), ('Spine1', 0.67), ('Spine2', 1.0)):
        rig.set_rot(n, hr.slerp(chest, w) @ rig.rest_rot[n])
    look = p.get('head', chest)
    rig.set_rot('Neck', chest.slerp(look, 0.5) @ rig.rest_rot['Neck'])
    rig.set_rot('Head', look @ rig.rest_rot['Head'])
    for side, hand, elbow in (('Right', 'sword_hand', 'sword_elbow'), ('Left', 'shield_hand', 'shield_elbow')):
        la, lb = rig.lengths[side][:2]
        rig.two_bone(side + 'Arm', side + 'ForeArm', side + 'Hand', hips + Vector(p[hand]) * k, hips + Vector(p[elbow]) * k, la, lb)
        rig.hand_follows_forearm(side)
        rig.curl(side, p.get('fist', {}).get(side, 60))
    if rig.sword is not None and 'blade' in p:
        rig.grip(Vector(p['blade']), Vector(p.get('edge', (0, 0, -1))))
    if rig.shield is not None and 'shield_face' in p:
        rig.face_shield(Vector(p['shield_face']))
    # Astride: the thighs out round the barrel and forward, the shins down, the heels low.
    for side, sgn in (('Left', 1.0), ('Right', -1.0)):
        la, lb = rig.lengths[side][2:]
        ankle = hips + Vector((0.12 * k + p.get('heel', 0.0), sgn * (half + 0.10 * k), -(la + lb) * 0.80 + p.get('rise', 0.0) * -0.6))
        rig.two_bone(side + 'UpLeg', side + 'Leg', side + 'Foot', ankle, ankle + Vector((1.0, sgn * 0.9, 0.6)), la, lb)
        rig.set_rot(side + 'Foot', Quaternion(Z, math.radians(sgn * 12)) @ rig.rest_rot[side + 'Foot'])
    return rig.snapshot()


def bake_snaps(rig, name, keys, snaps):
    """keys: [(seconds, pose, ease)] over snapshots; every frame keyed."""
    arm = rig.arm
    ad = arm.animation_data or arm.animation_data_create()
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    ad.action = act
    last = int(round(keys[-1][0] * FPS))
    prev = {}
    for f in range(last + 1):
        t = f / FPS
        i = max(j for j in range(len(keys)) if keys[j][0] <= t + 1e-9)
        if i == len(keys) - 1:
            a = b = keys[i]
            u = 0.0
        else:
            a, b = keys[i], keys[i + 1]
            u = keyer.EASE[b[2]]((t - a[0]) / (b[0] - a[0]))
        sa, sb = snaps[a[1]], snaps[b[1]]
        for pb in rig.P:
            la, qa = sa[pb.name]
            lb, qb = sb[pb.name]
            if qa.dot(qb) < 0:
                qb = -qb
            pb.location = la.lerp(lb, u)
            q = qa.slerp(qb, u)
            if pb.name in prev and prev[pb.name].dot(q) < 0:
                q = -q
            pb.rotation_quaternion = q
            prev[pb.name] = q.copy()
            pb.keyframe_insert('location', frame=f + 1)
            pb.keyframe_insert('rotation_quaternion', frame=f + 1)
    act.name = name
    ad.action = None
    return act


def rider_clips(horse_seconds):
    """The lancer's poses (hands from his hips' joint, a man's metres) and clips, timed to the horse's."""
    REINS = dict(shield_hand=(0.34, 0.08, 0.20), shield_elbow=(0.0, 0.45, 0.20), shield_face=(0.3, 1, 0))
    UP = dict(REINS, sword_hand=(0.24, -0.40, 0.30), sword_elbow=(-0.15, -0.58, 0.15), blade=(0.06, -0.10, 1), edge=(1, 0, 0),
              fist={'Right': 70, 'Left': 60})
    # The lance couched as a lancer couches it: gripped before the right hip, the shaft back under the upper arm and
    # clamped between it and the ribs, the elbow out at his side over it, pointed straight ahead past the horse's neck
    # on its right. The hand and elbow were found by trying a grid of them on the knight on his warhorse
    # (tools/sprites/tripo/couch_search.py): a fist holds a shaft across the forearm, so only a hand this close in and
    # an elbow this far out let the wrist aim it level and straight ahead (1 degree off) with the shaft clear of his
    # trunk and the horse; asked of a hand further forward, the wrist cannot turn that far and the lance comes round
    # across his chest. The chest's lean and the rise in the stirrups carry the hand with them.
    def couch(pitch, rise, down=0.03, **kw):
        return dict(REINS, chest=lean(pitch=pitch), head=lean(pitch=-pitch * 0.5), rise=rise, fist={'Right': 75, 'Left': 60},
                    sword_hand=(0.10, -0.30, 0.20), sword_elbow=(0.05, -0.42, 0.25),
                    blade=(1, -0.06, -down), edge=(0, 0, -1), **kw)
    COUCH = couch(14, 0.05)
    P = {'SIT': UP,
         'BREATHE': dict(UP, chest=lean(pitch=0), sword_hand=(0.24, -0.40, 0.32)),
         'LOOK': dict(UP, chest=lean(pitch=2, turn=10), head=lean(turn=38)),
         'SWAY_L': dict(UP, chest=lean(pitch=3, roll=3), hips_rot=lean(pitch=-4, roll=-2)),
         'SWAY_R': dict(UP, chest=lean(pitch=3, roll=-3), hips_rot=lean(pitch=-4, roll=2)),
         'COUCH': COUCH,
         'COUCH_UP': couch(18, 0.10),
         # The charge's strike: the point brought down on the man ahead, then up in the stirrups and into it, the
         # body behind the lance (the horse's weight does the work: the arm does not thrust).
         'DRAW': couch(20, 0.10),
         'THRUST': couch(27, 0.14),
         'HIT': dict(UP, chest=lean(pitch=-16, turn=-8), head=lean(pitch=-20), sword_hand=(0.05, -0.45, 0.35), blade=(0.3, -0.3, 0.9)),
         'CLING': dict(UP, chest=lean(pitch=22), head=lean(pitch=-10), sword_hand=(0.30, -0.28, 0.25), blade=(0.92, -0.12, 0.36)),
         'SLUMP': dict(UP, chest=lean(pitch=35, roll=-20), head=lean(pitch=40, roll=-15), sword_hand=(0.14, -0.50, -0.22),
                       sword_elbow=(0.02, -0.56, 0.08), blade=(0.6, -0.3, -0.7), shield_hand=(0.30, 0.25, -0.05), fist={'Right': 20, 'Left': 20}),
         'CHEER': dict(UP, chest=lean(pitch=-8), head=lean(pitch=-15), sword_hand=(0.15, -0.34, 0.95), sword_elbow=(0.0, -0.60, 0.60),
                       blade=(0.0, -0.05, 1), rise=0.06)}
    T = horse_seconds
    clips = {
        'idle': [(0.0, 'SIT', 'linear'), (1.0, 'BREATHE', 'ease'), (2.0, 'LOOK', 'ease'), (2.8, 'LOOK', 'linear'), (T['idle'], 'SIT', 'ease')],
        'walk': [(0.0, 'SWAY_L', 'linear'), (T['walk'] / 2, 'SWAY_R', 'ease'), (T['walk'], 'SWAY_L', 'ease')],
        'run': [(0.0, 'COUCH', 'linear'), (T['run'] / 2, 'COUCH_UP', 'ease'), (T['run'], 'COUCH', 'ease')],
        'attack': [(0.0, 'SIT', 'linear'), (0.22, 'COUCH', 'ease'), (0.40, 'DRAW', 'ease'), (0.52, 'THRUST', 'in'), (0.66, 'THRUST', 'linear'),
                   (T['attack'], 'SIT', 'ease')],
        'flinch': [(0.0, 'SIT', 'linear'), (0.12, 'HIT', 'out'), (0.25, 'HIT', 'linear'), (T['flinch'], 'SIT', 'ease')],
        'die_back': [(0.0, 'SIT', 'linear'), (0.25, 'CLING', 'ease'), (0.7, 'HIT', 'ease'), (1.1, 'SLUMP', 'ease'), (T['die_back'], 'SLUMP', 'linear')],
        'die_front': [(0.0, 'SIT', 'linear'), (0.3, 'HIT', 'out'), (0.9, 'SLUMP', 'ease'), (T['die_front'], 'SLUMP', 'linear')],
        'die_crumple': [(0.0, 'SIT', 'linear'), (0.2, 'HIT', 'out'), (0.8, 'SLUMP', 'ease'), (T['die_crumple'], 'SLUMP', 'linear')],
        'die_burn': [(0.0, 'SIT', 'linear'), (0.3, 'CLING', 'ease'), (0.8, 'HIT', 'ease'), (1.4, 'SLUMP', 'ease'), (T['die_burn'], 'SLUMP', 'linear')],
        'celebrate': [(0.0, 'SIT', 'linear'), (0.5, 'CLING', 'ease'), (1.1, 'CHEER', 'ease'), (1.6, 'CHEER', 'linear'), (T['celebrate'], 'SIT', 'ease')],
    }
    return P, clips


FRAMES = {'idle': 10, 'walk': 12, 'run': 10, 'attack': 12, 'flinch': 5, 'die_back': 10, 'die_front': 10, 'die_crumple': 10, 'die_burn': 12, 'celebrate': 14}
KINDS = {'idle': 'loop', 'walk': 'cycle', 'run': 'cycle', 'celebrate': 'loop', 'attack': 'still', 'flinch': 'still'}
