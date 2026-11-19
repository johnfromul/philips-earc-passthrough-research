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

## F0 preflight result (2026-11-19)

Source: private firmware_preflight.zip, 5252 bytes, SHA-256 e1804ce93f09af6cf5482d8a20c62e66bdd31871e9fca773e6e6080d0b770256. The boot properties confirm V670 AU04, security patch 2026-06-05, MT9653_Overseas/tcl9618/mt5896, ARM32, locked/green/enforcing, active slot _b and Virtual A/B. Neither ro.oem_unlock_supported nor sys.oem_unlock_allowed returns a value; no unlock capability is established. bootctl inaccessible or absent in shell. The actual system root and /vendor mount EROFS read-only, /product and /system_ext ext4 read-only; com.tcl.tcore@653670001 mounted from loop14. All four audio hashes match the earlier captures, including system/APEX client dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9, HAL e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad and primary XML 4fbbdd581ec2415d04aab5edfc139a5585af10cac9ed85bbd4ab523dc0518884. super is present; product_b-base, product_b-cow-img and product_b-src mappings are present. These suggest snapshot-related state; exact merge status unknown. Do not switch slots, cancel updates or flash while state is unresolved.

No stock package metadata was provided in this capture. An OTA is not automatically a full USB recovery image. Community listings identify V670 OTA and an older V669 PKG, but exact region/model/revision compatibility, bootloader acceptance and recovery procedure are unverified. See https://dvdprime.com/g2/bbs/board.php?bo_table=tvmonitor&wr_id=473805 and https://dvdprime.com/g2/bbs/board.php?bo_table=tvmonitor&wr_id=473397; listed fingerprints are US AU02 rather than the running GB AU04, so do not select by filename alone.

Next: inspect the original V670 ZIP the owner previously installed using tools/inspect_stock_ota.py. It reports ZIP hash, OTA metadata, CrAU v2 partition inventory, old/new partition info and sizes, without extracting or installing. It does not verify signatures or compatibility. Independently query adb shell update_engine_client --status and adb shell snapshotctl dump (read-only; may be unavailable). Do not issue merge/cancel or slot-changing commands. Verify a stock recovery procedure and compatible package for this physical unit before any hardware trial. See https://source.android.com/docs/core/ota/virtual_ab/implement.

### Offline HAL follow-up

__apply_iec_config_by_stream (VA 0x73570) checks stream +0x2ac == 3 and configures output-side pipeline parameters, including BitWidth and a conditional 16/24-bit value at 0x737ee–0x73830 and 0x73888–0x738b4. This does not establish compressed IEC61937 input consumption. Explicit comparisons to 0x0d000000 are present in a format-name lookup at 0x6c9c4 and accounting/underrun logic at 0xae77a and 0xae9de (which select 2); none proves direct eARC bitstream delivery. Investigate classification/write path offline; do not simply force the passthrough eligibility predicate.

## Stock OTA inventory (2026-11-19)

Owner supplied stock_ota_inventory.json (5299 bytes, SHA-256 8e7b889f85f26ed16103699eccf90fb18c1aa5a651ffd8c332873112226c012b). OTA V8-T653T01-LF1V670.zip is 2199585436 bytes, SHA-256 3d2fdf5363d86061f7b30855458e673a39cada336ec0359a12a11533362793e9. CrAU v2, minor version 0, 16 partitions, no old partition information. Manifest is consistent with a full A/B payload. ZIP signature was not verified. Crucially, post-build is TCL/G08_4K_US/G08:14/UTT2.250416.001/AU02:user/release-keys whereas running firmware is TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04:user/release-keys. Same marketing version and device G08 do not establish identical files, safe cross-region install or recovery compatibility.

First offline extraction targets system (469409792 bytes), vendor (343285760), system_ext (1016545280), vbmeta (12288), total 1829253120 bytes. The manifest also includes boot, vendor_boot, mboot, optee, armfw, dtbo, bootdata, oem, product, tclconfig, tvconfig and vendor_dlkm; no need to extract or modify these in the current audio investigation.

Run scripts/extract_stock_ota.ps1 on Windows x64, with the actual local ZIP path or enter it at the prompt. The script checks the exact package hash, reruns the manifest inspector, downloads the official payload-dumper-go 2.1.0 x64 archive and verifies its published SHA-256 before executing, extracts only those four images, and checks each output length and SHA-256 against the manifest. Report private/stock_images_verified.json. This is not verification of the OTA signature or installed-device compatibility. No device commands or modifications. Keep images local; next inspect files from EROFS/ext4 and compare four known audio hashes from preflight. If matches, the shared binary analysis applies but the entire image remains unverified. If different, disassemble actual on-device binaries only. Need a verified recovery procedure/package for this physical unit and confirmed snapshot state before any hardware trial; a separately labeled GB AU04 package is not mandatory. See owner clarification below.

Tool release: https://github.com/ssut/payload-dumper-go/releases/tag/2.1.0 ; CLI: https://github.com/ssut/payload-dumper-go/blob/main/README.md . Script not yet executed on the owner's 2.2GB archive.

## Verified offline extraction and next file-level audit

Owner report stock_images_verified.json (2323 bytes, SHA-256 08105a21761fda95d1b790e13472af2dc81d6a2b3d68ce29aad35e28f6495925) confirms all four extracted images match the stock manifest size and SHA-256. Tool version 2.1.0. Neither OTA signature verification nor installed-device compatibility is established.

Next run tools/compare_stock_audio.py under WSL/Linux as a normal user, with erofs-utils and e2fsprogs installed. It rechecks exact image hashes, identifies EROFS/ext4 by magic, extracts using fsck.erofs or read-only debugfs (no mounts), and compares the system ARM32 client, TCL tcore APEX ARM32 client, vendor HAL and primary policy against the preflight hashes. It retains an offline Linux workspace in the user's home directory; allow several GB of free WSL disk space. The original images remain unchanged. Missing files and unsupported image/APEX layouts are reported, not silently accepted. The report is private/stock_audio_compare.json. This tool has not yet run on the actual images; filesystem extraction may be version-dependent. A match of all four roles validates only those exact files, not other parts of US AU02 versus GB AU04.

Windows PowerShell from repo root (WSL distro already set up):
    wsl -l -v
    wsl -e bash -lc "sudo apt update && sudo apt install -y erofs-utils e2fsprogs python3"
    wsl -e python3 tools/compare_stock_audio.py

Reference: https://manpages.debian.org/trixie/erofs-utils/fsck.erofs.1.en.html ; debugfs is read-only unless -w is supplied. If WSL is missing, install a supported Linux distribution first. Do not run the extraction script as root. Do not repack/sign/flash yet; exact recovery, supported boot development path, and snapshot state remain unresolved.

## File-level comparison and additional eARC classifier (2026-11-19)

Owner report stock_audio_compare.json, 1563 bytes, SHA-256 bd2e5fe797846db3b426e6a97c4c7556704f212972046f47046f7d8e1d1ecd0c, establishes exact matches for stock system:/system/lib/libaudioclient.so (702872 bytes), vendor:/lib/hw/audio.primary.merak.so (874764 bytes), and vendor:/etc/primary_audio_policy_configuration.xml (31186 bytes). system/vendor EROFS, system_ext ext4. Report finds system_ext:/apex/com.tcl.tcore.apex but not a regular ARM32 library at the assumed payload path. Earlier compare tool explicitly excluded symlinks; missing is not a hash mismatch. The active APEX library previously pulled from the TV is independently verified byte-identical to the stock system library (both SHA-256 dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9). Therefore earlier disassembly applies to both live copies. The stock APEX source/layout remains unresolved. Linker config shows isolated com_tcl_tcore namespace with search path /apex/com.tcl.tcore/${LIB}; default namespace exports the TCL-specific policy/flinger libraries, not libaudioclient from that namespace. This does not prove which copy every process loads. Do not modify the APEX blindly.

Run tools/inspect_tcore_layout.py using the retained WSL workspace; no need to re-extract. It identifies regular files, symlinks and APEX archives, without following symlinks to the host. Inspect adb shell cat /apex/apex-info-list.xml to determine the active package path/version. Neither tool establishes APEX signing/deployment compatibility.

### Independent HAL blocker

Re-disassembled the exact matching HAL from earlier TV extraction. utils_earc_set_decoder_format (Thumb VA 0x71b88, 576 bytes) reads stream +0xc0, masks main format 0xff000000. For MAT 0x24000000, branch 0x71c06 -> 0x71c5a assigns 6; for TrueHD 0x0e000000, branch 0x71c28 -> 0x71c5e assigns 7. IEC61937 0x0d000000 takes 0x71c40 -> 0x71c6a (unsupported default), logs "%s: unsupport format (0x%08x)" (string VA 0x36dcb, literal resolution 0x71c76), then assigns 0 at 0x71c7e. 0x71d18 calls a PLT entry with the strings EarcOutput (VA 0x2d2f8), Param_DecodeFormat (0x3e0b7), value and length 4. A separate format classification gate exists in addition to utils_isPassthroughSupported. This is not proof zero always causes PCM output or that mapping IEC to MAT would work. IEC payload can carry different codecs; a proper design must classify the actual burst payload or implement appropriate transparent carriage, validate receiver support, and preserve non-PCM signaling. Do not globally alias IEC to TrueHD or MAT. Continue tracing input write and digital output contract before generating executable patch. No flashable firmware exists.

## APEX resolved and offline client prototype

See [IEC client experiment](iec-client-experiment.md). Stock APEX client is a symlink to /system/lib/libaudioclient.so; active APEX is factory 653670001. Client-only prototype generated and structurally verified offline on matching original ELF, 11 bytes changed across four sites; no runtime validation, HAL or policy patch. Original APEX remains unchanged. Not flashable.

## MAT carrier and privacy tracks

[Offline carrier reference](iec-mat-carrier-reference.md) bounds and validates the MAT burst before forwarding intact bytes; debug parser is not proof of a live playback path. [Privacy/debloat plan](privacy-audit.md) remains independent of the audio module. No security protections removed or firmware flashed.

## eARC endpoint and policy follow-up

[Endpoint audit and policy prototype](earc-endpoint-and-policy.md) identifies the named ALSA non-PCM device lookup, confirms EarcOutput ProcessData is a no-op, and implements a narrow offline eARC-only mixPort candidate. Validated transport, runtime profile advertisement and deployment still missing.


## Owner clarification — V670 stock baseline

The owner confirms that the very V670 US AU02 OTA under investigation was used to update this physical TV successfully, which subsequently reports GB AU04. This establishes owner-reported normal update compatibility. The matching system client, vendor HAL, primary policy and active APEX client hashes provide independent file-level corroboration. The reason for the fingerprint difference is not established; do not infer a failed update or require a separately labeled GB OTA solely from those strings. Retain this original unmodified ZIP as the known-good stock baseline. Normal update success does not demonstrate that the same ZIP can recover a non-booting TV, that a same-version reinstall is accepted, or that a modified image can boot. Recovery procedure and supported development boot path, snapshot status and functional audio transport remain independent gates. No cross-region flash/downgrade or new device write proposed.
