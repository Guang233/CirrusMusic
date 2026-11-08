# Audio fixtures

Synthetic tones only (no recordings): 0.5 s, 44.1 kHz stereo, 440 Hz left and 660 Hz right,
22050 frames. Used by `tst_audio_decoder` and `tst_audio_engine`.

```bash
ffmpeg -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)|0.5*sin(2*PI*660*t):s=44100:d=0.5" -c:a pcm_s16le tone.wav
lame -b 128 tone.wav tone.mp3
flac -8 tone.wav -o tone.flac
```
