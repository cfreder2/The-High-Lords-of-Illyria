# The Sound Editor

One place for the game's sound: **inspect** what is there, **audition** candidates, **record** your own
effects and voice lines, **choose**. Choosing puts the sound into the game's working tree at once.
Nothing is ever committed for you.

## Start it

    python3 tools/sound-editor/serve.py            # then open the address it prints (http://127.0.0.1:8770/tools/sound-editor/)
    python3 tools/sound-editor/serve.py --workspace ~/somewhere-else --port 8780

Python's standard library only, bound to 127.0.0.1. It needs `ffmpeg` to install anything: on the PATH,
or at `<workspace>/bin/ffmpeg`, or named by `SOUND_EDITOR_FFMPEG`; without it the pages still audition
and record, and say what to run. The microphone works because the page is served from 127.0.0.1.

Opened without this server (the audit's `serve.sh`, or any static server) the pages are read-only: the
game's sounds play, and a banner says to start `serve.py`. Opened as a file they cannot read anything.

## The workspace (not in git)

Default `~/Desktop/sound-sources/`. The editor reads and writes only these:

| Path | What |
|---|---|
| `masters/`, `candidates/`, `data.js` | Downloaded CC0 candidates and their licences (made by the workspace's own `tools/build_candidates.py`). |
| `LICENCES.md` | Where each download came from. |
| `recordings/sfx/<sound id>/<id>_ownNN.wav` | Your effect takes, exactly as recorded (32-bit float WAV), each with a `.json` beside it. |
| `recordings/voice/<set>/<situation>_<n>.takeNN.wav` | Your takes of a voice line. |
| `<sound>.json` beside a master or a take | Its edit: trim, fades, loudness target, gain, mono/stereo, loop seam (and pitch/reverb for voices that are not men). The sound can always be rendered again from it. |
| `choices.json` | What is chosen for each sound and line. The server owns it. |
| `bin/ffmpeg` | Optional: an ffmpeg for the server. |

A workspace inside the repository is refused, except `tools/sound-editor/workspace/`, which git ignores.

## Effects

`effects.html` lists every sound in `game/web/sfx/sounds.json`. Open one:

- **Variants in the game**: the ordered list the game picks from at random. To begin with these are the
  game's originals. Drag (or ▲▼) to reorder, ✕ to drop one.
- **Available**: the workspace's candidates for that sound, your takes, and any original you dropped.
  **+ use as a variant** or **use only this** installs at once and says what it did
  (`In the game: snd_weap_sword_armor_1 ← your take 3`), with **Undo** on the same message.
  **Use the game's original** puts the whole sound back as git has it.
- **● Record** records from the microphone into the workspace; the take appears under Available with its
  editor open (waveform, drag the two handles to trim, fades, loudness, gain, A/B against what is in the game).
  An edit to a sound that is in the game re-installs it.
- ▶ plays a source as it would be installed, through the game's own chain at the camera distance set in
  the top bar (or dry). Mix level and category are the sound's entries in `sounds.json`.
- Sounds the workspace has candidates for but the game does not list are under *Not in the game yet*:
  audition and record there; install by borrowing them from a real sound's section
  (*Show another event's candidates here…*).

## Voices

`voices.html` reads `game/web/voice/lines.json` as it is. Pick a voice set: its casting note, the units that
use it, and its lines by situation with each situation's direction. **● Record next** opens the studio:
one line at a time, the most-heard first (selected, move, attack, the cries), then by priority.

| Key | Does |
|---|---|
| Space | record / stop (it also stops by itself after the silence you set) |
| Enter | keep the last take (it goes into the game) and move on |
| R | do it again (the last take is deleted unless it is the kept one) |
| P | play the last take |
| S, → / ← | skip / back |
| Esc | close |

Every take stays in the workspace. The set's header shows how far along it is and how evenly it was
recorded. Wordless sets (brute, undead, fiend, beast) show the sound wanted in place of words; for those
and the archon a pitch-down and a reverb can be **previewed**, and are only put into a file if you tick
*bake into the installed file* in a take's editor.

## What installing does

For each chosen source the server renders it with ffmpeg from its edit to the game's format
(effects: mono 48 kHz MP3 at -18 LUFS, loops stereo at -24 with a seam crossfade; voice lines: mono
at -16 LUFS; the same figures as `tools/sfx/convert.sh` and `tools/voice/convert.sh`) and writes
`game/web/sfx/<id>_<n>.mp3` or `game/web/voice/<set>/<situation>_<n>.mp3`. Then:

- `game/web/sfx/sounds.json` (the variant list, mix level, category), `stats.json`, `recorded.json`;
  `game/web/voice/available.json`;
- `docs/SOUND_EFFECTS.md` by `tools/sfx/script.py`; `docs/VOICE_LINES.md` and `docs/VOICE_CHECKLIST.md`
  by `tools/voice/script.py`;
- `docs/sound_credits.json` and the generated `docs/SOUND_CREDITS.md`: one line per shipped sound that
  is not the game's own rendering. For your recordings it reads
  `Own recording · the owner · The owner's own recording; no attribution needed · recordings/… · the date`.

It refuses anything whose licence entry is missing or is not CC0, and anything outside
`game/web/sfx`, `game/web/voice` and the workspace.

**Installed changes** (top of either page) lists what differs from git, with **Undo** per file: the file
comes back from git and the manifests, docs and credits follow.

## Check it in the game, then commit

1. Listen on the editor's pages, then *Inspect* (`tools/audit/sounds.html`: the waveform, the faults,
   the mix under load), then in a battle (reload a dev build; `sfx-test.html` in the game).
2. `git status` shows exactly what the editor changed. `git add game/web/sfx game/web/voice docs` and
   commit when you are content; `git checkout -- game/web docs` (or Undo in the page) if not.

From the sprite audit, a unit's Sounds panel has *Open in Sound Editor* links to its effects and its voice set.
