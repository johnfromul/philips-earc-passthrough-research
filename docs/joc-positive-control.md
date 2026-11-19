# E-AC3 JOC positive control (2026-11-19)

TCL 65C9K, Q85H Pro, firmware V670, Auto, eARC. Private reports_joc.zip (382244 bytes; SHA-256 a50b22de0712bf18d31dac3c5187df383f669845f2230b7aecf7cea3340e668d). Capture Probe_Auto_20261119_044026. Operator confirmed Atmos, normal audio, moving video; no raw logs or personal data published. Test session 913.

| Evidence | Lines | Observation |
|---|---:|---|
| live.log | 3123, 3201, 3205, 3325 | JOC test, encoding 18, Java mask 0xfc, native 0x0a000001, mask 0x3f, 48000 Hz, flags 0x41, direct HW_AV_SYNC, eARC |
| live.log | 14059–14078 | MM_IN, DEC(2), DVB_AC3, AC3P, PlayType(-1,Unkown), bMmOmxMode:0 |
| live.log | 14130 | HW-sync v2 header parsed successfully |
| live.log | 14211 | TPC REACH_SYNC, hwSyncId 65556 |
| live.log | 14216–14232 | main DEC(2) MM_IN, EAC3 input; Auto eARC MAT and encoder MAT, reconstruct pipeline EncoderFormat=MAT |
| live.log | 14244–14249 | 48000 Hz, DSP channel map 0x333f; AC3 type DDP, isAtmos transitions unknown to 1 |
| live.log | 14306–14308 | hasAtmos:1, playback:1, STREAM_CODEC=167772161, STREAM_ATOMS=1 |
| live.log | 14347–14353 | thal_audio is_atmos:1; DolbyInfo.is_atmos:1, UI AtmosEnable:1 |
| live.log | 21922 | 2441524 bytes, 794 complete units, 678 rendered callbacks, last video PTS 22566666 us, head 1088784, stopped=false |
| flinger_active.txt | 353–389 | active E_AC3_JOC direct HW_AV_SYNC output, session 913 |
| observations.txt | operator responses | Atmos / normal / moving |

No occurrences of "Too long without an audio frame". JOC and grouped TrueHD have the same MM_IN decoder index and PlayType unknown; the latter reports MAT_TRUEHD isAtmos=0 (reports_v021.zip diagnostic_v021.txt lines 14207–14235,14524,59917). The failed v0.2.2 prepacked MAT modes instead report hasAtmos=-1 and zero playback head. HDMI uses DEC(0), HDMI_IN and reports isAtmos=1.

JOC has about 2.4 seconds of startup in the 25-second bounded test, after which the audio head progresses approximately 48000 samples per second. Same public AudioTrack / tunneled-video approach demonstrably carries Atmos for one codec; no evidence of a generic third-party Atmos prohibition or broken shared clock. The outgoing eARC format is reported as MAT in Auto; this is a vendor log, not a wire audit or proof of bit-perfect E-AC3 passthrough. The AVR cap flags are unchanged (MAT_TRUEHD=0, MAT_ATMOS=1). No claim that TrueHD shares the same decoder-specific configuration.

## Next steps

1. Verify SHA-256 identity of samples/truehd.thd on host versus installed device file. Explicitly confirm the same MKV through Kodi WASAPI/HDMI (the earlier HDMI logs prove MAT+Atmos in that session but not the selected file identity). If hashes match, validate a second known-good 48 kHz 7.1 TrueHD Atmos MKV using the already working group16 test; no new APK needed. Preserve original samples.
2. Static investigation, not additional speculative MAT payloads: inspect libmi3_audio_hal.so (SHA-256 07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a). Thumb ELF VAs: StartDecoderDevice 0x16c420; WriteCompressDev 0x16ce74; Decoder_Open 0x14c96c; GetMatType 0x1713b0; IsAtmosStream 0x17253c. The exported _MI_LAPAF_FILTER_DecoderCommon_SetMatMlpOnly (0x16a9d8, 176 bytes) stores the supplied 32-bit parameter at offset 0x348 of referenced state (0x16a9e0–0x16a9e4), if both pointers are non-null. This is a candidate for tracing readers/callers, not established cause or an app-accessible setting. No MatMlpOnly messages in JOC or v0.2.1 live logs. Reproduce using llvm-objdump -d --triple=thumbv7a-linux-android --start-address=0x16a9d8 --stop-address=0x16aa88.
3. If the second sample fails as well, compare decoder-specific TrueHD initialization and input contract with AC3P, including metadata query paths and DSP control requests. Do not modify source identities, privileges, firmware, or force status flags. No MPEG-TS.
