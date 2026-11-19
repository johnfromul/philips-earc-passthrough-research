# P0 privacy inventory results — V670 GB AU04

Source: private privacy_audit.zip, 8627 bytes, SHA-256 5b6ce6718a2d2a1352d334f0e4bdff3b9f05ebe4016bad3a087b4271f564673b. Capture 2026-11-19. All eight logged ADB commands exited zero. Parsed package records using the final equals separator before package ID (updated /data/app paths themselves can contain equals signs). System packages 166, enabled 157, disabled 9; enabled and disabled sets are disjoint and match inventory. 34 packages report UID 1000. Enabled does not mean a process is currently running.

## Existing state

Disabled: com.tcl.tv.tclhome_passive, com.google.android.tungsten.setupwraith, com.tcl.esticker, com.tcl.waterfall.overseas, com.netflix.ninja, com.google.android.apps.tv.launcherx, com.tcl.logkit, com.tcl.exhibit, com.tcl.messagebox. Preserve current state rather than disabling them again. HOME resolver points to a third-party launcher; no change requested.

RECORD_AUDIO AppOps query in allow mode reports com.google.android.katniss, com.google.android.tts, com.google.android.youtube.tv. CAMERA query returns "No operations." GET_USAGE_STATS stdout is empty, exit 0. These are limited query results, not exhaustive permissions/actual-usage or native process audits. They neither prove surveillance nor prove its absence. The selected ACR, PerfData, HeraEye flags and mic status agree with the previous inventory. Do not remove HeraEye recovery/watchdog or alter privileged system services based on flags alone.

## Candidate classification (provisional)

See evidence/privacy-candidates.csv for selected package observations with UID and enabled state. Function labels and suggested categories are hypotheses based on package and installed APK names; not yet verified manifests or runtime/network evidence. No identified package is confirmed spyware.

- OPTIONAL_CANDIDATE: consumer features with distinct app identity; review manifest and dependencies, ask owner which features to retain, test separately if owner approves.
- AUDIT: feedback, ads, detection, or ambiguous features; inspect without disabling.
- KEEP: privileged/core audio, TV, network, security, update and recovery packages. UID 1000 is a strong caution signal (not proof of indispensability). Do not disable core packages or shared UID groups.

Priority for the next manifest audit: ChannelPlus, MagicConnect, SmartAlexa, browser, TCL account, Dashboard, SmartLinkCore, T_Solo, instructions, RepairGuide, GoogleFeedback, MediaTek BackgroundDetection, AdServices API, TvFeedbackConsent. scripts/collect_privacy_details.ps1 uses adb pm path and adb pull, Android SDK build-tools aapt dump xmltree/permissions/badging for each base/split APK. It preserves the APK copies under private/ and includes only text manifest metadata/hash results in privacy_details.zip; redacts randomized installation paths where possible. No APK code runs and no package/settings changes. Script has not been tested on the owner's Windows environment. Review report before sending.

For evidence of network transfer, inspect local router/AdGuard DNS logs for this TV: idle/home/HDMI/Moonfin matched observation windows. Share domain names and counts plus scenario, not URLs, payloads, client addresses or tokens. DNS results cannot independently identify the originating Android package or content sent. Avoid automatic domain blocking solely from name or a single request; retain updates/security and legitimate feature endpoints. If no confirmed telemetry destination or package is found, no removal should be represented as a privacy fix.

P0 inventory complete; targeted audit pending. No changes to TV or firmware. Audio work remains independent; no new HAL runtime test or flashable build in this commit.
