# Decoder configuration contract and DSP firmware provenance

## Verified input and scope

Same original ARM32 libmi3_audio_hal.so (SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a), stock AArch64 machine/soc/loader modules (hashes in kernel-audio-driver-audit.md). All four hashes recalculated. Reopened the original private JOC, v0.2.2, Minirama and HDMI log ZIPs. No new playback or TV modification.

## Actual codec mapping

- _MI_APAF_FILTER_MS12V2_MAT_Init at 0x1864c4 passes 4; _MI_APAF_FILTER_MS12V2_DDPDEC_Init at 0x153608 passes 1. Both tail-call decCommFunc_Lapaf_Init, which stores that argument at decoder state +0x4cc (0x16374e).
- _dspCommFunc_LAPAF_ConvertApaf2OffloadCodecType at 0x170c08 reads +0x4cc, validates index (format minus 1, <21 and bitmask 0x13ffff), looks up a 32-bit value at 0x92714 (PC-relative literal 0x170c94). First four entries: 2,25,4,28. Therefore DDPDEC->2, MAT->28.
- Local split BTF in soc module names MSAPI_AUD_DVB_AC3=2, MSAPI_AUD_DVB_AC3P=3, MSAPI_AUD_DVB_MAT=28 (type-area member offset 0x718). Distinct lower-level AU_DVB_STANDARD_MAT=27; do not interchange these enums.
- _dspCommFunc_LAPAF_OpenDecoderDevice dispatches state first word at 0x163ea0. Values 0 and 3 use the DTV helper; value 1 takes the branch that calls conversion at 0x16403c and sets configuration +0xc (relative to sp+0x58) to 2 at 0x1642d2, with two candidate decoder-open attempts. Other values take the branch converting at 0x164148 and setting +0xc to 4 at 0x1644be/0x1644fe, again two candidate attempts. The first word is a LAPAF internal source category, not the kernel audio source enum. Do not equate these internal values or infer exact HDMI path from them alone.

## Userspace to kernel config contract

mi_apaf_compress_open (0x1501a0), conditional branch at 0x1502f6:
- Standard path stores codec 1 into object +0x20.
- Vendor path at 0x1503ac stores 14 at object +0x20, constructs a 36-byte metadata structure starting with key 1 at sp+0xa0. The source/format/group/stc/force-ID words are copied from the input configuration at offsets +0xc,+0x10,+4,+0x14,+0x20 into metadata offsets +4,+8,+12,+16,+20. At 0x1503d4, passes that structure to STI_IOCTL with 0x40244314 (SET_METADATA). Later 0x15043a issues 0x40844312 (SET_PARAMS).
- In machine module, compr_hw_in_ops metadata slot (.data+0x800) uses thunk .text+0xd334 to .text+0x6d98. Key 1 stores those five words into decoder state +0xd4,+0xd8,+0xcc,+0xdc,+0xe8 respectively. Log literal at .rodata+0x4b74 explicitly names source, format, group, stc source, force_id; function name string .rodata+0x2bd7 is _tv_adec_in_config.
- This explains why codec 14 in the standard compressed-params callback leaves the decoder codec intact: the vendor metadata already configured it. This is **decoder configuration metadata**, not Dolby Atmos object metadata. The actual encoded data still goes through the AES buffer in mm-dsp-input-boundary.md.

## Runtime confirmation from existing captures

| Capture | source | format | group | stc source | force_id |
|---|---:|---:|---:|---:|---:|
| JOC (04:41:06) | 4 | 2 | 0 | 1 | 2 |
| v0.2.2 (04:28:28) | 4 | 28 | 0 | 1 | 2 |
| v0.2.2 (04:30:10) | 4 | 28 | 0 | 1 | 2 |
| v0.2.2 (04:32:12) | 4 | 28 | 0 | 1 | 2 |
| v0.2.2 (04:33:15) | 4 | 28 | 0 | 1 | 2 |
| Minirama (05:02:24) | 4 | 28 | 0 | 1 | 2 |

All extracted from _tv_adec_in_config log lines in the private ZIPs; no such line found in hdmi_working.zip. The earlier HDMI positive control independently establishes HDMI_IN and MAT, not this exact metadata struct. Existing JOC Atmos positive control and MM MAT failures remain unchanged. MAT and grouped TrueHD use the same requested decoder codec; there may still be different downstream configuration and payload handling.

## DSP firmware boundary

mtk-tvsnd-dsp-loader.ko SHA-256 575b9af8349a7305a76c032ffab20f891c8d12427c695cb1de64c819a6f8e086: workqueue handler at .text+0xc8 queries device tree/reserved memory, then calls MApi_AUDIO_offload_WritePreInitTable (0x178), SetDspBaseAddr (0x188) and Initialize (0x18c). No request_firmware import in this small loader.

The soc driver has two request_firmware_direct calls at .text+0x422c and 0x43a4. Resolved nearby literals identify dconfig and sconfig filenames (from caller data, snprintf %.*s), followed by _MApi_AUDIO_COPY_Parameter with FLEXT_SET_DCONFIG or FLEXT_SET_SCONFIG. These calls are configuration files, not proof of a standalone DSP executable. HAL_AUDSP_DspLoadCode also contains code-segment loading and verification and a conditional MDrv_AUDIO_TEE_R_Send_Cmd call at 0xa8068 (command 0x20). No inference that the protected DSP code is accessible as plaintext on the host; no attempt to alter TEE, signature or access controls.

## Next actionable owner step

Inventory the already extracted stock vendor tree before extracting further partitions. From Windows PowerShell in the repo root:
    
    wsl -e bash -lc 'find "$HOME" -maxdepth 1 -type d -name "tcl_stock_*" -print'
    wsl -e bash -lc 'for d in "$HOME"/tcl_stock_*/vendor; do [ -d "$d" ] || continue; echo "=== $d ==="; find "$d" -type f \( -ipath "*/firmware/*" -o -iname "*dsp*" -o -iname "*dconfig*" -o -iname "*sconfig*" -o -iname "*audio*.bin" -o -iname "*dolby*" \) -printf "%P\t%s\n"; done' | Set-Content -Encoding utf8 .\private\dsp_inventory.txt

This only lists names and sizes; no mount, ADB, firmware upload, or modification. Review the listing before sharing. If no candidate file is found, trace stock config/device-tree references and HAL_AUDSP_DspLoadCode selection further; do not assume an accessible standalone decoder binary. If a candidate is found, establish its partition/role and digest before requesting any private copy. Continue checking MM-specific decoder parameter and DSP-reported Atmos readback against the archived controls. No patch or flashable firmware yet.
