# Roadmap

Goal: Dolby TrueHD Atmos (MKV) from internal Android apps (Moonfin/Kodi/own app) to the Q85H Pro over eARC on the TCL 65C9K, firmware V670 GB AU04. Secondary: reversible debloat.

The previous roadmap (a chronological log) is preserved in git history before this commit; detailed evidence lives in the topic docs linked below.

## Verified state (re-checked live over ADB, 2026-11-19 22:00)

| Item | Value |
|---|---|
| Fingerprint | `TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04:user/release-keys`, patch 2026-06-05 |
| Boot | `vbmeta.device_state=locked`, `verifiedbootstate=green`, `veritymode=enforcing`, slot `_b`, bootloader 01.01.261024 |
| Unlock props | `ro.oem_unlock_supported` and `sys.oem_unlock_allowed` empty |
| Build | `ro.debuggable=0`, `ro.secure=1`, SELinux Enforcing, kernel 5.15.192-android14-11, 32-bit userspace |
| Audio file hashes | client `dff37369…`, HAL `e47bfd65…`, policy `4fbbdd58…` (system and tcore APEX client identical): match all earlier captures |
| MKV TrueHD prop | `vendor.mtk.mkv.support.truehd` unset; `ro.feature.media_dolby_truehd=false` |
| Policy | eARC profiles advertise TRUEHD (0x0e000000) and MAT (0x24000000); no IEC61937 |

Static claims re-verified on binaries pulled from the live TV (`private/verify_20261119/`):
- `libaudioclient.so` contains the TCL filter `... is not /system/bin/mediaserver, remove support direct IEC61937`.
- `audio.primary.merak.so` `utils_isPassthroughSupported` (0x70bd8): accepts 0x0c/0x0e/0x24…, returns 0 for 0x0d000000 (IEC61937).
- `libaudiopipelinetopology.so` `DecoderPipelineManager::create` (0x494fa–0x49556): for `Mat` pipelines, `MatMlpOnly = (source == "MM") ? 1 : 0`.
- `libmtkmkvextractor.so` gates TrueHD on `vendor.mtk.mkv.support.truehd`.

## What is settled

| Path | Result | Doc |
|---|---|---|
| E-AC3 JOC from app, full A/V tunnel | **Atmos** on soundbar, DEC(2) isAtmos=1 | [joc-positive-control](joc-positive-control.md) |
| TrueHD over HDMI from PC | **Atmos**, DEC(0) HDMI_IN isAtmos=1 | [hdmi-comparison](hdmi-comparison.md) |
| TrueHD raw from app, tunnel + 16/24-AU grouping | Clean audio, **Dolby Surround**, isAtmos=0 (two samples) | [minirama-retest](minirama-retest.md) |
| MAT payloads (IEC/LE/BE) from app | Silence / stall: input contract unknown, stop trying variants | [mat-tunnel-results](mat-tunnel-results.md) |
| IEC61937 from app | Blocked in client (process check) and HAL (2 format gates) | [custom-firmware-iec61937](custom-firmware-iec61937.md) |
| TCL Media Center MKV | TrueHD track dropped by extractor property gate; shell setprop denied by SELinux | [native-extractor-stage4](native-extractor-stage4.md) |
| Public MediaCodec TrueHD/MAT decoder | None present | [runtime-codecs-and-bypass](runtime-codecs-and-bypass.md) |
| Sample framing | Source starts at major sync, timing fine; not the cause | [truehd-parser-alignment](truehd-parser-alignment.md) |

**Conclusion:** without root/firmware changes there is no remaining path on V670. The data reaches the DSP; the Atmos layer is lost on the internal (MM) source. Leading hypothesis: `MatMlpOnly=1` for MM source makes the decoder use only the MLP base (non-Atmos) substream. Unproven.

## Plan with root (pending: ESP-based access, 2026-11-20)

0. **Backup first.** Dump all partitions (at least boot/vendor_boot/vbmeta/dtbo/super/tclconfig/tvconfig and any calibration partitions) before writing anything. Keep the original V670 OTA ZIP (SHA-256 `3d2fdf53…`) as the stock baseline.
1. **Extractor gate**: `setprop vendor.mtk.mkv.support.truehd true`, then MKV in TCL Media Center. Cheap; shows whether the native player route yields Atmos.
2. **MatMlpOnly hypothesis** (highest value): make the MM path use `MatMlpOnly=0` (runtime hook, e.g. Frida on the audio HAL process, before any file patch), replay the existing TRUEHD_GROUP16 tunnel test, watch `isAtmos` and the soundbar.
3. Only if 2 fails: IEC61937 passthrough (client filter + HAL `utils_isPassthroughSupported` + `utils_earc_set_decoder_format` + eARC-only policy profile). Offline client prototype exists: [iec-client-experiment](iec-client-experiment.md), [earc-endpoint-and-policy](earc-endpoint-and-policy.md), [iec-mat-carrier-reference](iec-mat-carrier-reference.md).
4. Persist whatever works (Magisk module / overlay), then regression: PCM, E-AC3 JOC, HDMI Atmos, CEC, standby, OTA behavior.

## Debloat (no root needed, reversible)

Use `pm disable-user --user 0 <pkg>`; restore with `pm enable --user 0 <pkg>`. One package at a time, test home/settings/HDMI/eARC/remote after each.

- Already disabled: tclhome_passive, setupwraith, **com.tcl.browser (BrowseHere)**, esticker, waterfall.overseas, netflix.ninja, launcherx, logkit, exhibit, messagebox. A second user (10) exists where BrowseHere remains enabled.
- Candidates (owner decides): com.tcl.magiconnectfree (Firebase analytics), com.tcl.smartalexa, com.tcl.ocean.instructions, com.tcl.repairguide (remote screen-sharing diagnosis), com.google.android.feedback.
- Keep: com.tcl.tvinput, com.tcl.systemserver, HeraEye/hera APEX, tcore, T-Solo, consent/update services.

Findings: [privacy-manifest-results](privacy-manifest-results.md), [privacy-inventory-results](privacy-inventory-results.md).

## Audio kernel preflight and fallback

- [Audio kernel compatibility preflight](audio-kernel-compatibility-preflight.md): six stock modules have modversions/BTF, no standard appended module signature marker; dependencies documented. Run the offline boot audit tool to determine selected config keys. Latest owner preflight still cannot confirm Virtual A/B merge state. No live kernel identity or load compatibility established.
- [Virtual HDMI audio source](virtual-hdmi-audio-source.md) remains a conditional fallback only if the preferred MM/HAL route fails and a real memory-fed compressed bypass transport is established.
