# HDMI 2 `TvInputService` label audit — live C9K, 2026-11-24

Read-only investigation over wireless ADB (`192.168.0.82:5555`). No input, CEC setting, package, or device configuration was changed.

## Finding

The `TvInputService` entry appearing beside HDMI 2 is an extra TV Input Framework (TIF) input for a CEC-discovered device on HDMI 2. It is not the hardware HDMI 2 passthrough input itself.

`dumpsys tv_input` distinguishes the two:

- Hardware id `16`, `hdmi_port=2` maps to `com.tcl.tvinput/.TvPassThroughService/HW16` — the actual HDMI 2 input.
- A separate id, `com.tcl.tvinput/.TvPassThroughService/HDMI200004`, appears in `mHdmiInputIdMap` for CEC logical address `4`.
- The corresponding CEC device has physical address `0x2000` (HDMI 2), `port_id=2`, and an empty `display_name`.

The installed `com.tcl.tvinput` package has application label `TvInputService`. Its `TvPassThroughService` has no service-specific label in Package Manager, so the generic application label is available as a fallback. This matches the live `InputSourceManager` log, which enumerates labels in the order `HDMI 1`, `TvInputService`, `HDMI 2`, `HDMI 3`, `HDMI 4`.

At capture time the CEC device was reported in standby (`power_status=1`). HDMI 2's current port status was false, while the CEC/TIF record was still listed; this suggests the entry may persist as a standby or stale source after hot-unplug. CEC history showed port 2 connect at 23:16:19 and disconnect at 23:16:55.

## Assessment

The likely cause is that TCL's pass-through TV input service exposes a second TIF input for the CEC playback device and that device supplied no display name. The TV's source-list code then shows the package label `TvInputService` for that extra input. The actual HDMI 2 input remains `HW16` / `HDMI 2`.

If the goal is to remove the extra label, the relevant behavior is the TCL TV input/service source enumeration or its handling of unnamed CEC devices. Changing HDMI port settings is not indicated by these observations. A targeted code-level fix would need to hide or name only the CEC-derived input; disabling the shared TV input service could remove real HDMI inputs too.

## Evidence collected

- `dumpsys tv_input`: hardware and CEC input IDs, hardware-to-port map, and CEC device list.
- `dumpsys hdmi_control`: CEC physical address, port status, power status, and hotplug history.
- `dumpsys package com.tcl.tvinput` / `cmd package query-services`: `TvPassThroughService` has no explicit label; application label resource is present.
- Exact installed APK pulled read-only to the ignored private audit folder; `aapt dump badging` resolves its application label to `TvInputService`.
- `InputSourceManager` log shows the generic label between HDMI 1 and HDMI 2.
