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

## Current device state (2026-11-23 UART/Magisk session)

| Item | Current observation |
|---|---|
| AVB | `avb set-devicestate 0` completed; environment save reported `OK` |
| dm-verity | Disabled with `avb set-verity disable`; MMC write reported `OK` |
| Boot image | Magisk v31.1 image written to active slot `boot_b`; U-Boot reported both MMC writes `OK` |
| Android | Kernel, ramdisk, and device tree loaded `OK`; owner completed the initial setup after the unlock reset |
| ADB | Wireless debugging re-enabled after Magisk's first-run restart; ADB is connected over the new TLS port |
| Root / SELinux | Magisk 31.1 is active; `/debug_ramdisk/su -c id` returns `Permission denied`, so UID 0 is unverified. SELinux is `Enforcing` |

The user completed setup, accepted Magisk's first-run restart, and re-enabled Wireless debugging. Enable or approve Magisk Superuser access for ADB/`shell`, then continue with the [UART root attempt log](c9k-v670-uart-root-attempt-2026-11-23.md). The 2026-11-19 table above is a pre-unlock baseline, not the current boot state.

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

**Conclusion:** without root/firmware changes there is no remaining path on V670. The data reaches the DSP; the Atmos layer is lost on the internal (MM) source. Leading hypothesis: `MatMlpOnly=1` for MM source makes the decoder use only the MLP base (non-Atmos) substream. Unproven — and see the caveat below.

**Mechanism identified (2026-11-20, live MS12 pull, [ms12-mat-transcode-trace](ms12-mat-transcode-trace.md)):** the internal (MM) path is a TrueHD→MAT **transcode** inside `libms12v27.so`. The TrueHD decoder *does* extract the Atmos object metadata (EVO/OAMD: `thd_evo_parse_extra_data`), but the MAT re-encoder carries Atmos only when its EVO-present flag (encoder state `+0x212c`, read by `mat_enc_is_evo_present`) is set, and MS12 opens the MM transcode in a **channel-based (MLP-only) `dec_mode`** (`transcode_open`/`mat_enc_is_channel_based`), so the objects the decoder extracted are not re-injected → `isAtmos=0`. HDMI bypasses the transcode (objects intact → `isAtmos=1`); JOC uses a separate decoder path that preserves Atmos. The HAL `MatMlpOnly` (`+0x348`, write-only in `libmi3_audio_hal.so`, see [mat-mlp-consumer-trace](mat-mlp-consumer-trace.md)) is the HAL's *name* for this MS12 mode, not the lever itself. **Conclusion: not a decode/codec failure but a source-bound policy to transcode internal TrueHD in MLP-only mode.** A runtime hook is only useful if a single value (HAL op-mode arg, MS12 `dec_mode`, or DSP `dolby_param`/`MAT_HDMI_bypass`) flips it — trace that before spending root budget.

## Historical GhostLock/root plan (2026-11-22; updated by the UART attempt log)

The backup, GhostLock carrier, and runtime-hook steps below were a pre-unlock plan. The 2026-11-23 Magisk boot modification is recorded separately above; a full partition backup, GhostLock run, root verification, and audio modification have not been completed.

0. **Backup first.** Dump all partitions (at least boot/vendor_boot/vbmeta/dtbo/super/tclconfig/tvconfig and any calibration partitions) before writing anything. Keep the original V670 OTA ZIP (SHA-256 `3d2fdf53…`) as the stock baseline.
1. **GhostLock V670 port**: an analysis-only profile is registered in the local source copy and the live kernel selected it through a standalone read-only ARM32 inspector (`primitive_arming=REFUSED`). The stock image's embedded BTF confirms the V670 task, cred, waiter, page, and kmem_cache layouts; `/proc/slabinfo` confirms the 1024-byte mm_struct cache. Live `/proc/zoneinfo` confirms the 3-GiB physical RAM extent, and exact V670 ELF analysis fixes the futex hash table at 1024 entries; the scanner now uses that value and rejects ambiguous candidates, but the V670 route has not exercised it or tied it to a known target page. Static compat MCAST stack-copy geometry matches the stale waiter placement, but the live carrier is not validated. `kernel_phys_load=0x26000000` and runtime SLUB values remain unconfirmed; PFN bits are masked, so the target reclaim witness is still unavailable to the shell. See [GhostLock V670 profile preflight](ghostlock-v670-profile-preflight.md). The V643 package and official arm64-only APK do not match this V670 TV; live carrier execution, reclaim sequence, and compatible root handoff remain unresolved.
2. **Runtime hook — corrected target** (see [ms12-mat-transcode-trace](ms12-mat-transcode-trace.md), [dsp-dolby-param-map](dsp-dolby-param-map.md)): do **not** hook `DecoderCommon.SetMatMlpOnly` — write-only, and the discrete MAT APAF filters are stubs. The DSP exposes an explicit **`ATMOS_ENCODE_ENABLE`** parameter (distinct from the `MAT_HDMI_bypass` HDMI gate) — the prime suspect lever for object carriage on the MM re-encode path. Plan: (a) with root+Frida, trace `HAL_DEC_R2_Set_SHM_PARAM` (0x90c60) / `HAL_AUDIO_DigitalTx_ApplySetting` logging `ATMOS_ENCODE_ENABLE`/`DOLBY_ATMOS_ENABLE`/`MAT_HDMI_bypass` for MM-TrueHD vs HDMI-TrueHD; (b) cross-check `_MI_APAF_FILTER_MS12V2_FLAP_SetParameter` (0x13dbf8) + MS12 encoder `+0x212c`; (c) if the param differs by source, set it on for MM, fail-closed, and verify `isAtmos`. Retarget `tools/hook_mat_mlp.js`. Live GB AU04 `mtk-tvsnd-soc.ko` is byte-identical to the analyzed stock module (confirmed on-device).
3. **Extractor gate**: `setprop vendor.mtk.mkv.support.truehd true`, then MKV in TCL Media Center. Cheap; shows whether the native player route yields Atmos.
4. Only if 2 fails: IEC61937 passthrough (client filter + HAL `utils_isPassthroughSupported` + `utils_earc_set_decoder_format` + eARC-only policy profile). Offline client prototype exists: [iec-client-experiment](iec-client-experiment.md), [earc-endpoint-and-policy](earc-endpoint-and-policy.md), [iec-mat-carrier-reference](iec-mat-carrier-reference.md).
5. Persist whatever works (Magisk module / overlay), then regression: PCM, E-AC3 JOC, HDMI Atmos, CEC, standby, OTA behavior.

## Debloat (no root needed, reversible)

Use `pm disable-user --user 0 <pkg>`; restore with `pm enable --user 0 <pkg>`. One package at a time, test home/settings/HDMI/eARC/remote after each.

- Already disabled: tclhome_passive, setupwraith, **com.tcl.browser (BrowseHere)**, esticker, waterfall.overseas, netflix.ninja, launcherx, logkit, exhibit, messagebox. A second user (10) exists where BrowseHere remains enabled.
- Candidates (owner decides): com.tcl.magiconnectfree (Firebase analytics), com.tcl.smartalexa, com.tcl.ocean.instructions, com.tcl.repairguide (remote screen-sharing diagnosis), com.google.android.feedback.
- Keep: com.tcl.tvinput, com.tcl.systemserver, HeraEye/hera APEX, tcore, T-Solo, consent/update services.

Findings: [privacy-manifest-results](privacy-manifest-results.md), [privacy-inventory-results](privacy-inventory-results.md).

## Audio kernel preflight and fallback

- [Audio kernel compatibility preflight](audio-kernel-compatibility-preflight.md): six stock modules have modversions/BTF, no standard appended module signature marker; dependencies documented. Run the offline boot audit tool to determine selected config keys. Latest owner preflight still cannot confirm Virtual A/B merge state. No live kernel identity or load compatibility established.
- [Virtual HDMI audio source](virtual-hdmi-audio-source.md) remains a conditional fallback only if the preferred MM/HAL route fails and a real memory-fed compressed bypass transport is established.

- [Stock audio Device Tree topology](audio-device-tree-topology.md): verified driver-side OF identifiers; collect candidate DT nodes from existing hash-verified vendor_boot/dtbo images using `python tools/inspect_stock_audio_dt.py`. Actual DT trees and selected overlay remain unverified; no device changes.
