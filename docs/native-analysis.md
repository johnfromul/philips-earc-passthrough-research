# Native library analysis (read-only)

## Inventory

The owner extracted the following artifacts from V670. `/apex/com.tcl.tcore` (`@653670001`) provides TCL-specific audio components. Relevant process names include `audioserver`, `mediaserver`, `vendor.mediatek.hardware.audio.service`, `vendor.mediatek.hardware.tpc@1.0-service`, and TCL audio service. The recorded `/proc/<pid>/maps` were unreadable/empty, so we cannot determine actual mapped libraries at runtime solely from the inventory. PIDs change. `lshal` was partially restricted.

Use `scripts/collect_firmware.ps1` to gather inventory, pull the candidate libraries, and retain the original device path tree under `private/firmware_libs`. Use only read access; no bootloader, root or firmware writes. See the SHA-256 manifest for the exact extracted binaries. Paths unavailable on your device are reported and do not imply absence from all firmware versions.

## IEC61937 restriction

`/system/lib/libaudioclient.so` and `/apex/com.tcl.tcore/lib/libaudioclient.so` in the extraction are byte-identical. The client contains strings:

```
is not /system/bin/mediaserver, remove support direct IEC61937
is not /system/bin/mediaserver, not support direct IEC61937
is not /system/bin/mediaserver, not support offload IEC61937
is not /system/bin/mediaserver, can not create track
is /system/bin/mediaserver, can create track
/proc/%d/cmdline
```

Static analysis found a check of the caller's process identity via `/proc/%d/cmdline` and a special-case rejection of the IEC encoding (`0x0D000000`). This is consistent with AudioTrack construction returning INVALID_OPERATION (-38) and the app receiving an UnsupportedOperationException. Observed ELF virtual addresses (Thumb bit cleared, firmware-specific):

| Location | ELF VA | Reported behavior |
|---|---:|---|
| AudioSystem getDirectProfilesForAttributes entry | 0x9BB50 | Entry/trampoline |
| Direct profiles implementation | 0x684D0 | Filtering |
| Caller-identity check | 0x687C0–0x68932 | Identity verification |
| Remove profile range | 0x68934–0x6895E | Profile removal |
| Direct playback support | 0x67F00 | IEC unsupported |
| Offload support | 0x5C7AA | IEC unsupported |
| AudioTrack::set | 0x6D650 | Refuses initialization |

The table records previous static-analysis locations and should be verified against the manifest-matched binary with an ARM Thumb-aware disassembler. ELF VAs are not runtime ASLR addresses. These notes are for diagnosis, not patch instructions. No modified client or impersonation of a privileged process is provided.

The exact error string was not found in inspected AudioFlinger, AudioPolicy, HAL, parser, MS12 and stagefright libraries. Absence of a string does **not** show absence of enforcement. The captured AudioPolicy configuration lacks an IEC61937 mixport profile; Android additionally selects compatible formats, sample rates, channel masks and device routes. The system policy manager has names `openDirectOutput`, `getProfileForOutput`, `searchCompatibleProfileHwModules`, `getDirectPlaybackSupport`. Static analysis does not establish that bypassing a client check would create a working server-side route.

`libaudiospdif.so` has `SPDIFEncoder::isFormatSupported` (~0x33D6). The inspected implementation supports AC3, E-AC3/JOC, DTS and DTS-HD, not TrueHD or MAT. AudioFlinger `SpdifStreamOut` uses this encoder; that is different from Dolby MS12 MAT encoder. Confirm the switch against the exact binary before making a claim about another build.

## DSP metadata and pipeline

`/vendor/lib/libmi3_audio_hal.so` includes these symbols (Thumb-cleared addresses, confirmed using readelf):

| Symbol | ELF VA |
|---|---:|
| `_dspCommFunc_LAPAF_IsAtmosStream` | 0x17253C |
| `_dspCommFunc_LAPAF_GetMatType` | 0x1713B0 |
| `_decCommFunc_Lapaf_FilterParamEvent_Update` | 0x171D94 |
| `_MI_LAPAF_UTILS_GetAudioInfo2` | 0x14AE30 |
| `MI_APAF_ALSA_UTILS_LAPAF_GetKcontrolParams` | 0x148108 |

Previous disassembly traced the Atmos property query (0x56) through `_MI_LAPAF_UTILS_GetAudioInfo2` to the ALSA control functions. This suggests the HAL obtains stream status from lower-level MediaTek audio processing rather than parsing everything itself. It is not proof of the exact DSP firmware implementation. Changing a status flag cannot regenerate discarded object metadata. `utils_ApplyOutputPath`, `_MI_APAF_CustomizedManager_DigitalAudio_GetFormatConvert` and `__utils_judge_Metadata_source` are further candidates for investigating topology, conversion and metadata routing.

`libmi3_audio_hal.so` declares a dependency on `libms12v27.so`. Symbols in that library include `mat_enc_dthd_scan_for_au`, `mat_enc_process_chunk`, `mat_enc_pack_emdf_payloads`, `mat_dec_evo_open`, `mat_enc_pcm_add_bytes`. Their presence does not demonstrate that the encoder is used by our BYPASS path or accessible via a supported Android API. The MAT layout fed to Java probes is experimental and preserves the original TrueHD payload; it is not equivalent to PCM objects encoded into MAT 2.x.

## ALSA/permissions

`/dev/snd/comprC0D12` through `comprC0D16` have `system:audio`, mode `0660`, SELinux `audio_device`. Shell UID 2000 has no audio group; SELinux is enforcing. `/proc/asound/cards` and `/proc/asound/pcm` return permission denied. `tinymix` is unavailable in shell. A helper installed as a normal app would not gain these permissions. No attempt to alter permission, driver, or SELinux state was made.

## Reproducible analysis

```sh
python tools/scan_firmware.py private/firmware_libs --out private/native-analysis
python tools/analyze_logs.py private/reports --out private/log-analysis.json
```

`scan_firmware.py` scans only local extracted ELF files. It builds SHA-256 records, searches specified diagnostic strings and symbol names using `readelf` (if installed), and records DT_NEEDED dependencies. Optional `--disassemble` generates Thumb-aware symbol disassembly using LLVM objdump when available; no binaries are changed. On Windows install LLVM and use WSL or a compatible `readelf`. In Ghidra import the extracted 32-bit ARM ELF with auto-analysis, find the diagnostic strings and inspect cross-references, then verify caller and route selection against AOSP. Do not upload proprietary disassemblies or raw binary dumps. Evidence generated with different firmware must be kept separate.

### Source references for public implementations

- [AOSP multimedia tunneling](https://source.android.com/docs/devices/tv/multimedia-tunneling)
- [AOSP frameworks/av](https://android.googlesource.com/platform/frameworks/av/)
- [Media3 source](https://github.com/androidx/media) (search TrueHD rechunker and DefaultAudioSink)
- [FFmpeg source](https://github.com/FFmpeg/FFmpeg) (libavformat/spdifenc.c)
- [Nuvio TV issue 4618](https://github.com/NuvioMedia/NuvioTV/issues/4618) and [experimental PR 3107](https://github.com/NuvioMedia/NuvioTV/pull/3107) (historical references, status may change)

