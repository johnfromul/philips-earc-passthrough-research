# Experimental stock-based firmware project: IEC61937

Status: preparation only, no flashable image. Owner requested custom firmware after the non-root tests. Target TCL 65C9K, T653T01, G08_4K_GB, AU04, V8-T653T01-LF1V670, Android 14. Retain stock vendor/DSP, display processing, calibration, remote and TV input, eARC/CEC, streaming functions. Modify only the audio path necessary for standard app IEC61937. No MPEG-TS, no process impersonation, no forced status flags, no PCM masquerading. Do not redistribute vendor binaries or OEM signing material.

## Observed boot constraints

Previous private firmware_inventory.zip properties.txt shows bootloader 01.01.261024, ro.boot.vbmeta.device_state=locked, verifiedbootstate=green, veritymode=enforcing, ro.boot.dynamic_partitions=true, ro.build.ab_update=true and slot _b at that capture. Current state must be rechecked. A modified stock image cannot simply be installed as a signed OTA on a locked bootloader. Do not assume that a supported unlock path exists or that A/B guarantees recovery.

Use scripts/firmware_preflight.ps1 with the exact current stock package if available. It reads only targeted properties, partition names/mounts, bootctl query results, audio paths/hashes and optional ZIP metadata. It does not retrieve device secrets, flash, reboot or modify settings. Keep firmware packages and outputs private.

References:
- Android Verified Boot device state: https://source.android.com/docs/security/features/verifiedboot/device-state
- AVB: https://android.googlesource.com/platform/external/avb/
- Community precedent on different models/builds (not proof of compatibility): https://github.com/lukasa1993/tcl-tv-toolkit

Before any device change: obtain exact V670 stock package and independently confirm matching recovery image and supported recovery procedure for this physical model/region, available USB media, build metadata, partition layout and bootloader state. Confirm operator accepts possible data wipe, warranty/DRM changes, boot failure. Do not flash an arbitrary C6K/P8K image, downgrade casually or change bootloader/TEE/calibration/DRM partitions.

## Three audio gates

1. **Client**: V670 libaudioclient.so, identical system and com.tcl.tcore copies in prior capture; SHA-256 dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9. The previous analysis documents explicit media-server identity conditions at direct profile discovery, direct/offload support, and AudioTrack::set. Stock-based implementation should use ordinary format/route capability checks consistently, not spoof an application identity.
2. **Policy**: current primary_audio_policy_configuration.xml declares TrueHD/MAT for offload and HW_AV_SYNC, not IEC61937. The HDMI EARC devicePort has no static profiles in this file; runtime profiles are negotiated/generated. A dedicated direct eARC-only profile is a design candidate, subject to actual sink and HAL support. Do not advertise IEC for speaker, headphones or ARC indiscriminately. Do not assume a static XML change overrides the dynamic route.
3. **HAL**: read-only disassembly of audio.primary.merak.so (SHA-256 e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad) confirms utils_isPassthroughSupported (Thumb VA 0x70bd8, 86 bytes) masks to the main format byte from stream +0xc3. At 0x70c10–0x70c1e it accepts 0x0c000000 or 0x0e000000; 0x0d000000 (IEC61937) returns false (cmp 0xe000000 then movne r0,#0). MAT 0x24000000 returns true through 0x70bee–0x70bfa. This is an additional eligibility predicate, not proof of the complete open/write path. Merely exposing profiles or forcing the predicate true does not prove that the HAL can consume IEC bursts or deliver them unmodified. __apply_iec_config_by_stream (0x73570) exists, but the name alone is insufficient to infer IEC61937 input support.

Reproduce the last observation with:
    llvm-objdump -d --triple=thumbv7a-linux-android --start-address=0x70bd8 --stop-address=0x70c2e audio.primary.merak.so

## Milestones

- F0 inventory and stock package inspection, exact recoverability and supported owner-unlock decision. No flash until independently reviewed.
- F1 offline design: audit all IEC61937 open/output classification, carrier and burst handling, direct output/eARC routing, dynamic profiles and capability checks. Identify active binary locations (APEX vs system) and corresponding build source/patch strategy for the exact stock package. Source-level change preferred where possible; no blind AOSP library replacement or edit of AVB/APEX signatures without a supported development setup.
- F2 local experimental, reversible audio-only build/overlay on a supported development configuration. Compare files against a stock-hash manifest. Use narrow eARC capability conditions and preserve fallback. No unverified global format advertising. Recovery plan reviewed before hardware trial.
- F3 open-only probe: real third-party API sees IEC61937 profile and can open appropriate direct track; prove route actually eARC. Then low-volume short burst test with correct validated carrier, stop on noise. Compare audio frame progression, decoder/output logs and soundbar. Normal audio plus Atmos is required, not track creation alone.
- F4 Moonfin MKV integration and regression checks: AC3/EAC3 JOC, PCM, DTS if supported, speaker/ARC, HDMI Atmos, CEC, HDR, TV startup and standby. Restore stock on failure.

No firmware image or root method has been validated on this exact unit. Next step is F0; continue static HAL work offline in parallel when stock package metadata is available.
