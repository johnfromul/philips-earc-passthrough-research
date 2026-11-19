# Audio Passthrough Tester — 0.1.0

Minimal, dependency-free Java Android TV diagnostic app for TCL 65C9K / Google TV 14. It tests actual `AudioTrack` construction and playback independently of Kodi, Moonfin and Media3. It is a **diagnostic prototype, not a firmware fix**. No root, hidden API reflection, native libraries, system modification, telemetry upload or network permission. Java-only APK supports ARMv7 and ARM64 Android 12+.

## Why this exists

The observed setup is a TCL 65C9K with Q85H Pro, firmware V8-T653T01-LF1V670 / G08_4K_GB / AU04. The same Amaze TrueHD Atmos sample works from a Windows HDMI input (direct to soundbar and through TV/eARC) but fails from internal Android playback routes. E-AC3 Atmos works. Kodi software decoding plays sound without Atmos. The supplied V670 logs show a TrueHD offload track on eARC (`0x20011`) and an explicit removal of the IEC61937 direct profile from Moonfin's application capability query. Auto mode selects MAT, yet `hasAtmos:0`. This does not establish whether a direct `AudioTrack` request for IEC or MAT succeeds, nor whether the HW AV sync profile can avoid the faulty path.

## Build on Windows

Prerequisites: JDK 17 (Android Studio's JBR works); Android SDK Platform 34, Android SDK Build-Tools 34.0.0; `adb` in PATH. In Android Studio → SDK Manager install **Android 14 (API 34)** and, in SDK Tools with Show Package Details enabled, **Android SDK Build-Tools 34.0.0**. You do not need an emulator or NDK.

Open PowerShell in this directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

If the default SDK or JDK folders differ:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -SdkRoot 'C:\Users\YOUR_USER\AppData\Local\Android\Sdk' -JavaHome 'C:\Program Files\Android\Android Studio\jbr'
```

The script calls javac, D8, AAPT2, zipalign and apksigner directly: no Gradle or Maven downloads. Output: `out/AudioPassthroughTester.apk`. It generates a local debug key in `out/` (retain it for updates). Alternatively, open the project in Android Studio, sync with Gradle 8.7 / AGP 8.5.2 and select Build APK, or run `gradle assembleDebug` with JDK 17 and Gradle 8.7. Gradle output is `app/build/outputs/apk/debug/app-debug.apk`. An optional GitHub Actions workflow builds the debug APK on a repository you control and attaches it to the workflow run; no repo is created by these files. Debug builds from different signing keys may require uninstalling the old app (deletes its samples/report).

**Verification status:** host-side sample generation and frame parsing were executed successfully against synthetic 48 kHz TrueHD, E-AC3 and PCM fixtures; the Android SDK could not be installed in the authoring environment, so APK compilation, Windows PowerShell scripts and TV playback are not yet verified. No compiled APK is included.

## Prepare real test samples on PC

Install Python 3.9+ and FFmpeg/FFprobe (in PATH). Use the **same known-good 48 kHz 7.1 TrueHD Atmos Amaze** file that produced Atmos from your laptop. Supply the working E-AC3 Atmos demo as the optional positive control (48 kHz 5.1). If input contains multiple TrueHD or E-AC3 tracks, select the exact global stream index using `--truehd-index` or `--eac3-index` (see `ffprobe -hide_banner <file>`).

```powershell
python .\tools\prepare_samples.py 'C:\path\amaze.m2ts' --eac3 'C:\path\eac3-atmos.mkv'
```

The source can also be MKV. The script extracts up to 30 seconds from the beginning; no TrueHD or E-AC3 re-encoding, no video. It uses FFmpeg's IEC61937 muxer to produce a standard HBR stream, then extracts the MAT payload with header/length/sync validation. It prepares:

| File | Contents |
|---|---|
| `truehd.thd` | Elementary TrueHD frames, unchanged |
| `truehd.iec` | LE IEC61937 TrueHD bursts (61,440 bytes each, 20 ms at 192 kHz / 8 channels) |
| `truehd.matbe` | Extracted 61,424-byte MAT frames in payload byte order |
| `truehd.matle` | Alternate 16-bit word order for experimental vendor input |
| `pcm.s16le` | Software-decoded stereo 48 kHz PCM control; not Atmos |
| `control.eac3` | Optional original E-AC3/JOC elementary stream |
| `manifest.json` | File hashes, sizes, selected input stream information |

MAT files are **TrueHD-bearing MAT frames**, not a licensed Dolby MAT 2.x PCM/object encoder. The packed input preserves the compressed source payload; that does not establish that TCL's raw MAT API accepts this framing. No Atmos detection or encoding is performed on PC; verify the source against the known-working laptop test. Expect ~90 MB for 30 seconds IEC and another ~90 MB for each MAT variant. The last incomplete burst may be discarded by FFmpeg. No demo media is bundled.

## Install and transfer files

Connect with ADB wireless debugging as before. Verify the target using `adb devices -l`; if necessary, set `$env:ANDROID_SERIAL` to the connected device, not the pairing port. Then:

```powershell
powershell -ExecutionPolicy Bypass -File .\install_samples.ps1
```

This installs and starts the app and pushes prepared samples into `/sdcard/Android/data/ro.saitatter.audioprobe/files/`. If using the Gradle APK, pass `-Apk '.\app\build\outputs\apk\debug\app-debug.apk'`. If ADB push is denied on your device, these paths are app-specific external storage; alternatively use `adb shell run-as` with the debug APK, or report the error. The UI shows its actual directory. To reopen:

```powershell
adb shell am start -n ro.saitatter.audioprobe/.MainActivity
```

## Test order

**Set the physical soundbar volume very low.** Wrongly processed encoded audio can be loud. STOP immediately if noise occurs (or mute with remote). No playback starts automatically. Do not use a fake PCM encoding for encoded payloads. Close other players, set TV audio to eARC and Digital Audio Out to **Pass Through**, and make sure soundbar input is eARC. Repeat with Auto only after collecting the Pass Through results. Never change TV audio mode during a test.

1. Press **Capabilities**. It enumerates output devices, device profiles, direct profiles and direct playback support (separately queried for media/HW sync). This is only what the calling app can see.
2. Select **PCM stereo control** → **Play 20 sec**. Verify normal sound. Select observed result and press Record result.
3. Select **E-AC3 / JOC control (RAW)** → Play, if the optional file was supplied. The app sends the E-AC3 bitstream; the soundbar should report Atmos if the route handles JOC. This control has no HW sync variant.
4. For each compressed route below, start with **Open only**. This attempts real construction even when the query returns unsupported. It immediately releases the track without writing data. If it succeeds, press **Play 20 sec** and record the actual soundbar format and whether sound is correct. Open failure is a valid result. Do not select "Dolby Atmos + normal sound" if only the badge appears but no sound plays.
5. Start with **TrueHD RAW** (baseline), then **IEC61937 192k 7.1**, **TrueHD HW AV Sync**, and **TrueHD explicit offload**. Only then test MAT variants and IEC HW AV Sync. Record every outcome.

| Mode | Java encoding | Rate / layout | Request |
|---|---:|---|---|
| PCM | 2 | 48k stereo | Mixer/control |
| E-AC3 | 6 | 48k 5.1 | RAW |
| TrueHD RAW | 14 | 48k 7.1 | Offload not requested |
| TrueHD explicit offload | 14 | 48k 7.1 | `setOffloadedPlayback(true)` |
| TrueHD HW AV Sync | 14 | 48k 7.1 | `FLAG_HW_AV_SYNC`, timestamped writes |
| IEC | 13 | 192k 7.1 | Packed payload, ordinary streaming |
| IEC sync | 13 | 192k 7.1 | Packed payload + HW sync |
| MAT BE | 19 | 48k 7.1 | Raw MAT (experimental) |
| MAT LE | 19 | 48k 7.1 | Alternative byte order (experimental) |
| MAT 192 | 19 | 192k 7.1 | Rate variation (experimental) |
| MAT sync | 19 | 48k 7.1 | Timestamped writes (experimental) |

Encoding 19 is a framework MAT value on Android 14 but not part of the ordinary public SDK on that release. The app uses the numeric value without hidden API calls; `AudioFormat.Builder`/`AudioTrack.Builder` may reject it. HW AV Sync is requested via public audio attributes and timestamped writes, not a full audio/video tunneled MediaCodec session. A successful or failed audio-only test does not establish full tunneling behavior. The app does not directly choose Audio HAL flags — Android policy does. Even RAW without explicit offload may use the HAL offload mixport. `AudioTrack.isOffloadedPlayback()` reports the app request, not necessarily actual HAL flags.

## Collect evidence

The app saves a rolling-in-session on-screen log and an **append-only** `probe.log` on the device. It records query results, Java configuration, build exceptions, session ID, route, accepted byte counts, timestamp/head progress and your manual observation. It cannot inspect the actual HDMI wire or identify Atmos by itself. While each important test is playing (within 20 seconds), run this in a PC PowerShell window:

```powershell
powershell -ExecutionPolicy Bypass -File .\collect.ps1 -Name IEC_PassThrough
```

Collect again for `TRUEHD_SYNC_PassThrough` or `MAT_BE_Auto` as needed. `collect.ps1` captures policy, flinger, logcat and firmware to timestamped `reports/` folders. For a lightweight live trace in another PowerShell window:

```powershell
adb logcat -v threadtime | Select-String 'AudioProbe|remove support direct IEC61937|GetFormatConvert|DigitalOutputFormat|MAT_TRUEHD|hasAtmos|isAtmos|eARC_Tx|HW_AV_SYNC|AudioFlinger|AudioTrack'
```

You can retrieve the complete app log after all tests with:

```powershell
adb pull /sdcard/Android/data/ro.saitatter.audioprobe/files/probe.log .\probe.log
```

Share reports privately; Android logcat can contain network addresses, device identifiers and other private information. The app itself has no network permission. To distinguish capability filtering from actual playback failure, compare query, build, write progress, HAL flags, routed device, vendor Atmos detection, and the soundbar's real output. A track opening is not success; PCM/Dolby Surround are not Atmos. Successful IEC with a hidden profile would justify exploring a Moonfin/Media3 custom sink. Failure of the tested public pathways does not prove that every proprietary or privileged route is impossible.

## Host-side parser test

```powershell
javac -d hosttest app/src/main/java/ro/saitatter/audioprobe/FrameReader.java tools/FrameReaderTest.java
java -cp hosttest ro.saitatter.audioprobe.FrameReaderTest samples
```

## References

- Android Audio capabilities: https://developer.android.com/training/tv/playback/audio-capabilities
- Android AudioTrack: https://developer.android.com/reference/android/media/AudioTrack
- Android AudioManager: https://developer.android.com/reference/android/media/AudioManager
- FFmpeg IEC muxer source: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/spdifenc.c
- Nexio IEC implementation notes: https://johnneerdael.github.io/nexio/android/technical/iec.html
