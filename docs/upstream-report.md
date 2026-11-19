# Firmware issue report draft: TCL 65C9K internal TrueHD Atmos via eARC

**Subject:** TCL 65C9K V8-T653T01-LF1V670: HDMI TrueHD Atmos works, but internal Android MKV playback loses Atmos metadata

## Environment

TCL 65C9K, Google TV / Android 14, firmware V8-T653T01-LF1V670 (AU04), ARM32 app ABI; TCL Q85H Pro soundbar connected by eARC. Tested Auto and Pass Through. Issue also observed on V643. Goal: normal lossless TrueHD Atmos playback from locally demuxed MKV in ordinary Android apps (Moonfin/Jellyfin, Kodi, diagnostic AudioTrack app), no root or firmware modification.

## Reproduction and results

1. Play a verified 48 kHz Dolby TrueHD + Atmos MKV in an internal app with audio passthrough and eARC enabled. The diagnostic app submits raw TrueHD through a direct AudioTrack using HW_AV_SYNC, with 16 or 24 access units per timestamp and a corresponding tunneled H.264 video session. Audio/video play normally with Dolby Surround (Auto) or PCM indication (Pass Through), not Dolby Atmos.
2. For the internal raw TrueHD session the MediaTek decoder reports MAT_TRUEHD, isAtmos=0 and STREAM_ATOMS=0. Grouped playback corrected earlier audio stalls/noise, without restoring Atmos.
3. In the same diagnostic app, using an E-AC3 JOC source and the same full A/V tunnel pattern in Auto, the soundbar displays Dolby Atmos and DEC(2) reports isAtmos=1. This is a positive control for internal Atmos support and the eARC connection.
4. External Windows Kodi WASAPI playback through HDMI into the TV, then eARC to the soundbar, produces Dolby Atmos according to the operator. Captured HDMI hardware MAT path: 192 kHz/8 channels, HDMI_IN, DEC(0), MAT_TRUEHD, isAtmos=1. The precise file identity in that capture is not independently established; an exact alternate-stream HDMI comparison was deferred.
5. Direct IEC61937 AudioTrack is filtered from app capabilities and track creation fails. Prepacked MAT over a public MAT AudioTrack (192 kHz, full tunnel) creates a track but does not yield usable frames (IEC/LE) or stalls (BE). The undocumented MAT byte contract is not established.
6. TCL Media Center's own MKV extractor drops A_TRUEHD and the player reports zero audio tracks. Static inspection confirms it reads vendor.mtk.mkv.support.truehd with default false; property unset. This does not affect independent app demuxers. Runtime MediaCodecList finds no TrueHD/MAT decoder.

Expected: supported lossless Dolby TrueHD Atmos passthrough or an Atmos-preserving MAT output from internal apps. Actual: no Atmos for recognized raw TrueHD; no useful audio for tested app-packed MAT; no native MKV audio track.

## Questions for TCL / MediaTek

- Is TrueHD Atmos from an internal Android application supported on this model and firmware? If so, what public Android API, audio encoding, input framing and output configuration are required?
- Is the IEC61937 exclusion for ordinary app tracks intentional? Could an OEM update expose a standards-compliant IEC61937 direct profile when an eARC receiver advertises support?
- Is the native Matroska TrueHD gate deliberately disabled? Can it be enabled in an official firmware update on this model?
- In Auto, why does raw TrueHD recognize MAT_TRUEHD but report Atmos=0 while HDMI MAT and internal E-AC3 JOC report Atmos=1? Is an internal decoder configuration or stream-presentation selection required?
- Is there a supported app-level route preserving the original Atmos objects, rather than PCM-only decoding or upmixing?

The public research repository contains curated evidence, reproducible sample preparation and tester source: https://github.com/saitatter/tcl-earc-passthrough-research . No proprietary libraries, commercial media or complete device logs are included. A source-specific MatMlpOnly parameter was identified but its causal role is unproven; internal bypass enum names are not public APIs. Please do not treat these as verified fixes.

Draft only. Not submitted or sent.
