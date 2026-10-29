#!/usr/bin/env python3
"""Renders every sound effect in game/web/sfx/sounds.json (and the voices'
placeholder cries listed in game/web/voice/lines.json) into the game's MP3s.

    python3 tools/sfx/synth/render.py              # everything
    python3 tools/sfx/synth/render.py snd_expl_satchel snd_ui_click   # just these
    python3 tools/sfx/synth/render.py --check      # every file there, and sane

Deterministic: each variant's noise comes from a seed made of its id and
number. One-shots are normalised to -18 LUFS (peaks at most -1 dBFS) and
written as mono MP3; ambience loops to -24 LUFS, stereo, made seamless and
padded so the page can loop the middle (loop_start/loop_seconds in
stats.json). A recording dropped in by tools/sfx/convert.sh is never
overwritten: ids listed in game/web/sfx/recorded.json are skipped.
Needs numpy, scipy and ffmpeg.
"""
import json
import os
import subprocess
import sys
import tempfile
import zlib
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dsp  # noqa: E402
import sounds  # noqa: E402
import vocals  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
WEB = os.path.join(ROOT, "game", "web")
MANIFEST = os.path.join(WEB, "sfx", "sounds.json")
LINES = os.path.join(WEB, "voice", "lines.json")
STATS = os.path.join(WEB, "sfx", "stats.json")
RECORDED = os.path.join(WEB, "sfx", "recorded.json")

ONESHOT_LUFS = -18.0
LOOP_LUFS = -24.0
VOICE_LUFS = -18.0
LOOP_PAD = 0.1  # seconds of wrapped audio either side of a loop (the decoder's priming)


def seed(*parts):
    return zlib.crc32("/".join(map(str, parts)).encode())


def encode(x, path, stereo):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        tmp = f.name
    try:
        wavfile.write(tmp, dsp.SR, (np.clip(x, -1, 1) * 32767).astype(np.int16))
        rate = "112k" if stereo else "72k"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-ac", "2" if stereo else "1",
                        "-ar", str(dsp.SR), "-codec:a", "libmp3lame", "-b:a", rate, "-map_metadata", "-1", path], check=True)
    finally:
        os.unlink(tmp)


def stats_of(x, path, loop=None):
    mono = x if x.ndim == 1 else x.mean(axis=1)
    s = {"seconds": round(len(x) / dsp.SR, 3), "peak_db": round(20 * np.log10(np.max(np.abs(x)) + 1e-12), 2),
         "lufs": round(float(dsp.loudness(x)), 2), "dc": round(float(np.mean(mono)), 5),
         "bytes": os.path.getsize(path)}
    if loop:
        s["loop_start"] = LOOP_PAD
        s["loop_seconds"] = round(loop, 4)
    return s


def render_sound(job):
    sid, v, rel = job
    rng = np.random.default_rng(seed(sid, v))
    x = sounds.RECIPES[sid](rng, v)
    fin = sounds.FINISH[sid]
    loop = None
    if fin["loop"]:
        loop = len(x) / dsp.SR
        pad = int(LOOP_PAD * dsp.SR)
        x = np.concatenate([x[-pad:], x, x[:pad]])
        x = dsp.normalise(x, LOOP_LUFS, -3.0)
    else:
        tail, wet = fin["space"]
        if tail > 0:
            ir = dsp.reverb_ir(np.random.default_rng(seed("space", sid, v)), tail, tail / 3.5, 7000)
            x = dsp.trim_tail(dsp.convolve(x, ir, wet), -60)
        x = dsp.fade(dsp.normalise(x, ONESHOT_LUFS, -1.0), 0.0005, 0.01)
    path = os.path.join(WEB, rel)
    encode(x, path, x.ndim == 2)
    return rel, stats_of(x, path, loop)


def render_vocal(job):
    voice, situation, v, rel = job
    rng = np.random.default_rng(seed("voice", voice, situation, v))
    x = vocals.render(rng, voice, situation, v)
    ir = dsp.reverb_ir(np.random.default_rng(seed("space", voice, situation, v)), 0.4, 0.1, 6000)
    x = dsp.trim_tail(dsp.convolve(x, ir, 0.12), -60)
    x = dsp.fade(dsp.normalise(x, VOICE_LUFS, -1.0), 0.0005, 0.01)
    path = os.path.join(WEB, rel)
    encode(x, path, False)
    return rel, stats_of(x, path)


def jobs(only):
    with open(MANIFEST, encoding="utf-8") as f:
        m = json.load(f)
    recorded = set()
    if os.path.exists(RECORDED):
        with open(RECORDED, encoding="utf-8") as f:
            recorded = set(json.load(f))
    sfx = []
    for s in m["sounds"]:
        if only and s["id"] not in only:
            continue
        for n, rel in enumerate(s["files"]):
            if rel not in recorded:
                sfx.append((s["id"], n, rel))
    with open(LINES, encoding="utf-8") as f:
        lines = json.load(f)
    voc = []
    for st in lines["sets"]:
        for sit in st["situations"]:
            for n, line in enumerate(sit["lines"]):
                rel = line.get("placeholder")
                if rel and (not only or sit["id"] in only or st["id"] in only):
                    voc.append((st["id"], sit["id"], n, rel))
    return m, sfx, voc


def check():
    with open(MANIFEST, encoding="utf-8") as f:
        m = json.load(f)
    with open(STATS, encoding="utf-8") as f:
        stats = json.load(f)
    with open(LINES, encoding="utf-8") as f:
        lines = json.load(f)
    bad = []
    files = [rel for s in m["sounds"] for rel in s["files"]]
    files += [l["placeholder"] for st in lines["sets"] for sit in st["situations"] for l in sit["lines"] if "placeholder" in l]
    missing_recipe = [s["id"] for s in m["sounds"] if s["id"] not in sounds.RECIPES]
    for rel in files:
        if not os.path.exists(os.path.join(WEB, rel)):
            bad.append(f"missing: {rel}")
            continue
        s = stats.get(rel)
        if not s:
            bad.append(f"no stats: {rel}")
            continue
        if s["peak_db"] > -0.5:
            bad.append(f"clips: {rel} ({s['peak_db']} dBFS)")
        if s["lufs"] < -45:
            bad.append(f"near silent: {rel} ({s['lufs']} LUFS)")
        if abs(s["dc"]) > 0.01:
            bad.append(f"DC offset: {rel} ({s['dc']})")
        if not 0.04 <= s["seconds"] <= 12:
            bad.append(f"odd length: {rel} ({s['seconds']} s)")
    for sid in missing_recipe:
        bad.append(f"no recipe: {sid}")
    for b in bad:
        print(b)
    total = sum(os.path.getsize(os.path.join(WEB, r)) for r in files if os.path.exists(os.path.join(WEB, r)))
    print(f"{len(files)} files, {total / 1e6:.2f} MB, {len(bad)} problems")
    return 1 if bad else 0


def main(argv):
    if "--check" in argv:
        return check()
    only = set(a for a in argv if not a.startswith("-"))
    m, sfx, voc = jobs(only)
    stats = {}
    if os.path.exists(STATS):
        with open(STATS, encoding="utf-8") as f:
            stats = json.load(f)
    with ProcessPoolExecutor(max_workers=os.cpu_count()) as ex:
        for rel, s in ex.map(render_sound, sfx):
            stats[rel] = s
            print(f"{rel}: {s['seconds']} s, {s['lufs']} LUFS, peak {s['peak_db']} dB, {s['bytes'] // 1024} KB")
        for rel, s in ex.map(render_vocal, voc):
            stats[rel] = s
    with open(STATS, "w", encoding="utf-8") as f:
        json.dump(dict(sorted(stats.items())), f, indent=0)
        f.write("\n")
    total = sum(s["bytes"] for s in stats.values())
    print(f"rendered {len(sfx)} sound files and {len(voc)} placeholder cries; all files {total / 1e6:.2f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
