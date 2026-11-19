# Stock audio kernel module audit — V670 US AU02

## Provenance

Owner-supplied private/earc_drivers_stock.zip: 1022043 bytes, SHA-256 8a7c72a3ccbd8588b7eb67774635d0b96152c34fcc190b7c07808a576edfe055. Manifest identifies verified vendor_dlkm image, 89944064 bytes, SHA-256 02cdb6ac2e318eb1f96b8679d1272b9a2d3e3c984056b5bb8438c5d5d9a0c6a1. Independently recalculated SHA-256 of each of the six ZIP members; all match the manifest. All sizes match the earlier live file listing. Modules use AArch64; installed Android native audio userspace is ARM32. Live module byte identity remains unverified. No modules published, installed or executed.

| Module | Bytes | SHA-256 |
|---|---:|---|
| audio-public-if.ko | 12952 | 72a3a6d5434f315c7e394576c7876240155203a25fad839976b8598588aa0772 |
| mtk-earc.ko | 210200 | 37b94c83a7e706c3cadf193216d31d39c5633beeacfa26a287427073618a95e7 |
| mtk-tvsnd-dsp-loader.ko | 12648 | 575b9af8349a7305a76c032ffab20f891c8d12427c695cb1de64c819a6f8e086 |
| mtk-tvsnd-machine.ko | 350400 | 35fc9eaaed6c273ab620b8550d36627f5fe76151b354864c9e068d4c105bfcf6 |
| mtk-tvsnd-platform.ko | 662992 | b879d5971caa139761783a199977a8ca4a31800ba2c8e9bcdf176d281fcd1d62 |
| mtk-tvsnd-soc.ko | 3071872 | 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4 |

Vermagic: mtk-earc uses 5.15.192-android14-11; five others use 5.15.192-android14-11-g98e46779394f-dirty. All have SMP preempt mod_unload modversions aarch64. These are not a guarantee of load compatibility.

## ALSA common PCM is not a dedicated non-PCM eARC endpoint

Addresses below are section-relative offsets of ET_REL objects, not runtime addresses. Used llvm-objdump -dr and resolved relocations and literal data from the original objects.

mtk-tvsnd-machine .text+0xd264 loads device number 9, 0xd26c R_AARCH64_CALL26 common_pcm_register_device. That function at 0x1764 calls snd_pcm_new at 0x17cc: name from .rodata+0x1e94 = tvsnd-common-pcm, device argument preserved, six playback and four capture substreams. Both directions receive PCM ops table at .rodata+0x59f8. Subsequent registration calls create capture-only autochpater devices at 10 and 11, and compressed devices starting at 12. .data relocations at 0x1530, 0x1608, 0x16e0 name the first three static DAI link entries MM, MMAP, MMAP_CPU_AUDIO. Other entries include capture, OFFLOAD and amplifier links. No literal EARC NONPCM Playback / EARC Playback occurs in any of the six module files. This search and the partial registration analysis do not exclude dynamically named or externally configured endpoints.

Common PCM open at 0x3450/0x34e8/0x3514/0x3540 calls MApi_AUDIO_offload_PCM_Open. The fallback name literals at .rodata+0x267e/0x5336/0x91b/0x2403 resolve to HW DMA Reader1, HW DMA Reader2, SW DMA Reader1 and SW DMA Reader2. A write call at 0x517c passes handle, buffer, byte count to MApi_AUDIO_offload_PCM_Write; the platform module also calls it at .text+0x16204.

mtk-tvsnd-soc MApi_AUDIO_offload_PCM_Write (.text+0x2d70) calls MDrv_AUDIO_PCM_Write at 0x2db8 under an optional mutex. MDrv_AUDIO_PCM_Write (.text+0x28e38) includes conditional conversion call MDrv_AUDIO_PCM_converter at 0x29324; callback at 0x29394 takes buffer and length, updates accounting afterward. Do not route IEC bursts through this ordinary PCM path merely because a 192 kHz PCM interface might open. A conditional converter and speaker mixing options make bit-preserving non-PCM delivery unproven. New direct ALSA transport not established.

## BTF and TrueHD bypass control

The stock modules contain split BTF. For this exact soc module, .BTF starts at file offset 0x121d88 and has 421618 bytes. Its local string segment begins mtk_soc_priv, riu_base, bank_num, reg_bank. The first named struct record references string offset 0x206234, followed by member name references 0x206241, 0x20624a, 0x206253; these validate the local string base. No base vmlinux BTF was supplied, so only local named entries are interpreted; shared/base types are not resolved.

Enum record member values recovered from the local BTF (also seen in platform/machine for the first three):
- Audio_ParamType_trueHD_bypass_enable = 0x18
- Audio_infoType_get_trueHD_bypass_enable = 0x22
- Audio_Comm_infoType_Get_Dolby_TrueHD_Bypass_Capability = 0x51
- R2_SHM_PARAM_DOLBY_MAT_HDMI_BYPASS = 0xb4
- DIGITAL_OUTPUT_MAT_TRUEHD = 6
- DIGITAL_OUTPUT_MAT_TRUEHD_ATMOS = 7

The ordinary control chain in soc module: MApi_AUDIO_offload_SetAudioParam2 at 0x2738 calls _MApi_AUDIO_SetAudioParam2 at 0x2744; the latter calls MDrv_AUDIO_SetAudioParam2 at 0xd064. HAL_MAD_Init at 0x92bfc–0x92c44 installs .text+0xa9560 (CFI jump table stub with JUMP26 HAL_MAD_SetAudioParam2) as g_FuncPrt_Hal_SetAudioParam2. MDrv_AUDIO_SetAudioParam2 checks the pointer and forwards arguments. HAL_MAD_SetAudioParam2 at 0x92c74 uses w23 as param index. At 0x92e20–0x92e44 it checks index <= 0xf7 and loads an int32 relative jump table entry in .rodata+0x29a4, based at .text+0x92e38. Slot 0x18 is .rodata+0x2a04, destination .text+0x92ec0. That branch sets w21=0 and optionally logs "[MAD]%s: infoType[%x] not Supported" from .rodata+0x209e1 before returning. Therefore this dispatcher does not implement the named TrueHD bypass setter. This is an exact-stock module static finding, not a live call or proof that no other implementation exists. No runtime setters invoked.

## Actual existing MAT bypass path

HAL_AUDIO_SPDIF_BypassMode (0x7a004) conditionally sets g_AudioVars2 field +0x676 at 0x7a4a4 and 0x7a5c8. HAL_AUDIO_DigitalTx_ApplySetting calls HAL_DEC_R2_Set_SHM_PARAM with parameter 0xb4, value from that field, at 0x7b544 and 0x7b55c (second argument 0 and 1 respectively). Verified BTF identifies the parameter as R2_SHM_PARAM_DOLBY_MAT_HDMI_BYPASS. HAL_AUDIO_SPDIF_ApplySetting compares the corresponding current and next fields at 0x7c0e8–0x7c0f4. HAL_AUDIO_Digital_GetBypassCodec checks field +0x676 at 0x7ff5c–0x7ff74 and takes a 6/7 result path. BTF confirms the two output values are MAT_TRUEHD and MAT_TRUEHD_ATMOS. These are driver control and reporting mechanisms, not evidence of metadata preservation or valid playback from MM_IN. Do not force the field or result code; doing so could falsely indicate Atmos or misroute data.

Next: resolve source/codec/receiver predicates for the existing bypass mode, compare with working HDMI and multimedia paths, then trace actual decoder/DSP payload transfer. Existing binary set is sufficient for this static step, no more ADB collection requested. Do not add a new kernel interface or patch a PCM path or override licensing/security checks based on this audit. Client/AudioPolicy/MAT framing prototypes remain offline; no live test or flashable image.
