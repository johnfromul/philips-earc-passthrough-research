# Device changes applied to the C9K (2026-11-23, rooted session)

All reversible. The device is rooted (Magisk 31100, bootloader unlocked/orange, dm-verity disabled). Partition
backups are on USB `c9k_backup_20261123/` (18 partitions + SHA-256 manifest, host copy in
`private/backups_20261123/manifest.txt`).

## 1. Debloat (reversible, `pm disable-user --user 0`)

Disabled for user 0 (restore any with `pm enable --user 0 <pkg>`):

| Package | Was |
|---|---|
| com.tcl.browser | BrowseHere (browser + Firebase/Crashlytics/Analytics) |
| com.tcl.magiconnectfree | MagiConnect (Firebase analytics companion) |
| com.tcl.repairguide | Remote Diagnosis (agora screen-sharing + WRITE_SECURE_SETTINGS) |
| com.google.android.feedback | Market/Google Feedback (crash/log reporting) |
| com.tcl.smartalexa | Alexa |
| com.tcl.channelplus | ChannelPlus (Live TV) |
| com.tcl.dashboard | TCL Home / Dashboard (MQTT smart-home panel) |
| com.tcl.guard | TGuard (usage tracking + USB monitor + cloud "portal"; READ_LOGS/USAGE_STATS/QUERY_ALL_PACKAGES/INTERNET/WRITE_SECURE_SETTINGS) |
| com.tcl.exhibit | Exhibition / retail demo |
| com.netflix.ninja | Netflix |
| com.netflix.tokenmanager | Netflix token manager |
| com.disney.disneyplus | Disney+ |
| com.amazon.amazonvideo.livingroom | Prime Video |
| com.wbd.stream | HBO Max / Max |

(`com.tcl.channelplus` = TCL's live "Channel" app, already in the list above. `com.tcl.tv` is the antenna/tuner
player and was left enabled.)

Kept deliberately: `com.android.tv.feedbackconsent` (consent UI, not the collector). Firmware OTA re-enables disabled
packages — re-run after updates.

Restore all:
```
for p in com.tcl.browser com.tcl.magiconnectfree com.tcl.repairguide com.google.android.feedback \
         com.tcl.smartalexa com.tcl.channelplus com.tcl.dashboard com.tcl.guard com.tcl.exhibit \
         com.netflix.ninja com.netflix.tokenmanager com.disney.disneyplus; do
  adb shell pm enable --user 0 $p; done
```

## 2. Replacement launcher (cleanyourtv)

Built from https://github.com/gathin23/cleanyourtv (`launcher/`, Gradle, `com.gathin.tvhome`). Set up in **safe
redirect mode** (the author flags this exact model — "TCL Smart TV Pro, Android 14" — as one-way/crash-loop if the
stock launcher is disabled, so `launcherx` was **kept** as registered Home):
- **Build:** the default APK ships `stock_home_redirect` empty, so the Home-key service does nothing. Set it before
  building — in `launcher/app/src/main/res/values/config.xml`:
  `<string name="stock_home_redirect" translatable="false">com.google.android.apps.tv.launcherx</string>` — then
  `gradlew :app:assembleDebug` (JDK 21, SDK 34). Built APK saved to `private/launcher/gathin-tvhome.apk`.
- **Install + redirect:** `adb install -r` the APK, then enable its Home-key accessibility service:
  `settings put secure enabled_accessibility_services com.gathin.tvhome/com.gathin.tvhome.HomeKeyService` +
  `settings put secure accessibility_enabled 1`. With `stock_home_redirect` set, the service subscribes to
  `TYPE_WINDOW_STATE_CHANGED`, detects `launcherx` opening on Home, and jumps to `com.gathin.tvhome`. `launcherx`
  stays the registered Home/fallback. Trade-off of the safe mode: a brief flash of the stock launcher before the jump
  (eliminating it needs disabling `launcherx`, the risky one-way step, so it was not done). Note: `adb install -r`
  clears the accessibility-services setting, so re-apply the two `settings put` lines after any reinstall.
- **Build config:** the default APK ships `stock_home_redirect` empty (no redirect). It was set in
  `launcher/app/src/main/res/values/config.xml` to `com.google.android.apps.tv.launcherx` before building; the
  HomeKeyService then subscribes to `TYPE_WINDOW_STATE_CHANGED` and jumps when `launcherx` opens.
- **Boot persistence (important):** Android resets `accessibility_enabled` to 0 on every boot, and re-writing
  `enabled_accessibility_services` to its current value does not fire the rebind observer — so after a reboot the
  redirect is dead until the service list is *changed*. A Magisk boot script handles this:
  `/data/adb/service.d/cleanyourtv-home.sh` (source kept at `private/launcher/cleanyourtv-home.sh`) waits for
  `sys.boot_completed`, clears then re-sets the accessibility service list (forcing the rebind), sets
  `accessibility_enabled=1`, re-asserts `ota_disable_automatic_update=1`, and foregrounds the new launcher. Remove it
  with the launcher revert.

## 3b. Updates disabled

`settings put global ota_disable_automatic_update 1` (persists; also re-asserted by the boot script). The Google TV
system/firmware update is driven by Google Play Services (`com.google.android.gms` SystemUpdate) which cannot be
disabled without breaking the device; the global flag stops the automatic check/download. With the bootloader
unlocked and Magisk present, a full OTA would in any case fail to apply (modified boot / AVB) or strip root, so the
flag plus declining any manual prompt is the practical "updates off". Re-enable: `settings put global
ota_disable_automatic_update 0`.

Revert the launcher:
```
adb shell settings put secure enabled_accessibility_services ""
adb shell settings put secure accessibility_enabled 0
adb shell pm uninstall com.gathin.tvhome      # or: pm disable-user --user 0 com.gathin.tvhome
```
(launcherx is untouched, so Home returns to stock immediately.)

## 3. Magisk module: `earc_output_test` (optional)

`/data/adb/modules/earc_output_test/system.prop` set `ro.feature.hdmi_earc=true` at boot, making the framework expose
`HDMI_EARC` (MAT/TrueHD/DTS-HD profiles) instead of `HDMI_ARC` (JOC-max). It did **not** deliver TrueHD Atmos
(internal TrueHD is still decoded by the DSP — see [frida-live-trace-session](frida-live-trace-session.md) §11), only
lifted the ARC format cap. **Removed at the owner's request** (`rm -rf /data/adb/modules/earc_output_test` + reboot);
audio is back to stock ARC-mode routing. Recreate it from §7 of the trace doc if ever wanted.

## 4. Boot warning (orange state) — investigated, NOT modified

The "device unlocked / can't be trusted / Boot state is: Orange / dm-verity corrupt" text lives in the **`mboot`**
partition (MediaTek LK bootloader); there is no separate logo partition and no userspace/prop way to suppress it.
Removing it needs an `mboot` patch — the highest-risk partition (a bad write = hard brick, recovery only via MTK BROM
+ SP Flash Tool / test points). It is cosmetic (a few seconds at boot). **Left unmodified by recommendation.**

## 5. Instrumentation cleanup

frida-server, Frida hooks and ftrace kprobes used during the audio investigation are all removed (volatile; nothing
persisted). `ro.feature.hdmi_earc` runtime override from the pre-reboot test was reverted; the boot-time value now
comes only from the Magisk module above.
