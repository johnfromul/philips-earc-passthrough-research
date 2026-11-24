# Investigation report

## Scope and configuration

| Item | Observed value |
|---|---|
| TV | TCL 65C9K, Android model `Smart TV Pro` |
| Soundbar | TCL Q85H Pro, connected over HDMI eARC |
| Firmware before update | V8-T653T01-LF1V643 / AU02 |
| Firmware after update | V8-T653T01-LF1V670 / AU04 |
| Build fingerprint | `TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04:user/release-keys` |
| Platform / reported hardware | `merak` / `mt5896`; `tcl9618` also appears in vendor context |
| Android userspace ABI | `armeabi-v7a, armeabi` (32-bit; not evidence of physical CPU width) |
| Test source | Amaze TrueHD Atmos, 48 kHz / 7.1, 30 seconds copied from original |

The target is correct playback and Atmos metadata retention from *internal Android apps* without requiring root or firmware modification. Soundbar display and listening observations are practical end-to-end checks, not a captured HDMI wire format. The owner reports that the original demo plays as Atmos through Windows Kodi WASAPI with both laptop → soundbar → TV and laptop → TV → eARC → soundbar; E-AC3 Atmos works from internal apps. Kodi software decoding plays audible PCM but not original Atmos objects. Direct Play at the Jellyfin server alone does not prove HDMI passthrough. A TV firmware upgrade from V643 to V670 did not fix the problem.

## Initial clients and policy

Moonfin, Kodi and Nuvio were investigated. Nuvio experimental lossless routes and Kodi Android RAW did not produce Atmos; Moonfin received PCM. In V643 and V670 traces, AudioFlinger accepted TrueHD 7.1 / 48 kHz, with `DIRECT|COMPRESS_OFFLOAD` output (0x20011) and eARC destination. V670 trace shows the capability filter for `org.moonfin.androidtv`:

```
getDirectProfilesForAttributes: ... is not /system/bin/mediaserver, remove support direct IEC61937
```

The primary policy XML defines compressed offload and HW_AV_SYNC direct output profiles for TrueHD/MAT, alongside PCM and other compressed formats. The eARC device advertises TrueHD/MAT; IEC61937 is absent from the captured profiles. Bypass selects vendor eARC format 5; Auto selects MAT (4). Vendor logging reports `MAT_TRUEHD(0) MAT_ATMOS(1,byte3:0x7)` under AVR capabilities; do not interpret this alone as proof of the soundbar's real format support. Auto reports the decoded stream as MAT_TRUEHD, isAtmos=0. An eARC input route or flag says nothing definitive about the bits on the cable.

## Diagnostic application timeline

### v0.1.0: audio-only probes

Using Java AudioTrack, actual track construction was attempted independently of reported capability. Direct support for TrueHD and MAT existed; IEC61937 returned false and failed at construction. PCM stereo was normal. E-AC3 control could not run because its input file was missing (not a codec failure). TrueHD RAW produced PCM in Pass Through and Dolby Surround in Auto; explicit TrueHD offload produced noise; TrueHD HW_AV_SYNC initially wrote ~241 KB then stalled, head zero. MAT BE and MAT HW sync stalled; MAT LE accepted ~61.5 MB with head zero and no sound. IEC HBR at 192 kHz/7.1, both ordinary and sync, failed to create the track. Full hardware A/V tunneling was *not* yet implemented.

Audio-only HW sync logs: `hw_sync_id 18446744073709551615`, invalid/unassigned ID; `no tpc audio hal handle fail`, `tpc is not ready`. The HW sync header v2 was parsed, but the shared video clock was absent. This motivated full tunneling.

### v0.1.1: additional framing and channel masks

| Mode | Outcome |
|---|---|
| IEC stereo 48 kHz | Construction failed |
| IEC stereo 192 kHz | Construction failed |
| IEC 5.1 192 kHz | Construction failed |
| IEC 7.1 48 kHz | Construction failed |
| MAT encoding (19) with IEC bursts, 48 kHz track config | 61,501,440 bytes accepted, head 0, no sound |
| MAT encoding (19) with IEC bursts, 192 kHz track config | 61,501,440 bytes accepted, head 0, no sound |

These two probes declared Android `ENCODING_DOLBY_MAT` (19) but fed IEC61937 bursts from `truehd.iec`; they did not test `ENCODING_IEC61937` (13) or establish a working MAT framing contract. The MAT HAL path opened, but reported `Too long without an audio frame`, so accepted writes are not evidence of successful frame recognition. All tested combinations failed; this does not prove every possible vendor framing fails.

### v0.2.0: full A/V tunnel

A real MediaTek `c2.mtk.avc.decoder` was configured with FEATURE_TunneledPlayback and a common audio session ID, with a real SurfaceView. PCM 5.1 control uses two low-level 440/550 Hz sine tones by design. A recorded “beep” for that control does not establish corrupt PCM; distinguish clean test tones from harsh noise. TrueHD audio-first and video-first both rendered moving video and audible but erroneous noise.

| Version / mode | Session | Last video PTS | Rendered callbacks | Head (48 kHz) | Observation |
|---|---:|---:|---:|---:|---|
| PCM | 785 | 24.500 s | 736 | 1,176,960 | Moving video, beep (intentional tones possible) |
| TrueHD audio-first | 793 | 20.300 s | 610 | 972,048 | Moving video, noise |
| TrueHD video-first | 801 | 20.367 s | 612 | 975,888 | Moving video, noise |

The 25 s test window includes startup latency. For the final session, HAL shutdown records a valid HW_SYNC 65542 and `Decoder_357 → SEBypassOutput → EarcOutput`, `PathType=HwAvSync`. DSP ADEC1 frames advanced; channel map 0x3F (5.1), 48 kHz. Near the end, STC=23396 vs PTS=19651 (as logged), suggesting accumulating lag. The logcat main buffer had rotated, so some startup lines were lost. Full tunneling fixes the stall, but not yet sound correctness or Atmos.

### v0.2.1: grouping access units

The original audio-only parser reads individual TrueHD AUs; per-unit HW sync writes can be very frequent (~1200/s for 40 samples at 48 kHz). Media3 uses grouped TrueHD rechunking (typically 16 syncframes). `TrueHdChunks` concatenates original consecutive units and keeps first PTS; no re-encoding or metadata rewriting. Compare 16 and 24 units to the earlier single-AU approach.

| TV setting (test order) | Group | Session | HW_SYNC | Last PTS | Callbacks | Observation |
|---|---:|---:|---:|---:|---:|---|
| Pass Through | 16 | 809 | 65543 | 23.833 s | 716 | PCM, normal sound |
| Pass Through | 24 | 817 | 65544 | 23.833 s | 716 | PCM, normal sound |
| Auto | 16 | 825 | 65545 | 23.833 s | 716 | Dolby Surround, normal sound |
| Auto | 24 | 833 | 65546 | 23.800 s | 715 | Dolby Surround, normal sound |

All four report `MAT stream type(... => 1,MAT_TRUEHD), isAtmos(... => 0)` and `DolbyInfo.is_atmos:0 (decoder atmos:0 UI AtmosEnable:1)`. Grouping eliminated harsh noise and substantially improved progress. It does *not* prove a particular internal timing bug nor an Atmos metadata drop point. App input session IDs are not HW sync IDs. Auto and Pass Through differ in vendor routing and soundbar display; neither indicates original Atmos.

### Source validation

FFmpeg 8.1.1 independently identified the exact extracted PC sample `truehd.thd` as `Dolby TrueHD + Dolby Atmos`, 48000 Hz, 7.1, s32 (24 bit); 30 s remux to MKA via stream copy succeeded. The original external HDMI demo was successful; the newly remuxed MKA end-to-end result has not been reported. Compare device and PC hashes before assuming their copies match. File type recognition alone cannot prove every AU retains metadata through the entire HAL.

## Native analysis

See [native-analysis.md](native-analysis.md) for exact binary paths and locations, hashes, and verification tooling. Explicit software restriction is confirmed in the extracted client library; the missing Atmos indication is downstream of a successful TrueHD track and valid shared clock. Neither is solved by merely forcing an Atmos flag. Static search alone cannot rule out a different server-side restriction. The native player test instead identified TCL ExoMediaPlayer using the MTK Matroska extractor; both modes showed zero audio tracks. See [native player test](native-player-test.md) and [stage 4 extractor analysis](native-extractor-stage4.md). The extractor feature gate is confirmed and ADB property modification was denied by SELinux.

## HDMI positive control and v0.2.2 design

The subsequent [HDMI comparison](hdmi-comparison.md) records hardware MAT input at 192 kHz and 7.1 in Auto mode, DSP decoder 0 reports MAT_TRUEHD and Atmos=1. Internal grouped TrueHD uses MM_IN and DSP decoder 2, with Atmos=0. The TV-input AudioTrack writes dummy data, so matching format/rate alone cannot reproduce the HDMI hardware routing. v0.2.2 adds three MAT 192 kHz full-tunnel test variants (full IEC, LE MAT and BE MAT). These are pending device tests and are not a fix. The exact sample hash equivalence and operator's separate PC MKA validation remain unconfirmed. MPEG-TS is explicitly out of scope; focus on MKV in Moonfin.

## Evidence strength

Raw logs were captured at different times and contain repeated tests; compare by session and timestamp, not global grep counts. Some dumps were taken after playback stopped. User observations are recorded separately from HAL state; missing logs, unsupported profiles and silence are distinct outcomes. No HDMI analyzer, kernel trace, proprietary DSP firmware trace, or successful IEC packet injection has been obtained. See [evidence index](../evidence/README.md) for local artifact hashes and derived data.

## Rooted eARC endpoint retest (2026-11-23)

Restored the reversible `earc_output_test` Magisk property module and rebooted. `ro.feature.hdmi_earc=true`; AudioPolicy now lists HDMI EARC as available and advertises MAT and Dolby TrueHD profiles. ALSA remains unchanged: `/proc/asound/pcm` has D9 as `tvsnd-common-pcm`, but no `EARC NONPCM Playback` line. The exact live HAL helper searches this file for that name, so it still cannot resolve the named PCM endpoint in this boot.

Used the installed Audio Passthrough Tester v0.2.4 with **Open only**; it creates and releases a track without streaming sample bytes:

| Mode | Encoding / config | Query | Open result |
|---|---|---|---|
| IEC HBR | `ENCODING_IEC61937` (13), 192 kHz, 7.1 | `direct=false`, `directSupport=0x0` | `AudioTrack.Builder.build()` failed: `Cannot create AudioTrack` |
| IEC stereo | `ENCODING_IEC61937` (13), 192 kHz, stereo | `direct=false`, `directSupport=0x0` | `AudioTrack.Builder.build()` failed: `Cannot create AudioTrack` |
| MAT BE | `ENCODING_DOLBY_MAT` (19), 48 kHz, 7.1 | `direct=true`, `directSupport=0x3` | Track initialized (`state=1`, session 89); no playback |

These open-only results show that the current eARC policy admits a MAT-tagged track but refuses the tested IEC61937 AudioTrack profiles. At that point no payload had been written, so the MAT byte contract, routing, and Atmos preservation were still unverified. No ALSA node was opened, and no mixer control was written. The separate UART/Magisk boot modification and current device state are recorded in the [C9K UART root attempt log](c9k-v670-uart-root-attempt-2026-11-23.md) and [endpoint inventory](earc-endpoint-and-policy.md).

### MAT BE playback attempt

Later ran the installed MAT BE mode for one attempt. AudioTrack reported route ID 18 / `AUDIO_DEVICE_OUT_HDMI_EARC`, then accepted 286,720 bytes (four complete 61,424-byte units plus a partial next unit). The playback head stayed at 0 and the timestamp position remained 0. After 3 seconds without additional write progress, the app aborted with `No write progress for 3s` and released the track. These are AudioTrack write counts, **not proof that those bytes reached the HDMI wire**. The operator reported that no sound was heard. No retry or IEC payload playback followed.

#### Read-only failure diagnosis

The saved `probe.log` confirms this was `ENCODING_DOLBY_MAT` (19), 48 kHz/7.1, with no HW A/V sync. The device copy of `truehd.matbe` has the SHA-256 recorded in its preparation manifest, so this run used the prepared sample without transfer corruption. That only checks file identity; it does not prove that the extracted payload matches the TV HAL's input contract.

The contemporaneous HAL/kernel logs show the selected route more clearly than the AudioTrack API does: the HAL opened the stream as MAT, assigned `Decoder_93` the MM input source and `DVB_MAT` format, opened compressed decoder device `C0D14`, and started decoder 2. The vendor topology connected `SrcDecoder_93` through an `Encoder` to `EarcOutput`. Thus the test entered the TV's MAT decoder/re-encoder pipeline; it did **not** establish direct delivery to an eARC non-PCM DMA endpoint. This agrees with `/proc/asound/pcm`, which still has no separately named `EARC NONPCM Playback` endpoint.

The decoder did not produce observable playback progress before the app queue stopped accepting writes: `head=0`, timestamp frame position `0`, followed by `No write progress for 3s`. At teardown the HAL reported `has_atmos_stream:0`; this is a teardown status, not a frame-by-frame readback. The evidence points to a stalled MAT decoder/input path, but does not distinguish invalid or unsupported MAT framing from a decoder/clock/configuration issue. The first sync bytes passed the app's superficial check; the entire stream was not independently validated against a documented Android/TCL MAT input contract. The next useful step is read-only tracing/comparison of the existing MAT and working JOC decoder input setup and write path, rather than replaying another speculative byte layout.
