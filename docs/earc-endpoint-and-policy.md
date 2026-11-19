# Non-PCM endpoint and narrow AudioPolicy prototype — V670

Privacy work is separate; no package changes in this commit. Reopened local original ELF files and verified audio.primary.merak.so SHA-256 e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad and libmi3_audio_hal.so SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a.

## Live write boundary

utils_out_write_data, Thumb VA 0x89040 in primary HAL, passes a stack data packet to MI_APAF_DataManager_WriteData. At 0x893e8 it places the buffer and remaining byte count at offsets +8/+12 relative to the packet at sp+0x38; 0x893ec copies two words from stream +0x1f0, 0x893f4 calls helper 0x8a2b0 that updates other packet fields. 0x893f8–0x893fc passes a pipeline handle and the packet to PLT 0xcf850. Confirmed by .rel.plt entry 469, R_ARM_JUMP_SLOT at 0xd37d0, MI_APAF_DataManager_WriteData. Return value is subtracted from remaining bytes and added to the accounted bytes at 0x89402–0x8940a; this matters for partial writes. The conditional parser branch also calls this API at 0x89756 after Parser_Read. Do not assume all streams use that parser.

In libmi3_audio_hal.so, MI_APAF_DataManager_WriteData (0xa3a18) resolves a pipeline; at 0xa3c18 loads callback from object +0x2a8, at 0xa3c20 calls it if non-null. This is a pipeline-dependent write, not a direct eARC sink. Earlier work traced decoder writes to compressed-device boundary. Directly calling the pipeline manager with an arbitrary handle would not prove a valid encoded output route.

## EARC output is a control component, not a proven byte sink

- _MI_APAF_FILTER_EARC_OUTPUT_ProcessData at 0x10f970–0x10f972 is exactly movs r0,#0; bx lr. No data write in this body.
- _MI_APAF_FILTER_EARC_OUTPUT_SetParam_DecoderFormat at 0x111084 stores value at state +0x140 (0x11108c), optionally logs and returns zero. This is not a demonstrated transmission switch.
- ELF objects mi_apaf_earc_pcm_playback_dev_name (VA 0x1d57dc, 14 bytes, file offset 0x1d27dc) = EARC Playback; mi_apaf_earc_nonpcm_playback_dev_name (VA 0x1d57ea, 21 bytes, file offset 0x1d27ea) = EARC NONPCM Playback. Associated non-PCM index at VA 0x2a9424. MI_APAF_ALSA_UTILS_InitEarcNonPcmDevInfo at 0x147b18 calls _MI_APAF_ALSA_UTILS_GetDevInfo at 0x147b44. Names and lookup are concrete evidence of designed endpoints, not proof these exist or accept MAT on this TV.
- Earlier stage2 ALSA listing includes pcmC0D0p, D1p, D2p and restricted D9p; names not mapped. /proc/asound/cards and /proc/asound/pcm stdout was empty in that capture. No exact eARC non-PCM device index established. New scripts/collect_earc_endpoint.ps1 queries targeted sysfs playback/card metadata and module filenames, capturing stderr; may fail under stock shell. No opening /dev/snd nodes or writing mixer controls. Do not guess endpoint number. No matching literal EARC NONPCM Playback or InitEarcNonPcm in searched HDMI, JOC, Minirama and MAT live logs.

## Offline policy prototype

AOSP describes direct mix ports and dynamically negotiated digital sink profiles (https://source.android.com/docs/core/audio/implement-policy). This is a local design only: dedicated DIRECT + HW_AV_SYNC candidate at IEC61937/192000/7.1, attached solely to HDMI EARC, maxOpenCount and maxActiveCount 1. No devicePort capability added, no change to other routes, no generic IEC or PCM fallback; this does NOT advertise a working profile at runtime. Choose HW_AV_SYNC because previous verified A/V tunnel had that route, not because Moonfin necessarily uses it. Ordinary non-tunnel direct profile remains future work. Must verify actual transport and dynamic policy behavior before deployment.

tools/plan_iec_policy.py exact-stock SHA-256 gate (4fbbdd581ec2415d04aab5edfc139a5585af10cac9ed85bbd4ab523dc0518884), separate output, semantic comparison removing just the one candidate port and its route reference. Host tests passed on the earlier supplied 31189-byte representative policy (SHA-256 f7150e32f11bf05d612e22086f1785ad8b074197f75ae5c34274a87a14d513bf): deterministic, eARC-only link, unchanged remaining XML structure, reject duplicate/missing route. CLI correctly rejected that fixture as non-exact stock. Exact 31186-byte stock XML remains in owner's WSL workspace and has NOT been tested here. No runtime schema/HAL validation. Test on exact local policy with tools/test_iec_policy.py; run generator only offline. No AVB/APEX signing or flash changes.

Next decision: if sysfs exposes a named non-PCM endpoint, establish driver format/ownership and timing contract from matching stock kernel/module source. If unavailable, inspect stock vendor_dlkm offline; do not write to an unknown output. A policy and client experiment without a validated HAL transport still is not flashable. No changes to TV, no Atmos result claimed.

## Runtime endpoint inventory and exact stock policy generation (2026-11-19)

Private archive earc_endpoint.zip (5700 bytes, SHA-256 b6e03819e34e68f1b41bbf325ae50cc0bcecb60b307782aee73ef239902f9823). Fingerprint matches installed GB AU04. sound_sysfs.txt and playback_metadata.txt establish card0 path /sys/devices/platform/mtk-tvsnd-machine/sound/card0, with playback PCM nodes D0, D1, D2, D9 and compressed nodes D12–D16. Card1 is snd_aloop; cards2 and 3 expose capture nodes. Reads of pcm_class, uevent, dev and card id failed with Permission denied. /proc/asound/cards and /proc/asound/pcm also denied. A zero exit code for the playback loop does not mean the individual cat commands succeeded. No concrete EARC non-PCM node mapped. No playback or mixer writes.

audio_modules.txt lists /vendor/lib/modules symlink to /vendor_dlkm/lib/modules and the following candidate kernel modules (on-disk presence only; loading status not established):

| Name | Bytes |
|---|---:|
| mtk-earc.ko | 210200 |
| mtk-tvsnd-machine.ko | 350400 |
| mtk-tvsnd-platform.ko | 662992 |
| mtk-tvsnd-soc.ko | 3071872 |
| mtk-tvsnd-dsp-loader.ko | 12648 |
| mtk-tvsnd-soc-JP.ko | 3127088 |

The previously acquired LAPAF library has exported MI_APAF_ALSA_UTILS_InitEarcNonPcmDevInfo at 0x147b18. Its call at 0x147b44 goes to _MI_APAF_ALSA_UTILS_GetDevInfo at 0x14669c. It loads the name, result string and index pointers before the call; the presence of this lookup does not establish any fixed device number. The new collection script scripts/collect_earc_drivers.ps1 pulls only these six modules, records hashes, kernel release and optional loaded-module list. May be denied by SELinux; no bypass is attempted. Archive earc_drivers.zip is private, not a repository artifact. If pull is denied, use the verified OTA vendor_dlkm partition offline and separately confirm against device evidence. No module installation or changes.

Owner's policy_report.json (546 bytes, SHA-256 0da9b2fc568240bed51d468880fe2f14f4b56123dce7196fa10a74866cc269af) confirms exact-stock generator passed with original hash 4fbbdd581ec2415d04aab5edfc139a5585af10cac9ed85bbd4ab523dc0518884, candidate hash 053aafe663b5218b65ce10022cf873b8e69c75b4b3766775e8afb072db9c2655, IEC61937 192000/8, DIRECT/HW_AV_SYNC, HDMI EARC only, device port unchanged. As the report states, runtime_validated=false and flashable=false. This closes exact stock structural generation, not Android policy parse or route negotiation at runtime.

Next: inspect the module tables/string references and DAI link/PCM/ASoC driver definitions for actual non-PCM endpoint mapping, rate/channel/format constraints and write contract. If an MM-fed non-PCM endpoint is missing, investigate existing DSP transport rather than guessing ALSA IDs. Do not deploy until transport, recovery and supported development boot configuration are established.

## Driver capture follow-up

earc_drivers.zip, 6891 bytes, SHA-256 e2b947104585baba471fcca381fd485ee0545d1c5c2d019d521f26d3ad74e635. Kernel reports 5.15.192-android14-11, fingerprint GB AU04. All six module pulls exit 1 with explicit Permission denied during remote stat; modules directory is empty. /proc/modules succeeded and shows live mtk_earc, mtk_tvsnd_machine, mtk_tvsnd_platform, mtk_tvsnd_soc, mtk_tvsnd_dsp_loader and audio_public_if. The JP alternative is not listed as loaded. The module allocations in /proc/modules are not on-disk file lengths or a byte identity check. No further restricted ADB pulls needed.

Fallback: scripts/extract_stock_ota.ps1 -VendorDlkmOnly prompts for the same V670 OTA path, verifies its existing pinned hash, extracts only vendor_dlkm.img under private/stock_v670_us_au02/drivers_images, and verifies it against the OTA manifest (89944064 bytes, SHA-256 02cdb6ac2e318eb1f96b8679d1272b9a2d3e3c984056b5bb8438c5d5d9a0c6a1). Original four images and their report are preserved. tools/collect_stock_earc_drivers.py uses the existing read-only extraction helpers, selects the five loaded MediaTek drivers plus audio-public-if.ko, hashes each, checks ELF identification, records vermagic if present and compares sizes with earlier TV listing (audio-public-if.ko 12952 bytes). Output private/earc_drivers_stock.zip; never commit it. Same module sizes or vermagic do not prove byte identity with the live GB AU04 build. No root, flash or module load. No kernel module patch proposed. Scripts not tested on the actual vendor_dlkm image.

Next inspect the resulting stock binaries for ASoC link and PCM names, endpoint constraints and callback semantics. The previous HAL/policy prototypes remain offline only.

## Stock driver findings

See [kernel driver audit](kernel-audio-driver-audit.md). D9 is tvsnd-common-pcm with six playback substreams; PCM write path has a conditional format converter. No dedicated eARC non-PCM endpoint established. BTF confirms named TrueHD bypass parameter and unsupported setter slot, plus the real DigitalTx MAT HDMI bypass shared-memory path. No device changes.
