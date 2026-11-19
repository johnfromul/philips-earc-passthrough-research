# Privacy and reversible debloat track

Owner requested a privacy-oriented variant alongside the audio experiment. Separate modules, separate rollback and independent regression tests. No claims of spyware absent evidence. Do not remove SELinux, sandbox, permissions, AVB integrity checks, updater security, TLS trust, keystore, DRM or essential crash recovery merely because they are security-related. An owner-approved development boot configuration is a separate deployment issue, not a privacy feature. Do not disable watchdogs or remove core TCL services without dependency analysis.

## Initial evidence from original firmware_inventory.zip

Read-only earlier device properties:

| Key | Value | Meaning/limitation |
|---|---|---|
| ro.feature.sambatv_acr | false | Samba ACR feature flag disabled; not proof all viewing telemetry is absent |
| persist.tcl.far_field_mic_status | 1 | Far-field mic status flag; no proof of continuous collection |
| persist.tcl.feature.PerfData.bootRecordEnable | true | Boot recording feature name |
| persist.tcl.feature.PerfData.appBootRecordEnable | false | App boot recording flag |
| persist.tcl.feature.PerfData.inputRecordEnable | false | Input recording flag |
| persist.tcl.feature.PerfData.googleFixedEnable | true | Semantics unknown |
| sys.tcl.report_dream_info | true | Report-related flag; destination/content unknown |
| ro.feature.heraeye_enable | true | HeraEye diagnostics enabled |
| ro.feature.heraeye_event_record | true | Event recording flag |
| ro.feature.heraeye_app_bugreport | true | App bug report flag |
| ro.feature.heraeye_crash_hung | true | Crash/hang monitoring flag |
| ro.feature.heraeye_diagnosis_repair | true | Diagnosis/repair feature; may be important for recovery |

HeraEye may combine diagnostics with recovery/watchdog behavior. Do not remove it as a whole based on its name. Earlier processes include com.tcl.systemserver, com.tcl.tvinput, com.tcl.ttvs, com.tcl.inputmethod.international, com.tcl.airplay2 and com.tcl.miracast. Names alone establish no spying. Active factory APEX com.tcl.hera also exists; do not assume every component inside is optional. Package inventory plus manifests, traffic behavior and dependencies are needed. A permission or AppOps allow mode is not evidence of actual use.

## Phase P0

Run scripts/collect_privacy_audit.ps1. Read-only system package names and paths/UIDs, enabled/disabled state, selected AppOps, HOME resolution and targeted flags. Errors captured with exit codes; optional queries may fail. No user accounts, browsing data, personal recordings, credentials, full settings dump or network capture. Review archive before sharing and keep private.

Observe device traffic via the owner's local DNS resolver/router under matched scenarios (idle, home screen, HDMI, Moonfin, standby); compare destination domains and counts, not private request contents. DNS alone is incomplete (cached lookups, direct IP, encrypted DNS). Do not infer purpose solely from destination. Prefer existing TV opt-outs for viewing data, ads personalization, mic and usage diagnostics.

## Phase P1

After actual package inventory, classify with evidence: KEEP/core, optional feature, verified telemetry/ads candidate, unknown. Inspect per-package manifest/permissions/services and dependencies as needed; reversible user-0 disabling only of specifically identified optional packages after review, one at a time, with re-enable instructions and rollback. Keep audio regression tests, input switching, launcher/settings, remote, streaming/DRM if desired, OTA security and standby. Preserve the ability to receive official updates. Network-domain blocks only for well-understood telemetry destinations, not certificate/security/update services; verify functionality and avoid blocking shared service domains. No package removal or setting modification performed in P0. Later integrate only verified changes into a separate optional privacy profile; never bundle with a first audio experiment.
