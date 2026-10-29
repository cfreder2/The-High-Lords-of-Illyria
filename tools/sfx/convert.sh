#!/usr/bin/env bash
# Converts recorded (or CC0) takes into the game's sound effect files (docs/SOUND_EFFECTS.md).
#
#   tools/sfx/convert.sh <takes dir>     e.g. ~/takes/snd_weap_arrow_bowstring_release_2.wav
#
# Each <id>_<n>.wav (any folder depth) becomes game/web/sfx/<id>_<n>.mp3:
# silence trimmed from both ends, loudness-normalised like the rendered files
# (one-shots mono at -18 LUFS; snd_amb_* loops stereo at -24 LUFS, not
# trimmed), 48 kHz MP3. Each is listed in game/web/sfx/recorded.json so
# tools/sfx/synth/render.py never overwrites it, and the doc is refreshed.
# Only CC0 sounds or your own recordings. Needs ffmpeg and python3.
set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
    echo "usage: $0 <folder of takes named <sound id>_<n>.wav>" >&2
    exit 2
fi
command -v ffmpeg >/dev/null || { echo "ffmpeg is needed (apt install ffmpeg / brew install ffmpeg)" >&2; exit 1; }

src=$1
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../../game/web/sfx"
trim="silenceremove=start_periods=1:start_threshold=-55dB:start_silence=0.02,areverse,\
silenceremove=start_periods=1:start_threshold=-55dB:start_silence=0.05,areverse"

count=0
names=()
while IFS= read -r -d '' wav; do
    name=$(basename "${wav%.*}" | tr 'A-Z' 'a-z')
    if [[ ! $name =~ ^snd_[a-z0-9_]+_[0-9]+$ ]]; then
        echo "skipped (not <id>_<n>): $wav" >&2
        continue
    fi
    if [[ $name == snd_amb_* && $name != snd_amb_thunder_* && $name != snd_amb_swamp_frogs_* && $name != snd_amb_cave_drip_* ]]; then
        ffmpeg -hide_banner -loglevel error -y -i "$wav" -af "loudnorm=I=-24:TP=-3:LRA=11" -ac 2 -ar 48000 \
            -codec:a libmp3lame -b:a 112k "$out/$name.mp3"
    else
        ffmpeg -hide_banner -loglevel error -y -i "$wav" -af "$trim,loudnorm=I=-18:TP=-1:LRA=11,afade=t=in:d=0.003" -ac 1 -ar 48000 \
            -codec:a libmp3lame -b:a 72k "$out/$name.mp3"
    fi
    echo "sfx/$name.mp3"
    names+=("sfx/$name.mp3")
    count=$((count + 1))
done < <(find "$src" -type f \( -iname '*.wav' -o -iname '*.flac' \) -print0)

python3 - "$out/recorded.json" "${names[@]}" <<'EOF'
import json, os, sys
path, new = sys.argv[1], sys.argv[2:]
have = set(json.load(open(path))) if os.path.exists(path) else set()
json.dump(sorted(have | set(new)), open(path, "w"), indent=0)
EOF
echo "converted $count takes"
# Loops recorded here have no wrapped padding: their loop points are the whole file.
python3 - "$out/stats.json" "${names[@]}" <<'EOF'
import json, os, subprocess, sys
path, new = sys.argv[1], sys.argv[2:]
stats = json.load(open(path)) if os.path.exists(path) else {}
web = os.path.dirname(os.path.dirname(path))
for rel in new:
    secs = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                 os.path.join(web, rel)], capture_output=True, text=True).stdout or 0)
    s = {"seconds": round(secs, 3), "peak_db": -1.0, "lufs": -18.0, "dc": 0.0, "bytes": os.path.getsize(os.path.join(web, rel)), "recorded": True}
    if "/snd_amb_" in rel and not any(k in rel for k in ("thunder", "frogs", "cave_drip")):
        s.update({"lufs": -24.0, "loop_start": 0.0, "loop_seconds": round(secs, 4)})
    stats[rel] = s
json.dump(dict(sorted(stats.items())), open(path, "w"), indent=0)
EOF
python3 "$here/script.py"
