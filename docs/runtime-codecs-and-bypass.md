# Runtime codec discovery and vendor parameter investigation

Firmware V670, Android 14. Private codec_routes.log, 16659 bytes, SHA-256 181c3636139de25c16cd5df13c934e718bee30809dbc2a5aa30be60e1703d479.

## Runtime result

v0.2.4 completed without cancellation (line 189). Both REGULAR_CODECS (lines 3–95) and ALL_CODECS (96–188) list no TrueHD or MAT decoder. MIME-only and format queries (48 kHz, eight channels; also 192 kHz for both MAT candidates) all return null for audio/true-hd, audio/vnd.dolby.mlp, audio/truehd and audio/vnd.dolby.mat (80–89, 173–182). AC3, EAC3 and JOC controls all return c2.mtk.ac3.decoder (90–95, 183–188), with runtime maximum six input channels (4–9, 97–102), differing from the previously collected XML. HDMI capture is advertised under audio/vnd.hdmicap (30–31, 123–124). No codec was instantiated. Close public MediaCodec discovery as an avenue for now; this does not prove absence of private firmware functionality.

## TrueHD bypass symbols are internal enumeration entries

Rechecked actual V670 libmi3_audio_hal.so (see binary hash manifest). The following strings appear as pointers in internal enum-to-string tables in .data.rel.ro, not as public AudioManager keys. File offsets in this segment differ from ELF VAs by 0x2000.

| String | rodata VA | Pointer VA | Table and zero-based index |
|---|---|---|---|
| ParamType_trueHD_bypass_enable | 0x2c271 | 0x193710 | pAudParam2TypeStr, 24 (0x18) |
| _get_trueHD_bypass_enable | 0x4d70e | 0x193b0c | pAudInfo2TypeStr, 34 (0x22) |
| _Get_Dolby_TrueHD_Bypass_Capability | 0x192bf | 0x193e94 | pCommAudInfoTypeStr, 81 (0x51) |

The tables start at 0x1936b0, 0x193a84 and 0x193d50 respectively. SetAudioParam2 at 0x14ab30 and GetCommAudioInfo at 0x14b1bc construct a request and call the common control-request PLT stub at 0x18d1b0, resolved via R_ARM_JUMP_SLOT at 0x1b5bb0 to MI_APAF_ALSA_UTILS_LAPAF_SetKcontrolParams. These are internal typed requests, not established client APIs. A bounded immediate-argument search of direct PLT calls to SetAudioParam2/GetAudioInfo2/GetCommAudioInfo did not find an explicit r0 load of 0x18/0x22/0x51 respectively in the previous 16 disassembled instructions. Indirect calls and computed arguments remain possible. Do not treat the enum labels as evidence that the feature is active or callable by an unprivileged app.

The named GetKcontrolParams PLT entry is 0x18db10, not 0x18d1b0. No setter invocation attempted.

DisableAtmos in both native libraries is a store-only setter: libmi3_audio_hal.so 0x168e54–0x168e68 stores at +0x1c0, and libcodec2_mtkadwrapper.so implementation 0x5a594–0x5a5a8 does the same. No direct DSP command in those success paths. No literal matches to trueHD_bypass_enable or MatMlpOnly in the primary HAL's custom get/set parameter string references inspected; this is not an exhaustive proof across every service or indirect key dispatch.

## Dolby MS12 interface

The earlier codec_routes.zip services.txt includes vendor.dolby.audio.ms12.IMs12/default, vendor.mediatek.hardware.audioext.IAudioExt/audioext, vendor.tcl.audio.ITclAudio/default and /taudio. Existence does not imply shell or app permissions.

audio.primary.merak.so (SHA-256 e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad):
- dolby::MS12Server::setBitstreamPassthrough, Thumb VA 0xbc348, size 16 bytes, ignores bool and writes result value 1 to the output parameter at 0xbc34c–0xbc34e; no routing or decoder configuration in this body. The symbolic meaning of result 1 is not established. Do not interpret it as a working setter.
- allowAtmosBitstreamOutput, 0xbc0d4, stores a bool to server +0xb2 and inverted bool to +0x8a on one branch (0xbc1b8–0xbc1c0). It does not establish restoration of missing TrueHD metadata.
- lockBitstreamOutputToAtmos_l, 0xba648, optionally writes a byte to +0xb1. This is not reconstruction of source metadata.
- utils_isPassthroughSupported, 0x70bd8, returns true for TrueHD main format 0x0e000000 at 0x70c16–0x70c1e and MAT 0x24000000 at 0x70bee–0x70bfa. This is a format-eligibility predicate only, not a claim of actual lossless Atmos passthrough.

All static observations are firmware-specific. No changes to vendor parameters, privileges, process identity or firmware. Existing tests already show the input is recognized as TrueHD but reports Atmos=0, contrasting with working JOC/HDMI. Output flags cannot restore missing objects. Next read-only step: inspect publicly accessible dumpsys diagnostics of the already observed vendor services, without invoking transactions or setters. If dumps are unavailable or uninformative, record the limitation and pursue OEM supported route/firmware correction rather than speculative playback permutations.
