#!/usr/bin/env python3
"""The voice actors' script, generated from game/web/voice/lines.json.

    python3 tools/voice/script.py           # docs/VOICE_LINES.md and docs/VOICE_CHECKLIST.md
    python3 tools/voice/script.py --index   # also game/web/voice/available.json (what is recorded)
    python3 tools/voice/script.py --check   # fails if the docs are out of date with the manifest

Lines already recorded (an .mp3 under game/web/voice/<set>/) are ticked in
the checklist. tools/voice/convert.sh runs --index after converting takes.
"""
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MANIFEST = os.path.join(ROOT, "game", "web", "voice", "lines.json")
WEB = os.path.join(ROOT, "game", "web")
SCRIPT = os.path.join(ROOT, "docs", "VOICE_LINES.md")
CHECKLIST = os.path.join(ROOT, "docs", "VOICE_CHECKLIST.md")
AVAILABLE = os.path.join(WEB, "voice", "available.json")

GUIDE = """\
## Recording guide

**Setup.** A quiet, soft room (a wardrobe full of clothes beats a bare kitchen). A pop filter, the
mouth about 30 cm from the microphone, a little off-axis. Record **mono, 48 kHz, 24-bit WAV**.
Set the gain so the loudest shouts peak around **-6 dBFS** and never clip; quiet lines will be
brought up later, so do not move closer for them.

**Takes.** Record **2-3 takes** of every line, with about **0.5 s of silence** before and after
each. Keep the best take; mark it by naming the file without a take suffix (or with `_a`, `_b`,
`_c` for the takes: the converter keeps `_a` unless you delete it).

**Delivery.** These are barks, not speeches: short, in character, as if heard across a battlefield
(Myth, Warlords). Shout the shouted lines for real, from the chest, standing up. Vary the takes
(pitch, speed, emphasis); the game picks a different line each time, so variety is worth more than
perfection. The **delivery note** on each line says how. The enemy plays the same recordings pitched
lower, so one actor per voice is enough; a different actor for each voice is better.

**Non-verbal voices** (Brute, Undead, Fiend, Beast) have no words: the text in brackets describes
the sound. Growl, moan, hiss, roar; layering two takes, or an animal recording, is fine.

**File names.** Name each take exactly as listed, in a folder per voice, e.g.
`soldier/hold_line_2.wav` (or `soldier/hold_line_2_a.wav` for take a). Lower case, no spaces.

## Putting the recordings in the game

1. Put the WAV files in one folder per voice, e.g. `~/takes/soldier/selected_1.wav`.
2. Run the converter (needs `ffmpeg`):

       tools/voice/convert.sh ~/takes

   It trims the silence, normalises each line to **-16 LUFS** (true peak -1.5 dB), makes it mono
   and writes **MP3** (48 kHz, ~96 kbps, plays on iPhones) to `game/web/voice/<voice>/<name>.mp3`,
   then updates `game/web/voice/available.json` and the checklist.
3. Rebuild (or just reload the page in a dev build). Every recorded line replaces its placeholder
   (the browser reading the text aloud); lines not yet recorded keep the placeholder, so you can
   record a few at a time. The placeholder can be switched off in the ⚙ settings.
"""


def load():
    with open(MANIFEST, encoding="utf-8") as f:
        return json.load(f)


def recorded():
    found = set()
    vdir = os.path.join(WEB, "voice")
    for dirpath, _, files in os.walk(vdir):
        for name in files:
            if name.endswith(".mp3") or name.endswith(".m4a"):
                rel = os.path.relpath(os.path.join(dirpath, name), WEB).replace(os.sep, "/")
                found.add(rel)
    return found


def counts(m):
    rows, total = [], 0
    for s in m["sets"]:
        n = sum(len(x["lines"]) for x in s["situations"])
        total += n
        rows.append((s, n))
    return rows, total


def script(m):
    sits = {x["id"]: x for x in m["situations"]}
    rows, total = counts(m)
    out = ["# Voice lines: the recording script", "",
           "Generated from `game/web/voice/lines.json` by `python3 tools/voice/script.py`; do not edit by hand.",
           "Every line the soldiers can say, by voice and situation, with its file name and how to deliver it.",
           "A printable tick-list of the same files is in [VOICE_CHECKLIST.md](VOICE_CHECKLIST.md).", "",
           GUIDE, "## Counts", "", "| Voice | Used by | Situations | Lines |", "|---|---|---:|---:|"]
    for s, n in rows:
        fb = f" (else {s['fallback']})" if s.get("fallback") else ""
        out.append(f"| {s['name']} (`{s['id']}`){fb} | {', '.join(s['units'])} | {len(s['situations'])} | {n} |")
    out += [f"| **Total** | | | **{total}** |", ""]
    out += ["## Situations", "", "| Id | Situation | When | Priority |", "|---|---|---|---:|"]
    for x in m["situations"]:
        out.append(f"| `{x['id']}` | {x['title']} | {x['when']} | {x['priority']} |")
    out += ["", "Priority 5 is the gravest news (always heard); 0 is idle chatter (heard only when all is quiet).", ""]
    for s, n in rows:
        out += [f"## {s['name']} (`{s['id']}`): {n} lines", "",
                f"*Used by:* {', '.join(s['units'])}.  ", f"*Casting:* {s['casting']}"]
        if s.get("fallback"):
            out.append(f"  \n*Anything not listed here is said in the `{s['fallback']}` voice.*")
        out.append("")
        for x in s["situations"]:
            sit = sits[x["id"]]
            out += [f"### {s['name']}: {sit['title']} (`{x['id']}`)", "", f"*When:* {sit['when']}  ",
                    f"*Delivery:* {x['note']}", "", "| File | Line | Delivery |", "|---|---|---|"]
            for line in x["lines"]:
                name = line["file"].split("/", 1)[1].rsplit(".", 1)[0] + ".wav"
                out.append(f"| `{name}` | {line['text']} | {line.get('note', x['note'])} |")
            out.append("")
    return "\n".join(out)


def checklist(m, have):
    rows, total = counts(m)
    done = sum(1 for s in m["sets"] for x in s["situations"] for l in x["lines"] if l["file"] in have)
    out = ["# Voice lines: checklist", "",
           "Generated by `python3 tools/voice/script.py`; ticks show the lines already in `game/web/voice/`.",
           f"Print it and tick off each line as you record it. **{done} of {total} recorded.**", ""]
    for s, n in rows:
        out += [f"## {s['name']} (`{s['id']}/`, {n} lines)", ""]
        for x in s["situations"]:
            for line in x["lines"]:
                name = line["file"].split("/", 1)[1].rsplit(".", 1)[0]
                mark = "x" if line["file"] in have else " "
                out.append(f"- [{mark}] `{name}` {line['text']}")
        out.append("")
    return "\n".join(out)


def main(argv):
    m = load()
    have = recorded()
    docs = {SCRIPT: script(m) + "\n", CHECKLIST: checklist(m, have) + "\n"}
    if "--check" in argv:
        stale = [p for p, text in docs.items() if not os.path.exists(p) or open(p, encoding="utf-8").read() != text]
        for p in stale:
            print(f"out of date: {os.path.relpath(p, ROOT)} (run python3 tools/voice/script.py)")
        return 1 if stale else 0
    for p, text in docs.items():
        with open(p, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"wrote {os.path.relpath(p, ROOT)}")
    if "--index" in argv:
        with open(AVAILABLE, "w", encoding="utf-8") as f:
            json.dump(sorted(have), f, indent=0)
            f.write("\n")
        print(f"wrote {os.path.relpath(AVAILABLE, ROOT)}: {len(have)} recordings")
    rows, total = counts(m)
    print(f"{total} lines in {len(rows)} voices, {len(have)} recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
