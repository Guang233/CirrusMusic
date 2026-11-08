#!/bin/bash
# Test signals for the audio PoC: a 10 s chirp at 44.1 kHz, cut at a sample that is not on
# an MP3 frame boundary, each half encoded as MP3 (LAME, CBR 320k) and FLAC.
# usage: make_samples.sh OUTDIR     (needs ffmpeg, lame, flac)
set -euo pipefail
out=${1:?usage: make_samples.sh OUTDIR}
mkdir -p "$out"
cd "$out"
cut=220501
expr='0.5*sin(2*PI*(200*t+40*t*t))'
ffmpeg -loglevel error -y -f lavfi -i "aevalsrc=${expr}|${expr}:s=44100:d=10" -c:a pcm_s16le whole.wav
ffmpeg -loglevel error -y -i whole.wav -af "atrim=end_sample=${cut}" -c:a pcm_s16le a.wav
ffmpeg -loglevel error -y -i whole.wav -af "atrim=start_sample=${cut},asetpts=N/SR/TB" -c:a pcm_s16le b.wav
for f in whole a b; do
    lame --quiet -b 320 "$f.wav" "$f.mp3"
    flac --silent -f -8 "$f.wav" -o "$f.flac"
done
echo "samples in $out (cut at frame $cut)"
