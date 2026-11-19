# Remaining experiments and upstream handoff

## Priority 1: validate identical media

- [ ] Verify PC and TV `truehd.thd` SHA-256 match (`Get-FileHash -Algorithm SHA256`, `adb shell sha256sum <app files>/truehd.thd`).
- [ ] Play the stream-copy MKA on Windows Kodi WASAPI; record soundbar Atmos indication and correct audio.
- [ ] Provide a genuine E-AC3 JOC positive control in the Android tester, including full tunnel; compare decoder `isAtmos` to soundbar output.

## Priority 2: native playback

- [ ] Run `scripts/native_test.ps1`, open the generated MKV with the built-in TCL player, first Pass Through then Auto.
- [ ] Capture live logcat and active dumps as described in reproduction.md; identify actual process and format (`NuPlayer`, MediaPlayer, vendor player may use different paths). Do not infer privilege just from an app name.
- [ ] Compare `AudioTrack` construction, `IEC61937` filter, selected route, DSP channel map, MAT type and Atmos flag. Native player may still use a vendor pipeline; no proof of IEC routing yet.
- [ ] Repeat with a verified different TrueHD Atmos sample and a TrueHD non-Atmos control, then with a compatible EAC3 positive control.

## Priority 3: native static analysis

- [ ] Reproduce string xrefs and ELF symbol offsets against manifest hashes. Compare identical client libraries with AOSP.
- [ ] Check exact `openDirectOutput` selection and hardware profiles. Determine whether the missing IEC profile is deliberate/static or dynamic; no conclusion based only on string search.
- [ ] Inspect driver information-query and metadata topology paths; document evidence rather than assuming status flag causes metadata loss.
- [ ] Compare a working firmware/model if available through lawful read-only extraction and identical sample / test procedure.

## Upstream handoff

Share anonymized results with TCL and relevant app developers. Include model, firmware, exact source verification, expected/actual behavior, direct HDMI positive control, grouped A/V test observations, filtered direct-profile message and `isAtmos=0` evidence. Ask TCL for supported public playback path to TrueHD Atmos and whether IEC restrictions/profile omissions are intentional. Avoid publishing proprietary binary files or customer device identifiers. Do not describe the current tester as a fix. A successful track opening without normal Atmos audio is not success.

Potential future work: a standards-compliant non-privileged app path or OEM firmware correction. No root or system modification is needed for these next experiments. Do not change bootloader, SELinux or system libraries while diagnosing.
