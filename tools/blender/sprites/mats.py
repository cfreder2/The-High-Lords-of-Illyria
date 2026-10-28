"""Materials: physically based shaders for skin, cloth, steel, mail, leather,
feathers, horse coat... Each also carries the team-mask switch: when the view
layer property "mask" is 1, every material turns into flat black emission,
except the team's cloth, which turns white. Rendering once with mask = 0 and
once with mask = 1 gives the picture and the matte of where the side's colour
goes. Team cloth is near-white in the picture; the game multiplies it by the
side's colour (kUnitFs).

Textures use the undeformed (Generated) or UV coordinates, so patterns stay
on the surface as the rig moves it.
"""

import bpy


def _node(nt, kind, loc, **inputs):
    n = nt.nodes.new(kind)
    n.location = loc
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def _noise(nt, scale, detail=4.0, coord='Generated', loc=(-900, 0)):
    tc = nt.nodes.get('coords') or _node(nt, 'ShaderNodeTexCoord', (-1200, 0))
    tc.name = 'coords'
    nz = _node(nt, 'ShaderNodeTexNoise', loc, Scale=scale, Detail=detail)
    nt.links.new(tc.outputs[coord], nz.inputs['Vector'])
    return nz


def make(name, color, rough=0.6, metal=0.0, team=False, sheen=0.0, sss=0.0, sss_radius=(0.05, 0.02, 0.01),
         var=0.0, var_scale=40.0, bump=0.0, bump_scale=120.0, bump_coord='Generated', emission=None,
         coat=0.0, aniso=0.0, weave=0.0, spec=0.5, streaks=0.0, rings=0.0):
    """A Principled material. var: colour variation by noise; bump: surface
    relief; weave: a cloth weave on the UVs; streaks: hair or feather barbs
    along the UVs' v direction."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = _node(nt, 'ShaderNodeOutputMaterial', (600, 0))
    bsdf = _node(nt, 'ShaderNodeBsdfPrincipled', (0, 0))
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Metallic'].default_value = metal
    if 'Specular IOR Level' in bsdf.inputs:
        bsdf.inputs['Specular IOR Level'].default_value = spec
    if sheen:
        bsdf.inputs['Sheen Weight'].default_value = sheen
        bsdf.inputs['Sheen Roughness'].default_value = 0.5
    if sss:
        bsdf.inputs['Subsurface Weight'].default_value = sss
        bsdf.inputs['Subsurface Radius'].default_value = sss_radius
        bsdf.inputs['Subsurface Scale'].default_value = 1.0
    if coat:
        bsdf.inputs['Coat Weight'].default_value = coat
    if aniso:
        bsdf.inputs['Anisotropic'].default_value = aniso
    col = (*color, 1.0)
    if var or streaks or weave:
        ramp_in = None
        mix = _node(nt, 'ShaderNodeMix', (-300, 200))
        mix.data_type = 'RGBA'
        mix.blend_type = 'MULTIPLY'
        mix.inputs[0].default_value = 1.0
        mix.inputs[6].default_value = col
        fac = None
        if var:
            nz = _noise(nt, var_scale, loc=(-900, 300))
            mr = _node(nt, 'ShaderNodeMapRange', (-600, 300))
            mr.inputs['To Min'].default_value = 1.0 - var
            mr.inputs['To Max'].default_value = 1.0 + var * 0.6
            mr.inputs['From Min'].default_value = 0.3
            mr.inputs['From Max'].default_value = 0.7
            nt.links.new(nz.outputs['Fac'], mr.inputs['Value'])
            fac = mr.outputs['Result']
        if streaks:
            tc = nt.nodes.get('coords') or _node(nt, 'ShaderNodeTexCoord', (-1200, 0))
            tc.name = 'coords'
            wv = _node(nt, 'ShaderNodeTexWave', (-900, 600), Scale=streaks, Distortion=2.0, Detail=2.0)
            wv.wave_type = 'BANDS'
            wv.bands_direction = 'Y'
            nt.links.new(tc.outputs['UV'], wv.inputs['Vector'])
            mr2 = _node(nt, 'ShaderNodeMapRange', (-600, 600))
            mr2.inputs['To Min'].default_value = 0.86
            mr2.inputs['To Max'].default_value = 1.04
            nt.links.new(wv.outputs['Fac'], mr2.inputs['Value'])
            if fac is None:
                fac = mr2.outputs['Result']
            else:
                mm = _node(nt, 'ShaderNodeMath', (-450, 450))
                mm.operation = 'MULTIPLY'
                nt.links.new(fac, mm.inputs[0])
                nt.links.new(mr2.outputs['Result'], mm.inputs[1])
                fac = mm.outputs['Value']
        if weave:
            tc = nt.nodes.get('coords') or _node(nt, 'ShaderNodeTexCoord', (-1200, 0))
            tc.name = 'coords'
            ck = _node(nt, 'ShaderNodeTexChecker', (-900, 900), Scale=weave)
            ck.inputs['Color1'].default_value = (1.0, 1.0, 1.0, 1)
            ck.inputs['Color2'].default_value = (0.9, 0.9, 0.9, 1)
            nt.links.new(tc.outputs['UV'], ck.inputs['Vector'])
            mm = _node(nt, 'ShaderNodeMath', (-450, 800))
            mm.operation = 'MULTIPLY'
            if fac is None:
                mm.inputs[0].default_value = 1.0
            else:
                nt.links.new(fac, mm.inputs[0])
            nt.links.new(ck.outputs['Fac'], mm.inputs[1])
            mr3 = _node(nt, 'ShaderNodeMapRange', (-300, 800))
            mr3.inputs['From Min'].default_value = 0.0
            mr3.inputs['From Max'].default_value = 1.0
            mr3.inputs['To Min'].default_value = 0.94
            mr3.inputs['To Max'].default_value = 1.0
            nt.links.new(ck.outputs['Fac'], mr3.inputs['Value'])
            if fac is None:
                fac = mr3.outputs['Result']
            else:
                mm2 = _node(nt, 'ShaderNodeMath', (-200, 800))
                mm2.operation = 'MULTIPLY'
                nt.links.new(fac, mm2.inputs[0])
                nt.links.new(mr3.outputs['Result'], mm2.inputs[1])
                fac = mm2.outputs['Value']
        comb = _node(nt, 'ShaderNodeCombineColor', (-450, 200))
        for k in range(3):
            nt.links.new(fac, comb.inputs[k])
        nt.links.new(comb.outputs['Color'], mix.inputs[7])
        nt.links.new(mix.outputs[2], bsdf.inputs['Base Color'])
    else:
        bsdf.inputs['Base Color'].default_value = col
    if bump:
        nz = _noise(nt, bump_scale, detail=6.0, coord=bump_coord, loc=(-900, -400))
        bp = _node(nt, 'ShaderNodeBump', (-300, -400), Strength=bump, Distance=0.002)
        nt.links.new(nz.outputs['Fac'], bp.inputs['Height'])
        nt.links.new(bp.outputs['Normal'], bsdf.inputs['Normal'])
    if rings:
        # Mail: interlocking rings, a Voronoi cell's rim raised, its hole dark.
        tc = nt.nodes.get('coords') or _node(nt, 'ShaderNodeTexCoord', (-1200, 0))
        tc.name = 'coords'
        vo = _node(nt, 'ShaderNodeTexVoronoi', (-900, -700), Scale=rings)
        vo.feature = 'F1'
        nt.links.new(tc.outputs['Generated'], vo.inputs['Vector'])
        ring = _node(nt, 'ShaderNodeMapRange', (-600, -700))
        ring.interpolation_type = 'SMOOTHSTEP'
        ring.inputs['From Min'].default_value = 0.15
        ring.inputs['From Max'].default_value = 0.42
        nt.links.new(vo.outputs['Distance'], ring.inputs['Value'])
        bp = _node(nt, 'ShaderNodeBump', (-300, -700), Strength=0.7, Distance=0.003)
        nt.links.new(ring.outputs['Result'], bp.inputs['Height'])
        nt.links.new(bp.outputs['Normal'], bsdf.inputs['Normal'])
        dark = _node(nt, 'ShaderNodeMapRange', (-300, -900))
        dark.inputs['To Min'].default_value = 0.55
        dark.inputs['To Max'].default_value = 1.0
        nt.links.new(ring.outputs['Result'], dark.inputs['Value'])
        mul = _node(nt, 'ShaderNodeMix', (-150, -900))
        mul.data_type = 'RGBA'
        mul.blend_type = 'MULTIPLY'
        mul.inputs[0].default_value = 1.0
        src = bsdf.inputs['Base Color'].links[0].from_socket if bsdf.inputs['Base Color'].is_linked else None
        if src is not None:
            nt.links.new(src, mul.inputs[6])
        else:
            mul.inputs[6].default_value = col
        cc = _node(nt, 'ShaderNodeCombineColor', (-200, -1000))
        for k in range(3):
            nt.links.new(dark.outputs['Result'], cc.inputs[k])
        nt.links.new(cc.outputs['Color'], mul.inputs[7])
        nt.links.new(mul.outputs[2], bsdf.inputs['Base Color'])
    shader = bsdf.outputs['BSDF']
    if emission is not None:
        em = _node(nt, 'ShaderNodeEmission', (0, -300))
        em.inputs['Color'].default_value = (*emission[0], 1)
        em.inputs['Strength'].default_value = emission[1]
        add = _node(nt, 'ShaderNodeAddShader', (250, -100))
        nt.links.new(shader, add.inputs[0])
        nt.links.new(em.outputs['Emission'], add.inputs[1])
        shader = add.outputs['Shader']
    # The team-mask switch.
    attr = _node(nt, 'ShaderNodeAttribute', (200, 300))
    attr.attribute_type = 'VIEW_LAYER'
    attr.attribute_name = 'mask'
    flat = _node(nt, 'ShaderNodeEmission', (200, -400))
    flat.inputs['Color'].default_value = (1, 1, 1, 1) if team else (0, 0, 0, 1)
    flat.inputs['Strength'].default_value = 1.0
    sw = _node(nt, 'ShaderNodeMixShader', (400, 0))
    nt.links.new(attr.outputs['Fac'], sw.inputs['Fac'])
    nt.links.new(shader, sw.inputs[1])
    nt.links.new(flat.outputs['Emission'], sw.inputs[2])
    nt.links.new(sw.outputs['Shader'], out.inputs['Surface'])
    return m


def library():
    """The shared palette, muted as Myth's was."""
    L = {}
    L['skin'] = make('skin', (0.62, 0.42, 0.31), rough=0.55, sss=0.25, var=0.12, var_scale=25, bump=0.08, bump_scale=300)
    L['skin_pale'] = make('skin_pale', (0.78, 0.6, 0.5), rough=0.5, sss=0.3, var=0.08, var_scale=25)
    L['lips'] = make('lips', (0.5, 0.25, 0.22), rough=0.45, sss=0.2)
    L['eye'] = make('eye', (0.03, 0.025, 0.02), rough=0.15, coat=0.6)
    L['eye_white'] = make('eye_white', (0.75, 0.72, 0.68), rough=0.2)
    L['hair_gold'] = make('hair_gold', (0.62, 0.45, 0.2), rough=0.38, streaks=60, var=0.2, var_scale=15, aniso=0.6, sheen=0.3)
    L['hair_brown'] = make('hair_brown', (0.16, 0.1, 0.06), rough=0.4, streaks=60, var=0.2, aniso=0.6)
    L['robe'] = make('robe', (0.82, 0.8, 0.74), rough=0.8, sheen=0.5, var=0.05, var_scale=8, weave=400, bump=0.05)
    L['team'] = make('team', (0.86, 0.86, 0.86), rough=0.85, sheen=0.45, var=0.08, var_scale=10, weave=300, team=True)
    L['linen'] = make('linen', (0.55, 0.5, 0.42), rough=0.85, sheen=0.4, var=0.1, var_scale=10, weave=300)
    L['hose'] = make('hose', (0.28, 0.24, 0.19), rough=0.85, sheen=0.3, var=0.1)
    L['leather'] = make('leather', (0.22, 0.12, 0.06), rough=0.55, var=0.25, var_scale=12, bump=0.15, bump_scale=200, coat=0.15)
    L['leather_dark'] = make('leather_dark', (0.12, 0.08, 0.05), rough=0.5, var=0.2, var_scale=12, bump=0.12, bump_scale=200, coat=0.2)
    L['steel'] = make('steel', (0.78, 0.78, 0.8), rough=0.22, metal=1.0, var=0.08, var_scale=6, bump=0.03, bump_scale=60)
    L['steel_dark'] = make('steel_dark', (0.5, 0.5, 0.52), rough=0.32, metal=1.0, var=0.15, var_scale=8, bump=0.06, bump_scale=60)
    L['blade'] = make('blade', (0.88, 0.89, 0.92), rough=0.34, metal=0.65, aniso=0.3, coat=0.3)
    L['mail'] = make('mail', (0.4, 0.4, 0.42), rough=0.5, metal=1.0, var=0.5, var_scale=140, rings=75)
    L['gold'] = make('gold', (0.9, 0.66, 0.3), rough=0.25, metal=1.0, var=0.05)
    L['halo'] = make('halo', (1.0, 0.85, 0.5), rough=0.3, metal=0.5, emission=((1.0, 0.75, 0.3), 1.3))
    L['feather'] = make('feather', (0.86, 0.84, 0.79), rough=0.55, sheen=0.6, streaks=90, var=0.06, var_scale=4, sss=0.08, sss_radius=(0.02, 0.02, 0.02))
    L['feather_shaft'] = make('feather_shaft', (0.92, 0.9, 0.84), rough=0.35)
    L['wood'] = make('wood', (0.33, 0.2, 0.1), rough=0.6, streaks=40, var=0.2)
    L['coat_bay'] = make('coat_bay', (0.16, 0.055, 0.018), rough=0.45, sheen=0.1, var=0.12, var_scale=5, coat=0.08, bump=0.04, bump_scale=400)
    L['coat_black'] = make('coat_black', (0.035, 0.028, 0.025), rough=0.42, sheen=0.4, var=0.1, var_scale=6, coat=0.15)
    L['mane'] = make('mane', (0.03, 0.025, 0.02), rough=0.38, streaks=80, aniso=0.6, var=0.25, var_scale=10, sheen=0.3)
    L['hoof'] = make('hoof', (0.07, 0.06, 0.05), rough=0.35, coat=0.3)
    L['horse_eye'] = make('horse_eye', (0.02, 0.015, 0.01), rough=0.05, coat=1.0)
    return L
