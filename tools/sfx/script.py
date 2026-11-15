#!/usr/bin/env python3
"""The sound effects list, generated from game/web/sfx/sounds.json.

    python3 tools/sfx/script.py           # docs/SOUND_EFFECTS.md
    python3 tools/sfx/script.py --check   # fails if the doc is out of date with the manifest

Sounds replaced by a recording (listed in game/web/sfx/recorded.json by
tools/sfx/convert.sh) are marked; the rest are rendered by tools/sfx/synth.
"""
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WEB = os.path.join(ROOT, "game", "web")
MANIFEST = os.path.join(WEB, "sfx", "sounds.json")
STATS = os.path.join(WEB, "sfx", "stats.json")
RECORDED = os.path.join(WEB, "sfx", "recorded.json")
DOC = os.path.join(ROOT, "docs", "SOUND_EFFECTS.md")

GUIDE = """\
## How it works

The battle has no music (as in Myth): every sound tells the player something. `game/src/sfx.cpp` turns each
tick's simulation events (missiles loosed and landing, blows by damage type and armour, blasts, gates, the
death matrix's deaths, bodies of men on the move, lit fuses) into positional cues, merged and throttled so a
volley is one louder twang and never a roar of noise; it also works out the ambience (wind by the land under
the view, the day's air, swamp insects, carrion flies over the dead, the enemy's war drums, fire, rain, and
from the far camera the mass of a fight and the tramp of columns). The table of cues (`kSpecs` in `sfx.cpp`:
id, priority, gap, whether it carries far) and this manifest (files, levels, and the `mix` block: channels,
fade, the distance filter and reverb) are the one list both players read: `game/web/sfx.js` (Web Audio) and
`game/native/audio.cpp` (the desktop's mixer). Each cue is panned, quieter, duller and more reverberant with
distance, at most `mix.strips` at once, the gravest taking the place of the slightest with a short fade.
Cries of pain and death are voices (`docs/VOICE_LINES.md`).

**Hearing** (`hear()` in `sfx.cpp`): what is on screen is heard. The reach widens with the camera
(160 m + 1.1 x its distance; 420 m + 1.5 x for the loud ones); its height takes the level down only to a floor
(about -13 dB by distance 600, -8 dB for blasts and gates) and dulls the tone instead; past 600 single sounds fade
out and the loops `snd_amb_battle_far` and `snd_amb_march_column` carry the field. **Pace**: at most 24 cues a
second of real time (a burst of 8) whatever the game's speed, priority 4 and 5 always; gaps are real time, so
at 4x and 8x they are four and eight times as many ticks, delays are divided by the speed, and priority 0 and
movement are left out from 4x. **Blows and the picture**: a hand blow's hit sounds when its attack clip shows it land
(half the clip after the strike, by the striker's kind: `Sfx::setBlowSeconds` from the sheets), the whoosh leading it. Paused, nothing starts, what is playing fades, the ambience drops to a third.

## Hooks: sounds for features being built

Call `battle.sfx().hook(Sound::X, where)` during the step in which the thing happens (before
`Sfx::observe`, i.e. from the battle's event collection); it is placed, merged and throttled like any cue.
For a sound in the player's ears whatever the camera, `battle.sfx().ui(Sound::X)`.

| Feature | Call | Notes |
|---|---|---|
| Feet, a body, a stone or boulder into water | `SplashSmall`, `SplashBody`, `SplashBig` | wading men already sound as `FootWet` by the ground under them |
| Rolling boulder | `BoulderLaunch` once; `BoulderRumble` every half second while it rolls (its gap is 12 ticks); `BoulderImpact` on ground or wall; `BoulderCrush` for men under it | launch, rumble and impact carry far |
| Satchel lay and detonate chain | nothing to call: a laid satchel sounds `SatchelLay` to its owner, any satchel whose fuse is burning down hisses `SatchelFuse` every 1.5 s (thrown, or lit by fire or a blast beside it), and each blast is `SatchelBlast` | read from the world's projectiles in `Sfx::fuses` |
| Fireball quenched in water | `FireSteam` | in place of `FireballBlast` |
| A call for help and its answer | `CallHelp` at the caller, `CallAnswer` at the squad that turns to it | each at most once in 2 s |
| Cavalry charge striking a line | `ChargeImpact` | carries far |
| A line breaking | `Rout` | at most once in 3 s |


Every sound is rendered by `python3 tools/sfx/synth/render.py` (numpy, scipy and ffmpeg; deterministic) into
`game/web/sfx/<id>_<n>.mp3`: one-shots mono at -18 LUFS, ambience loops stereo at -24 LUFS, made seamless. The
level column is the mix level the page applies on top. Audition them all on `sfx-test.html` (the ⚙ panel's
*Test sounds* link).

## Replacing a sound with a recording

1. Record or find the sound. **Licence: only CC0 (public domain) sounds or your own recordings**, nothing that
   needs attribution or forbids redistribution: the game is open and its files are published as they are.
   Freesound.org's CC0 filter, Sonniss' GDC bundles (royalty-free, check the licence) and your own microphone
   are good sources. Keep the source and licence in a note next to your takes.
2. Record **mono, 48 kHz, 24-bit WAV** (ambience: stereo), a little longer than you need, with silence either
   side. Make several takes: each becomes one variant (`_1`, `_2`, ...); the game picks a different one each time.
3. Name each take after the file it replaces, e.g. `snd_weap_arrow_bowstring_release_2.wav`, and run

       tools/sfx/convert.sh ~/takes

   It trims the silence, normalises the loudness like the rendered files, writes the MP3 in place and lists it
   in `game/web/sfx/recorded.json`, so re-rendering never overwrites it. Then rebuild or reload.
"""


def load():
    with open(MANIFEST, encoding="utf-8") as f:
        return json.load(f)


def doc(m):
    recorded = set()
    if os.path.exists(RECORDED):
        with open(RECORDED, encoding="utf-8") as f:
            recorded = set(json.load(f))
    stats = {}
    if os.path.exists(STATS):
        with open(STATS, encoding="utf-8") as f:
            stats = json.load(f)
    total = sum(s.get("bytes", 0) for k, s in stats.items() if k.startswith("sfx/"))
    files = sum(len(s["files"]) for s in m["sounds"])
    out = ["# Sound effects", "",
           "Generated from `game/web/sfx/sounds.json` by `python3 tools/sfx/script.py`; do not edit by hand.",
           f"{len(m['sounds'])} sounds in {files} files ({total / 1e6:.1f} MB of MP3; the page fetches what a battle needs when audio starts, the rest soon after).", "",
           GUIDE]
    for cat in m["categories"]:
        rows = [s for s in m["sounds"] if s["category"] == cat["id"]]
        if not rows:
            continue
        out += [f"## {cat['title']}", "", "| Sound | When it plays | How | Variants | Level | How to record or source it |",
                "|---|---|---|---:|---:|---|"]
        for s in rows:
            rec = sum(1 for f in s["files"] if f in recorded)
            secs = [stats[f]["seconds"] for f in s["files"] if f in stats]
            length = f", {min(secs):.1f}-{max(secs):.1f} s" if secs else ""
            state = f"{rec} recorded" if rec else "rendered"
            who = {"page": " (played by the page)", "ambience": " (an ambience loop, faded by the game)", "future": f" (for {s.get('future', 'later')}: not played yet)",
                   "hook": " (a hook: the feature that plays it is being built; ask for it with Sfx::hook)"}.get(s["emitted_by"], "")
            out.append(f"| **{s['title']}**<br>`{s['id']}` | {s['when']}{who} | {s['behaviour']}{length} | {len(s['files'])} ({state}) | "
                       f"{s['level']:+.0f} dB | {s['record']} |")
        out.append("")
    return "\n".join(out)


def main(argv):
    text = doc(load()) + "\n"
    if "--check" in argv:
        ok = os.path.exists(DOC) and open(DOC, encoding="utf-8").read() == text
        if not ok:
            print("out of date: docs/SOUND_EFFECTS.md (run python3 tools/sfx/script.py)")
        return 0 if ok else 1
    with open(DOC, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote docs/SOUND_EFFECTS.md")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
