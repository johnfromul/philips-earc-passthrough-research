# TCL eARC passthrough research

A reproducible investigation of Dolby TrueHD + Atmos playback from internal Android TV applications on the TCL 65C9K (Google TV 14, MediaTek) with a TCL Q85H Pro soundbar. **Diagnostic research, not a firmware fix.**

## Current status (2026-11-19)

- External HDMI → TV → eARC and HDMI → soundbar reproduce Atmos from the same demo, according to the device owner's observations. E-AC3 Atmos works from internal playback.
- On firmware V670, ordinary apps can open TrueHD and MAT tracks, but IEC61937 is explicitly filtered and track creation is refused.
- Full video/audio tunneling resolves the audio-only HW sync stall. Grouping TrueHD access units (16 or 24) restores clean audio and largely normal playback speed; neither produces an Atmos indication.
- Static inspection identifies TCL-specific client checks, absent IEC profiles in the captured policy, and the MediaTek DSP information query path.
- TCL Media Center drops MKV TrueHD tracks before decoding: its MediaTek extractor reads unset `vendor.mtk.mkv.support.truehd` (defaults false). ADB shell activation was denied by SELinux; no change occurred. Independent app demuxers are unaffected by this particular gate.
- HDMI capture in Auto shows a working hardware MAT path (192 kHz, 7.1, HDMI_IN, DEC(0), isAtmos=1), versus internal MM_IN, DEC(2), isAtmos=0. The HDMI AudioTrack carries dummy data, not the audio stream itself.
- E-AC3 JOC full A/V tunnel succeeds in Auto with normal sound, soundbar Atmos and DEC(2) isAtmos=1. The same source and HW sync work for JOC; TrueHD/MAT-specific ingestion remains unresolved.
- **v0.2.2** adds full A/V tunneling with MAT at 192 kHz for three payload variants; all three variants failed to produce audible Atmos: IEC and LE accept data without recognized audio frames, BE stalls. See [MAT tunnel results](docs/mat-tunnel-results.md). **v0.2.3** adds a dedicated MAT-only screen (same audio tests). **v0.2.4** adds a read-only audio codec inventory. MKV is the target, MPEG-TS is out of scope. No bypass or system modification is implemented.

| Start here | Purpose |
|---|---|
| [Investigation report](docs/investigation.md) | Full chronology, evidence, test matrix and limitations |
| [Native analysis](docs/native-analysis.md) | Binary locations, symbol addresses, reproducibility and hypotheses |
| [Native MKV test](docs/native-player-test.md) | Zero audio tracks, native player evidence |
| [Extractor stage 4](docs/native-extractor-stage4.md) | Feature flag and SELinux denial |
| [HDMI comparison](docs/hdmi-comparison.md) | Working hardware path and v0.2.2 test design |
| [Minirama retest](docs/minirama-retest.md) | Different stream counters; same Dolby Surround and isAtmos=0 |
| [Second TrueHD sample](docs/truehd-second-sample.md) | Attempted alternate sample; identity verification failed, retest required |
| [Codec2 inventory](docs/codec2-route-analysis.md) | No TrueHD/MAT decoder declared in collected XML; runtime query pending |
| [Decoder dispatch and parser](docs/decoder-dispatch-and-parser.md) | Relocation-resolved setter, parser eligibility and Codec2 next step |
| [MAT initialization](docs/mat-mlp-only-analysis.md) | Source-dependent decoder parameter, static evidence and limitations |
| [JOC positive control](docs/joc-positive-control.md) | Internal tunneled Atmos success; isolates failure to TrueHD/MAT-specific path |
| [MAT tunnel results](docs/mat-tunnel-results.md) | Completed three MAT variants and next controls |
| [Reproduction guide](docs/reproduction.md) | Build, prepare media, deploy, test, collect |
| [Next experiments](docs/roadmap.md) | Native playback, comparative controls, upstream report |
| [Evidence index](evidence/README.md) | Source identifiers, SHA-256 manifests and derived observations |
| [Historical v0.1.0 README](docs/original-v0.1.0.md) | Audio-only design and test modes |
| [Historical v0.2.0 README](docs/original-v0.2.0.md) | Full A/V tunnel design |

## Quick start (Windows)

Install JDK 17, Android SDK platform and build-tools 34, adb, Python 3.9+, and FFmpeg/ffprobe. Connect TV using wireless debugging; select the correct device with `$env:ANDROID_SERIAL` when multiple are connected.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
python .\tools\prepare_samples.py 'C:\path\known-good-atmos.mkv' --eac3 'C:\path\eac3-joc.mkv'
powershell -ExecutionPolicy Bypass -File .\install_samples.ps1
powershell -ExecutionPolicy Bypass -File .\install_tunnel.ps1
```

The E-AC3 argument is optional. Original generated H.264 and PCM control fixtures are included. Supply your own legally obtained TrueHD and E-AC3 sources. Never redistribute commercial test clips. Turn soundbar volume **very low** before starting: incorrectly handled encoded audio can produce loud noise. Stop if unexpected noise occurs. See the reproduction guide for test order and logging.

## Repository layout

```text
app/                 Java Android diagnostic app (no network permission)
tools/               Sample generation, host tests, evidence analysis
scripts/             Read-only ADB and native inspection utilities
tunnel_samples/      Original synthetic video and quiet PCM controls
docs/                Research notes and experiment instructions
evidence/            Selected observations and hashes, not raw device dumps
.github/workflows/   Android debug build CI
```

Git tags retain the actual historical source patches: v0.1.0, v0.1.1, v0.2.0, v0.2.1. Main includes v0.2.4 source and documentation, utilities and packaging fixes. Host tests cover parsing/grouping and sample preparation, not Android hardware compatibility.

## Privacy, rights and support

No extracted TCL/MediaTek/Dolby binaries, commercial Atmos demos, signing keys, APKs or complete device logs are included. Keep local dumps under `private/`; generated files are ignored by Git. Review all derived evidence before publication; use the read-only tools on hardware you own or are authorized to investigate. Only publish media and code you have rights to distribute. No license has been selected for the original project; decide on a license before a public release. This project is not affiliated with TCL, Dolby, MediaTek, Jellyfin or Google.
