# Native MKV player experiment (V670, 2026-11-19)

The owner remuxed the probe's 30-second TrueHD sample and synthetic H.264 video into MKV with FFmpeg 8.1.1 using stream copy. FFprobe detected TrueHD + Atmos, 48 kHz, 7.1, 24-bit. Playback from USB in TCL Media Center produced silence in both Pass Through (soundbar PCM) and Auto (soundbar Dolby Surround).

## Log evidence

Private capture: native_passthrough.log (52,671 lines when decoded as UTF-16; SHA-256 0588af6e7ab94e2fda16d75c132d5942f62fc48e8403eb4659de9e114d49f6b8). Do not publish raw capture; it contains unrelated personal/network information.

| Lines (1-based) | Observation |
|---|---|
| 12477, 12486 | MTK Matroska Extractor confidence 1.6 vs standard Matroska confidence 0.6 |
| 12489, 12505–12509 | MKV logs `is suppoort truehd = 0`, recognizes `A_TRUEHD`, reports `A_TRUEHD is not supported truehd.` and ignores track |
| 12646 | ExoMediaPlayer reports videoTrackCount=1, audioTrackCount=0, textTrackCount=0 |
| 33327, 33343–33345, 33477 | Auto test repeats the same behavior |
| 10068–10071 | HAL input LPCM, Bypass, eARC PCM |
| 30955–30958 | HAL input LPCM, Auto, eARC MAT |

The native player is TCL Media Center (com.tcl.ui_mediaCenter:videoPlayer); its visible path is ExoMediaPlayer plus the system MediaTek Matroska extractor, not a demonstrated NuPlayer passthrough. Mode-selection events are not proof of TrueHD submission. In both playback attempts, the track was removed upstream of decoding. In contrast, Moonfin and the tester use independent extraction and can submit TrueHD.

See [stage 4 binary analysis](native-extractor-stage4.md) for the confirmed property gate. No need to repeat this experiment in MPEG-TS: current target is MKV.
