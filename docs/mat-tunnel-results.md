# MAT 192 kHz full-tunnel results (2026-11-19)

Target V670, TCL 65C9K, Auto, eARC. Private reports_v022.zip (1,268,929 bytes, SHA-256 8b3d464a0dbbc3b4939f1e79209b424838beef57d4c51f2bd821e7d56f2b6a0c). Three complete captures and two earlier partial attempts; the complete captures are used here. The UI may be v0.2.3 (MAT-only screen); the test variants originated in v0.2.2. Raw logs remain private.

| Variant | Session | Complete units | Bytes accepted | Video callbacks | Audio head | Operator audio | Display |
|---|---|---:|---:|---:|---:|---|---|
| MAT192_IEC | 889 | 1275 | 78336000 | 737 | 0 | silence | unknown |
| MAT192_LE | 897 | 1275 | 78315600 | 736 | 0 | silence | unknown |
| MAT192_BE | 905 | 4 (plus partial) | 303004 | 234 | 0 | failed | unknown |

IEC and LE reached the configured 25-second limit with moving video. BE stalled and raised IOException: No audio write progress for 8s. Operator marked video failed; callbacks progressed until termination. No independent soundbar format observation (operator entered unknown). Do not equate absence of Atmos detection with confirmed transmission of PCM.

## Evidence pointers

All line numbers below are 1-based in the respective live.log inside the archive.

| Event | Probe_Auto_20261119_042954 (IEC) | Probe_Auto_20261119_043152 (LE) | Probe_Auto_20261119_043256 (BE) |
|---|---:|---:|---:|
| AudioFlinger MAT 192k, 0x63f, flags 0x41 eARC | 1306 | 1992 | 1851 |
| MM/Mat/HwAvSync pipeline | 1328 | 2014 | 1875 |
| Valid hw_sync_id (65553/65554/65555 respectively) | 1489 | 2173 | 2027 |
| HAL input MAT, Auto output MAT | 3755–3758 | 5119–5122 | 4757–4762 |
| DEC(2), DVB_MAT, MM_IN | 5683 | 6328 | 6408 |
| StartDecoderDevice, PlayType(-1,Unkown), bMmOmxMode:0 | 5700 | 6336 | 6475 |
| Successfully parsed HW sync header v2, size 0x14 | 6154 | 6595 | 6797 |
| playback:1, hasAtmos:-1, Ch:-1 | 6218 | 7035 | 7063 |
| First no-audio-frame timeout | 12497 | 13623 | none |
| Final app outcome | 24943 | 25277 | 14636 |

The IEC and LE logs contain 9,194 and 9,204 instances of `Too long without an audio frame`, respectively. No MAT stream-type classification event or hasAtmos:0/1 for these three captures. MAT format selection and positive HW AV sync setup do not validate MAT byte framing. The decoder does not report usable stream information; no proof that these payloads conform to the undocumented platform MAT input contract. BE stopped accepting data. Earlier TrueHD RAW grouped tests gave usable audio but isAtmos=0; this is a distinct failure. HDMI uses DEC(0), HDMI_IN, PlayType HDMI, MAT_TRUEHD and isAtmos=1, with hardware-supplied data. Do not spoof source or privilege.

## Next work

Run a real E-AC3 JOC control from a locally owned known-good Atmos MKV through the existing full A/V tunnel mode 5, in Auto. Verify FFprobe's JOC/Atmos profile, 48 kHz and 6 channels; stream copy only; no synthetic EAC3 re-encode can preserve JOC. Verify source in a known-good player. Compare MM_IN decoder initialization, frame events, metadata classification and soundbar result. Also check the local vs TV TrueHD sample SHA-256. If JOC works, investigate the TrueHD/MAT-specific ingestion path via read-only static analysis (StartDecoderDevice, Decoder_Open, WriteCompressDev, and format-dependent input handling). If JOC fails, first validate the test harness's frame grouping and timing. No additional MAT variants until evidence identifies the intended input format.

The optional media.codec service is absent on the TV. The fixed collector continues and still obtains active flinger, policy, live logcat and observations. No need to repeat the three MAT tests.
