# HDMI known-good comparison and MAT full-tunnel experiment (2026-11-19)

Firmware V670, TCL 65C9K and Q85H Pro. Private archive hdmi_working.zip.
SHA-256 of the available archive: 9cbaf6ee9adcbec86bc5e5b20d7c6aa00fc4086b060fc502127e17fcfd735294.
SHA-256 of hdmi_truehd_working.log inside it: cfee1e6cc9c0573d5785ea19c04a0ffc2a6bce98255fecd83dbd140ad486cbf1.

## Observed positive control

The capture includes PCM transitions and the active MAT interval approximately 04:02:04.784–04:02:34.607. The logged TV mode is **Auto**, not the proposed Pass Through. The exact played file and physical soundbar indication are not independently verified by the log (operator followed the MKV test protocol). The DSP reports Atmos, not an independent wire capture.

| Evidence file | 1-based lines | Finding |
|---|---|---|
| hdmi_truehd_working.log | 36218–36222 | PC data type 22 (0x16), MIME audio/vnd.dolby.mlp |
| hdmi_truehd_working.log | 36278–36294 | com.tcl.tvinput UID 1000 opens MAT (encoding 19, native 0x24000000), 192 kHz, mask 0x63f, HW_AV_SYNC direct output, session 873 |
| hdmi_truehd_working.log | 36297, 36316, 36327 | TV input passthrough; HDMI/Mat/HwAvSync pipeline; AStream External Input Passthrough Mode |
| hdmi_truehd_working.log | 36431–36434 | tvinputhal writes dummy data |
| hdmi_truehd_working.log | 36490–36493 | Auto, MAT input, MAT encoder and output; AVR MAT_TRUEHD(0), MAT_ATMOS(1) |
| hdmi_truehd_working.log | 36654–36656, 36696 | DEC(0), DVB_MAT, HDMI_IN, PlayType HDMI |
| hdmi_truehd_working.log | 36811–36816 | ch_map 0x333f; MAT_TRUEHD, isAtmos 1 |
| hdmi_truehd_working.log | 36871–36872, 36912–36918 | hasAtmos:1, STREAM_ATOMS=1 |
| hdmi_flinger.txt | 342–384 | active direct HW_AV_SYNC MAT 192k, 8ch, 42 writes, 1336 bytes total; no effect chains |
| hdmi_flinger.txt | 1037–1044 | Hdmi3 -> Decoder_437 -> SrcDecoder_437; Encoder -> EarcOutput |
| hdmi_policy.txt | 1083–1084 | active hw_av_sync output MAT 192k |
| diagnostic_v021.txt (reports_v021.zip) | 14211, 14235, 14524 | internal MM_IN DEC(2), PlayType(-1, Unkown), MAT_TRUEHD isAtmos 0 |

Actual HDMI data arrives through hardware capture, not AudioTrack's dummy buffer. HDMI demonstrably processes MAT and Atmos; this is not equivalent to submitting MAT payload bytes from an app. AVR capability flags match internal tests and cannot alone explain the failure. The HDMI hardware route is not an app input route.

## Next experiment: v0.2.2

Keep source MM; use public AudioTrack API and full A/V tunnel with same source, encoding MAT (19), 192000 Hz, 7.1, HW_AV_SYNC. Test three payload variants: full IEC burst (61440 bytes), LE MAT payload (61424 bytes), BE MAT payload (61424 bytes). They are previously generated FFmpeg fixtures; one timestamp per 20 ms burst. Never label payloads PCM. MAT format with full tunnel is new; variants previously tested without video or with audio-only sync. No claim that they meet an undocumented MAT AudioTrack contract.

| Mode | File | Rate |
|---|---|---|
| 8. MAT192_IEC | truehd.iec | 192000 |
| 9. MAT192_LE | truehd.matle | 192000 |
| 10. MAT192_BE | truehd.matbe | 192000 |

Use Auto first, matching the HDMI capture. Keep physical volume very low; stop immediately on noise. Capture live logcat before playing and active dumps where possible. Record audio/video result for every mode. Success requires normal audible sound and Atmos indication plus correlation with HAL/DSP flags. If these fail, inspect decoder initialization, AStream public/vendor interfaces and source-specific parameter handling read-only. No MPEG-TS, privilege or system changes. Android build and device results pending.
