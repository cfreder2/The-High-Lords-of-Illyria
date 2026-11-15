#!/usr/bin/env python3
"""The sound audit's numbers, and the code's own list of what asks for each sound, as one file the Sounds page reads.

    python3 tools/audit/import_sound_audit.py [AUDIT_DIR]      (default ~/Desktop/sound-audit)

Reads AUDIT_DIR/measurements.csv (one row an effect file), voice_placeholders.csv and PUNCHLIST.md (the flags
column of its measurements table, and its coverage table: what a player expects to hear and what plays), and from
the repository game/include/illyria/game/sfx.h and game/src/sfx.cpp (the Sound enum, kSpecs, kCore in sfx.js, and
every place a sound is asked for). Writes tools/audit/sound-audit.json. Without AUDIT_DIR's files the code's part
is still written, so the page works with no measurements.
"""
import csv
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AUDIT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/Desktop/sound-audit")

FILE_COLS = ["seconds", "lufs", "target_lufs", "lufs_err", "played_lufs_at_gain1", "true_peak_db", "clipped_samples", "attack_ms",
             "centroid_hz", "pct_lt300", "pct_2k_8k", "pct_gt8k", "dominant_hz", "dominant_pct", "closest_variant", "closest_wave_corr",
             "loop_seconds", "loop_level_range_db"]
VOICE_COLS = ["seconds", "lufs", "true_peak_db", "centroid_hz", "pct_lt300"]


def num(v):
    try:
        return round(float(v), 3)
    except (TypeError, ValueError):
        return v if v else None


def read_csv(name, cols):
    path = os.path.join(AUDIT, name)
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            out[row["file"]] = {c: num(row.get(c)) for c in cols if row.get(c) not in (None, "")}
    return out


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def punchlist():
    path = os.path.join(AUDIT, "PUNCHLIST.md")
    flags, coverage, items = {}, [], []
    if not os.path.exists(path):
        return flags, coverage, items, None
    text = open(path).read()
    audited = re.search(r"Audit of ([^,]+), of the tree at `[^`]*` \(commit `([0-9a-f]+)`\)", text)
    section = ""
    group = ""
    for line in text.splitlines():
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        m = re.match(r"\*\*(P\d-\d+)\. (.+?)\*\*", line)
        if m:
            items.append({"id": m.group(1), "title": m.group(2).rstrip(".")})
        if not line.startswith("|") or set(line) <= set("|-: "):
            continue
        c = cells(line)
        if section.startswith("Coverage") and len(c) == 3 and c[0] != "Event":
            if c[0].startswith("**") and not c[1] and not c[2]:
                group = c[0].strip("*")
            elif c[0].startswith("**"):
                coverage.append({"group": c[0].strip("*"), "event": c[0].strip("*"), "state": c[1], "notes": c[2]})
            else:
                coverage.append({"group": group, "event": c[0], "state": c[1], "notes": c[2]})
        elif section.startswith("Measurements") and c[0].startswith("`"):
            # (rows of loops end in a figure, not in flags)
            flags["snd_" + c[0].strip("`")] = [f.strip() for f in c[-1].split(",") if re.search(r"[A-Za-z]", f)]
    return flags, coverage, items, ({"date": audited.group(1), "commit": audited.group(2)} if audited else None)


def code():
    """The Sound enum in order, kSpecs beside it, and each place a sound is asked for."""
    header = open(os.path.join(ROOT, "game/include/illyria/game/sfx.h")).read()
    body = header[header.index("enum class Sound"):]
    body = body[body.index("{") + 1:body.index("};")]
    names = [m.group(1) for m in re.finditer(r"^\s*([A-Z]\w+),", body, re.M) if m.group(1) != "Count"]
    cpp = open(os.path.join(ROOT, "game/src/sfx.cpp")).read()
    specs = re.findall(r'\{"(snd_\w+)",\s*(\d+),\s*(\d+),\s*(true|false)\}', cpp)
    assert len(specs) == len(names), (len(specs), len(names))
    sounds = {}
    for name, (sid, priority, gap, loud) in zip(names, specs):
        sounds[sid] = {"enum": name, "priority": int(priority), "gapTicks": int(gap), "loud": loud == "true", "asked": []}
    by_enum = {v["enum"]: k for k, v in sounds.items()}
    for folder in ("game/src", "game/app", "game/native", "game/web"):
        for base, _, files in os.walk(os.path.join(ROOT, folder)):
            for f in sorted(files):
                if not f.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(base, f)
                rel = os.path.relpath(path, ROOT)
                for n, line in enumerate(open(path, errors="replace"), 1):
                    for m in re.finditer(r"Sound::(\w+)", line):
                        sid = by_enum.get(m.group(1))
                        # (the death's `fall` is chosen on one line and asked for on others: the choosing counts)
                        if sid and ("want(" in line or "alert(" in line or "Sound fall" in line or "Sound hit" in line or "hit = " in line or "return Sound::" in line):
                            sounds[sid]["asked"].append({"at": f"{rel}:{n}", "code": line.strip()[:160]})
    js = open(os.path.join(ROOT, "game/web/sfx.js")).read()
    core = re.findall(r'"(snd_\w+)"', js[js.index("kCore"):js.index("];", js.index("kCore"))])
    layers = sorted(set(re.findall(r'setLayer\("(snd_\w+)"', js)))
    ui = sorted(set(re.findall(r'ui\("(snd_\w+)"\)', js)))
    recorded = []
    rec = os.path.join(ROOT, "tools/sfx/recorded.json")
    if os.path.exists(rec):
        r = json.load(open(rec))
        recorded = sorted(r if isinstance(r, list) else r.keys())
    return {"sounds": sounds, "core": core, "layers": layers, "ui": ui, "recorded": recorded}


def main():
    flags, coverage, items, audited = punchlist()
    out = {
        "about": "Generated by tools/audit/import_sound_audit.py: do not edit. The measurements are the sound audit's; 'code' is read from sfx.h, sfx.cpp and sfx.js.",
        "audited": audited,
        "code": code(),
        "files": read_csv("measurements.csv", FILE_COLS),
        "voiceFiles": read_csv("voice_placeholders.csv", VOICE_COLS),
        "flags": flags,
        "coverage": coverage,
        "punchlist": items,
    }
    path = os.path.join(ROOT, "tools/audit/sound-audit.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=0, separators=(",", ":"))
        f.write("\n")
    asked = sum(1 for s in out["code"]["sounds"].values() if s["asked"])
    print(f"{path}: {len(out['files'])} files measured, {len(flags)} sounds flagged, {len(coverage)} coverage rows, "
          f"{asked} of {len(out['code']['sounds'])} enum sounds asked for, {len(items)} punch-list items")
    for sid, s in out["code"]["sounds"].items():
        if not s["asked"]:
            print("  never asked for:", sid)


if __name__ == "__main__":
    main()
