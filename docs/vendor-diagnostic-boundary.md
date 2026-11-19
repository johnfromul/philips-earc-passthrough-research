# Vendor diagnostic boundary and MS12/LAPAF dispatch

V670, TCL 65C9K (Android 14). Read-only inspection. No service transactions other than dumpsys and no changes to device configuration.

## Runtime diagnostics

The operator ran adb shell dumpsys for each of the following services; all four returned FAILED_TRANSACTION on the console:

- vendor.dolby.audio.ms12.IMs12/default
- vendor.mediatek.hardware.audioext.IAudioExt/audioext
- vendor.tcl.audio.ITclAudio/default
- vendor.tcl.audio.ITclAudio/taudio

The private vendor_audio.zip (410 bytes; SHA-256 011381ae6d5e544455f567de6dc9c7ec1d6262a7469678bbd7017f1009b56be1) contains four zero-byte stdout files (ms12.txt, audioext.txt, tclaudio.txt, taudio.txt). This agrees with the operator's separately pasted stderr. These names appeared in the previous service inventory. FAILED_TRANSACTION is not proof of a specific SELinux denial, missing dump implementation or particular Binder permission. No successful diagnostic return; do not repeat the command or invoke undocumented transactions.

## Dolby implementation versus active HAL dispatch

The supplied libms12v27.so (SHA-256 4f771adc0737afb6380a29be3bd5bb85276bdf8ef5c5073e4d92021cfe5f27c7) exports:

| Symbol | ELF Thumb entry VA |
|---|---|
| dthdi_dec_process_au | 0x2a91f8 |
| dthd_au_decoder_decode_access_unit | 0x2a9c54 |
| dthd_evo_parse_extra_data | 0x2b0500 |
| ms12v2_matdec_init | 0x1cbdc0 |
| ms12v2_matdec_decode | 0x1cc570 |
| mat_enc_dthd_scan_for_au | 0x325b24 |
| mat_enc_pack_emdf_payloads | 0x32348c |

These establish presence of implementation code, not runtime execution, supported license/profile, or accessibility from an ordinary app.

libmi3_audio_hal.so declares libms12v27.so as DT_NEEDED. Its _MI_APAF_FILTER_MS12V2_MAT_Init (0x1864c4) sets r1=4 and branches to decCommFunc_Lapaf_Init through thunk 0x18cb38. Its _MI_APAF_FILTER_MS12V2_MAT_ProcessData (0x1864d2) branches through thunk 0x18cb50 to decCommFunc_Lapaf_ProcessData (0x165378). Within that shared function, 0x165bd2 calls _dspCommFunc_LAPAF_GetMatType (0x1713b0); 0x165d8a and 0x165f4a call _decCommFunc_Lapaf_FilterParamEvent_Update (0x171d94). There is no established direct invocation of the exported software TrueHD decoder in these wrappers. Static inspection cannot rule out its use elsewhere or in DSP firmware. The _MI_APAF_FILTER_MAT_ENC_Init at 0x180670 and ProcessData at 0x18067c each consist of movs r0,#0 followed by bx lr. These are not sufficient to prove the absence of MAT encoding; the hardware/DSP path may supply it.

The prior write-path trace established _MI_LAPAF_UTILS_Decoder_Write and mi_apaf_compress_write. Runtime evidence distinguishes hardware HDMI MAT (Atmos=1), internal E-AC3 JOC (Atmos=1), internal grouped raw TrueHD (Atmos=0) and unrecognized app-packed MAT (Atmos=-1). The binary findings therefore strengthen the case for a vendor-side input/decoder-contract investigation rather than a generic MediaCodec or MAT carriage change. No app-level fix or demonstrated single root cause yet.

Reproduce with readelf -Ws/-d and llvm-objdump -d --triple=thumbv7a-linux-android on the previously acquired hashed binaries. Do not distribute the binaries, raw dumps or commercial sample media.

## Decision

No further repeated playback or undirected vendor probes. Ask TCL for the supported app-level TrueHD Atmos route, the intended raw TrueHD and MAT contracts, and clarification of the IEC61937 restrictions and native extractor flag. Exact alternate-sample HDMI control deferred by owner; keep its limitation explicit. If OEM provides a supported route or firmware change, test it against the existing known-good controls. See upstream-report.md for the English technical report draft.
