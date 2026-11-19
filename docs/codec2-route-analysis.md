# Codec2 route inventory (V670, 2026-11-19)

Private archive codec_routes.zip, 848826 bytes, SHA-256 1fa3db86ee66ff69c0b2f4e069dc3f348c88c292e2adaa52dbee8b4dc2c33331. Fingerprint TCL/G08_4K_GB/G08:14/UTT2.250416.001/AU04:user/release-keys. Eight XML files and four ELF32 ARM libraries were inspected; no raw dumps or vendor binaries are published.

## Declared components

Neither media_codecs-mt5896.xml nor media_codecs-mt5879.xml declares TrueHD or MAT as a decoder MIME type. No such declarations were found in the six other collected XMLs. The generic vendor media_codecs.xml is an empty MediaCodecs element with a CTS workaround comment; XML inspection alone does not establish the effective runtime MediaCodecList.

- mt5896 XML lines 552–564: c2.mtk.hdmi.capture (audio/vnd.hdmicap, max 8 channels), atv and analog capture.
- Lines 565–578: c2.mtk.ac3.decoder supports audio/ac3, audio/eac3 and audio/eac3-joc; each advertises a maximum of two input channels in this XML. This is not evidence of an Atmos-preserving output.
- Lines 579–596: c2.mtk.dts.decoder supports several DTS formats.
- Lines 719–734: legacy OMX AC3 family declaration, no TrueHD.
- mt5879 lines 614–660: analogous capture and AC3/DTS components.
- The collected lshal lists MediaTek default and software Codec2 stores; their existence is not proof of a usable TrueHD component.

## Binary inspection

| Library | Size | SHA-256 |
|---|---:|---|
| libcodec2.MTK.so | 876704 | 9c9197a7f6b87a3238803688350058eb7c13d7aece36a83cec0091a291b77666 |
| libcodec2_mtk_audio.so | 488260 | a578c98a6630977f8a134a50ea06e93fc93e14279b5f675d2a141367b36b981e |
| libcodec2_mtkadwrapper.so | 602364 | b34cee53dd5a88dc9abe7be85089e9de2f28757dbb2877d41f6bd800649f1906 |
| libcodec2_mtk_c2store.so | 39900 | 2f3780060d9af2ff31c1e4421a5f1dc95f549162f14526ca7c8862243ecc5d98 |

The strings audio/vnd.dolby.mlp appear at file offsets 0x10b62 (mtk_audio) and 0x47a13 (MTK). Both export audioMediaDefs::MEDIA_MIMETYPE_AUDIO_DOLBY_MAT (ELF VAs 0x7085c and 0xd41e8 respectively). No dedicated c2.mtk.truehd or c2.mtk.mat decoder name was found in these libraries. The mtk_audio library exports C2MtkHdmiCap::_AudioFormat2MediaType at Thumb VA 0x4d4d4, compares the HDMI audio-format value to 0x600 at 0x4d568, branches at 0x4d56e, and selects a MIME pointer at 0x4d5b4–0x4d5b8. The already captured real HDMI logs identify the 0x600 input as audio/vnd.dolby.mlp. This is consistent with MAT capture; it is not evidence of an MKV decoder accepting that MIME.

The audio decoder library has C2MtkAudioDec and C2MtkAc3Dec symbols. Finding raw audio or MAT strings in a shared library does not establish an app-accessible decoder or its output contract. The c2store library exposes listComponents and createComponent; runtime enumeration is the appropriate next check.

## Wrapper clues

libcodec2_mtkadwrapper.so contains strings _Get_Dolby_TrueHD_Bypass_Capability (file offset 0xd35e), ParamType_trueHD_bypass_enable (0x123cc), _get_trueHD_bypass_enable (0x1c227), DisableAtmos (0x172f5), MatMlpOnly (0x1c65e), and MatAtmosStream. These are candidates for read-only source analysis, not public Android keys or demonstrated controls.

The wrapper independently exports _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly at Thumb 0x66274, a branch to 0x5ab10. The valid-pointer path 0x5ab14–0x5ab24 dereferences its two arguments, stores a word at state +0x348, and returns 0, just like the primary HAL's corresponding setter. The separate _MI_LAPAF_FILTER_DecoderCommon_SetDisableAtmos thunk is 0x66250 -> 0x5a594. No runtime effect or client access is inferred from the name. Searches of the prior v021, JOC, Minirama and HDMI live logs found no explicit matches for DisableAtmos, atmos_disEn or trueHD_bypass_enable.

## Runtime confirmation (v0.2.4)

The tester includes a dedicated read-only Inspect audio codecs button. It records ALL_CODECS and REGULAR_CODECS audio decoder names/types/capabilities plus MIME-only and rate/channel format discovery results for TrueHD, MAT and AC3/EAC3/JOC controls. It does not open or start a codec, request a privileged input, write audio, spoof HDMI, modify firmware, or assume a vendor MIME is supported. The report is overwritten on each run at the app's external files directory as codec_routes.log.

If the runtime list confirms no TrueHD/MAT decoder, deprioritize MediaCodec and do not add a speculative MediaCodec playback mode. If it advertises a candidate, examine the reported component and documented output format before testing. A PCM-only decoder does not solve Atmos preservation. The remaining established I/O path is AudioTrack -> internal HAL/DSP, where TrueHD is recognized but Atmos is not. The exact Minirama HDMI identity check remains deferred at owner request.

## Runtime follow-up

[Runtime codec discovery and vendor parameter investigation](runtime-codecs-and-bypass.md) records the completed v0.2.4 query: both lists return null for all tested TrueHD and MAT MIME types. Public MediaCodec playback deprioritized.
