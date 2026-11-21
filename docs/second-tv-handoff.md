# Second TV handoff (2026-11-20)

Handoff for continuing on another PC / new Claude Code session. Goal: test whether the C9K hypothesis also applies to the owner's second TCL TV (model and firmware not yet recorded; possibly a C655 variant).

## Verified C655 owner baseline — 2026-11-21

The second TV has now been identified from an ADB evidence bundle as **Realtek RTD2875P / rtd6748, G10 board, R75PT01 V461, Android 12, kernel 5.4.274+, slot _a, AVB locked/green, SELinux enforcing**. It is **not** MT5896. Current policy exposes E-AC3 JOC/AC4 but not TrueHD/MAT/IEC61937; runtime eARC service reports support but no active link in the captured state. Primary HAL `audio.primary.rtd6748.so` was not pullable without additional access. No playback comparison or UART capture was in the archive.

See [C655 Realtek baseline](c655-realtek-baseline.md) for evidence hashes and next steps. **Hardware follow-up (2026-11-21):** owner measured Delock 65168 service-breakout contacts and 3.3 V candidate UART idle levels, but has not connected ESP32 or captured serial output. Exact continuity / voltage readings, device equipment, pin-map status, and electrical stop conditions are in [C655 HDMI Service UART bench log](c655-hdmi-service-uart-bench-log.md) and [structured readings](../evidence/c655-hdmi-service-electrical-2026-11-21.json). Do not infer a verified common GND from a 0 V reading between isolated terminal 11 and terminal 17. Remaining parts of this document are **the earlier pre-capture plan**, retained for context; C9K patch hashes and the MatMlpOnly idea must not be applied to C655 without Realtek-specific proof.

## Hypothesis under test (from C9K, V670 GB AU04)

- TrueHD Atmos from internal apps plays clean but as **Dolby Surround** (`MAT_TRUEHD, isAtmos=0`), while TrueHD over external HDMI and E-AC3 JOC from apps give **Atmos** (`isAtmos=1`). See [roadmap](roadmap.md) "What is settled".
- Leading explanation (unproven): `libaudiopipelinetopology.so` `DecoderPipelineManager::create` sets `MatMlpOnly = (source == "MM") ? 1 : 0` for `Mat` pipelines; the decoder then uses only the non-Atmos MLP substream. Hook target on C9K: `_MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly` in `libmi3_audio_hal.so` ([mat-mlp-hook-strategy](mat-mlp-hook-strategy.md)).

## Questions for the second TV

1. **Same platform?** SoC, board, Android version/ABI, audio HAL name, policy XMLs. If not MediaTek merak/mi3 family, the hypothesis does not transfer directly.
2. **Same symptom?** App TrueHD → Atmos or Dolby Surround on the soundbar? Controls: E-AC3 JOC from app, TrueHD over external HDMI.
3. **Same code?** Compare hashes of audio libraries with C9K; if they differ, check statically whether the `MM` source predicate and `SetMatMlpOnly` exist.

C9K reference hashes (SHA-256, [evidence/firmware-binary-hashes.csv](../evidence/firmware-binary-hashes.csv)):

| File | SHA-256 |
|---|---|
| `/system/lib/libaudioclient.so` | `dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9` |
| `/vendor/lib/hw/audio.primary.merak.so` | `e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad` |
| `/vendor/lib/libaudiopipelinetopology.so` | `96a1985e18199368b89c28aea376952073b7d332753b50b13fc121f3dba9cf29` |
| `/vendor/lib/libmi3_audio_hal.so` | `07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a` |

## Connection status

- Second TV has **USB debugging only**, no "Wireless debugging" menu.
- First try: `adb connect <tv-ip>:5555` with USB debugging on (many Android TVs accept TCP ADB then); accept the RSA prompt on the TV ("Always allow").
- If that fails: USB ADB from a PC (TV must expose a device-mode USB port; may need an A-to-A cable or a service port). This is why a different PC may be used.
- `emulator-5562 offline` seen in `adb devices` on the original PC is not the TV; ignore it.
- If the C9K is also connected, pass `--serial` / set `$env:ANDROID_SERIAL`.

## Steps (read-only, no root, no settings changes)

Record first: exact model (e.g. 55C655), firmware string from Settings → About, eARC/Pass Through setting, soundbar input.

```powershell
adb devices -l
# 1. Baseline platform capture
python .\tools\collect_tv_audio_audit.py --label <MODEL>-stock --logs
# 2. While playing each sample manually (start logcat capture before playback):
python .\tools\collect_tv_audio_audit.py --label <MODEL>-truehd --logs --reports-only
python .\tools\collect_tv_audio_audit.py --label <MODEL>-joc    --logs --reports-only
python .\tools\collect_tv_audio_audit.py --label <MODEL>-hdmi   --logs --reports-only
```

Outputs go to `private/device_audits/` (git-ignored). Then:

1. Compare identity (fingerprint, `ro.board.platform`, `ro.hardware`, ABI) with C9K values in [roadmap](roadmap.md).
2. Hash the pulled audio libraries and compare with the table above.
3. In logs, look for `isAtmos`, `MAT_TRUEHD`, `MM_IN`, `HDMI_IN`, `MatMlpOnly`, decoder index DEC(n).
4. If libraries match C9K: hypothesis transfers as-is; the second TV only adds a second confirmation of the symptom. If they differ: static analysis of the new `libaudiopipelinetopology.so` / `libmi3_audio_hal.so` (search for `MatMlpOnly`, `"MM"`), see [mat-bypass-source-predicates](mat-bypass-source-predicates.md).

For app playback tests, the diagnostic app and samples are needed (`build.ps1`, `install_samples.ps1`, `install_tunnel.ps1`; see [reproduction](reproduction.md)). Keep soundbar volume very low.

## Not in git (copy manually if moving PCs)

- `private/` (C9K dumps, pulled binaries, reports), `samples/`, `samples_alt/`, `out/debug.keystore` (keep it, or the app must be reinstalled), stock OTA ZIP.
- Samples can be regenerated with `tools/prepare_samples.py` from the original MKVs.
- Claude Code chat history and memory are local to the original PC and do not transfer.

## Rules

Never use C9K images, patch offsets or firmware on the second TV. No rooting, flashing, setprop or mixer writes without explicit owner decision. Do not publish serials, IPs or raw dumps.
