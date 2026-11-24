# TCL eARC passthrough research

Dolby TrueHD Atmos from internal Android TV apps on a **TCL 65C9K** (Google TV 14, MediaTek `mt5896`, firmware `V670 GB AU04`) to a TCL Q85H Pro soundbar over eARC. Plus an optional reversible debloat track.

## Status: SOLVED (2026-11-25)

**Internal Android TV → eARC TX lossless Dolby TrueHD Atmos passthrough is working cleanly end-to-end** on stock C9K V670 with **no HDMI input, no DSP firmware patch, and no boot/DTBO changes**, deployed reversibly via a single Magisk module:

- **Kernel driver ([`native/tcl_earc_mat.c`](native/tcl_earc_mat.c) → `/dev/earc_mat_bypass`):** Exposes a 32-frame (`640 ms`) lock-free SPSC buffered character device with a dedicated `kthread` (`0.25 ms` hrtimer polling via `usleep_range_state`) that locks onto `DEC-R2`'s live 20.000 ms MAT 2.0 cadence in the SE DSP Non-PCM DDR ring (`0x2249C000..0x22640000`, `28 × 61,440` bytes) and overwrites each silent MAT slot immediately before `SE DSP` streams it out over eARC TX (`0x951115 = 0x25`).
- **Audio HAL shim ([`native/audio_primary_merak_shim.c`](native/audio_primary_merak_shim.c) → `/vendor/lib/hw/audio.primary.merak.so`):** Wraps the stock 32-bit vendor HAL in-place, intercepts **both** `AUDIO_FORMAT_IEC61937` (192 kHz 7.1 MAT, e.g. Kodi `AudioTrack (IEC)` / AudioProbe) **and** `AUDIO_FORMAT_DOLBY_TRUEHD` (48/96/192 kHz raw TrueHD, e.g. Moonfin / Jellyfin / Plex / Stremio / ExoPlayer — packed live 24 AUs → 61,440-byte bit-exact MAT 2.0 bursts) into `/dev/earc_mat_bypass` with `CLOCK_MONOTONIC` presentation timestamps for A/V lip-sync, and leaves all other streams untouched.
- **Audio Policy + `libaudioclient.so` ([`tools/patch_tcl_audio_stack.py`](tools/patch_tcl_audio_stack.py)):** Exposes `direct_iec61937 output` on `HDMI EARC` (preserving all dynamic EDID profiles) and removes TCL's 5 hardcoded `/system/bin/mediaserver` package-name blocks in `/system/lib/libaudioclient.so`.
- **Magisk auto-load module ([`magisk/tcl_earc_mat_bypass/`](magisk/tcl_earc_mat_bypass/)):** Loads `tcl_earc_mat.ko` on boot (with `uname -r` guard and `audio_device` SELinux label) and overlays the HAL shim, policy XML, and patched `libaudioclient.so`.
- **Verified live from cold boot (zero HDMI):**
  - 30 s full-file `truehd.iec` test (`1499` frames): `29.979 s`, `already_ready=0`, `timeouts=0`, clean **Dolby Atmos** on soundbar.
  - Standard Android `AudioTrack` (`ENCODING_IEC61937`, `192000 Hz`, `7.1`, `direct=true`) in **AudioProbe**: `505` frames, `underruns=0`, `already_ready=0`, `relocks=0`, clean **Dolby Atmos** on soundbar.

👉 **Full architecture, build steps, and how to find the model/firmware-specific DDR addresses & CRCs on other TCL / MediaTek TVs:** **[`docs/earc-mat-bypass-porting-guide.md`](docs/earc-mat-bypass-porting-guide.md)**.

## Historical baseline & investigation (2026-11-19 – 2026-11-24)

- **Works:** E-AC3 Atmos from apps; TrueHD Atmos from an external HDMI source.
- **Does not work:** TrueHD from internal apps is decoded and re-encapsulated; the sink reports Dolby Surround rather than Atmos. The prior `enc=19` probe declared Dolby MAT but fed bytes from `truehd.iec` (IEC61937 bursts), so its silence on ARC and eARC does not test the Android IEC61937 encoding-13 path or establish the MAT framing contract. The HAL Atmos callback reported `atmosStream(0)` for the internal MLP session; raw DSP status and transmitted object metadata were not independently read back. TCL Media Center also drops MKV TrueHD tracks (`vendor.mtk.mkv.support.truehd` unset, not settable from shell).
- **Resolved so far (2026-11-23, rooted live trace — [frida-live-trace-session](docs/frida-live-trace-session.md)):** the old `MatMlpOnly` hypothesis is **disproven** — forcing it `1→0` live via Frida changed nothing (the HAL field is inert). External HDMI input takes a hardware MAT bypass and preserves the Atmos positive control. The internal TrueHD route reaches DSP decode/re-encapsulation; a full MM-vs-HDMI DSP SHM-parameter diff found no exposed Atmos toggle. Offline loader analysis found post-upload segment readback comparison and a conditional TEE command `0x20`; it has **not** established that TEE authenticates DSP firmware. No working internal-app TrueHD Atmos route or patch has been found. The exact GB/AU04 `armfw` comparison and the TEE command semantics remain open.

## Current device state (2026-11-23)

- AVB device state was unlocked and dm-verity disabled through the live U-Boot console. Magisk v31.1 patched image was written to active `boot_b`.
- Wireless ADB is connected and `su -c id` confirms UID 0; SELinux remains `Enforcing`.
- Current audio routing exposes HDMI EARC via the restored `earc_output_test` module. The MAT-tagged track opened and routed to eARC, but the test stalled after AudioTrack accepted 286,720 bytes; playback head stayed at zero, so wire transmission is unproven. The tested IEC61937 track did not open, and ALSA still has no separately named `EARC NONPCM Playback` endpoint. Details: [roadmap](docs/roadmap.md) and [endpoint inventory](docs/earc-endpoint-and-policy.md).
- The earlier Magisk first-run check had returned `Permission denied` before the later root confirmation. Full chronological record: [C9K UART root attempt](docs/c9k-v670-uart-root-attempt-2026-11-23.md).

Plan and verified details: **[docs/roadmap.md](docs/roadmap.md)**.

## Docs

| Topic | Doc |
|---|---|
| **Working eARC MAT bypass & porting guide** | **[earc-mat-bypass-porting-guide](docs/earc-mat-bypass-porting-guide.md)**, [nonpcm-dplayback-driver-audit](docs/nonpcm-dplayback-driver-audit-2026-11-24.md) |
| Full chronology and test matrix | [investigation](docs/investigation.md) |
| HDMI vs internal comparison | [hdmi-comparison](docs/hdmi-comparison.md) |
| E-AC3 JOC positive control | [joc-positive-control](docs/joc-positive-control.md) |
| TrueHD grouped retest | [minirama-retest](docs/minirama-retest.md) |
| MAT tunnel failures | [mat-tunnel-results](docs/mat-tunnel-results.md) |
| MatMlpOnly static analysis | [mat-mlp-only-analysis](docs/mat-mlp-only-analysis.md), [mat-bypass-source-predicates](docs/mat-bypass-source-predicates.md), [mat-mlp-hook-strategy](docs/mat-mlp-hook-strategy.md), [mat-mlp-consumer-trace](docs/mat-mlp-consumer-trace.md), [ms12-mat-transcode-trace](docs/ms12-mat-transcode-trace.md), [dsp-dolby-param-map](docs/dsp-dolby-param-map.md) |
| Decoder/DSP internals | [decoder-dispatch-and-parser](docs/decoder-dispatch-and-parser.md), [decoder-config-contract](docs/decoder-config-contract.md), [mm-dsp-input-boundary](docs/mm-dsp-input-boundary.md), [kernel-audio-driver-audit](docs/kernel-audio-driver-audit.md), [dsp-candidate-audit](docs/dsp-candidate-audit.md), [truehd-parser-alignment](docs/truehd-parser-alignment.md) |
| MKV extractor gate | [native-extractor-stage4](docs/native-extractor-stage4.md), [native-player-test](docs/native-player-test.md) |
| IEC61937 firmware design | [custom-firmware-iec61937](docs/custom-firmware-iec61937.md), [iec-client-experiment](docs/iec-client-experiment.md), [earc-endpoint-and-policy](docs/earc-endpoint-and-policy.md), [iec-mat-carrier-reference](docs/iec-mat-carrier-reference.md), [earc-output-and-recovery-gate](docs/earc-output-and-recovery-gate.md), [virtual-hdmi-audio-source](docs/virtual-hdmi-audio-source.md) |
| Codecs | [codec2-route-analysis](docs/codec2-route-analysis.md), [runtime-codecs-and-bypass](docs/runtime-codecs-and-bypass.md), [vendor-diagnostic-boundary](docs/vendor-diagnostic-boundary.md) |
| Debloat / privacy | [privacy-audit](docs/privacy-audit.md), [privacy-inventory-results](docs/privacy-inventory-results.md), [privacy-manifest-results](docs/privacy-manifest-results.md) |
| Build, deploy, test | [reproduction](docs/reproduction.md) |
| OEM report draft | [upstream-report](docs/upstream-report.md) |
| Evidence hashes | [evidence/README](evidence/README.md) |
| Second TV handoff | [second-tv-handoff](docs/second-tv-handoff.md), [inspection results](docs/second-tv-results.md) |
| C655 actual Realtek baseline | [c655-realtek-baseline](docs/c655-realtek-baseline.md) |
| C655 HDMI Service UART | [Electrical bench measurements](docs/c655-hdmi-service-uart-bench-log.md), [structured readings](evidence/c655-hdmi-service-electrical-2026-11-21.json), [verified ESP32 setup and passive capture](docs/esp32-service-uart.md) |
| C9K UART and Magisk attempt | [Root attempt log](docs/c9k-v670-uart-root-attempt-2026-11-23.md), [UART hardware findings](docs/c9k-hdmi-service-uart.md) |

## Quick start (Windows)

Needs JDK 17, Android SDK 34 build-tools, adb, Python 3.8+, FFmpeg.

```powershell
adb connect <tv-ip>:<port>          # wireless debugging
powershell -ExecutionPolicy Bypass -File .\build.ps1
python .\tools\prepare_samples.py 'C:\path\truehd-atmos.mkv' --eac3 'C:\path\eac3-joc.mkv'
powershell -ExecutionPolicy Bypass -File .\install_samples.ps1
powershell -ExecutionPolicy Bypass -File .\install_tunnel.ps1
python .\tools\host_test.py         # host-side parser/grouping tests
```

Keep soundbar volume very low when testing encoded formats.

## Collect evidence on additional TV models

For any authorized TCL TV connected over ADB, including C655 variants:

```powershell
python .\tools\collect_tv_audio_audit.py --label C655-stock --logs
```

See [multi-device audio audit](docs/multi-device-audio-audit.md) for device selection, optional package inventory, offline stock OTA/image verification and filesystem extraction. Outputs remain under git-ignored private/device_audits. This does not root or modify the TV. No cross-model firmware or patch compatibility is assumed.

## Single ZIP: TV baseline + optional ESP32 UART

Use the combined wrapper to put the existing ADB/stock audit and ESP bridge diagnostics into **one** private ZIP:

```powershell
python .\tools\collect_tv_research_bundle.py --label C655-stock --no-esp
# With commissioned ESP, correctly wired and separately powered:
python .\tools\collect_tv_research_bundle.py --label C655-uart --skip-adb --esp-toolkit "..\tcl-tv-toolkit" --esp-host "ESP_IP" --uart-seconds 90
```

See [combined collector guide](docs/combined-device-and-uart-collector.md). UART is **passive RX only**; the wrapper never sends bootloader commands or roots/flashes a TV. Select a specific ADB target with `--serial` when multiple TVs are connected. The ZIP is saved under `private/research_bundles/` and must be reviewed before sharing.

## Layout

```text
native/     Kernel bypass driver (tcl_earc_mat.c), Audio HAL shim (audio_primary_merak_shim.c), and DDR/ring diagnostic LKMs
magisk/     Magisk auto-load module template (tcl_earc_mat_bypass: module.prop, service.sh, sepolicy.rule)
app/        Java diagnostic app (AudioTrack / tunnel probes, no network permission)
tools/      CRC extractor & XML/libaudioclient patcher (patch_tcl_audio_stack.py), sample prep, host tests, register & topology scripts
scripts/    ADB collection and preflight scripts
docs/       Research notes and porting guide (earc-mat-bypass-porting-guide.md)
evidence/   Derived observations and hashes (no raw dumps)
private/    Git-ignored: device dumps, stock images, pulled binaries, compiled .ko/.so, media
```

No TCL/MediaTek/Dolby binaries, firmware, commercial media or keys are committed. Not affiliated with TCL, Dolby, MediaTek or Google.
