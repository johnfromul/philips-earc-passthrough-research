# TCL 65C9K eARC / Atmos research

Investigation of Dolby TrueHD Atmos from **internal** Android TV apps on a TCL 65C9K (Google TV 14, MediaTek
MT5896, chassis T653T01, firmware V8-T653T01-LF1V670 / GB AU04) to a TCL Q85H Pro soundbar over eARC — plus a
reversible debloat + custom-launcher setup on the rooted TV.

## Conclusion (verified on the rooted device, 2026-11-23/24)

**Internal-app TrueHD Atmos is not achievable on V670.** The TV **decodes** internal compressed audio on its audio
DSP and re-encapsulates TrueHD into MLP-only MAT, dropping the Atmos object (EVO) layer; it only passes DD+/E-AC3
through untouched. This was confirmed from every angle with live root instrumentation (Frida + kernel ftrace kprobes),
not inference — full trail in [frida-live-trace-session](docs/frida-live-trace-session.md):

- The long-standing **`MatMlpOnly` hypothesis is disproven** — forcing it `1→0` live changed nothing (the HAL field
  is inert).
- A clean MM-vs-HDMI diff of the **entire DSP SHM parameter namespace** (resolved from the module's split BTF) shows
  **no Atmos-relevant parameter differs** — the behavior lives in the closed DSP firmware.
- The DSP firmware is **TEE / Secure-Engine authenticated** (`ST_AUDIO_TEE_FIRMWARE_INFO`, `DSP_SE`), so a modified
  firmware would be rejected at load — decompiling/patching it is a dead end even with the bootloader unlocked.
- The **userspace IEC61937/MAT passthrough** bypass was tested end-to-end: a correctly-framed stream (ffmpeg
  `-f spdif`) opens a MAT offload output to eARC but **stalls (`head=0`, silent)** — the app→IEC61937→eARC transport
  does not clock on this platform. The HDMI hardware bypass (which does work) cannot be fed from internal apps.

### What works / what to do instead

| Source | Result |
|---|---|
| Internal app, **E-AC3 / DD+ JOC** | **Atmos** (`isAtmos=1`) — passed through to eARC. Use this for streaming apps. |
| Internal app, **TrueHD** | Clean audio but **Dolby Surround** (`isAtmos=0`); objects stripped by the DSP. |
| **External HDMI source**, TrueHD | **Atmos** — the TV HW-bypasses the original MAT to eARC. |

For local TrueHD-Atmos files, use a device that **bitstreams TrueHD** on the TV's HDMI input (Raspberry Pi +
LibreELEC/Kodi, an Nvidia Shield, or a PC with Kodi passthrough) — it uses the working HDMI→eARC bypass path.

## Rooting (UART → U-Boot, this exact set)

Root was obtained over the **HDMI service connector's UART** into the live U-Boot 2019.04 console (no exploit needed).
Details and the full command/output log: [c9k-hdmi-service-uart](docs/c9k-hdmi-service-uart.md),
[c9k-v670-uart-root-attempt](docs/c9k-v670-uart-root-attempt-2026-11-23.md).

**Hardware — a USB-to-UART adapter at 1.8 V logic (NOT 3.3/5 V):**
- HDMI service connector **pin 14 = TV TX** (console out) → adapter RX
- **pin 2 = TV RX** (commands in) → adapter TX (through a 1.8 V level path)
- **pin 11 = signal ground / reference**
- **Never connect HDMI pin 18 (+5 V)** to UART gear. Make/break connections only with the TV powered off.
- Serial: **115200 8N1** (e.g. PuTTY). You should reach the `=>` U-Boot prompt and see the boot log.

**USB stick:** FAT32, containing the Magisk-patched `boot_b` image (patch the stock `boot_b` with Magisk, verify
kernel byte-identical + ramdisk changed before flashing). Keep the untouched stock `boot_b` for recovery.

**At the U-Boot `=>` prompt** (active slot was `_b`; adjust for `_a`):
```text
avb init mmc
env set devicestate unlock
avb set-devicestate 0            # 0 = unlock, 1 = lock
avb set-verity disable
saveenv
usb_partial_upgrade_to_emmc c9k_v670_boot_b_magisk.img boot_b
reset
```
Then complete Android setup, install Magisk Manager, and grant Superuser for ADB/shell (`su -c id` → `uid=0`).

> ⚠️ With the patched boot installed, **do not relock** the bootloader. A bad `mboot`/bootloader write is a hard brick
> (BROM/SP Flash Tool recovery only). The orange "device is unlocked" boot message lives in `mboot` and was left alone
> on purpose — it is cosmetic and not worth the brick risk.

## Device changes applied (all reversible)

See **[device-changes-20261123](docs/device-changes-20261123.md)** for the exact packages, commands and restore steps:
- **Debloat:** 14 packages disabled via `pm disable-user --user 0` (TCL browser/analytics/Guard/Alexa/ChannelPlus/
  Dashboard/Exhibition/Remote-Diagnosis/Feedback, Netflix, Disney+, Prime Video, HBO Max).
- **Launcher:** [cleanyourtv](https://github.com/gathin23/cleanyourtv) `com.gathin.tvhome`, built with
  `stock_home_redirect=launcherx` and set up in safe redirect mode (stock `launcherx` kept as fallback). A Magisk
  `service.d` boot script re-asserts the Home-key accessibility redirect on every boot (Android clears it otherwise).
- **Updates off:** `settings put global ota_disable_automatic_update 1`.
- Partition backups (18 critical partitions + SHA-256 manifest) are on USB; host copy of the manifest in
  git-ignored `private/backups_20261123/`.

## Docs

| Topic | Doc |
|---|---|
| **Live rooted investigation (why Atmos isn't possible)** | [frida-live-trace-session](docs/frida-live-trace-session.md) |
| Audio evidence trail (static → live) | [investigation](docs/investigation.md), [hdmi-comparison](docs/hdmi-comparison.md), [joc-positive-control](docs/joc-positive-control.md), [minirama-retest](docs/minirama-retest.md), [mat-tunnel-results](docs/mat-tunnel-results.md) |
| MAT/DSP decode analysis | [mat-mlp-only-analysis](docs/mat-mlp-only-analysis.md), [mat-mlp-consumer-trace](docs/mat-mlp-consumer-trace.md), [ms12-mat-transcode-trace](docs/ms12-mat-transcode-trace.md), [dsp-dolby-param-map](docs/dsp-dolby-param-map.md), [mat-bypass-source-predicates](docs/mat-bypass-source-predicates.md) |
| Decoder / kernel / DSP internals | [decoder-dispatch-and-parser](docs/decoder-dispatch-and-parser.md), [decoder-config-contract](docs/decoder-config-contract.md), [mm-dsp-input-boundary](docs/mm-dsp-input-boundary.md), [kernel-audio-driver-audit](docs/kernel-audio-driver-audit.md) |
| IEC61937 / eARC design | [custom-firmware-iec61937](docs/custom-firmware-iec61937.md), [iec-client-experiment](docs/iec-client-experiment.md), [earc-endpoint-and-policy](docs/earc-endpoint-and-policy.md), [iec-mat-carrier-reference](docs/iec-mat-carrier-reference.md) |
| Rooting (UART / U-Boot) | [c9k-hdmi-service-uart](docs/c9k-hdmi-service-uart.md), [c9k-v670-uart-root-attempt](docs/c9k-v670-uart-root-attempt-2026-11-23.md) |
| Device changes (debloat / launcher / updates) | [device-changes-20261123](docs/device-changes-20261123.md) |
| Debloat / privacy analysis | [privacy-manifest-results](docs/privacy-manifest-results.md), [privacy-inventory-results](docs/privacy-inventory-results.md) |
| Build, deploy, test | [reproduction](docs/reproduction.md) |
| Evidence hashes | [evidence/README](evidence/README.md) |

## Reproduce (diagnostic app)

Needs JDK 17+, Android SDK 34 build-tools, adb, Python 3.9+, FFmpeg. Builds the `ro.saitatter.audioprobe` app
(AudioTrack / tunnel / MAT-IEC probes, no network permission):

```powershell
adb connect <tv-ip>:<port>
powershell -ExecutionPolicy Bypass -File .\build.ps1
python .\tools\prepare_samples.py 'C:\path\truehd-atmos.mkv' --eac3 'C:\path\eac3-joc.mkv'
powershell -ExecutionPolicy Bypass -File .\install_samples.ps1
```

Keep soundbar volume very low when testing encoded formats.

## Layout

```text
app/        Java diagnostic app (AudioTrack / tunnel / MAT probes, no network permission)
tools/      Sample prep, host tests, offline firmware/binary analysis, serial_capture.ps1
scripts/    ADB collection and preflight scripts
docs/       Research notes
evidence/   Derived observations and hashes (no raw dumps)
private/    Git-ignored: device dumps, stock/boot images, pulled binaries, backups, media
```

No TCL/MediaTek/Dolby binaries, firmware, commercial media or keys are committed. Not affiliated with TCL, Dolby,
MediaTek or Google.
