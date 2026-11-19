# Native decoder dispatch and parser eligibility (2026-11-19)

Read-only analysis of the previously supplied V670 binaries, ARM32 Thumb. No additional playback was needed. The owner elected to skip a further exact-Minirama HDMI control; the existing HDMI result remains a separate positive control, and exact cross-route stream identity is not claimed.

## 1. Confirm MatMlpOnly dispatch

libmi3_audio_hal.so SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a.

- MS12V2 MAT filter entry points are wrappers, not stubs: Init 0x1864c4 sets r1=4 and branches through a thunk to decCommFunc_Lapaf_Init. SetParameter 0x1864d6 and GetParameter 0x1864da branch through thunks to shared LAPAF implementations.
- decCommFunc_Lapaf_SetParameter at 0x16bee8 handles special keys (WriteData, DecoderFormat, OpenDecoderDevice, StartDecoderDevice, level limits, DtsDmxPcmOut), then iterates the 200-entry _astDecoderParamCmdList at 0x1b4f3c (8 bytes per entry); 0x16c036–0x16c05a compares names and invokes the resolved parameter callback.
- Entry 42 at 0x1b508c points to the MatMlpOnly string at 0x4db45. Its function pointer at 0x1b5090 is zero on disk and is relocated at load time. Decoding the APS2 packed .rel.dyn section (file offset 0xb2d8, 320 entries) resolves this pointer to R_ARM_ABS32, dynsym index 762, _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly (Thumb symbol 0x16a9d9).
- Valid-pointer success path at 0x16a9e0–0x16a9ec only stores a word to state +0x348, returns zero, and contains no DSP call. StartDecoderDevice at 0x16c420–0x16c964 applies other controls (0x16c640–0x16c73a), and calls Decoder_SetCmd and SetAudioParam2, but no demonstrated direct application of MatMlpOnly. The parameter could still have an indirect consumer. Do not infer an Atmos disable switch.
- WriteData dispatch at 0x16c180 calls _dspCommFunc_LAPAF_WriteCompressDev (0x16ce74); at 0x16cff2 it invokes _MI_LAPAF_UTILS_Decoder_Write (0x14e038). The latter calls mi_apaf_compress_ack_pts at 0x14e0be, mi_apaf_compress_write at 0x14e0d0, and starts the compressed device as needed at 0x14e12e. This establishes the downstream compressed-device boundary, not proof of end-to-end byte identity.

## 2. Parser selection is conditional and excludes TrueHD and MAT

audio.primary.merak.so SHA-256 e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad.

utils_get_audio_parser_name(audio_format_t), 0x88950–0x889d0: masks format with 0xff000000. Returns ac3 for AC3, EAC3 and JOC main formats, dts for DTS/DTS-HD, ac4 for AC4. TrueHD (0x0e000000) and MAT (0x24000000) take default 0x8899e–0x889a2, resolving to "unknown" at 0x30cbc.

More importantly, utils_isParserSupported(mtk_stream_out*), 0x887fc–0x888cc: reads vendor.hal.audio.disable.parser (0x2e3cb; no value in earlier inventory), then tests the stream's audio main format. Neither 0x0e000000 nor 0x24000000 is on the supported list; both return 0. In the output-open path 0x5eb16–0x5eb36, the code first calls this eligibility function, skips Parser_Open when it returns false, and independently requires a stream-state field at +0x2ac equal to 5.

libaudioparser.so SHA-256 89eef9e90f1f4d09456459648699d408a8f7dc7a2508d1a294fe2f52ea35d5d9.

- Parser_Open export 0x11634 branches to implementation 0xc2a0. Explicit truehd string comparison at 0xc346–0xc348, truehd_parser_init call 0xc350; unknown comparison 0xc382–0xc384, fake_parser_init at 0xc3f0. The library contains a real TrueHD parser which this primary HAL eligibility gate does not select for the native TrueHD-format stream.
- utils_out_write_data has a parser branch, with Parser_Write at 0x89658, Parser_Do_Parse at 0x896e2, Parser_Read at 0x89704. Do not assume this branch executes for tunneled TrueHD/MAT; the eligibility gate argues against it for these format values. Neither the JOC nor Minirama nor MAT nor HDMI captures contained explicit parser-name setup logs matching the searched names. This is a static distinction, not a demonstrated reason Atmos metadata is missing.

## 3. Remaining app-accessible candidate: MediaCodec

The original firmware_inventory.zip lists MediaTek HIDL Codec2 store default (1.0/1.1) and software store (1.0/1.1/1.2), with /vendor/lib/libcodec2_mtk_audio.so, libcodec2_mtk_c2store.so, libcodec2_mtkadwrapper.so and libcodec2.MTK.so. Inventory confirms existence, not TrueHD support or an Atmos-preserving route. Collect codec configuration XML and those binaries locally using collect_codec_routes.ps1. Inspect actual advertised component names, MIME types, include relationships, and output contract. A decoder emitting only PCM will not solve object-metadata passthrough by itself.

No repeat of prior playback tests, no property changes, no impersonation, no firmware patch or speculative payload variants. Exact HDMI control is deferred by owner choice; keep that evidentiary limitation explicit.
