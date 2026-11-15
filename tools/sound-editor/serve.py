#!/usr/bin/env python3
"""The Sound Editor's local server (Python standard library only; bound to 127.0.0.1).

    python3 tools/sound-editor/serve.py [--port 8770] [--workspace ~/Desktop/sound-sources]

Serves the repository (the editor, the audit pages, game/web as it is) and a workspace folder kept
OUTSIDE git: downloaded candidates (candidates/, masters/, data.js with their licences), the owner's
recordings (recordings/), the choices (choices.json) and edits (a .json beside each sound).

Choosing a sound in the page installs it into the working tree at once: the source is rendered by
ffmpeg with its edit (trim, fades, loudness, channels, loop seam) to the game's format, written to
game/web/sfx/<id>_<n>.mp3 or game/web/voice/<set>/<situation>_<n>.mp3, the manifests and generated
docs are refreshed (tools/sfx/script.py, tools/voice/script.py) and its provenance is written to
docs/sound_credits.json and docs/SOUND_CREDITS.md. Nothing is ever committed; every change can be
undone from the page (the file comes back from git). Only CC0 candidates and own recordings install.
"""
import argparse
import array
import datetime
import functools
import http.server
import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB = os.path.join(ROOT, "game", "web")
SFX, VOICE = os.path.join(WEB, "sfx"), os.path.join(WEB, "voice")
CREDITS_JSON = os.path.join(ROOT, "docs", "sound_credits.json")
CREDITS_MD = os.path.join(ROOT, "docs", "SOUND_CREDITS.md")
WS = ""            # the workspace, set in main()
FFMPEG = None
LOCK = threading.Lock()
OWN = "The owner's own recording; no attribution needed"
ID = re.compile(r"^snd_[a-z0-9_]{1,80}$")
EVENT = re.compile(r"^(snd|new)_[a-z0-9_]{1,80}$")
CAND = re.compile(r"^(snd|new)_[a-z0-9_]{1,80}_cand\d{2}$")
OWN_SFX = re.compile(r"^((?:snd|new)_[a-z0-9_]{1,80})/((?:snd|new)_[a-z0-9_]{1,80})_own(\d{2})\.wav$")
OWN_VOICE = re.compile(r"^([a-z0-9_]{1,40})/([a-z0-9_]{1,60}_\d{1,3})\.take(\d{2})\.wav$")
VOICE_FILE = re.compile(r"^voice/([a-z0-9_]{1,40})/([a-z0-9_]{1,60}_\d{1,3})\.mp3$")
SFX_FILE = re.compile(r"^sfx/(snd_[a-z0-9_]{1,80})_(\d{1,2})\.mp3$")


class Refused(Exception):
    """Something the page asked for that the server will not do; the message is shown to the owner."""


# ------------------------------------------------------------------ small helpers
def inside(base, path):
    base, path = os.path.realpath(base), os.path.realpath(path)
    return path == base or path.startswith(base + os.sep)


def read_json(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)


def write_json(path, data, indent=1):
    write_text(path, json.dumps(data, indent=indent, ensure_ascii=False) + "\n")


def git(*args, binary=False):
    r = subprocess.run(["git", "-C", ROOT, *args], capture_output=True)
    if r.returncode:
        return None
    return r.stdout if binary else r.stdout.decode("utf-8", "replace")


def head_json(rel, default):
    text = git("show", "HEAD:" + rel)
    try:
        return json.loads(text) if text else default
    except ValueError:
        return default


def tracked(rel):
    return git("cat-file", "-e", "HEAD:" + rel) is not None


def restore(rel):
    """A game file as git has it: checked out again, or removed if git never had it."""
    path = os.path.join(ROOT, rel)
    if not inside(SFX, path) and not inside(VOICE, path):
        raise Refused("not a game sound: " + rel)
    if tracked(rel):
        git("checkout", "HEAD", "--", rel)
    elif os.path.exists(path):
        os.remove(path)
        folder = os.path.dirname(path)
        if inside(VOICE, folder) and folder != VOICE and not os.listdir(folder):
            os.rmdir(folder)


def find_ffmpeg():
    for c in (os.environ.get("SOUND_EDITOR_FFMPEG"), os.path.join(WS, "bin", "ffmpeg"), shutil.which("ffmpeg")):
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


NO_FFMPEG = ("ffmpeg was not found, so nothing can be installed. Either: brew install ffmpeg; or: python3 -m venv ~/sound-venv && "
             "~/sound-venv/bin/pip install imageio-ffmpeg && cp \"$(~/sound-venv/bin/python -c 'import imageio_ffmpeg as f; print(f.get_ffmpeg_exe())')\" "
             "<workspace>/bin/ffmpeg; or set SOUND_EDITOR_FFMPEG to its path. Then start the server again.")


def ff(args, data=None):
    if not FFMPEG:
        raise Refused(NO_FFMPEG)
    r = subprocess.run([FFMPEG, "-hide_banner", "-nostdin", *args], input=data, capture_output=True)
    return r.returncode, r.stdout, r.stderr.decode("utf-8", "replace")


# ------------------------------------------------------------------ the manifests
def manifest():
    return read_json(os.path.join(SFX, "sounds.json"), {"sounds": []})


def lines_manifest():
    return read_json(os.path.join(VOICE, "lines.json"), {"sets": [], "situations": []})


def is_loop(sound, head_stats):
    """An ambience loop: git's stats give its first file loop points."""
    return any("loop_seconds" in head_stats.get(f, {}) for f in sound.get("files", [])) or \
        (sound["id"].startswith("snd_amb_") and "loop" in sound.get("behaviour", ""))


def candidates():
    """The workspace's candidates with their provenance, from data.js (tools/build_candidates.py there)."""
    path = os.path.join(WS, "data.js")
    try:
        text = open(path, encoding="utf-8").read()
        data = json.loads(text.split("=", 1)[1].rsplit(";", 1)[0].replace("NaN", "null"))
    except (OSError, ValueError, IndexError):
        return {}, {}
    by_event, by_name = {}, {}
    for ev in data:
        if not EVENT.match(ev.get("id", "")):
            continue
        rows = []
        for c in ev.get("cands", []):
            name = os.path.basename(c.get("file", "")).rsplit(".", 1)[0]
            if not CAND.match(name) or not os.path.isfile(os.path.join(WS, "masters", name + ".flac")):
                continue
            row = {"src": "cand:" + name, "name": name, "event": ev["id"], "url_master": "/_ws/masters/" + name + ".flac", "url_mp3": "/_ws/candidates/" + name + ".mp3",
                   "raw": c.get("raw", ""), "source": c.get("source", ""), "author": c.get("author", ""), "url": c.get("url", ""), "licence": c.get("licence", ""),
                   "flag": c.get("flag", ""), "dur": c.get("dur"), "lufs": c.get("lufs"), "edit": read_json(os.path.join(WS, "masters", name + ".flac.json"), {}).get("edit", {})}
            rows.append(row)
            by_name[name] = row
        by_event[ev["id"]] = {"title": ev.get("title", ev["id"]), "loop": bool(ev.get("loop")), "note": ev.get("note", ""), "cands": rows}
    return by_event, by_name


def own_takes(kind):
    """Own recordings in the workspace: {key: [take...]}; key is the event id, or '<set>/<situation>_<n>'."""
    base, out = os.path.join(WS, "recordings", kind), {}
    if not os.path.isdir(base):
        return out
    for folder in sorted(os.listdir(base)):
        d = os.path.join(base, folder)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            rel = folder + "/" + name
            m = (OWN_SFX if kind == "sfx" else OWN_VOICE).match(rel)
            if not m or (kind == "sfx" and m.group(1) != m.group(2)):
                continue
            side = read_json(os.path.join(d, name + ".json"), {})
            key = folder if kind == "sfx" else folder + "/" + m.group(2)
            out.setdefault(key, []).append({"src": "own:" + kind + "/" + rel, "name": name, "take": int(m.group(3)), "url": "/_ws/recordings/" + kind + "/" + rel,
                                            "edit": side.get("edit", {}), "info": side.get("info", {}), "created": side.get("created", ""), "device": side.get("device", "")})
    return out


def choices():
    c = read_json(os.path.join(WS, "choices.json"), {})
    c.setdefault("sfx", {})
    c.setdefault("voice", {})
    return c


def save_choices(c):
    write_json(os.path.join(WS, "choices.json"), c)


def credits():
    return read_json(CREDITS_JSON, {})


def save_credits(c):
    if not c and not tracked("docs/sound_credits.json"):
        for p in (CREDITS_JSON, CREDITS_MD):
            if os.path.exists(p):
                os.remove(p)
        return
    write_json(CREDITS_JSON, dict(sorted(c.items())))
    out = ["# Sound credits", "",
           "Generated by the Sound Editor (`tools/sound-editor/serve.py`) from `docs/sound_credits.json`; do not edit by hand.",
           "Every shipped sound that is not rendered by `tools/sfx/synth`: where it came from and under what licence.",
           "Only CC0 (public domain) sounds and the owner's own recordings are installed; none needs attribution.", "",
           "| Game file | Source | Author | Licence | From | Installed | Note |", "|---|---|---|---|---|---|---|"]
    for f, e in sorted(c.items()):
        out.append("| `%s` | %s | %s | %s | %s | %s | %s |" % (f, e.get("source", ""), e.get("author", ""), e.get("licence", ""),
                                                               ("<%s>" % e["url"]) if e.get("url") else e.get("file", ""), e.get("date", ""), e.get("note", "").replace("|", "/")))
    write_text(CREDITS_MD, "\n".join(out) + "\n")


# ------------------------------------------------------------------ sources and their licence
def resolve(src, by_name=None):
    """A source string from the page -> (path, provenance). Refuses anything not CC0 or own."""
    if not isinstance(src, str):
        raise Refused("bad source")
    if src.startswith("cand:"):
        name = src[5:]
        if not CAND.match(name):
            raise Refused("bad candidate name")
        by_name = by_name if by_name is not None else candidates()[1]
        row = by_name.get(name)
        path = os.path.join(WS, "masters", name + ".flac")
        if not row or not os.path.isfile(path) or not inside(os.path.join(WS, "masters"), path):
            raise Refused("%s has no licence entry in the workspace's data.js: not installed" % name)
        if not row["licence"].strip().upper().startswith("CC0"):
            raise Refused("%s is licensed '%s', not CC0: not installed" % (name, row["licence"] or "nothing stated"))
        return path, {"source": row["source"], "author": row["author"], "licence": row["licence"], "url": row["url"], "file": row["raw"],
                      "note": ("Check: " + row["flag"]) if row["flag"] else ""}
    if src.startswith("own:"):
        rel = src[4:]
        kind, _, rest = rel.partition("/")
        if kind not in ("sfx", "voice") or not (OWN_SFX if kind == "sfx" else OWN_VOICE).match(rest):
            raise Refused("bad recording name")
        path = os.path.join(WS, "recordings", kind, *rest.split("/"))
        if not os.path.isfile(path) or not inside(os.path.join(WS, "recordings"), path):
            raise Refused("no such recording: " + rel)
        side = read_json(path + ".json", {})
        return path, {"source": "Own recording", "author": "the owner", "licence": OWN, "url": "", "file": "recordings/" + rel,
                      "note": "recorded " + side.get("created", "")[:10]}
    raise Refused("unknown source: " + src[:40])


def edit_of(path):
    return read_json(path + ".json", {}).get("edit", {})


def num(edit, key, default, lo, hi):
    try:
        v = float(edit.get(key, default))
    except (TypeError, ValueError):
        v = default
    return default if math.isnan(v) else max(lo, min(hi, v))


# ------------------------------------------------------------------ rendering with ffmpeg
def probe(path):
    _, _, err = ff(["-i", path])
    d = re.search(r"Duration: (\d+):(\d+):(\d+\.\d+)", err)
    return (int(d.group(1)) * 3600 + int(d.group(2)) * 60 + float(d.group(3))) if d else 0.0


def loudness(path, seconds):
    """Integrated loudness (EBU R128) and true peak; a sound under 1.2 s is measured repeated, which gives its ungated level."""
    pre = ["-stream_loop", str(int(math.ceil(3.0 / max(seconds, 0.02))))] if seconds < 1.2 else []
    _, _, err = ff([*pre, "-i", path, "-af", "ebur128=peak=true", "-f", "null", "-"])
    tail = err[err.rfind("Summary:"):]
    i, p = re.search(r"I:\s+(-?[\d.]+|-inf) LUFS", tail), re.search(r"Peak:\s+(-?[\d.]+|-inf) dBFS", tail)
    f = lambda m: float(m.group(1)) if m and m.group(1) != "-inf" else float("-inf")
    return f(i), f(p)


def render(src, edit, kind, dest):
    """kind: 'oneshot' | 'loop' | 'voice'. Returns the stats of what was written."""
    total = probe(src)
    if total <= 0:
        raise Refused("ffmpeg cannot read " + os.path.basename(src))
    t0 = num(edit, "t0", 0, 0, total)
    t1 = num(edit, "t1", total, 0, total)
    if t1 - t0 < 0.02:
        t0, t1 = 0, total
    target = num(edit, "target", {"oneshot": -18, "loop": -24, "voice": -16}[kind], -40, -6)
    gain = num(edit, "gain", 0, -24, 12)
    ceiling = -1.5 if kind == "voice" else -1.0
    stereo = (kind == "loop") if edit.get("channels") not in (1, 2) else edit["channels"] == 2
    if kind == "voice":
        stereo = False
    chain = ["atrim=start=%.5f:end=%.5f" % (t0, t1), "asetpts=PTS-STARTPTS", "aresample=48000"]
    semis = num(edit, "pitch", 0, -12, 12) if edit.get("bake_fx") else 0
    if semis:
        chain += ["asetrate=%d" % round(48000 * 2 ** (semis / 12.0)), "aresample=48000"]
    seconds = (t1 - t0) / (2 ** (semis / 12.0))
    graph = ",".join(chain)
    seam = kind == "loop" and edit.get("seam", True) and seconds > 0.6
    if seam:
        cf = min(1.0, seconds * 0.15)
        graph += (",asplit[a][b];[a]atrim=start=%.5f,asetpts=PTS-STARTPTS[body];[b]atrim=end=%.5f,asetpts=PTS-STARTPTS[head];"
                  "[body][head]acrossfade=d=%.5f:c1=qsin:c2=qsin" % (cf, cf, cf))
        seconds -= cf
    else:
        fi = num(edit, "fade_in", 2 if kind != "voice" else 10, 0, 5000) / 1000.0
        fo = num(edit, "fade_out", 30 if kind != "loop" else 5, 0, 10000) / 1000.0
        fi, fo = min(fi, seconds / 3), min(fo, seconds / 3)
        if fi > 0:
            graph += ",afade=t=in:d=%.5f" % fi
        if fo > 0:
            graph += ",areverse,afade=t=in:d=%.5f,areverse" % fo
    if edit.get("bake_fx") and edit.get("reverb"):
        graph += ",aecho=0.8:0.6:45|90:0.25|0.15"
    with tempfile.TemporaryDirectory(prefix="sound-editor-") as tmp:
        a, b = os.path.join(tmp, "a.wav"), os.path.join(tmp, "b.wav")
        code, _, err = ff(["-y", "-i", src, "-filter_complex", graph, "-ac", "2" if stereo else "1", "-ar", "48000", "-c:a", "pcm_f32le", a])
        if code:
            raise Refused("ffmpeg could not render it: " + err.strip().splitlines()[-1][:200])
        lufs, peak = loudness(a, seconds)
        applied = 0.0
        for _ in range(3):   # gain to the target; a limiter only if the peak would pass the ceiling; measure again
            if lufs < -69:
                raise Refused("the kept part is silent: nothing was installed (check the microphone, or the trim)")
            step = target + gain - lufs
            if abs(step) < 0.15:
                break
            af = "volume=%.3fdB" % step
            if peak + step > ceiling:
                af += ",alimiter=limit=%.4f:attack=2:release=60:level=false:latency=true" % (10 ** (ceiling / 20.0))
            code, _, err = ff(["-y", "-i", a, "-af", af, "-c:a", "pcm_f32le", b])
            if code:
                raise Refused("ffmpeg could not set the level: " + err.strip().splitlines()[-1][:200])
            a, b = b, a
            applied += step
            lufs, peak = loudness(a, seconds)
        enc = {"oneshot": ["-b:a", "72k"], "loop": ["-b:a", "112k"], "voice": ["-q:a", "5"]}[kind]
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        out = os.path.join(tmp, "out.mp3")
        code, _, err = ff(["-y", "-i", a, "-ac", "2" if stereo else "1", "-ar", "48000", "-c:a", "libmp3lame", *enc, out])
        if code:
            raise Refused("ffmpeg could not write the MP3: " + err.strip().splitlines()[-1][:200])
        _, pcm, _ = ff(["-i", out, "-f", "s16le", "-ac", "1", "-ar", "48000", "-"])
        samples = array.array("h")
        samples.frombytes(pcm[:len(pcm) // 2 * 2])
        pk = max((abs(v) for v in samples), default=0) / 32768.0
        shutil.copyfile(out, dest)
    stats = {"seconds": round(len(samples) / 48000.0, 3), "peak_db": round(20 * math.log10(max(pk, 1e-6)), 2), "lufs": round(lufs, 2), "dc": 0.0,
             "bytes": os.path.getsize(dest), "recorded": True}
    if kind == "loop":
        stats.update({"loop_start": 0.0, "loop_seconds": round(min(seconds, stats["seconds"]), 4)})
    return stats


# ------------------------------------------------------------------ installing effects
def sfx_state(sound, head):
    """The ordered sources now in the game for a sound: the workspace's choice, or git's own files."""
    c = choices()["sfx"].get(sound["id"])
    return c if c else ["game:%d" % (i + 1) for i in range(len(head.get("files", sound["files"])))]


def run_generators(which):
    """The repo's own generators: docs/SOUND_EFFECTS.md, or docs/VOICE_LINES.md and docs/VOICE_CHECKLIST.md.

    The voice index (game/web/voice/available.json: the recorded lines the game may fetch) is written here and not by
    `tools/voice/script.py --index`, which today would also list the 125 rendered placeholder cries (it walks the whole
    folder); the game only ever asks whether a line's own file is in the list.
    """
    for script in which:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", script, "script.py")], capture_output=True, cwd=ROOT)
    if "voice" in which:
        files = {l["file"] for s in lines_manifest()["sets"] for x in s["situations"] for l in x["lines"]}
        keep = [f for f in head_json("game/web/voice/available.json", []) if f not in files and os.path.exists(os.path.join(WEB, f))]
        write_json(os.path.join(VOICE, "available.json"), sorted(set(keep) | {f for f in files if os.path.exists(os.path.join(WEB, f))}), indent=0)


def choose_sfx(sid, srcs):
    if not ID.match(sid or ""):
        raise Refused("bad sound id")
    m = manifest()
    sound = next((s for s in m["sounds"] if s["id"] == sid), None)
    if not sound:
        raise Refused(sid + " is not in sounds.json: the game has no such sound yet, so there is nowhere to install it")
    head_m = head_json("game/web/sfx/sounds.json", {"sounds": []})
    head = next((s for s in head_m["sounds"] if s["id"] == sid), {"files": []})
    head_stats = head_json("game/web/sfx/stats.json", {})
    n_head = len(head["files"])
    if not isinstance(srcs, list) or not 1 <= len(srcs) <= 12:
        raise Refused("a sound needs between 1 and 12 variants")
    original = ["game:%d" % (i + 1) for i in range(n_head)]
    kind = "loop" if is_loop(sound, head_stats) else "oneshot"
    by_name = candidates()[1]
    plan = []
    for src in srcs:   # everything is checked before anything is written
        g = re.match(r"^game:(\d{1,2})$", src) if isinstance(src, str) else None
        if g:
            k = int(g.group(1))
            if not 1 <= k <= n_head:
                raise Refused("the game has no original variant %d of %s" % (k, sid))
            plan.append(("game", k, None, None))
        else:
            path, prov = resolve(src, by_name)
            plan.append(("file", src, path, prov))
    stats = read_json(os.path.join(SFX, "stats.json"), {})
    recorded = set(read_json(os.path.join(SFX, "recorded.json"), []))
    cred = credits()
    today = datetime.date.today().isoformat()
    said = []
    for i, (what, ref, path, prov) in enumerate(plan):
        rel = "sfx/%s_%d.mp3" % (sid, i + 1)
        dest = os.path.join(SFX, "%s_%d.mp3" % (sid, i + 1))
        if not inside(SFX, dest):
            raise Refused("path outside game/web/sfx")
        if what == "game":
            src_rel = "sfx/%s_%d.mp3" % (sid, ref)
            data = git("show", "HEAD:game/web/" + src_rel, binary=True)
            if data is None:
                raise Refused("git has no " + src_rel)
            with open(dest, "wb") as f:
                f.write(data)
            st = dict(head_stats.get(src_rel, {}))
            if ref == i + 1:
                recorded.discard(rel)
                cred.pop(rel, None)
            else:      # an original in another slot: kept from the renderer, and said so
                recorded.add(rel)
                st["recorded"] = True
                cred[rel] = {"source": "The game's own rendered sound", "author": "tools/sfx/synth", "licence": "the game's own", "url": "", "file": "game/web/" + src_rel,
                             "date": today, "note": "variant %d moved to slot %d" % (ref, i + 1)}
            if st:
                stats[rel] = st
            said.append("%s_%d ← the game's original %d" % (sid, i + 1, ref))
        else:
            stats[rel] = render(path, edit_of(path), kind, dest)
            recorded.add(rel)
            cred[rel] = dict(prov, date=today)
            said.append("%s_%d ← %s" % (sid, i + 1, ("your take %d" % int(path[-6:-4])) if ref.startswith("own:") else os.path.basename(path)[:-5]))
    # Slots past the new count: back to what git has (or gone).
    for i in range(len(plan), max(n_head, len(sound["files"]), 12)):
        rel = "sfx/%s_%d.mp3" % (sid, i + 1)
        if os.path.exists(os.path.join(WEB, rel)) or tracked("game/web/" + rel):
            restore("game/web/" + rel)
        recorded.discard(rel)
        cred.pop(rel, None)
        if rel in head_stats:
            stats[rel] = head_stats[rel]
        else:
            stats.pop(rel, None)
    files = ["sfx/%s_%d.mp3" % (sid, i + 1) for i in range(len(plan))]
    was = json.dumps(sound, sort_keys=True)
    sound["files"], sound["variants"] = files, len(files)
    if srcs == original:      # the game's original: its mix level and category too, as git has them
        for key in ("level", "category"):
            if key in head:
                sound[key] = head[key]
    if json.dumps(sound, sort_keys=True) != was:
        write_json(os.path.join(SFX, "sounds.json"), m)
    write_json(os.path.join(SFX, "stats.json"), dict(sorted(stats.items())), indent=0)
    rec_path = os.path.join(SFX, "recorded.json")
    if recorded or tracked("game/web/sfx/recorded.json"):
        write_json(rec_path, sorted(recorded), indent=0)
    elif os.path.exists(rec_path):
        os.remove(rec_path)
    save_credits(cred)
    c = choices()
    if srcs == original:
        c["sfx"].pop(sid, None)
    else:
        c["sfx"][sid] = srcs
    save_choices(c)
    run_generators(["sfx"])
    return "In the game: " + "; ".join(said) if srcs != original else "%s is the game's original again" % sid


def set_meta(sid, level, category):
    if not ID.match(sid or ""):
        raise Refused("bad sound id")
    m = manifest()
    sound = next((s for s in m["sounds"] if s["id"] == sid), None)
    if not sound:
        raise Refused("no such sound")
    if level is not None:
        if not isinstance(level, (int, float)) or not -40 <= level <= 12:
            raise Refused("the mix level must be between -40 and +12 dB")
        sound["level"] = int(level) if float(level).is_integer() else round(float(level), 1)
    if category is not None:
        if category not in [c["id"] for c in m["categories"]]:
            raise Refused("no such category")
        sound["category"] = category
    write_json(os.path.join(SFX, "sounds.json"), m)
    run_generators(["sfx"])
    return "%s: mix level %s dB, category %s (sounds.json)" % (sid, sound.get("level", 0), sound["category"])


# ------------------------------------------------------------------ installing voice lines
def voice_line(line_id):
    for s in lines_manifest()["sets"]:
        for x in s["situations"]:
            for l in x["lines"]:
                if l["id"] == line_id:
                    return s, x, l
    raise Refused("no such line: " + str(line_id)[:60])


def choose_voice(line_id, src):
    s, x, line = voice_line(line_id)
    m = VOICE_FILE.match(line["file"])
    if not m or m.group(1) != s["id"]:
        raise Refused("unexpected file name in lines.json: " + line["file"])
    dest = os.path.join(WEB, *line["file"].split("/"))
    if not inside(VOICE, dest):
        raise Refused("path outside game/web/voice")
    cred, c = credits(), choices()
    if src is None:
        restore("game/web/" + line["file"])
        cred.pop(line["file"], None)
        c["voice"].pop(line_id, None)
        said = "%s is as git has it again (%s)" % (line["file"], "recorded" if os.path.exists(dest) else "no recording: the placeholder plays")
    else:
        if not (isinstance(src, str) and src.startswith("own:voice/" + s["id"] + "/" + m.group(2) + ".take")):
            raise Refused("that take is not of this line")
        path, prov = resolve(src)
        render(path, edit_of(path), "voice", dest)
        cred[line["file"]] = dict(prov, date=datetime.date.today().isoformat(), note=("“%s” · " % line["text"]) + prov["note"])
        c["voice"][line_id] = src
        said = "In the game: %s ← your take %d" % (line["file"][6:-4], int(src[-6:-4]))
    save_credits(cred)
    save_choices(c)
    run_generators(["voice"])
    return said


# ------------------------------------------------------------------ recordings and edits
def save_recording(q, body):
    kind = q.get("kind")
    if len(body) < 44 or body[:4] != b"RIFF" or body[8:12] != b"WAVE":
        raise Refused("not a WAV file")
    if kind == "sfx":
        event = q.get("event", "")
        if not EVENT.match(event):
            raise Refused("bad event name (use lower-case letters, digits and _; it starts with snd_ or new_)")
        folder, stem, pat = os.path.join(WS, "recordings", "sfx", event), event + "_own", "%s%02d.wav"
    elif kind == "voice":
        s, x, line = voice_line(q.get("line"))
        m = VOICE_FILE.match(line["file"])
        if not m:
            raise Refused("unexpected file name in lines.json")
        folder, stem, pat = os.path.join(WS, "recordings", "voice", s["id"]), m.group(2) + ".take", "%s%02d.wav"
    else:
        raise Refused("bad kind")
    if not inside(os.path.join(WS, "recordings"), folder):
        raise Refused("path outside the workspace")
    os.makedirs(folder, exist_ok=True)
    n = 1
    while os.path.exists(os.path.join(folder, pat % (stem, n))):
        n += 1
        if n > 99:
            raise Refused("99 takes already: delete some")
    path = os.path.join(folder, pat % (stem, n))
    with open(path, "wb") as f:
        f.write(body)
    try:
        meta = json.loads(q.get("meta") or "{}")
    except ValueError:
        meta = {}
    side = {"own": True, "created": datetime.datetime.now().isoformat(timespec="seconds"), "device": str(meta.get("device", ""))[:120],
            "edit": meta.get("edit") if isinstance(meta.get("edit"), dict) else {}, "info": meta.get("info") if isinstance(meta.get("info"), dict) else {}}
    write_json(path + ".json", side)
    rel = os.path.relpath(path, os.path.join(WS, "recordings")).replace(os.sep, "/")
    return {"src": "own:" + rel, "name": os.path.basename(path), "take": n, "url": "/_ws/recordings/" + rel, "edit": side["edit"], "info": side["info"], "created": side["created"],
            "device": side["device"], "said": "Saved to the workspace: recordings/" + rel}


def where_used(src):
    c = choices()
    return [sid for sid, lst in c["sfx"].items() if src in lst], [lid for lid, s in c["voice"].items() if s == src]


def save_edit(src, edit, info):
    path, _ = resolve(src)
    if not isinstance(edit, dict):
        raise Refused("bad edit")
    clean = {k: edit[k] for k in ("t0", "t1", "fade_in", "fade_out", "gain", "target", "channels", "seam", "pitch", "reverb", "bake_fx") if k in edit}
    side = read_json(path + ".json", {})
    side["edit"] = clean
    if isinstance(info, dict):
        side["info"] = info
    write_json(path + ".json", side)
    said = ["Edit saved beside the sound in the workspace."]
    sfx_ids, line_ids = where_used(src)        # a sound that is in the game is rendered again with its new edit
    for sid in sfx_ids:
        said.append(choose_sfx(sid, choices()["sfx"][sid]))
    for lid in line_ids:
        said.append(choose_voice(lid, src))
    return " ".join(said)


def delete_take(src):
    if not str(src).startswith("own:"):
        raise Refused("only own recordings can be deleted here")
    path, _ = resolve(src)
    sfx_ids, line_ids = where_used(src)
    if sfx_ids or line_ids:
        raise Refused("that take is in the game (%s): choose something else first" % ", ".join(sfx_ids + line_ids))
    os.remove(path)
    if os.path.exists(path + ".json"):
        os.remove(path + ".json")
    return "Deleted " + os.path.basename(path)


# ------------------------------------------------------------------ what differs from git, and undo
def changes():
    out = git("status", "--porcelain", "--untracked-files=all", "--", "game/web/sfx", "game/web/voice", "docs/SOUND_EFFECTS.md", "docs/VOICE_LINES.md",
              "docs/VOICE_CHECKLIST.md", "docs/SOUND_CREDITS.md", "docs/sound_credits.json")
    rows, cred = [], credits()
    for line in (out or "").splitlines():
        code, rel = line[:2], line[3:].strip().strip('"')
        web = rel[len("game/web/"):] if rel.startswith("game/web/") else None
        audio = bool(web and (SFX_FILE.match(web) or VOICE_FILE.match(web)))
        rows.append({"path": rel, "state": "new" if "?" in code or "A" in code else "deleted" if "D" in code else "replaced" if audio else "regenerated",
                     "audio": audio, "credit": cred.get(web) if web else None, "url": "/" + rel if audio else None})
    rows.sort(key=lambda r: (not r["audio"], r["path"]))
    return rows


def undo(rel):
    web = rel[len("game/web/"):] if isinstance(rel, str) and rel.startswith("game/web/") else ""
    m = SFX_FILE.match(web)
    if m:
        sid, slot = m.group(1), int(m.group(2))
        sound = next((s for s in manifest()["sounds"] if s["id"] == sid), None)
        head = next((s for s in head_json("game/web/sfx/sounds.json", {"sounds": []})["sounds"] if s["id"] == sid), {"files": []})
        if not sound:
            restore(rel)
            return rel + " restored from git"
        now = list(sfx_state(sound, head))
        if slot <= len(head["files"]):
            while len(now) < slot:
                now.append("game:%d" % (len(now) + 1))
            now[slot - 1] = "game:%d" % slot
        elif slot <= len(now):
            del now[slot - 1]
        return choose_sfx(sid, now)
    if VOICE_FILE.match(web):
        for s in lines_manifest()["sets"]:
            for x in s["situations"]:
                for l in x["lines"]:
                    if l["file"] == web:
                        return choose_voice(l["id"], None)
        restore(rel)
        run_generators(["voice"])
        return rel + " restored from git"
    raise Refused("only a game sound file can be undone here; the manifests and docs follow it")


# ------------------------------------------------------------------ what the pages read
def effects_payload():
    m = manifest()
    head_m = head_json("game/web/sfx/sounds.json", {"sounds": []})
    heads = {s["id"]: s for s in head_m["sounds"]}
    head_stats = head_json("game/web/sfx/stats.json", {})
    by_event, _ = candidates()
    own = own_takes("sfx")
    cred = credits()
    sounds = []
    for s in m["sounds"]:
        head = heads.get(s["id"], {"files": []})
        sounds.append({"id": s["id"], "loop": is_loop(s, head_stats), "originals": len(head["files"]), "chosen": sfx_state(s, head),
                       "head_level": head.get("level"), "head_category": head.get("category"),
                       "credits": {f: cred[f] for f in s["files"] if f in cred}})
    ids = {s["id"] for s in m["sounds"]}
    extra = [{"id": e, "title": by_event[e]["title"] if e in by_event else e, "loop": by_event[e]["loop"] if e in by_event else False,
              "note": by_event[e]["note"] if e in by_event else "An event of your own."}
             for e in sorted(set(by_event) | set(own)) if e not in ids]
    return {"sounds": sounds, "events": {k: v for k, v in by_event.items()}, "own": own, "extra": extra}


def voices_payload():
    return {"own": own_takes("voice"), "chosen": choices()["voice"], "credits": {f: e for f, e in credits().items() if f.startswith("voice/")}}


# ------------------------------------------------------------------ HTTP
class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = "SoundEditor/1"

    def translate_path(self, path):
        clean = urllib.parse.unquote(path.split("?", 1)[0].split("#", 1)[0])
        if clean.startswith("/_ws/"):
            rel = clean[5:]
            first = rel.split("/", 1)[0]
            if first in ("candidates", "masters", "recordings", "current") or rel in ("LICENCES.md", "README.md", "data.js"):
                self.directory = WS
                try:
                    got = super().translate_path("/" + rel)
                finally:
                    self.directory = ROOT
                allowed = os.path.join(WS, first) if "/" in rel else os.path.join(WS, rel)
                return got if inside(allowed, got) else os.path.join(ROOT, "__nothing__")
            return os.path.join(ROOT, "__nothing__")
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass

    def local(self):
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0]
        return host in ("127.0.0.1", "localhost")

    def reply(self, code, data):
        body = json.dumps(data).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if not url.path.startswith("/api/"):
            return super().do_GET()
        if not self.local():
            return self.reply(403, {"error": "local use only"})
        try:
            with LOCK:
                if url.path == "/api/status":
                    data = {"ok": True, "repo": ROOT, "workspace": WS, "ffmpeg": bool(FFMPEG), "ffmpeg_help": "" if FFMPEG else NO_FFMPEG,
                            "branch": (git("rev-parse", "--abbrev-ref", "HEAD") or "").strip(), "candidates": os.path.isfile(os.path.join(WS, "data.js"))}
                elif url.path == "/api/effects":
                    data = effects_payload()
                elif url.path == "/api/voices":
                    data = voices_payload()
                elif url.path == "/api/original":
                    rel = urllib.parse.parse_qs(url.query).get("file", [""])[0]
                    blob = git("show", "HEAD:game/web/" + rel, binary=True) if (SFX_FILE.match(rel) or VOICE_FILE.match(rel)) else None
                    if blob is None:
                        return self.reply(404, {"error": "git has no such file"})
                    self.send_response(200)
                    self.send_header("Content-Type", "audio/mpeg")
                    self.send_header("Content-Length", str(len(blob)))
                    self.end_headers()
                    self.wfile.write(blob)
                    return
                elif url.path == "/api/changes":
                    data = {"changes": changes()}
                else:
                    return self.reply(404, {"error": "no such call"})
            self.reply(200, data)
        except Refused as e:
            self.reply(400, {"error": str(e)})

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        origin = self.headers.get("Origin")
        if not self.local() or self.headers.get("X-Sound-Editor") != "1" or (origin and urllib.parse.urlsplit(origin).hostname not in ("127.0.0.1", "localhost")):
            return self.reply(403, {"error": "local use only"})
        try:
            size = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            size = -1
        if not 0 <= size <= 400 * 1024 * 1024:
            return self.reply(413, {"error": "too large"})
        body = self.rfile.read(size)
        try:
            with LOCK:
                if url.path == "/api/record":
                    q = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
                    return self.reply(200, save_recording(q, body))
                try:
                    j = json.loads(body.decode("utf-8") or "{}")
                except ValueError:
                    raise Refused("bad request")
                if url.path == "/api/choose":
                    said = choose_sfx(j.get("id"), j.get("srcs")) if j.get("kind") == "sfx" else choose_voice(j.get("line"), j.get("src"))
                elif url.path == "/api/meta":
                    said = set_meta(j.get("id"), j.get("level"), j.get("category"))
                elif url.path == "/api/edit":
                    said = save_edit(j.get("src"), j.get("edit"), j.get("info"))
                elif url.path == "/api/delete":
                    said = delete_take(j.get("src"))
                elif url.path == "/api/undo":
                    said = undo(j.get("path"))
                else:
                    return self.reply(404, {"error": "no such call"})
            self.reply(200, {"ok": True, "said": said})
        except Refused as e:
            self.reply(400, {"error": str(e)})
        except Exception as e:  # the page shows it; the server stays up
            self.reply(500, {"error": "%s: %s" % (type(e).__name__, e)})


def main():
    global WS, FFMPEG
    ap = argparse.ArgumentParser(description="The Sound Editor's local server.")
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--workspace", default=os.environ.get("SOUND_EDITOR_WORKSPACE", "~/Desktop/sound-sources"))
    a = ap.parse_args()
    WS = os.path.realpath(os.path.expanduser(a.workspace))
    if inside(ROOT, WS) and not inside(os.path.join(ROOT, "tools", "sound-editor", "workspace"), WS):
        sys.exit("The workspace must be outside the repository (or tools/sound-editor/workspace/, which git ignores).")
    os.makedirs(os.path.join(WS, "recordings"), exist_ok=True)
    FFMPEG = find_ffmpeg()
    port = a.port
    for _ in range(20):
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) != 0:
                break
        port += 1
    http.server.ThreadingHTTPServer.request_queue_size = 128
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=ROOT))
    print("Sound Editor: http://127.0.0.1:%d/tools/sound-editor/" % port)
    print("  workspace:  %s" % WS)
    print("  ffmpeg:     %s" % (FFMPEG or "NOT FOUND: you can audition and record, not install. " + NO_FFMPEG))
    sys.stdout.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
