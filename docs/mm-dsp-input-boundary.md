# Multimedia compressed input to DSP: end-to-end host-side boundary

## Scope

Read-only static analysis of the previously supplied private firmware_libs.zip and earc_drivers_stock.zip. Reverified:
- libmi3_audio_hal.so SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a
- mtk-tvsnd-machine.ko SHA-256 35fc9eaaed6c273ab620b8550d36627f5fe76151b354864c9e068d4c105bfcf6
- mtk-tvsnd-soc.ko SHA-256 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4

Userspace is ARM32/Thumb (ELF virtual addresses below); kernel objects are AArch64 ET_REL (section-relative offsets). Live kernel byte identity is not established. No device commands or changes.

## Userspace write contract

In libmi3_audio_hal.so, mi_apaf_compress_open (0x1501a0) formats /dev/snd/comprC%uD%u (string VA 0x75cc9; arguments at 0x1501f6–0x1501fc), calls MI_APAF_ALSA_Sti_Open at 0x15024a and saves the fd at object +0x10. The device number is a parameter, not a fixed eARC destination.

mi_apaf_compress_write (0x150c4c) calls STI_IOCTL at 0x150d2c with 0x801c4321 (compressed availability query), then mi_util_Poll at 0x150d64 if necessary. At 0x150d9c–0x150db0 it caps the write length against availability and calls __write_chk(fd, currentBuffer, chunk, -1). It advances total/remaining counters at 0x150dc0–0x150dc8 and advances the buffer at 0x150dca on a completed chunk; short writes have a separate path. PLT stubs 0x18d700, 0x18d720, 0x18e290 and 0x18e2a0 resolve through .rel.plt to STI_IOCTL, MI_APAF_ALSA_Sti_Open, mi_util_Poll and __write_chk respectively. This is compressed-device ingestion with backpressure, not an eARC PCM write.

_dspCommFunc_LAPAF_OpenDecoderDevice (0x163e80) has four _MI_LAPAF_UTILS_Decoder_Open call sites (0x164346, 0x164412, 0x1644d4, 0x164518), plus SetAudioParam2 and SetInputMux calls (0x164954/0x164960). The source-specific branches and codec argument provenance should be examined next; the four call sites do not imply four independent hardware sinks.

## Kernel registration and compressed input

In mtk-tvsnd-machine.ko, mtk_compr_audio_register_device at .text+0x7f70 constructs names tvsnd-compress-dec%u, and at 0x812c–0x813c selects an ops table from .rodata+0x62d8, storing it at object +0x438. Relocations at 0x62d8/e0/e8/f0/f8 select compr_hw_in_ops, compr_hw_out_ops, in, out, in. The prior audit found device numbers 12–16; the registration path and earlier sysfs inventory do not establish which decoder the active MAT session opened.

mtk_tv_compr_ops (.data+0x720):
- set-params entry +0x10 resolves via CFI thunk .text+0xd3ac to 0x85f8; it calls the selected hardware ops +0x10 at 0x8668. For compr_hw_in_ops (.data+0x7e0), relocation at 0x7f0 resolves via thunk 0xd324 to 0x6ae4.
- copy entry +0x40 resolves via thunk 0xd3c4 to 0x8c7c; the wrapper loads hardware ops +0x40 at 0x8cc4 and calls at 0x8cf0. Input-table relocation at 0x820 resolves via thunk 0xd354 to 0x7104.

Input set-params reads the codec word at params +8, range checks 2–14 and dispatches via .rodata+0x5886, base .text+0x6b24, byte entries multiplied by four. Cases 2,6,9,10 set decoder-state +0xd8 to 1,4,16,15 respectively; case 14 reaches 0x6b3c without overwriting that field; other cases in this range go to 0x6d44. These numeric IDs are not yet mapped to the actual MAT session or vendor codec enums. At 0x6b44/0x6b58 the function calls OpenDecodeSystem/SetDecodeSystem. If state +0xd4 equals 4, 0x6b80 initializes MM2 AES info, 0x6b98 queries it, and 0x6bd4/0x6be0 obtains a DSP base and its noncached mapping. State +0x1a8 stores the mapped base, +0x1b0 stores a separately allocated staging buffer. The value 4 is consistent with MM_IN from the existing soc BTF analysis; the exact source field should be independently verified in machine BTF.

## Actual bytes and DSP write-pointer acknowledgement

The input copy routine at 0x7104 bounds the request against state +0x1a0 and copies from userspace into the staging buffer (+0x1b0) at 0x720c. On success it loops:
- 0x729c calls MApi_AUDIO_offload_MM2_checkAesInfo for the selected decoder.
- 0x72b4–0x72d4 computes the destination from the mapped base +0x1a8 and offset +0xfc, caps the chunk by available bytes at +0x104, and calls __memcpy_toio. No codec transformation in this specific copy loop.
- 0x728c calls MApi_AUDIO_offload_MM2_inputAesFinished with the decoder ID, copied length, a conditional first-chunk flag and a state value (+0x128). Exact timestamp flag semantics are not asserted.

In mtk-tvsnd-soc.ko, MApi_AUDIO_offload_MM2_inputAesFinished (0x2f08) calls MDrv_AUDIO_MM2_inputAesFinished at 0x2f60; the driver calls HAL_MAD_MM2_inputAesFinished at 0x482e4. The latter has two calls at 0x9b35c/0x9b4f0 to HAL_DEC_R2_Set_SHM_PARAM with parameter zero and computed write offset. Local BTF enum member R2_SHM_PARAM_ES_WR_PTR resolves to 0 (local string global offset 0x228b95, record member at type-area offset 0x111c0). This is distinct from the previously traced MAT HDMI bypass parameter 0xb4.

The shared-memory dispatch table (.rodata+0x21a0) slot zero contains 0x1c, yielding .text+0x90d94. At 0x90da4 it stores the offset at shared memory +0xd14 plus 0x40 for index 1; at 0x90da8 it calls HAL_DEC_R2_SetCommInfo. This establishes a concrete host-side compressed audio input and pointer notification boundary. It does not establish the DSP firmware's subsequent parsing, Dolby object metadata handling or eARC output.

## Engineering decision and next test

The existing MM2 path really transports bytes to a DSP input buffer; the obstacle is not simply the absence of a host-side byte-copy mechanism. Do not confuse this decoder input with a transparent end-to-end IEC61937 output. No direct ALSA non-PCM eARC endpoint has yet been established.

Next, resolve the exact format/source/codec configuration passed by the four LAPAF decoder-open branches (raw TrueHD, packed MAT, JOC and HDMI controls), map the kernel codec IDs and source fields with BTF, then trace the R2 decoder format/metadata reporting. Separately inventory the matching stock DSP firmware blobs and loader-selected paths; do not assume the host driver reveals the proprietary DSP parser contract. If there is no supported MM-fed MAT or raw TrueHD Atmos path, seek vendor guidance rather than force HDMI source or Atmos status. No further playback tests, root, module changes or flashing needed at this stage.

Reproduce with readelf -Ws/-rW/-SW and llvm-objdump -dr on the original hashed modules; for the userspace library use llvm-objdump -d --triple=thumbv7a-linux-android with the ranges above and resolve ARM PLT entries through .rel.plt. Keep proprietary binaries private.
