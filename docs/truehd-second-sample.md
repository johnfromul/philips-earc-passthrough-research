# Attempted second TrueHD experiment: sample identity unresolved (2026-11-19)

**Correction:** subsequent operator verification found the host Minirama sample is TrueHD + Dolby Atmos, 48 kHz, 7.1, 24-bit, SHA-256 `cd126fb45fc6a79b61ddc13d1a9aaab3db11856f4f0f5cdfef57bcf520e4f701`. The TV's active `truehd.thd` instead hashes to `1779b6f1d20108c3b8404ed62c3477b67f817e80ccfb4fc93b1e953887d3da64`, the original Amaze sample. The capture does not prove alternate media was played. Preserve the measurements as a labeled attempt, not an independent second-sample reproduction. Rerun only after upload and hash equality verification.

Private archive reports_truehd_alt.zip, 321498 bytes, SHA-256 7a3148c0ed690005f8bddf76d6e728dcc27916033fefe3e0f02b549735f4a0d8. Capture Probe_Auto_20261119_045222, operator label Minirama_TrueHD_GROUP16. TCL 65C9K, V670, Auto, Q85H Pro via eARC. The operator selected a second sample; the archive alone does not independently verify the new sample hash, track profile, or identical HDMI playback.

| Metric | Observed |
|---|---|
| Session | 921 |
| AudioTrack | TRUEHD 48000 Hz, mask 0x63f, DIRECT + HW_AV_SYNC |
| HW sync ID | 65557 |
| Audio accepted | 17789192 bytes, 1829 complete chunks |
| Video | 715 render callbacks, last rendered PTS 23800000 us |
| Audio head | 1149408 |
| Operator audio / video | normal / moving |
| Soundbar | Dolby Surround |
| HAL recognized Atmos | 0 |
| No-audio-frame timeout count | 0 |

live.log line references: 3272 starts TRUEHD_GROUP16 (16 AU); 3432 configures track; 3535–3558 establish TPC; 13512 opens DEC(2), DVB_MAT, MM_IN; 13526 starts DecodeType(4,MAT), PlayType(-1,Unkown), bMmOmxMode:0. 14168–14169 select Auto output MAT. 14187 gives ch_map and es_ch_map 0x3F; 14192 classifies MAT_TRUEHD with isAtmos=0; 14250–14252 report recognized 48000 Hz, STREAM_ATOMS=0; 14288 and 14347 confirm DolbyInfo.is_atmos=0. 18189 records final counters. Operator observations in observations.txt.

This reproduces the original grouped TrueHD behavior, but the actual sampled file identity during capture is unverified; the later device hash matches the original. It is distinct from the prepacked MAT variants, which never recognized usable audio frames. The successful internal JOC test identifies Atmos on the same MM_IN decoder index. The original TrueHD host/TV hashes matched; the alternate profile and exact sample identity should be verified before claiming an independently validated second Atmos stream. Do not repeat the three failed MAT variants.

See [source-specific MAT initialization](mat-mlp-only-analysis.md).

## Follow-up

See [Minirama retest](minirama-retest.md) for session 929: materially different byte counts and the same decoder Atmos=0. The archive does not contain a device hash.
