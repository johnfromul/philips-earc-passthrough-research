# HDMI versus MM decoder readback — V670

## Evidence and correction

Reopened the original hdmi_working.zip (UTF-16LE with BOM), reports_joc.zip and reports_minirama_verified.zip (UTF-8), plus the stock module and firmware library archives. Recomputed soc 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4 and libmi3_audio_hal 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a. Stock module installed-byte identity still unverified.

| Capture | _tv_adec_in_config time | Source | Codec | Group | STC source | Force ID | Event time | Event decoder | Stream type | Atmos |
|---|---|---:|---:|---:|---:|---:|---|---:|---|---:|
| HDMI | 04:02:04.998 | 2 | 28 | 0 | 0 | 0 | 04:02:05.089 | 0 | 1 MAT_TRUEHD | 1 |
| MM JOC | 04:41:06.269 | 4 | 2 | 0 | 1 | 2 | 04:41:06.439 | 2 | 1 DDP | 1 |
| MM Minirama | 05:02:24.785 | 4 | 28 | 0 | 1 | 2 | 05:02:24.978 | 2 | 1 MAT_TRUEHD | 0 |

HDMI original log lines 36648 and 36816 (1-based splitlines). Earlier HDMI sessions with format 30 at 04:00:31.252 etc are distinct; previous decoder-config-contract.md incorrectly reported no HDMI config lines. For the actual MAT session at 04:02:04.997 (lines 36659–36663), output selection remains eARC=MAT and Enc=MAT, Auto; source HDMI_IN, main decoder 0. This corroborates common output selection in the JOC and Minirama controls, without claiming exact identical stream bytes or bitstream-on-wire identity. At the format-convert instant HDMI MAT_ATMOS input detail is still 0; the subsequent decoder event is 1. JOC and MM show stale-looking detail across transitions too. Do not use format-convert detail as a synchronized per-frame measurement.

## Same readback API for both decoder slots

ARM32 libmi3_audio_hal.so Thumb VA:
- _dspCommFunc_LAPAF_IsAtmosStream at 0x17253c reads decoder ID from decoder state +0x590 (0x172544), treats 0xff as invalid; calls _MI_LAPAF_UTILS_GetAudioInfo2 with selector 0x56, sign-extended decoder ID, caller result pointer and size 4 at 0x172556.
- _dspCommFunc_LAPAF_GetMatType at 0x1713b0 uses same decoder-ID field and selector 0xb1, call at 0x1713d8. Both calls use PLT 0x18d6f0, resolved with .rel.plt R_ARM_JUMP_SLOT.
- _MI_LAPAF_UTILS_GetAudioInfo2 (0x14ae30) builds the request with decoder ID, selector, result pointer and length, passes it to MI_APAF_ALSA_UTILS_LAPAF_GetKcontrolParams via PLT 0x18db10 at 0x14ae86. This is a control request, not encoded audio transfer.
- That helper at 0x148108 packages the request and calls the ALSA control ioctl wrapper at 0x148192. This is static reconstruction of the stock path, not a new diagnostic interface for shell use.
- Stock mtk-tvsnd-platform.ko audio_card_GetAudioInfo2 (.text+0x88b8) copies request, validates decoder ID <5 (0x8a1c), looks up the vendor selector in .rodata+0x8af8 indexed by selector+1 (0x8a34–0x8a4c). The entries for 0x56 and 0xb1 both map to themselves, checked directly against ELF .rodata. Calls MApi_AUDIO_offload_GetAudioInfo2 at 0x8afc with decoder ID, translated selector and result buffer. This extends the previously documented HAL_MAD_GetAudioInfo2 boundary (which may also qualify DSP-reported status). It does not imply the raw DSP status is necessarily identical to the user-visible event. No authorization, policy or status changes considered.

## DigitalTx is downstream and has two distinct operations

Rechecked HAL_AUDIO_DigitalTx_ApplySetting in stock soc: paired decoder shared-memory control writes include selectors 0xe, 0xd, 0xc0, 0x34, 0xb4 and 0xd9. 0xb4 is MAT HDMI bypass as established in mat-bypass-source-predicates.md. Separately 0x7b5e4–0x7b608 reads g_bSupportMs12MatEncoder and g_bNextEnableMatEncode and invokes masked register configuration. Not a buffer transfer. The shared-memory slot indices 0/1 in this function must not be assumed to be LAPAF DEC0/DEC2 IDs; they belong to a separate indexing domain until mapped. Nothing here establishes that altering an output control restores metadata.

## Engineering decision

Prioritize the MM MAT decoder's input/framing/initialization contract and readback, not a new digital output format or alternate DMA driver. DEC2 can preserve JOC Atmos; it is therefore not a universally Atmos-incapable decoder. DEC0/DEC2, source and STC differences are correlated in the existing captures, not isolated causal variables. Do not change decoder ID, STC source, HDMI source, capability or status flags as a substitute for a real transport. The next useful static work is tracing the source-specific setup and sample-framing contract against the stock software MAT/TrueHD implementation and determining whether an integration can preserve real object metadata; the direct DSP firmware consumer remains unverified. Exact same-sample HDMI control remains deferred. No modifications, new playback or flashable patch, no new owner collection requested.
