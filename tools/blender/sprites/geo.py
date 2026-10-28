"""Mesh building: lofted tubes, panels and small solids, with skin weights.

Everything is plain Python building lists of vertices and faces (no bpy.ops),
so the output depends only on the numbers here. A Part collects geometry and,
per vertex, its weights to the rig's bones; Part.make() turns it into a
Blender object with an Armature and a Subdivision Surface modifier.
"""

import math

import bpy
from mathutils import Matrix, Quaternion, Vector


def V(x, y=None, z=None):
    if y is None:
        return Vector(x)
    return Vector((x, y, z))


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def frame_for(axis, hint):
    """Two unit vectors across the direction axis, the first as near hint as can be."""
    axis = axis.normalized()
    u = hint - axis * hint.dot(axis)
    if u.length < 1e-6:
        u = Vector((0, 0, 1)) - axis * axis.z
        if u.length < 1e-6:
            u = Vector((1, 0, 0)) - axis * axis.x
    u.normalize()
    v = axis.cross(u)
    return u, v


class Rand:
    """A small deterministic generator (the same numbers on every machine)."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s >> 8) / 16777216.0

    def range(self, a, b):
        return a + (b - a) * self.next()


class Part:
    """Geometry of one object: vertices, faces, per-face material, per-vertex bone weights, UVs."""

    def __init__(self, name):
        self.name = name
        self.verts = []
        self.faces = []
        self.face_mat = []
        self.weights = []  # per vertex: {bone: w}
        self.uvs = []  # per face: list of (u, v) per corner, or None
        self.materials = []  # material names, index = face_mat
        self.sharp = set()  # faces shaded flat

    def mat(self, name):
        if name not in self.materials:
            self.materials.append(name)
        return self.materials.index(name)

    def vert(self, p, w):
        self.verts.append(Vector(p))
        self.weights.append(dict(w) if isinstance(w, dict) else {w: 1.0})
        return len(self.verts) - 1

    def face(self, idx, mat, uv=None, sharp=False):
        self.faces.append(tuple(idx))
        self.face_mat.append(self.mat(mat))
        self.uvs.append(uv)
        if sharp:
            self.sharp.add(len(self.faces) - 1)

    def transform(self, m, first=0):
        for i in range(first, len(self.verts)):
            self.verts[i] = m @ self.verts[i]

    # ---------------------------------------------------------------- shapes

    def loft(self, rings, mat, n=12, cap0=True, cap1=True, closed=True, uv_scale=(1.0, 1.0)):
        """A tube through rings. Each ring: dict with c (centre), u, v (unit
        vectors across), ru, rv (radii along u and v), w (bone weights, a dict
        or a function of the angle), and optionally shape(theta) -> radius
        factor, or off(theta) -> extra Vector. Angle 0 is along u."""
        first = len(self.verts)
        cols = n if closed else n + 1
        for r in rings:
            for i in range(cols):
                if closed:
                    th = 2 * math.pi * i / n
                else:  # an open sheet from angle a0 to a1 (capes, skirts)
                    a0, a1 = r.get('a0', -math.pi / 2), r.get('a1', math.pi / 2)
                    th = a0 + (a1 - a0) * i / n
                k = r['shape'](th) if 'shape' in r else 1.0
                p = r['c'] + r['u'] * (math.cos(th) * r['ru'] * k) + r['v'] * (math.sin(th) * r['rv'] * k)
                if 'off' in r:
                    p = p + r['off'](th)
                w = r['w'](th) if callable(r['w']) else r['w']
                self.vert(p, w)
        for j in range(len(rings) - 1):
            for i in range(n):
                a = first + j * cols + i
                b = first + j * cols + (i + 1) % cols
                # (closed: wraps round; open: cols = n + 1, so no wrap)
                c = first + (j + 1) * cols + (i + 1) % cols
                d = first + (j + 1) * cols + i
                uv = [((i) / n * uv_scale[0], j / (len(rings) - 1) * uv_scale[1]),
                      ((i + 1) / n * uv_scale[0], j / (len(rings) - 1) * uv_scale[1]),
                      ((i + 1) / n * uv_scale[0], (j + 1) / (len(rings) - 1) * uv_scale[1]),
                      ((i) / n * uv_scale[0], (j + 1) / (len(rings) - 1) * uv_scale[1])]
                self.face((a, b, c, d), mat(j, (i + 0.5) / n) if callable(mat) else mat, uv)
        if closed:
            for end, cap in ((0, cap0), (len(rings) - 1, cap1)):
                if not cap:
                    continue
                r = rings[end]
                ring = [first + end * cols + i for i in range(n)]
                centre = sum((self.verts[i] for i in ring), Vector()) / n
                # Nudge the pole outward so the cap stays rounded once subdivided.
                axis = (rings[1]['c'] - rings[0]['c']) if end == 0 else (rings[-1]['c'] - rings[-2]['c'])
                axis = axis.normalized() * (1 if end else -1)
                pole = self.vert(centre + axis * min(r['ru'], r['rv']) * r.get('dome', 0.35), self.weights[ring[0]])
                cm = mat(max(0, end - 1), 0.0) if callable(mat) else mat
                for i in range(n):
                    a, b = ring[i], ring[(i + 1) % n]
                    self.face((a, b, pole) if end else (b, a, pole), cm)
        return first

    def chain(self, points, radii, mat, weights, hint=Vector((1, 0, 0)), n=10, shape=None, rv_scale=None, cap0=True, cap1=True, dome=0.35):
        """A tube along a polyline; radii and weights per point. shape(i, theta)
        and rv_scale[i] shape the cross-sections."""
        rings = []
        for i, p in enumerate(points):
            if i == 0:
                d = points[1] - points[0]
            elif i == len(points) - 1:
                d = points[-1] - points[-2]
            else:
                d = (points[i + 1] - points[i]).normalized() + (points[i] - points[i - 1]).normalized()
            u, v = frame_for(d, hint)
            r = radii[i]
            ru, rv = (r, r * (rv_scale[i] if rv_scale else 1.0)) if not isinstance(r, tuple) else r
            ring = {'c': Vector(p), 'u': u, 'v': v, 'ru': ru, 'rv': rv, 'w': weights[i], 'dome': dome}
            if shape:
                ring['shape'] = (lambda th, i=i: shape(i, th))
            rings.append(ring)
        return self.loft(rings, mat, n=n, cap0=cap0, cap1=cap1)

    def ellipsoid(self, c, r, mat, w, axes=None, seg=16, rings=10, shape=None):
        """An ellipsoid (radii r along axes). shape(p_unit) -> radius factor."""
        ax = axes or (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
        first = len(self.verts)
        top = self.vert(c + ax[2] * r[2] * (shape(Vector((0, 0, 1))) if shape else 1), w)
        for j in range(1, rings):
            th = math.pi * j / rings
            for i in range(seg):
                ph = 2 * math.pi * i / seg
                q = Vector((math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th)))
                k = shape(q) if shape else 1.0
                self.vert(c + (ax[0] * (q.x * r[0]) + ax[1] * (q.y * r[1]) + ax[2] * (q.z * r[2])) * k, w)
        bot = self.vert(c - ax[2] * r[2] * (shape(Vector((0, 0, -1))) if shape else 1), w)
        row = lambda j: first + 1 + (j - 1) * seg
        for i in range(seg):
            self.face((top, row(1) + i, row(1) + (i + 1) % seg), mat)
        for j in range(1, rings - 1):
            for i in range(seg):
                a, b = row(j) + i, row(j) + (i + 1) % seg
                self.face((a, row(j + 1) + i, row(j + 1) + (i + 1) % seg, b), mat)
        for i in range(seg):
            self.face((row(rings - 1) + (i + 1) % seg, row(rings - 1) + i, bot), mat)
        return first

    def grid(self, fn, nu, nv, mat, w, closed_u=False, uv=True):
        """A surface p = fn(s, t) for s, t in [0, 1]; w a dict or w(s, t)."""
        first = len(self.verts)
        cols = nu if closed_u else nu + 1
        for j in range(nv + 1):
            for i in range(cols):
                s, t = i / nu, j / nv
                self.vert(fn(s, t), w(s, t) if callable(w) else w)
        for j in range(nv):
            for i in range(nu):
                a = first + j * cols + i
                b = first + j * cols + (i + 1) % cols
                c = first + (j + 1) * cols + (i + 1) % cols
                d = first + (j + 1) * cols + i
                coords = [(i / nu, j / nv), ((i + 1) / nu, j / nv), ((i + 1) / nu, (j + 1) / nv), (i / nu, (j + 1) / nv)] if uv else None
                self.face((a, b, c, d), mat((i + 0.5) / nu, (j + 0.5) / nv) if callable(mat) else mat, coords)
        return first

    def slab(self, fn, nu, nv, thick, mat, w, rim_mat=None, back_mat=None):
        """A thin solid: the surface fn(s, t) and a copy pushed back along its
        normal by thick, joined round the edge (shields, plates)."""
        def normal(s, t):
            e = 1e-3
            du = fn(min(1, s + e), t) - fn(max(0, s - e), t)
            dv = fn(s, min(1, t + e)) - fn(s, max(0, t - e))
            return du.cross(dv).normalized()
        front = self.grid(fn, nu, nv, mat, w)
        back = self.grid(lambda s, t: fn(s, t) - normal(s, t) * thick, nu, nv, back_mat or mat, w)
        # The back faces point away: flip them.
        nb = nu * nv
        for k in range(len(self.faces) - nb, len(self.faces)):
            self.faces[k] = tuple(reversed(self.faces[k]))
        cols = nu + 1
        edge = [(i, 0) for i in range(nu)] + [(nu, j) for j in range(nv)] + [(i, nv) for i in range(nu, 0, -1)] + [(0, j) for j in range(nv, 0, -1)]
        for k in range(len(edge)):
            i0, j0 = edge[k]
            i1, j1 = edge[(k + 1) % len(edge)]
            a, b = front + j0 * cols + i0, front + j1 * cols + i1
            self.face((b, a, back + j0 * cols + i0, back + j1 * cols + i1), rim_mat or mat)
        return front

    def box(self, c, axes, half, mat, w):
        x, y, z = (a * h for a, h in zip(axes, half))
        first = len(self.verts)
        for sz in (-1, 1):
            for sy in (-1, 1):
                for sx in (-1, 1):
                    self.vert(c + x * sx + y * sy + z * sz, w)
        f = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
        for q in f:
            self.face(tuple(first + i for i in q), mat)
        return first

    # ---------------------------------------------------------------- object

    def make(self, collection, armature=None, subdiv=2, smooth_shade=True, materials=None, solidify=0.0):
        me = bpy.data.meshes.new(self.name)
        me.from_pydata([tuple(v) for v in self.verts], [], self.faces)
        me.validate(verbose=False)
        # Generated texture coordinates in metres (halved), the same for every
        # object and fixed to the undeformed mesh, so patterns do not swim.
        me.use_auto_texspace = False
        me.texspace_location = (0.0, 0.0, 1.0)
        me.texspace_size = (1.0, 1.0, 1.0)
        for name in self.materials:
            me.materials.append(materials[name])
        mi = [0] * len(me.polygons)
        for k, p in enumerate(me.polygons):
            mi[k] = self.face_mat[k]
        me.polygons.foreach_set('material_index', mi)
        me.polygons.foreach_set('use_smooth', [smooth_shade and k not in self.sharp for k in range(len(me.polygons))])
        uvl = me.uv_layers.new(name='UV')
        for k, p in enumerate(me.polygons):
            uv = self.uvs[k]
            for c, li in enumerate(p.loop_indices):
                uvl.data[li].uv = uv[c] if uv and c < len(uv) else (0.0, 0.0)
        ob = bpy.data.objects.new(self.name, me)
        collection.objects.link(ob)
        if armature is not None:
            groups = {}
            for i, w in enumerate(self.weights):
                total = sum(w.values())
                for b, x in w.items():
                    if x <= 0:
                        continue
                    if b not in groups:
                        groups[b] = ob.vertex_groups.new(name=b)
                    groups[b].add([i], x / total, 'REPLACE')
            ob.parent = armature
            mod = ob.modifiers.new('rig', 'ARMATURE')
            mod.object = armature
        if solidify:
            s = ob.modifiers.new('thick', 'SOLIDIFY')
            s.thickness = solidify
            s.offset = 0
        if subdiv:
            s = ob.modifiers.new('smooth', 'SUBSURF')
            s.levels = subdiv
            s.render_levels = subdiv
        return ob


def blend(a, b, t):
    """Weights a and b mixed: (1-t) a + t b."""
    out = {}
    for k, x in a.items():
        out[k] = out.get(k, 0) + x * (1 - t)
    for k, x in b.items():
        out[k] = out.get(k, 0) + x * t
    return {k: x for k, x in out.items() if x > 1e-4}


def W(*pairs):
    """W('a', .5, 'b', .5) -> {'a': .5, 'b': .5}; W('a') -> {'a': 1}."""
    if len(pairs) == 1:
        return {pairs[0]: 1.0}
    return {pairs[i]: pairs[i + 1] for i in range(0, len(pairs), 2)}
