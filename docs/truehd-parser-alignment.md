# Native TrueHD parser, AU alignment and remaining diagnostic boundary

## Verified static input

Rehashed libaudioparser.so (89eef9e90f1f4d09456459648699d408a8f7dc7a2508d1a294fe2f52ea35d5d9), libmi3_audio_hal.so (07572cae7d738df1cf5cd2d18bf6b3e74472a8c9412d70ba6c12b8f365183e2a), libaudiopipelinetopology.so (96a1985e18199368b89c28aea376952073b7d332753b50b13fc121f3dba9cf29), audio.primary.merak.so (e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad), mtk-tvsnd-soc.ko (0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4). All previous hashes match. ARM32 Thumb virtual addresses below; no runtime binary execution.

## TrueHD parser contract

Exports are branch thunks: preparse 0x11694 -> 0x108b0; write 0x11698 -> 0x11124; read 0x116b8 -> 0x111b0; read_done 0x116c0 -> 0x11464; init 0x1173c -> 0x114cc.

- At 0x109a2 the frame inspection routine requires >=32 bytes available; at 0x109be–0x109e8 checks sync bytes f8 72 6f (ba/bb) at offset 4 and b7 52 at offset 12 before initial sync. Header fields initialize state at 0x11054–0x1106c; log confirms sample rate, channels, substream count. The routine later recognizes a new major sync at 0x10d02–0x10d34.
- At 0x10c98–0x10ca4 computes AU length from the 12-bit word count, doubled; rejects lengths <=5 via 0x10d6a. Accumulated offset limit 0xfe00 (0x10cd4); running AU count checked against 256 (0x10cf6).
- New major sync checks AU count divisible by eight (0x10d38–0x10d3a). The alternative logs "Access unit number not 8 alignment =%d" (string 0x44dd, resolved via literal at 0x11104). Do not infer all valid source streams have 24 AUs between major syncs.
- At 0x10a24 the loop tests accumulated count 24; 0x10e0c–0x10e28 publishes frame information, subtracts 24 from the running count and resets related counters. The message at 0x213f is referenced by the log at 0x10e38. read_done also increments a private counter by 24 (0x1146e). These are evidence of native 24-AU frame handling, not proof that every AudioTrack write must begin with major sync or that all parser output bytes are unmodified.
- Init creates a 65536-byte ring buffer (0x11520–0x1152c) and establishes state. Read includes sample-rate-dependent timestamp calculations (0x1128e–0x112d6). No claim of identical behavior to our simpler Java grouping.
- The primary HAL still does not select this parser for TrueHD or MAT (see decoder-dispatch-and-parser.md). No executable parser integration or format override attempted.

## Existing test implementation and offline test

Fetched current TrueHdChunks.java and FrameReader.java from main. The former concatenates consecutive complete AUs into fixed groups of 16 or 24 and uses the first timestamp; it does not check major sync. The latter uses the 12-bit word count and 16-bit input timing, with a fallback delta of 40 samples if zero or >1920. It assumes 48 kHz. No inference that this fallback has actually been used on the owner's sample.

Added tools/inspect_truehd_alignment.py, which reads elementary TrueHD, checks AU bounds, counts candidate major syncs matching ba and b752, measures phase modulo 24, fixed group starts coinciding with sync, timing deltas and full-file SHA-256. It does not decode, perform CRC verification, prove Atmos, edit files or communicate with devices. It intentionally rejects truncated streams and no-sync streams; it is not an MLP/bb parser.

Host-tested on locally generated FFmpeg 48-kHz stereo TrueHD silence (1 s; not Atmos). Results: 1200 AUs, first sync at AU0, 75 major sync candidates, all 74 gaps =16, phases mod24 =0/8/16 with 25 each. 75/75 fixed 16-AU groups start at major sync versus 25/50 fixed 24-AU groups. All 1199 timing deltas =40, zero Java fallback deltas. Removing final byte is correctly rejected. These results demonstrate that 24-AU chunks need not all start at major sync and no speculative alignment rule should be imposed without sample evidence.

Next run from repo root on the actual prepared sample:
    python tools/inspect_truehd_alignment.py samples/truehd.thd

Report private/truehd_alignment.json. Use --out with a different name if the report already exists. Compare exact hash with archived sample evidence; no new playback needed for this diagnostic. If source begins at major sync and group16 alignment is already correct, do not repeat grouping tests. If an actual discrepancy is detected, design a narrow controlled experiment with unchanged valid payload and timestamps.

## MatMlpOnly and raw Atmos diagnostic

Re-disassembled libmi3_audio_hal.so. Direct non-PC memory operands referencing #0x348 again yield two stores (0x161824, 0x16a9e4) and one read (0x1623a0). No additional direct DSP consumer found. Indirect aliases, structure copies and compiled-away accesses are not ruled out. The exact MatMlpOnly string occurs in topology and HAL, MatTrueHDOnly in HAL, MatAtmosStream in topology/HAL/primary HAL. Neither names nor the setter alone establish an Atmos switch.

The HAL also contains enum-name string _Is_Atmos_Stream_FW_flag (file offset 0x25ad1). This alone does not establish a public query route. Rechecked four archived live.log collections: no matching raw flag, MatMlpOnly/MatTrueHDOnly, MS12V2_Z or truehd_parser diagnostic lines found in the searched logs. Do not invoke undocumented services or patch authorization/reporting. Request supported read-only diagnostics from OEM, including DSP raw Atmos status versus the normal reported status and actual output. No new device activity.

## Decision

One offline sample framing check remains justified. It cannot by itself demonstrate the cause or a firmware fix. Existing DSP input transport, source-specific output constraints, and reported-status distinction still hold. Deployment remains blocked by lack of demonstrated working transport, exact regional recovery and supported development boot path. No HAL/DSP/boot patch produced.


## Owner's actual Amaze TrueHD report (2026-11-19)

private/truehd_alignment.json: 3228 bytes, SHA-256 085ae15716d21d93422a744c2c4318a29391a0952e6b443d23c5f3fc2e753bc4. The reported media hash 1779b6f1d20108c3b8404ed62c3477b67f817e80ccfb4fc93b1e953887d3da64 matches the original prior PC/TV hash. The report itself, not the commercial media bytes, was received in this step.
- 22,928,814 bytes, 36,000 AUs, first major sync at AU0, 360 header candidates, all rate code 0. All 359 recorded sync gaps divisible by four; 174 (48.47%) not divisible by eight. Gaps range 8–128 AUs. Fixed groups start at sync in 92/2250 (4.09%) 16-AU groups and 56/1500 (3.73%) 24-AU groups. Neither statistic is an AU validity test.
- Histogram contains exactly 35,999 deltas, range 19–84 input-timing units, 28,552 equal to 40; all are accepted by FrameReader without fallback. Sum = 1,439,960 timing units = 29.9991667 s to last AU start using the app's 48 kHz clock. Nominal 36,000*40/48000 = 30 s. Jitter exists but no demonstrated accumulated clock error. Histogram cannot determine maximum instantaneous timing deviation across the file.
- Reopened exact parser ELF. 0x10d38 tests low three bits of r3, branches at 0x10d3a to 0x11038 for nonzero. That branch logs via 0x10d90, then clears state at 0x10d96–0x10dac and returns -2 (0x10dae–0x10db2). This is a genuine error path, not just a warning. The counter is reset/reduced by multiples of eight elsewhere as previously described. This is cause for caution against enabling this parser for the real stream, not proof of a runtime rejection: the raw track did not use it and no direct parser execution on the sample occurred. The report does not include complete CRC or payload checks.

Decision: no evidence to justify turning on the native parser, changing the stream's sync words, dropping AUs or imposing an artificial 8-AU alignment requirement. Prior 16-AU and 24-AU playback already had normal sound without Atmos indication. A further clock-only A/B could be considered if measured timestamp jitter warrants it, but current evidence does not prioritize it over decoder/route investigation. No new playback needed now.

## Metadata routing control check

Rehashed audio.primary.merak.so (e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad). __utils_judge_Metadata_source at Thumb VA 0x70180 reads parameters from Decoder via indirect calls at 0x70210 and 0x70280; literal references at 0x701e4 and 0x70256 resolve to user chmod lock (0x2d2a0) and hdmi_dolby_atmos_lock (0x2d710). Diagnostic logs both values. At 0x7040e and 0x70452, literals resolve to PcmR and PcmrKeepLastMD. 0x7048e calls MI_APAF_ParameterManager_SetParameter via PLT 0xce180, .rel.plt slot 0xd321c; the code passes four-byte value 1 on the identified keep branch. This is a PCM-renderer metadata-retention control, not evidence of Dolby object metadata generation. No setters invoked.

Archived JOC and Minirama live logs show identical conditions before their divergent Atmos callbacks:
- JOC 04:41:06.282: isChmodLockUser=0, isAtmosLock=0, primary_media_active=0, isKeep=1, then successful PcmrKeepLastMD=1. Atmos callback 04:41:06.441 =1.
- Minirama 05:02:24.803–.804: same lock values, primary_media_active=0, isKeep=1; successful PcmrKeepLastMD=1; MLP callback and Atmos=0 at .981–.982.
- v0.2.2 MAT captures also show the same zero lock values and successful keep parameter.

Thus the observed lock/retention setting does not distinguish the working JOC from failed TrueHD and MAT; it cannot be identified as the cause on present evidence. Raw DSP flag and actual eARC payload remain unknown. Do not modify the setting on the assumption it will supply missing object metadata.
