# TCL eARC passthrough research

Dolby TrueHD Atmos from internal Android TV apps on a TCL 65C9K (Google TV 14, MediaTek mt5896, firmware V670 GB AU04) to a TCL Q85H Pro soundbar over eARC. Plus an optional reversible debloat track.

## Audio baseline (2026-11-19, re-verified live over ADB)

- **Works:** E-AC3 Atmos from apps; TrueHD Atmos from an external HDMI source.
- **Does not work:** TrueHD from apps plays clean but as Dolby Surround (`isAtmos=0`). IEC61937 is blocked by a TCL client process check and two HAL format gates. MAT payloads from apps are not recognized. TCL Media Center drops MKV TrueHD tracks (`vendor.mtk.mkv.support.truehd` unset, not settable from shell).
- **Leading hypothesis:** the pipeline sets `MatMlpOnly=1` only for the internal (MM) source, which would strip Atmos. Testing it needs root.

## Current device state (2026-11-23)

- AVB device state was unlocked and dm-verity disabled through the live U-Boot console. Magisk v31.1 patched image was written to active `boot_b`.
- The TV booted into Android and is waiting at initial setup after the unlock reset. Root and runtime SELinux state have not yet been verified; the SELinux-permissive boot setting was not used.
- Finish setup, reconnect Wi-Fi, and re-enable Wireless debugging before ADB verification. Full command/output record: [C9K UART root attempt](docs/c9k-v670-uart-root-attempt-2026-11-23.md).

Plan and verified details: **[docs/roadmap.md](docs/roadmap.md)**.

## Docs

| Topic | Doc |
|---|---|
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
app/        Java diagnostic app (AudioTrack / tunnel probes, no network permission)
tools/      Sample prep, host tests, offline firmware/binary analysis
scripts/    ADB collection and preflight scripts
docs/       Research notes
evidence/   Derived observations and hashes (no raw dumps)
private/    Git-ignored: device dumps, stock images, pulled binaries, media
```

No TCL/MediaTek/Dolby binaries, firmware, commercial media or keys are committed. Not affiliated with TCL, Dolby, MediaTek or Google.
