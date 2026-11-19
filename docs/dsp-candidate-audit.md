# Stock DSP candidate audit and Atmos status boundary

## Provenance

Owner-supplied private dsp_candidates_stock.zip, 44593 bytes, SHA-256 5f882d463ea6acceb3f7a4c180347810d9c9efa49dfa7e85a65dde1cc1ba2ebd. All 13 manifest members independently checked for exact size and SHA-256. Same stock US AU02 workspace; not proof of live GB AU04 identity. No device commands, no patch.

## Candidate classification

- firmware/dsp.bin (53809 bytes, SHA-256 6b9f348970c9b4aadff6e16d3c9e82ed94589ac1edfd7050de28432b7360b21b): raw non-ELF binary, structured repeating 3-byte patterns. Compared 48-byte varying-content windows against six stock modules and previously captured audio libraries. Content overlaps the program-memory arrays of mtk-tvsnd-soc.ko. For example, 409 matching contiguous bytes at dsp.bin+0x5a60 and mst_codec_sif_palsum_pm2+0x7b9; 381 bytes at dsp.bin+0x5181 and mst_codec_sif_btsc_pm2+9; 313 bytes at dsp.bin+0xac8e and mst_codec_sif_btsc_pm3+0x1f88. These comparisons were made on original bytes. No full-array identity for the named arrays. Strong evidence of related DSP program data including SIF broadcast audio segments, not evidence that this file is the active R2 MAT parser or the complete firmware. Instruction set and load selection unknown.
- firmware/DOLBY.bin (18944 bytes, SHA-256 906234f43b55adbf430323b314fa6a375bb12b6d4482dcf98440bdc5f4521717): repeated numerical tables with long constant runs and ramps, four 4736-byte regions, first and fourth exactly equal; not identified as executable. Likely lookup/tuning data, exact consumer unknown.
- Both eARC INIs are byte-identical (SHA-256 75beefee49cd2b7ead9379b81291a5da98b31f903a85cbdb1a5dcdf7b59453a4), five address/mask/value triples and a terminal marker. All six audio_init/pre_init variants are likewise register tables (620/487/487 and 7/137/137 lines), not named codec settings. No textual TrueHD, MAT, Atmos, bypass, passthrough or SPDIF option found. Do not alter registers.
- Both Dolby JSON files are device render descriptors v1, two render outputs each, internal processing mode, coefficients, geometry or latency. Not an eARC codec capability list. md5.txt lists six Seekwave radio files, neither DSP candidate.

## Embedded DSP loader

Matching stock mtk-tvsnd-soc.ko SHA-256 0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4. ELF .data file offset 0x32c0. Symbols mst_codec_pm1/2/3/4, mst_codec_dec_pm1/2/3 and SIF PALSUM/BTSC arrays are defined there. dsp_info at .data+0x1a4f0 (640 bytes) contains eight 80-byte entries. Codec keys at each entry +0x48: 0x21,0x4f,0x26,0x55,0x57,0x5f,0x61,0x60 (these are DSP code IDs, not AudioFormat values). Relocations in .rela.data resolve nonempty entries to named arrays +6. HAL_AUDSP_DspLoadCode (0xa7950) compares requested code to those keys then at 0xa7b50 calculates index*80. Calls DspLoadCodeSegment and DspVerifySegmentCode at 0xa7bf8/0xa7c14 and 0xa7c78/0xa7c94. Separate conditional TEE branch still exists. Neither decoder blob identity nor live selection of firmware/dsp.bin has been demonstrated.

Platform module request_firmware at .text+0xcf7c receives filename from object +0x28, then releases returned firmware at 0xcf8c on success; this call alone is not a demonstrated program-code consumer. No further extraction of protected code or modification of security mechanisms proposed.

## New finding: reported Atmos is not necessarily raw decoder Atmos

This distinction materially affects the interpretation of our prior playback results.

libmi3_audio_hal.so (same prior hash) _dspCommFunc_LAPAF_IsAtmosStream at Thumb 0x17253c passes information type 0x56, selected decoder ID and 4-byte output at 0x172550–0x172556. _dspCommFunc_LAPAF_GetMatType at 0x1713d2–0x1713d8 passes 0xb1 and invokes IsAtmosStream at 0x171520 after a successful type query.

In stock soc module, local BTF identifies Audio_infoType_Is_Atmos_Stream=0x56, Audio_infoType_Is_Atmos_Stream_FW_flag=0x57, Audio_infoType_Mat_Stream_Type=0xb1; R2_SHM_INFO_ATMOS_STREAM=0x53 and R2_SHM_INFO_MAT_STREAM_TYPE=0x57. The HAL_MAD_GetAudioInfo2 dispatch table .rodata+0x2f10 (signed 32-bit offsets relative to .text+0x948d4) resolves these three information cases to 0x94e9c, 0x94f44, 0x954ec respectively.

The ordinary Atmos query reads the DSP Atmos field but also conditions the returned boolean on the Dolby version and authorized MS12 profile. The separate FW_flag case interprets the same DSP field as nonzero/zero without those particular predicates. This is a static observation for the supplied module, not a request to bypass or change authorization. The ordinary getter's diagnostic format (rodata+0x365c1) labels values eDolbyVer, MS12V2_Z, AtmosStream; the FW_flag getter has an AtmosStream-only diagnostic at rodata+0x2cb1d. HAL_DEC_R2_Get_SHM_INFO resolves Atmos and MAT type via its existing named shared-memory selectors. This identifies the distinction between reported status and actual decoder status; neither is alone sufficient to establish object metadata delivery at the eARC sink.

No matching eDolbyVer/MS12V2_Z/AtmosStream(...) diagnostic found in the four archived JOC, grouped TrueHD, v0.2.2 MAT and HDMI log ZIPs. JOC and HDMI positive controls remain valid; no conclusion that the device lacks an Atmos or TrueHD license. The raw FW flag for the failing internal session is not independently established. Prior language that inferred actual metadata loss solely from isAtmos=0 must be qualified. Do not change license checks, source identity, profile or return values. Ask TCL for supported diagnostics distinguishing raw firmware status, authorized feature reporting, input framing and actual output.

## Next engineering decision

Prioritize the host-side input contract and supported configuration, not patching these blobs. Look at the source-dependent MAT decoder setup / actual writes and any supported vendor configuration options in the already available binaries and stock text files. Determine whether the archived logs contain any independent evidence of decoded object metadata (none established yet). Seek OEM clarification if distinction cannot be obtained without unauthorized access. No new playback repetition, no firmware installation. Recovery and exact regional stock compatibility still unresolved.
