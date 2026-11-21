# TCL C655 Realtek baseline from owner ADB evidence — 2026-11-21

## Scope, provenance and limitations

Owner supplied `C655-adb_20261120T232408827011Z.zip`, outer SHA-256 `bd6651935fde68248145fc0e4b52a44baa38faf746f9ae21fa355b988aeba87d`, inner `adb_audit.zip` SHA-256 `4ff9bc50657731366741758b1c901740d0704247ee6587510cbd3524b87c0c48`. Opened the nested ZIP directly, inspected identity, actual audio policy XML, media codec config, HDMI control, audio/AudioFlinger dumps, SELinux, device bindings, collection-status and selected ELF symbols. Outer bundle has **ADB only**; **no ESP status or UART RX capture**. No audio playback test or device modification occurred during this analysis. Do not commit the raw ZIP or proprietary binaries.

The collection returned success but recorded 49 non-copied candidate paths (including permissions); 284 files copied. Nested archive has 343 entries. Several optional commands returned nonzero (e.g. asound /proc and bootctl unavailable). The inner ZIP includes log lines with permission denials; missing results are not hardware absence.

## Confirmed live identity

| Property | Observed value |
| --- | --- |
| `ro.product.model` | `Smart TV Pro` (generic reported model, not unique retail size) |
| `ro.product.device` / board | `G10` / `G10` |
| `ro.soc.manufacturer` | `Realtek` |
| `ro.soc.model`, `ro.hardware`, `ro.hardware.version_id` | `RTD2875P`, `rtd6748`, `RTD2875P` |
| `ro.software.version_id` | `V8-R75PT01-LF1V461` |
| fingerprint | `TCL/G10_4K_GB/G10:12/STT2.230203.001/AS12:user/release-keys` |
| Android | 12, SDK 31 |
| kernel | `5.4.274+`, armv8l output, config.gz readable |
| active slot | `_a`; `ro.boot.dtbo_idx=0` |
| AVB / SELinux | `locked`, `green`, verity `enforcing`, SELinux `Enforcing` |
| bootloader version property | empty; not identified |

This is **not** the C9K MT5896/T653T01/V670 platform. Neither C9K software offsets/patches nor the MT5896-specific 4PDA ESP/U-Boot flashing sequence applies by assumption. The Realtek R75PT01 is also not equivalent to the article author's R51MT02/05 without matching hashes and layout.

## Audio policy actually returned from this TV

`/vendor/etc/audio_policy_configuration.xml`: SHA-256 `6500abbf5dfaa7ce9e033c37d23c765a324868353d52b0678d46ae2d2cb4685c`; inspected with XML parser.

- `compressed_offload` and `compressed_tunnel` advertise AAC variants, MP3, AC3, E_AC3, E_AC3_JOC and AC4; **no AUDIO_FORMAT_DOLBY_TRUEHD, MAT, IEC61937 or DTS-HD**. Neither do `HDMI Arc` and `HDMI EARC` device profiles. PCM profiles are separate. **Policy absence alone does not prove hardware or DSP incapacity**; HAL route and decoder contract also must support the format.
- Runtime `dumpsys media.audio_policy` lists only the **Speaker** in available outputs during capture, with AC3/E_AC3/E_AC3_JOC/AC4 among its profiles. The XML declares EARC as a **possible** port, not a currently connected output.
- `dumpsys audio` prints `TclAudioFunctionInfo: mIsAtmosSupported:true`, which corroborates Dolby Atmos feature reporting but not TrueHD.
- `/vendor/etc/media_codecs_realtek_audio_dolby.xml` declares OMX Realtek AC3, EAC3 and AC4 decoders; no public TrueHD there. **Caveat**: root `/vendor/etc/media_codecs.xml` includes the Realtek basic-audio file, not this Dolby file directly. An alternative `media_codecs_dolby_audio_only.xml` includes it. Presence of these definitions does not prove that a given codec component is registered/active at runtime.

## eARC hardware/software evidence and contradictory flags

- Live platform bindings: `/sys/bus/platform/devices/audio_hw/driver -> audio_hw`, `earc/driver -> earc0`, `rtkaudio/driver -> rtkaudio`. These are Realtek drivers, unlike `mtk-tv-offload-platform` on C9K.
- `/vendor/etc/vintf/manifest/vendor.hardware.tv.earc-service.realtek.xml` declares `vendor.hardware.tv.earc.IHdmiEarc/default`, and corresponding init service file starts `vendor.hardware.tv.earc-service.realtek`. Runtime service list contains the `vendor.hardware.tv.earc.IHdmiEarc/default` service.
- `/vendor/lib/librtk_hal_audio.so` (80,984 bytes) includes names `RHAL_AUDIO_EARC_MultiPCMOut`, `RHAL_AUDIO_ATMOS_EncodeOnOff` and numerous `KADP_Audio_Earc*` calls. `/vendor/lib/librtk_kadp_audio_hw.so` (56,900 bytes) includes `KADP_Audio_EarcGetConnecState`, `KADP_Audio_EarcGetRxState` and EARC ioctl strings. These are symbols/strings, **not proof of TrueHD or a host-fed MAT output path**.
- `ro.feature.hdmi_earc=false`, but `dumpsys hdmi_control` says `EarcSupport:true`, `EarcEnable:2`, `EarcMode:1`, `mEarcMode:1`. `earc_hal` log says `WAITING`. Treat the contradiction as product-feature vs service capability gating; neither setting alone establishes active lossless eARC.
- CEC reports four HDMI input ports with `arc:true` **only on HDMI port 3**. This identifies the reported ARC-capable port, not an electrically verified Service UART pinout. In this capture `ArcEnable:false`, `mArcEstablished:false`, `HdmiAudioCapabilities:[]`, speaker current output. No AVR/soundbar connection established.
- `dolby` kernel module is present; this does not prove a TrueHD decoder/transport.

## Access boundary and what is missing

The collector located `/vendor/lib/hw/audio.primary.rtd6748.so` and `audio.primary.default.so`, but `adb pull` of both failed with `Permission denied`. These are key for Realtek HAL behavior and may need **offline matching stock firmware** extraction, not further privileged ADB probes. `/proc/asound/{cards,devices,pcm}` also denied to shell; bootctl/update tools not found. The `vendor.mtk.mkv.support.truehd` property was empty and explicitly denied to shell; it is **MediaTek-specific** and not meaningful as a Realtek gate.

No actual TrueHD or EAC3 playback attempt is demonstrated in the provided bundle: filtered logcat mostly service/CEC, capture diagnostics and SELinux denials. There is no empirical playback pass/fail for C655, unlike the detailed C9K positive/negative controls. No UART data, boot interrupt or root proof. The C9K MatMlpOnly feature string/path is not imported across these platforms.

## Hardware follow-up: HDMI Service bench checks — 2026-11-21

Separate from the ADB ZIP above, the owner reported measurements on a **Delock 65168** breakout and passive HDMI cable. Pins **14→11 and 14→17 each measured approximately +3.3 V** with the TV on, while **17→11 measured approximately 0–0.1 V**. In unpowered continuity testing pins 11 and 17 showed no continuity to each other or to the separate shell **GRND** terminal. GRND had continuity to the HDMI shell. These measurements leave the shared UART ground unverified.

The initial readings are preserved in the structured evidence. The follow-up bench log records ESP32 commissioning and separate logic-analyzer captures from Delock terminal 14 with terminal 11 as reference. [Current bench log](c655-hdmi-service-uart-bench-log.md) · [Original structured readings](../evidence/c655-hdmi-service-electrical-2026-11-21.json)

## Next experiments (read-only)

1. Physically verify the retail model size, HDMI Service label, connector pin numbering and UART electrical levels before coupling ESP32. Use **passive receive first**, capturing with `tools/collect_tv_research_bundle.py --label C655-uart --skip-adb --esp-toolkit ..\\tcl-tv-toolkit --esp-host ESP_IP --uart-seconds 90`. Do not send the MT5896 bootloader commands/flash profile.
2. Connect the known working soundbar to the correct ARC/eARC port and re-run `dumpsys hdmi_control`/baseline bundle. Check `EarcSupport`, negotiated state, discovered audio formats and actual output selection; `EarcEnable=2` is not interpreted without documentation.
3. With controlled volume and manually initiated samples, capture separate `--reports-only --logs` bundles for internal E-AC3 JOC and internal TrueHD, optionally HDMI TrueHD positive control; record soundbar display and observable playback behavior. Existing baseline cannot prove a TrueHD defect.
4. Find exact matching **R75PT01 V461** stock OTA and extract the Realtek primary audio HAL and selected policies with hashes. Inspect that HAL before any codec/policy modifications or claims about a single flag. No flashing, disabling SELinux, AVB changes or reboot required for current diagnosis.

This report contains only high-level non-personal findings and provenance hashes, not the private raw content.
