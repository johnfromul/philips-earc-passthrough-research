# MatMlpOnly Runtime Hook Strategy & Firmware Audit (2026-11-20)

## Overview

Following exhaustive static and live ADB analysis on the TCL 65C9K (firmware V8-T653T01-LF1V670 AU04, kernel 5.15.192-android14-11, 32-bit armeabi-v7a userspace), the root cause for missing Dolby Atmos on internal application TrueHD playback has been isolated.

This document describes the runtime instrumentation plan using Frida (`tools/hook_mat_mlp.js`), the status of root profile requests, and the final state of all secondary strategies.

---

## 1. Verified Architecture & The Bottleneck

1. **Userspace (`libaudiopipelinetopology.so`)**:
   In `DecoderPipelineManager::create` (Thumb 0x494fa–0x49556):
   ```cpp
   if (pipeline_format == "Mat") {
       uint32_t mlpOnly = (source == "MM") ? 1 : 0;
       setPipelineParameter("MatMlpOnly", &mlpOnly, 4);
   }
   ```
   For internal applications (`source == "MM"`), `MatMlpOnly` is forced to `1`. This instructs the Dolby decoder pipeline to strip Object Audio Metadata (OAMD) and retain only the core MLP 5.1/7.1 substream. On external HDMI (`source == "HDMI"`), `MatMlpOnly` is `0` and Atmos is preserved.

2. **HAL Storage (`libmi3_audio_hal.so`)**:
   `_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` (Thumb export / offset `0x16a9d8`) writes this parameter to offset `+0x348` in the decoder filter state.

3. **Kernel Driver (`mtk-tvsnd-soc.ko`)**:
   In `HAL_AUDIO_SPDIF_BypassMode`, hardware bypass (`MAT_HDMI_bypass`) is gated on `eAudioSource == HDMI_IN (2)`. When `source == MM_IN (4)`, bypass is denied and stream is routed to internal re-encoding (`HAL_AUDIO_SPDIF_AutoMode`).

---

## 2. Frida Hook Strategy (`tools/hook_mat_mlp.js`)

A non-invasive, reversible runtime instrumentation script has been added to the repository: [`tools/hook_mat_mlp.js`](../tools/hook_mat_mlp.js).

### Hook Logic:
* Attaches to process `vendor.mediatek.hardware.audio.service` (`audioserver` user).
* Resolves `_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` in `libmi3_audio_hal.so` (with fallback to Thumb offset `0x16a9d8`).
* Intercepts `args[1]` (pointer to `uint32` parameter value).
* When `origVal == 1`, rewrites the memory value to `0` in real-time before the decoder configuration is committed.
* Does not modify system partition files, surviving without flashing risk.

### Execution Procedure (requires temporary root):
```bash
# Push and start frida-server (32-bit ARM)
adb push frida-server /data/local/tmp/
adb shell "su -c 'chmod 755 /data/local/tmp/frida-server && /data/local/tmp/frida-server &'"

# Launch hook from host
frida -U -n vendor.mediatek.hardware.audio.service -l tools/hook_mat_mlp.js

# Trigger test in audioprobe app
# Replay TRUEHD_GROUP16 tunneled session and observe Q85H Pro soundbar display.
```

---

## 3. Status of Root Acquisition

* **TV State Check (2026-11-20 live)**:
  `su` is absent on device (`uid=2000(shell)` in enforcing SELinux).
* **GhostLock Profile Request**:
  Opened issue on upstream repository:
  **[YuKongA/ghostlock-app #938](https://github.com/YuKongA/ghostlock-app/issues/938)**
  * Contains exact kernel version (`5.15.192-android14-11`), build timestamp, toolchain, CFI/SCS configuration, `perf_event_paranoid = -1`, and stock `boot.img` SHA-256 (`f4f5010d...`).
  * Awaiting profile generation from maintainers for temporary root execution.

---

## 4. Status of All Investigated Alternatives

| Strategy | Findings | Final Status |
|---|---|---|
| **ALSA Loopback (`mtk-aloop`)** | Fixed 48 kHz PCM contract; "Digital out" is a capture source selector, not an eARC sink. | **Ruled out** |
| **MediaCodec / Codec2** | Runtime scan via `audioprobe` returned NULL for all TrueHD/MAT MIME types. | **Ruled out** |
| **Virtual HDMI Source** | Hardware HDMI RX DMA/clocks are distinct from MM2 AES buffers; driver `MDrv_AUDIO_SetOutputPath` are empty `ret` stubs. | **High-complexity fallback only** |
| **MKV Extractor Gate** | `libmtkmkvextractor.so` has a single gate: `vendor.mtk.mkv.support.truehd`. Once root is available, `setprop` enables native player track visibility. Third-party apps (Moonfin/Kodi) bypass this. | **Confirmed single gate** |
| **IEC61937 Direct Bypass** | Blocked in client (`libaudioclient.so`), policy XML, and HAL (`audio.primary.merak.so` `utils_isPassthroughSupported` rejects `0x0d000000`). | **Secondary fallback** |
| **E-AC3 JOC (DD+ Atmos)** | Fully verified working from internal apps (`isAtmos=1`) to Q85H Pro over eARC. Moonfin direct-plays E-AC3 Atmos out of the box. | **Current working solution** |
