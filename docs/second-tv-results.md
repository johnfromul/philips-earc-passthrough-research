# Second TV inspection results (2026-11-20)

Follow-up to [second-tv-handoff](second-tv-handoff.md). Read-only over network ADB (USB did not enumerate on the PC; the TV accepted TCP ADB on port 5555 after the RSA prompt). No root, no setprop, no settings or mixer writes. Raw outputs: `private/device_audits/G10-stock_*.zip`, `private/device_audits/G10-hal/` (git-ignored).

## Identity

| Property | Second TV | C9K (reference) |
|---|---|---|
| Product / device | `G10_4K_GB` / `G10` ("Smart TV Pro") | 65C9K |
| SoC | **Realtek RTD2875P** (`ro.board.platform=rtd6748`) | MediaTek mt5896 |
| Android | 12 (SDK 31), `STT2.230203.001`, incr. AS50, patch 2025-05-05 | Google TV 14 |
| Software id | `V8-R75PT01-LF1V461` | V670 GB AU04 |
| ABI | armeabi-v7a only (32-bit) | |
| Boot | locked, verified boot green, verity enforcing | same |
| Audio HAL | `audio.primary.rtd6748.so` + `audio.tcl.default.so`, `librtk_*` | `audio.primary.merak.so`, `libmi3_audio_hal.so` |

## Answers to the handoff questions

1. **Same platform? No.** Realtek, not MediaTek merak/mi3. `libaudiopipelinetopology.so` and `libmi3_audio_hal.so` do not exist. None of the four C9K reference hashes can match, because the files are different libraries.
2. **Same symptom? Not testable as-is.** The audio policy advertises only AC3, E-AC3, E-AC3 JOC and AC4 on the offload/tunnel/HDMI mix ports. There is **no TrueHD, MAT or DTS** profile, and the only Dolby OMX decoders are AC3/E-AC3/AC4 (6 ch max). So TrueHD from apps cannot reach a passthrough path through the framework at all. The ceiling here is E-AC3 JOC (Dolby Digital Plus Atmos).
3. **Same code? No.** No `MatMlpOnly`, `SetMatMlpOnly`, `MAT_TRUEHD`, `MM_IN`/`HDMI_IN` source predicate in the pulled audio libs. The `AUDIO_FORMAT_DOLBY_TRUEHD`/`MAT*` strings in `audio.primary.rtd6748.so` are only the generic Android format-name tables.

**Conclusion:** the C9K `MatMlpOnly` hypothesis does not transfer. This TV adds no confirmation or refutation of it.

## Other observations

- eARC: `hdmi_control` reports `EarcSupport: true`, `EarcEnable: 2`, `EarcMode: 1`; `settings global hdmi_earc_connected=1`, `tcl_app_settings_eARC_mode=true`. But `ro.feature.hdmi_earc=false`, ARC is only on HDMI port 3, `mArcEstablished: false`, and the policy's available outputs showed only the speaker at capture time. The soundbar was likely not active/attached during the capture.
- `audio.tcl.default.so` has an SPDIF/digital-output enum `0:OFF,1:PCM,2:DDP,3:Auto,4:PassThrough,5:DD,6:MAT`, plus `RHAL_AUDIO_ATMOS_EncodeOnOff` and `MAT_ATMOS` in `librtk_hal_audio.so`. This suggests the Realtek DSP can output MAT (e.g. for HDMI-in TrueHD passthrough or re-encode), but nothing exposes it to apps. Current `tcl_sys_audio_spdifmode_hmdi_earc=3` (Auto).
- `settings global audio_avr_capability_ddp_atmos=0`: the TV currently does not see a DD+ Atmos-capable sink.
- `/proc/asound` is SELinux-denied for shell; HAL `.so` files could be read via `exec-out cat` but not via `adb pull`.

## Library hashes (SHA-256)

| File | SHA-256 |
|---|---|
| `/system/lib/libaudioclient.so` | `daa7f4586414273899b6f5798846b9e4df0c4a1f81a1a7b79469ab8925c46aed` |
| `/vendor/lib/librtk_hal_audio.so` | `4fe407b54b2881939a221fc7978a92404b2d306591ca686d5bb455afb7aec0a1` |
| `/vendor/lib/librtk_kadp_audio.so` | `e504ce2ce2bbcef8a657483ff7ed62db1ef13e337f9b11ed92a93444c375a2fc` |
| `/vendor/lib/librtk_audiodecoder.so` | `4a40aaf217e841c59c3103b7859e0810313cb17c5d44cd2d7e94ef3d04fb59cb` |
| `/vendor/etc/audio_policy_configuration.xml` | `6500abbf5dfaa7ce9e033c37d23c765a324868353d52b0678d46ae2d2cb4685c` |

## Possible next steps (owner decision)

- With the soundbar on and eARC up, re-capture (`collect_tv_audio_audit.py --label G10-earc --reports-only --logs`) to see which formats the sink advertises (`dumpsys media.audio_policy` encoded formats on HDMI_EARC).
- Optional control: E-AC3 JOC from an app should show Atmos on the soundbar. TrueHD over external HDMI is the only path where the `MAT` SPDIF mode could matter.
