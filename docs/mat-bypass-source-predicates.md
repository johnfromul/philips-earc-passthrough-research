# MAT source predicates and DSP shared-memory boundary

## Scope and verified input

Read-only static analysis of the owner-supplied stock mtk-tvsnd-soc.ko (US AU02), SHA-256 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4, independently recalculated again. **Update 2026-11-20:** the live GB AU04 module pulled from the device has the identical SHA-256 — byte-identical, so these findings apply verbatim to the running firmware. See [dsp-dolby-param-map](dsp-dolby-param-map.md). Addresses below are section-relative offsets of the AArch64 ET_REL object. No code executed on the TV.

## Evidence: MAT bypass and Auto are distinct

Local split BTF enum records resolve AU_DVB_STANDARD_MAT=0x1b, E_AUDIO_INFO_HDMI_IN=2, E_AUDIO_INFO_MM_IN=4, Audio_infoType_Is_Atmos_Stream=0x56 and Audio_infoType_Mat_Stream_Type=0xb1. The 0x40d0-byte shared state struct has eAudioSource at +0x1cc, g_bNextEnableMatBypass +0x676, g_bEnableMatBypass +0x67e, g_bNextEnableMatEncode +0x67b, g_bSupportMs12MatEncoder +0x684, g_bIsEARCDevice +0x3f5e. The BTF string base is 0x206234; no base vmlinux type resolution attempted.

HAL_AUDIO_SPDIF_BypassMode at .text+0x7a004 first calls HAL_AUDIO_Get_CodeTypeByDecodeID (0x7a088), HAL_MAD_GetAudioInfo2 with 0x56 (0x7a0a0) and 0xb1 (0x7a0b0). It reads eAudioSource at 0x7a14c. MM_IN=4 reaches the codec jump table directly; HDMI_IN=2 takes HDMI_NonpcmMonitor first (0x7a1d0), then can reach the same codec dispatch. MAT slot at .rodata+0x1710+4*(0x1b-2), relative to .text+0x7a194, resolves to 0x7a46c.

Two MAT branches can set g_bNextEnableMatBypass. Both require w25 (state byte +0x3e1) ==1 and g_bIsEARCDevice==1. The first requires bit0 of w27 (byte +0x3e4) ==1, then at 0x7a490–0x7a49c tests eAudioSource==2; 0x7a4a0–0x7a4a4 sets +0x676. The second requires bit0==0, then source==2 at 0x7a5ac–0x7a5b8 and the queried MAT stream type at [x29-0x20] equal to 1 at 0x7a5bc–0x7a5c4, then stores at 0x7a5c8. Non-HDMI branches do not perform those stores, instead clearing the HDMI ARC output mode at 0x7a6a4. The exact nested capability semantics of bytes +0x3e1/+0x3e4 are not asserted. No claim that all hardware modes or every path in the full driver are covered.

HAL_AUDIO_SPDIF_AutoMode (0x79974): MAT slot at .rodata+0x1584+4*(0x1b-2), relative to 0x79a84, resolves to 0x79a94. Branch 0x79a94–0x79ab8 checks those same capability bytes/bit and eARC plus g_bSupportMs12MatEncoder. It writes +0x67b (g_bNextEnableMatEncode) at 0x79ac4, not the bypass flag. This aligns with the earlier observed Auto encoder route; it does not establish where Atmos metadata was lost. The existing log evidence showed internal TrueHD isAtmos=0 while external HDMI and internal JOC had isAtmos=1.

## Actual DSP parameter store

HAL_AUDIO_DigitalTx_ApplySetting calls HAL_DEC_R2_Set_SHM_PARAM(0xb4, index, value, 0) for indices 0 and 1 at 0x7b544/0x7b55c, value loaded from +0x676. HAL_DEC_R2_Set_SHM_PARAM (0x90c60) validates low-byte index <=1 and dispatches via unsigned 16-bit table .rodata+0x21a0, base .text+0x90d24, multiplier 4. Slot 0xb4 resolves to 0x91744. Instructions there multiply index by 0x430 and store the value at shared-memory base + index*0x430 + 0xe0c (0x9174c). This matches local BTF structures: adec_param_shm at +0xd90, dolby_param at +0x60, MAT_HDMI_bypass at +0x1c. It is a real parameter write, not a sample-transfer routine. No DSP firmware execution or acknowledgement was observed. There is no demonstrated app-fed MAT data path in this setter.

## Reproducibility and implementation decision

tools/inspect_mat_bypass_tables.py is a read-only exact-hash-gated diagnostic: resolves three jump tables and checks selected local BTF enum values and field offsets. It does not alter modules or make device requests. The underlying table entries/BTF records were independently checked locally using Python struct and llvm-objdump; the standalone combined script still needs a separate execution check.

Do not patch the source test to impersonate HDMI_IN, force the DSP bit, force an Atmos status, or send compressed data through ordinary PCM. A genuine app-fed non-PCM path must first be demonstrated. The MAT encoder branch is distinct from MAT HDMI bypass; toggling the latter cannot create a missing input stream. An alternative to a generic IEC61937 API would be correcting actual raw TrueHD/MAT metadata handling on MM_IN, but no change can be justified until the input framing and DSP consumption are known. Existing raw and prepacked MAT experiments already failed to preserve Atmos, so repeating them unchanged is not useful.

Next engineering task: trace the existing MAT MM decoder configuration and data path, including MatMlpOnly and DSP compressed input format, against HDMI source initialization. If the proprietary DSP accepts a documented MM non-PCM input, design an integration at the HAL boundary with controlled lifetime, backpressure, HW A/V sync, negotiated eARC capability and fail-closed route changes. If no such path is established, obtain OEM implementation guidance instead of a speculative kernel bypass. Bootloader unlock/recovery and exact GB firmware compatibility remain independent blockers. No flashable image or newly functional HAL patch has been created. No new owner-side collection needed at this step.
