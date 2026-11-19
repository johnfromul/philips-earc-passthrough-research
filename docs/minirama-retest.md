# Minirama TrueHD retest (2026-11-19)

Private archive reports_minirama_verified.zip, 324670 bytes, SHA-256 6e6bd36ea1827195e83a3610af6cf638c30049ee96324b61932ac2d0a2ff1553. Capture Probe_Auto_20261119_050155. Session 929. Owner supplied the archive following instructions to upload and verify the alternate sample, whose host ffprobe identified Dolby TrueHD + Dolby Atmos, 48000 Hz, 7.1, 24-bit, host SHA-256 cd126fb45fc6a79b61ddc13d1a9aaab3db11856f4f0f5cdfef57bcf520e4f701. Neither pre- nor post-playback device file hash is included in this archive. The changed byte count and timing strongly support a different stream from session 921, but exact on-device media identity is not cryptographically established by the archive itself.

| Metric | Session 929 |
|---|---|
| Test | TRUEHD_GROUP16 |
| HW sync ID | 65558 |
| Audio bytes accepted | 26824188 |
| Audio units | 1868 |
| Video frames (render callbacks) | 733 |
| Last video PTS | 24400000 us |
| Playback head | 1177296 |
| Operator audio/video | normal/moving |
| Soundbar | Dolby Surround |
| DSP Atmos flag | 0 |
| No-audio-frame timeout count | 0 |

## Evidence, live.log 1-based lines

- 3880: TPC hw_sync_id 65558; 3888–3889 TPC pipeline.
- 6810, 6922, 11109: TRUEHD input, Auto.
- 9115: decoder_525 assigned DEC(2), DVB_MAT, MM_IN.
- 9124–9125: StartDecoderDevice Mat, DecodeType(4,MAT), PlayType(-1,Unkown), bMmOmxMode:0.
- 11195: decoder common channel map/es channel map 0x3f, sample rate 48000, MULTI_CHANNEL.
- 11201: MAT_TRUEHD, isAtmos 0.
- 11307 onward: hasAtmos:0, codec 234881024, playback 1.
- 11347, 11461: DolbyInfo.is_atmos:0, decoder atmos:0, UI AtmosEnable:1.
- 17812: TUNNEL_END, session 929, stopped=false, bytes=26824188, audioUnits=1868, renderedCallbacks=733, head=1177296.
- observations.txt: Dolby Surround, normal, moving. Optional media.codec capture unavailable; no impact on these measurements.

Compare session 921: 17789192 bytes and 1829 groups, Dolby Surround, decoder Atmos=0. Session 929 delivers 51% more bytes, and the same decoder classification. No evidence of generic HW-sync failure. The interpretation of the channel map does not prove the actual count of audible speaker channels. The earlier JOC positive control and HDMI control still stand. Exact same-stream HDMI validation remains useful.

## Next

Confirm installed sample SHA-256 (the new capture script records hashes pre/post automatically for Probe). Stream-copy the exact host alternate raw stream into an MKV with the synthetic video and verify its extracted audio hash before playing via Windows Kodi WASAPI into TV HDMI in Auto. Capture HDMI with collect_native.ps1 -Source HDMI -Mode Auto. This tests the exact raw stream that the internal playback app is intended to consume, not merely another file with an Atmos label. No new APK or further MAT permutations needed. The static MatMlpOnly hypothesis is unchanged and its DSP effect remains unproven.
