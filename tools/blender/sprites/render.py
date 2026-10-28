"""Photographing a figure into the game's sprite sheet.

The same five views as figures::view (45 degrees apart from the front round
to the back, 30 degrees above the horizon, orthographic), lit the same way in
every view: a key light over the viewer's left shoulder (as the runtime bake
lights its figures), a soft fill from the right and a rim light from behind;
a sky-to-earth world gives ambient light and occlusion. Cycles on the CPU,
denoised.

Each cell is rendered at ss times the sheet's pixels a metre on a pixel
lattice anchored at the feet, then reduced to the sheet's size with a hard
cutout and the runtime bake's 1 px dark outline (kResolveFs), so a sheet made
here drops into the renderer in place of a baked one.
"""

import json
import math
import os
import time

import bpy
import numpy as np
from mathutils import Matrix, Vector

from . import rig

ELEVATION = 0.52  # figures::kElevation
VIEWS = 5


def view(v):
    """figures::view: the picture's right and up, and the direction toward the camera."""
    az = -v * math.pi / 4
    c = Vector((math.cos(az), math.sin(az), 0))
    toward = c * math.cos(ELEVATION) + Vector((0, 0, math.sin(ELEVATION)))
    right = (-toward).cross(Vector((0, 0, 1))).normalized()
    up = right.cross(-toward)
    return right, up, toward


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    c = s.cycles
    c.device = 'CPU'
    c.seed = 0
    c.use_animated_seed = False
    c.samples = 96
    c.use_adaptive_sampling = True
    c.adaptive_threshold = 0.02
    c.use_denoising = True
    c.denoiser = 'OPENIMAGEDENOISE'
    c.max_bounces = 4
    c.diffuse_bounces = 2
    c.glossy_bounces = 2
    c.transmission_bounces = 2
    c.transparent_max_bounces = 4
    c.sample_clamp_indirect = 4.0
    c.blur_glossy = 1.0
    c.caustics_reflective = False
    c.caustics_refractive = False
    s.render.film_transparent = True
    s.render.filter_size = 1.2
    s.render.threads_mode = 'AUTO'
    s.render.use_persistent_data = True
    s.view_settings.view_transform = 'Standard'
    s.view_settings.look = 'None'
    s.view_settings.exposure = 0.0
    s.view_settings.gamma = 1.0
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGBA'
    s.render.image_settings.color_depth = '8'
    s.render.resolution_percentage = 100
    s.render.use_border = True
    s.render.use_crop_to_border = False
    bpy.context.view_layer['mask'] = 0.0
    _world(s)
    return s


def _world(s):
    w = bpy.data.worlds.new('sky')
    s.world = w
    w.use_nodes = True
    nt = w.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(tc.outputs['Generated'], sep.inputs['Vector'])
    nt.links.new(sep.outputs['Z'], ramp.inputs['Fac'])
    # Generated z runs -1..1 over the sphere: 0..1 here. Earth below, a pale sky above.
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.inputs['From Min'].default_value = -1.0
    mr.inputs['From Max'].default_value = 1.0
    nt.links.new(sep.outputs['Z'], mr.inputs['Value'])
    nt.links.new(mr.outputs['Result'], ramp.inputs['Fac'])
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.0, (0.12, 0.13, 0.09, 1)
    els[1].position, els[1].color = 1.0, (0.42, 0.5, 0.62, 1)
    e = els.new(0.47)
    e.color = (0.2, 0.2, 0.16, 1)
    e = els.new(0.53)
    e.color = (0.4, 0.42, 0.44, 1)
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.75
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])


class Studio:
    """A camera and three lights that turn with the view."""

    def __init__(self, scene):
        self.scene = scene
        cam = bpy.data.cameras.new('camera')
        cam.type = 'ORTHO'
        cam.clip_start, cam.clip_end = 0.1, 100
        self.cam = bpy.data.objects.new('camera', cam)
        scene.collection.objects.link(self.cam)
        scene.camera = self.cam
        self.lights = []
        for name, energy, angle in (('key', 3.4, 4.0), ('fill', 0.9, 30.0), ('rim', 2.6, 8.0)):
            l = bpy.data.lights.new(name, 'SUN')
            l.energy = energy
            l.angle = math.radians(angle)
            if name == 'fill':
                l.color = (0.85, 0.9, 1.0)
            if name == 'key':
                l.color = (1.0, 0.96, 0.9)
            ob = bpy.data.objects.new(name, l)
            scene.collection.objects.link(ob)
            self.lights.append(ob)

    def aim(self, v, centre_u, centre_v, scale):
        right, up, toward = view(v)
        m = Matrix((right, up, toward)).transposed()  # columns: the camera's x, y, z
        self.cam.matrix_world = Matrix.Translation(right * centre_u + up * centre_v + toward * 30.0) @ m.to_4x4()
        self.cam.data.ortho_scale = scale
        key = (toward * 0.7 + up * 0.5 - right * 0.5).normalized()
        fill = (toward * 0.8 + right * 0.6 - up * 0.1).normalized()
        rim = (-toward * 0.8 + up * 0.5 + right * 0.35).normalized()
        for ob, d in zip(self.lights, (key, fill, rim)):
            # A sun shines down its -z: point -z away from d.
            ob.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()


def mesh_objects():
    return [o for o in bpy.context.scene.objects if o.type == 'MESH' and not o.hide_render]


def extents(frames=None):
    """Per frame, per view: (u0, u1, v0, v1) of everything drawn, in metres."""
    scene = bpy.context.scene
    views = [view(v) for v in range(VIEWS)]
    out = []
    for f in (frames or range(1, rig.FRAMES + 1)):
        scene.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        pts = []
        for ob in mesh_objects():
            ev = ob.evaluated_get(dg)
            me = ev.to_mesh()
            n = len(me.vertices)
            if n:
                co = np.empty(n * 3, dtype=np.float64)
                me.vertices.foreach_get('co', co)
                co = co.reshape(n, 3)
                mw = np.array(ev.matrix_world)
                pts.append(co @ mw[:3, :3].T + mw[:3, 3])
            ev.to_mesh_clear()
        pts = np.concatenate(pts)
        row = []
        for right, up, _ in views:
            u = pts @ np.array(right)
            w = pts @ np.array(up)
            row.append((float(u.min()), float(u.max()), float(w.min()), float(w.max())))
        out.append(row)
    return out


def _load_png(path):
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    return np.asarray(im, dtype=np.float32) / 255.0


def choose_ppm(ext, base=48.0, max_px=256.0, budget=2048 * 2048 / 1.35):
    """Pixels a metre for a type's sheet, as Renderer::bakeSheet chooses them:
    the game's 48 unless a picture would be more than max_px across (giants,
    dragons), and less still if all 130 would not fit a 2048 atlas."""
    big = max(max(e[1] - e[0], e[3] - e[2]) for row in ext for e in row)
    ppm = min(base, max_px / big)
    area = sum((e[1] - e[0]) * (e[3] - e[2]) for row in ext for e in row)
    while area * ppm * ppm > budget:
        ppm *= 0.95
    return round(ppm, 1)


def render_cells(out_dir, ppm=48.0, ss=2, samples=96, log=print, views=range(VIEWS), frames=None):
    """Renders every cell (view x frame) at ppm * ss pixels a metre, picture
    and team mask, into out_dir/raw/. Returns the cells' lattice placement."""
    scene = bpy.context.scene
    studio = Studio(scene)
    raw = os.path.join(out_dir, 'raw')
    os.makedirs(raw, exist_ok=True)
    frames = list(frames or range(1, rig.FRAMES + 1))
    t0 = time.time()
    ext = extents(frames)
    log('extents of %d frames: %.1f s' % (len(frames), time.time() - t0))
    if ppm is None:
        ppm = choose_ppm(ext)
        log('%.1f px/m' % ppm)
    pp = ppm * ss
    cells = {}
    timing = {'picture': 0.0, 'mask': 0.0}
    # Each view's picture: the union of its frames on the lattice, a margin round.
    lattice = {}
    for v in views:
        u0 = min(e[v][0] for e in ext)
        u1 = max(e[v][1] for e in ext)
        w0 = min(e[v][2] for e in ext)
        w1 = max(e[v][3] for e in ext)
        lattice[v] = (int(math.floor(u0 * ppm)) - 2, int(math.floor(w0 * ppm)) - 2, int(math.ceil(u1 * ppm)) + 2, int(math.ceil(w1 * ppm)) + 2)
    # Frames outermost, then picture and matte, then the views: the deformed
    # meshes and the shaders are synced as few times as can be.
    for k, f in enumerate(frames):
        scene.frame_set(f)
        for mask in (0, 1):
            bpy.context.view_layer['mask'] = float(mask)
            scene.cycles.samples = samples if not mask else 16
            scene.cycles.use_denoising = not mask
            scene.cycles.use_adaptive_sampling = not mask
            for v in views:
                i0, j0, i1, j1 = lattice[v]
                W, H = (i1 - i0) * ss, (j1 - j0) * ss
                scene.render.resolution_x, scene.render.resolution_y = W, H
                studio.aim(v, (i0 + i1) / 2 / ppm, (j0 + j1) / 2 / ppm, max(W, H) / pp)
                e = ext[k][v]
                a0 = max(i0, int(math.floor(e[0] * ppm)) - 2)
                a1 = min(i1, int(math.ceil(e[1] * ppm)) + 2)
                b0 = max(j0, int(math.floor(e[2] * ppm)) - 2)
                b1 = min(j1, int(math.ceil(e[3] * ppm)) + 2)
                scene.render.border_min_x = (a0 - i0) / (i1 - i0)
                scene.render.border_max_x = (a1 - i0) / (i1 - i0)
                scene.render.border_min_y = (b0 - j0) / (j1 - j0)
                scene.render.border_max_y = (b1 - j0) / (j1 - j0)
                t = time.time()
                name = 'v%d_f%02d' % (v, f - 1)
                scene.render.filepath = os.path.join(raw, name + ('_mask' if mask else '') + '.png')
                bpy.ops.render.render(write_still=True)
                timing['mask' if mask else 'picture'] += time.time() - t
                cells[(v, f - 1)] = {'lattice': lattice[v], 'ss': ss}
        bpy.context.view_layer['mask'] = 0.0
        log('frame %d: %.0f s so far' % (f, time.time() - t0))
    log('rendered %d cells: pictures %.0f s, masks %.0f s' % (len(cells), timing['picture'], timing['mask']))
    meta = {'ppm': ppm, 'ss': ss, 'cells': {'%d_%d' % k: c for k, c in cells.items()},
            'seconds': {'pictures': round(timing['picture'], 1), 'masks': round(timing['mask'], 1), 'total': round(time.time() - t0, 1)}}
    with open(os.path.join(raw, 'lattice.json'), 'w') as fh:
        json.dump(meta, fh, indent=1)
    return meta


def preview(path, v=0, frame=1, ppm=200.0, samples=64, box=None):
    """One picture, large, for looking at the model (box: u0, u1, v0, v1 in metres)."""
    scene = bpy.context.scene
    studio = Studio(scene)
    e = box or extents([frame])[0][v]
    W = int((e[1] - e[0]) * ppm) + 8
    H = int((e[3] - e[2]) * ppm) + 8
    scene.render.resolution_x, scene.render.resolution_y = W, H
    scene.render.use_border = False
    studio.aim(v, (e[0] + e[1]) / 2, (e[2] + e[3]) / 2, max(W, H) / ppm)
    scene.frame_set(frame)
    scene.cycles.samples = samples
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    scene.render.use_border = True
    for ob in (studio.cam, *studio.lights):
        bpy.data.objects.remove(ob)
