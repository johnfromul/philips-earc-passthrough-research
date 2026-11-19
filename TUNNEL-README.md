# Audio Passthrough Tester v0.2.0 — full A/V tunneling patch

Apply over your existing project, preserving `out/debug.keystore`, `build.ps1`, and existing samples. This patch includes the v0.1.1 Java changes as well as the new activity. It uses public APIs and no external libraries or Gradle dependency changes.

1. Extract into the existing AudioPassthroughTester project root. Do not delete `out` or uninstall the TV app.
2. Run `powershell -ExecutionPolicy Bypass -File .\build.ps1` (the fixed version with `-parameters`).
3. Set `$env:ANDROID_SERIAL` to the TV serial from `adb devices -l` if necessary.
4. Run `powershell -ExecutionPolicy Bypass -File .\install_tunnel.ps1`. Only two new generated samples are pushed. `truehd.thd` on the TV is reused. `control.eac3` is optional.
5. On the TV, select eARC, Pass Through; lower the soundbar volume. Close other players. In Audio Passthrough Tester, choose **Full A/V tunnel**, then **Inspect**.
6. Run PCM 5.1 tunnel control; two quiet tones should be audible and the pattern should move. Select audio and video observations and Record result. Then run both TrueHD tests, recording each result. E-AC3 and JOC are optional and require an appropriate 48 kHz sample (`control.eac3`; JOC must actually contain Atmos).
7. For each TrueHD test, launch `powershell -ExecutionPolicy Bypass -File .\collect_tunnel.ps1 -Name truehd_audio_first` (or `truehd_video_first`) on PC immediately after confirming Start on TV. It captures active AudioFlinger, policy, codec and later logcat. Finally pull `adb pull /sdcard/Android/data/ro.saitatter.audioprobe/files/tunnel.log .\tunnel.log` after recording all observations. Logs can contain sensitive identifiers; review before posting publicly.

## Technical details

A fresh explicitly shared AudioManager session ID for each run. AudioTrack uses USAGE_MEDIA, CONTENT_TYPE_MOVIE, FLAG_HW_AV_SYNC, stream mode, timestamped nonblocking writes. MediaCodec explicitly selects a non-secure hardware AVC decoder advertising tunneled support, sets FEATURE_TunneledPlayback and KEY_AUDIO_SESSION_ID, and uses a real SurfaceView. No non-tunneled fallback. Video and audio sample PTS start at zero (the two tracks are not the same media content). Both ports are primed, then audio starts. The thread interleaves input feeding and polls occasional output buffers if supported. Codec callbacks and audio position are logged; rendering callbacks can be delayed/missing on vendors, so manual observation matters. An 8 second write-stall timeout and 25 second run limit apply to the feeding loop, not to vendor API calls which may block. Stop immediately if unexpected noise occurs. The app does not infer Atmos from API success.

Two TrueHD initialization orders (audio first; video configured first) allow checking session-to-HW-sync registration behavior. A synthetic 30 second H.264 640x360 30 fps AVC Baseline clip has no B frames. PCM is 30 seconds of 48 kHz signed 16 bit interleaved 5.1, quiet 440/550 Hz in front left/right and zeros elsewhere. These are original generated diagnostic fixtures, not movie samples.

Android SDK/APK compilation and target playback were not available in the authoring environment. Samples were generated and validated locally; the Java path is experimental. See https://source.android.com/docs/devices/tv/multimedia-tunneling .
