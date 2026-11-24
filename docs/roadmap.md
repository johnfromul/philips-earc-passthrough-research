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

## Current device state (2026-11-23 live Wireless ADB check)

| Item | Current observation |
|---|---|
| AVB | Device state is unlocked/orange; AVB verification is off |
| dm-verity | Disabled |
| Boot image | Magisk v31.1 patched image is installed on active slot `boot_b` |
| Android | Boot completed; owner completed setup after the unlock reset |
| ADB / root | Wireless ADB connected; `su -c id` confirms UID 0, Magisk context; SELinux remains `Enforcing` |
| eARC feature | Recreated reversible `earc_output_test` property module; `ro.feature.hdmi_earc=true` after reboot |
| Active output | AudioPolicy lists Speaker and HDMI EARC as available; eARC profiles include MAT and Dolby TrueHD |
| ALSA | Card 0 is `mtktvsndcard`; PCM D9 is `tvsnd-common-pcm` (6 playback substreams); compressed nodes D12–D16 exist. Even with eARC enabled, no PCM is named `EARC NONPCM Playback` |
| Loaded audio kernel modules | `mtk_earc`, `mtk_tvsnd_machine`, `mtk_tvsnd_platform`, `mtk_tvsnd_soc`, `mtk_tvsnd_dsp_loader` are loaded |

The 2026-11-19 table above is a pre-unlock baseline, not the current boot state. The 2026-11-23 UART/Magisk and reversible device changes are recorded in [device changes](device-changes-20261123.md) and the [UART root attempt log](c9k-v670-uart-root-attempt-2026-11-23.md).

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
| TrueHD raw from app | Audible after decode/re-encapsulation; sink reports **Dolby Surround**, not Atmos; callback reports `atmosStream(0)` | [frida-live-trace-session](frida-live-trace-session.md) |
| MAT AudioTrack (`enc=19`) | 48 kHz/7.1 MAT track opened and reported eARC route; 286,720 bytes accepted, then write stalled; playback head stayed 0. No wire transmission or Atmos preservation established | [investigation](investigation.md), [endpoint inventory](earc-endpoint-and-policy.md) |
| IEC61937 AudioTrack (`enc=13`) | On current eARC, 192 kHz/7.1 reports `direct=false` and fails track construction; no data written. No successful IEC route established | [investigation](investigation.md), [IEC client experiment](iec-client-experiment.md) |
| eARC non-PCM ALSA endpoint | HAL lookup searches `/proc/asound/pcm`; even with eARC enabled, no matching endpoint is registered. D9 and compressed D12–D16 remain unmapped | [earc-endpoint-and-policy](earc-endpoint-and-policy.md) |
| TCL Media Center MKV | TrueHD track dropped by extractor property gate; shell setprop denied by SELinux | [native-extractor-stage4](native-extractor-stage4.md) |
| Public MediaCodec TrueHD/MAT decoder | None present | [runtime-codecs-and-bypass](runtime-codecs-and-bypass.md) |
| Sample framing | Source starts at major sync, timing fine; not the cause | [truehd-parser-alignment](truehd-parser-alignment.md) |

**Current conclusion:** no working internal-app TrueHD Atmos route or patch has been found. Forcing `MatMlpOnly` from `1→0` live changed nothing, and the inspected MM-vs-HDMI DSP SHM namespace exposes no Atmos toggle. HDMI input takes a hardware MAT bypass and preserves the Atmos positive control. Internal TrueHD is decoded/re-encapsulated and the sink reports Dolby Surround; the callback reports `atmosStream(0)`, but raw DSP status and transmitted MAT object metadata were not independently measured. The correctly tagged MAT AudioTrack routes to eARC, but its payload write stalled with playback head zero; wire delivery and Atmos preservation remain unproven. See [live trace](frida-live-trace-session.md), [DSP candidate audit](dsp-candidate-audit.md), [live investigation](investigation.md#rooted-earc-endpoint-retest-2026-11-23), and [offline loader audit](dsp-loader-offline-audit-2026-11-23.md).

## Historical GhostLock/root plan (2026-11-22; updated by the UART attempt log)

The numbered plan below is historical. Since it was written, the partition backup was completed, Magisk root was obtained, the eARC output flag was applied through a reversible module, and the live `MatMlpOnly`/DSP-parameter investigation was completed. No DSP firmware or TEE change was made. See [device changes](device-changes-20261123.md) and the current conclusion above.

0. **Backup first.** ✅ Done 2026-11-23: 18 critical partitions (slot `_b`) dumped to USB `c9k_backup_20261123/` with SHA-256 manifest (host copy `private/backups_20261123/manifest.txt`): boot/vendor_boot/vbmeta/dtbo/tclconfig/tvconfig/oem/optee/armfw/mboot/misc/metadata/config/factory/impdata/persist/mtk_persist/certificate. `super` (4.5 GB) exceeds the USB's FAT32 4 GB file limit — not dumped (reconstructable from the stock OTA; not touched by the audio work); split-dump if ever needed. Keep the original V670 OTA ZIP (SHA-256 `3d2fdf53…`) as the stock baseline.
1. **GhostLock V670 port**: an analysis-only profile is registered in the local source copy and the live kernel selected it through a standalone read-only ARM32 inspector (`primitive_arming=REFUSED`). The stock image's embedded BTF confirms the V670 task, cred, waiter, page, and kmem_cache layouts; `/proc/slabinfo` confirms the 1024-byte mm_struct cache. Live `/proc/zoneinfo` confirms the 3-GiB physical RAM extent, and exact V670 ELF analysis fixes the futex hash table at 1024 entries; the scanner now uses that value and rejects ambiguous candidates, but the V670 route has not exercised it or tied it to a known target page. Static compat MCAST stack-copy geometry matches the stale waiter placement, but the live carrier is not validated. `kernel_phys_load=0x26000000` and runtime SLUB values remain unconfirmed; PFN bits are masked, so the target reclaim witness is still unavailable to the shell. See [GhostLock V670 profile preflight](ghostlock-v670-profile-preflight.md). The V643 package and official arm64-only APK do not match this V670 TV; live carrier execution, reclaim sequence, and compatible root handoff remain unresolved.
2. **Runtime hook / DSP params — completed read-only.** `MatMlpOnly` was forced `1→0` and had no effect. The full inspected DSP SHM namespace diff found no exposed Atmos toggle; no targeted DSP write was made. See [live trace](frida-live-trace-session.md) and [DSP parameter map](dsp-dolby-param-map.md).
3. **Extractor gate**: `setprop vendor.mtk.mkv.support.truehd true`, then MKV in TCL Media Center. Cheap; shows whether the native player route yields Atmos.
4. **IEC/MAT route — partial evidence corrected.** The old enc=19 test used IEC bursts under a MAT encoding, so neither true IEC61937 (encoding 13) nor a valid MAT contract has been proven. See [live trace](frida-live-trace-session.md).
5. **eARC output flag — re-enabled for endpoint investigation.** The property module is active again and AudioPolicy exposes HDMI EARC, but ALSA still has no named EARC non-PCM endpoint. See [device changes](device-changes-20261123.md) and [endpoint inventory](earc-endpoint-and-policy.md).

## Debloat (no root needed, reversible)

Use `pm disable-user --user 0 <pkg>`; restore with `pm enable --user 0 <pkg>`. One package at a time, test home/settings/HDMI/eARC/remote after each.

- Already disabled: tclhome_passive, setupwraith, **com.tcl.browser (BrowseHere)**, esticker, waterfall.overseas, netflix.ninja, launcherx, logkit, exhibit, messagebox. A second user (10) exists where BrowseHere remains enabled.
- Candidates (owner decides): com.tcl.magiconnectfree (Firebase analytics), com.tcl.smartalexa, com.tcl.ocean.instructions, com.tcl.repairguide (remote screen-sharing diagnosis), com.google.android.feedback.
- Keep: com.tcl.tvinput, com.tcl.systemserver, HeraEye/hera APEX, tcore, T-Solo, consent/update services.

Findings: [privacy-manifest-results](privacy-manifest-results.md), [privacy-inventory-results](privacy-inventory-results.md).

## Audio kernel preflight and fallback

- [Audio kernel compatibility preflight](audio-kernel-compatibility-preflight.md): six stock modules have modversions/BTF, no standard appended module signature marker; dependencies documented. Run the offline boot audit tool to determine selected config keys. Latest owner preflight still cannot confirm Virtual A/B merge state. No live kernel identity or load compatibility established.
- [Virtual HDMI audio source](virtual-hdmi-audio-source.md) remains a conditional fallback only if the preferred MM/HAL route fails and a real memory-fed compressed bypass transport is established.

- [DPlayback driver audit](nonpcm-dplayback-driver-audit-2026-11-24.md) confirms that active overlay 22 disables the alternate ASoC stack on V670. The stock `EARC-NONPCM` DAI provides a reference for an ALSA-to-DMA7 path. Live MMIO claims block parallel bring-up of the alternate SoC provider, and its TX window differs from the C9K's active eARC TX. The open question is how to route a host-owned DMA ring to the active TX. See also the [stock audio Device Tree topology](audio-device-tree-topology.md).
