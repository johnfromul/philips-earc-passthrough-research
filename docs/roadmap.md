# Remaining experiments and upstream handoff

Goal: MKV with TrueHD Atmos in Moonfin / internal Android apps, without root or firmware changes. MPEG-TS is out of scope.

## Priority 1: validate identical media

- [x] FFmpeg 8.1.1 detects Dolby TrueHD + Dolby Atmos in the extracted PC sample and native MKV stream-copy result.
- [ ] Verify PC and TV truehd.thd SHA-256 match (Get-FileHash -Algorithm SHA256 and adb shell sha256sum).
- [ ] Independently verify exact remuxed MKA on Windows Kodi WASAPI; separately confirm the exact file and actual soundbar indication for hdmi_working.zip.
- [ ] Add genuine E-AC3 JOC positive control in the Android tester, including full tunnel; compare decoder isAtmos to soundbar output.

## Priority 2: compare internal MKV playback with HDMI

- [x] Native MKV on USB, Pass Through and Auto: silence and no audio tracks (see native-player-test.md).
- [x] Identify extractor gate, property default false and failed shell activation (see native-extractor-stage4.md).
- [x] Capture functioning HDMI MAT path; logs actually indicate Auto, HDMI_IN, DEC(0), MAT_TRUEHD, isAtmos=1 (see hdmi-comparison.md).
- [x] Add v0.2.2 experimental 192 kHz MAT full A/V tunnel modes, using public API.
- [ ] Build/install v0.2.2 and test modes 8, 9, 10 with Auto; capture each test and note audio/video result.
- [ ] If successful, reproduce in Pass Through and with a second verified Atmos MKV. If unsuccessful, classify failures by track creation, HW sync, decoder frame recognition, Atmos metadata and output route.
- [ ] Repeat with a verified different TrueHD Atmos sample and TrueHD non-Atmos control.

## Priority 3: native static analysis (if v0.2.2 fails)

- [ ] Reproduce string xrefs and ELF symbol offsets against manifest hashes. Compare identical client libraries with AOSP.
- [ ] Check exact openDirectOutput selection and hardware profiles; determine missing IEC profile's origin.
- [ ] Compare source-specific decoder initialization and AStream behavior (HDMI_IN vs MM_IN), in particular _dspCommFunc_LAPAF_StartDecoderDevice and surrounding call sites. Inspect native parameter and metadata interfaces read-only; do not assume status flag causes metadata loss.
- [ ] Compare a working firmware/model if available through lawful read-only extraction and identical test procedure.

## Upstream handoff

Share anonymized results with TCL and relevant app developers. Include model, firmware, source verification, expected/actual behavior, external HDMI positive control, grouped A/V observations, native MKV feature gate and IEC filter evidence, DSP isAtmos flags. Ask TCL for a supported public playback path to TrueHD Atmos and whether the IEC restrictions and MKV gate are intentional. Avoid publishing proprietary binaries or personal identifiers. The diagnostic application is not a fix. Normal sound and Atmos indication, not successful track creation alone, define success.

No root/system modifications or SELinux changes. Do not spoof hardware source or process identity. Potential future solution: standards-compliant app path or OEM firmware correction.
