#!/usr/bin/env bash
# Converts recorded takes into the game's voice files (docs/VOICE_LINES.md).
#
#   tools/voice/convert.sh <takes dir>     e.g. ~/takes with ~/takes/soldier/selected_1.wav
#
# Each <voice>/<name>.wav (or <name>_a.wav: take a wins over b and c) becomes
# game/web/voice/<voice>/<name>.mp3: silence trimmed from both ends, mono,
# loudness-normalised to -16 LUFS (true peak -1.5 dB), 48 kHz MP3 at ~96 kbps
# (plays everywhere, iOS Safari included). Then the index of recorded lines
# and the checklist are refreshed. Needs ffmpeg and python3.
set -euo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
    echo "usage: $0 <folder of takes, one subfolder per voice>" >&2
    exit 2
fi
command -v ffmpeg >/dev/null || { echo "ffmpeg is needed (apt install ffmpeg / brew install ffmpeg)" >&2; exit 1; }

src=$1
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../../game/web/voice"
# Trim leading and trailing silence (below -50 dB), then measure-free
# single-pass loudness normalisation, then a short fade so nothing clicks.
filter="silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,\
silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.08,areverse,\
loudnorm=I=-16:TP=-1.5:LRA=11,afade=t=in:d=0.01"

count=0
shopt -s nullglob
for voice_dir in "$src"/*/; do
    voice=$(basename "$voice_dir")
    mkdir -p "$out/$voice"
    for wav in "$voice_dir"*.wav "$voice_dir"*.WAV; do
        name=$(basename "${wav%.*}" | tr 'A-Z' 'a-z')
        take=""
        if [[ $name =~ ^(.*)_([abc])$ ]]; then
            name=${BASH_REMATCH[1]}
            take=${BASH_REMATCH[2]}
            # Take a wins; b only without a; c only without a and b.
            [[ $take == b && -e "${wav%_b.*}_a.wav" ]] && continue
            [[ $take == c && ( -e "${wav%_c.*}_a.wav" || -e "${wav%_c.*}_b.wav" ) ]] && continue
        fi
        dest="$out/$voice/$name.mp3"
        ffmpeg -hide_banner -loglevel error -y -i "$wav" -af "$filter" -ac 1 -ar 48000 \
            -codec:a libmp3lame -q:a 5 "$dest"
        echo "$voice/$name.mp3"
        count=$((count + 1))
    done
done
echo "converted $count takes"
python3 "$here/script.py" --index
