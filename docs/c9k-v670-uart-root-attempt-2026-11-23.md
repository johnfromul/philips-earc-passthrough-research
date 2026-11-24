# TCL 65C9K V670 UART/Magisk attempt

Date: 2026-11-23
Target: TCL 65C9K / T653T01 / MT5896 / `G08_4K_GB` / V8-T653T01-LF1V670 (AU04), Android 14
Method: live U-Boot console over PuTTY on COM5, Magisk v31.1 patched stock boot
Status: **Succeeded.** AVB unlocked, dm-verity disabled, Magisk v31.1 patched boot written to `boot_b`. Root was initially pending Superuser approval (the `Permission denied` notes below are from that window); it was subsequently granted and **`su -c id` now returns `uid=0(root)`** (used throughout the 2026-11-23/24 session). The GhostLock route mentioned below was not used — this is a standalone UART+Magisk root.

## Starting evidence

- The passive HDMI service-UART capture had identified U-Boot 2019.04, AVB green/locked, and active slot `_b`. It also showed the 1.8 V UART and the C9K boot chain. See [UART findings](c9k-hdmi-service-uart.md).
- The owner confirmed the TX/RX level-shifter path was electrically validated. In this session, PuTTY on COM5 visibly exchanged commands and replies with the live U-Boot prompt. The ESP Wi-Fi automation endpoint was not used for these commands; do not treat this session as proof of an automated ESP send timeline.
- Read-only U-Boot help confirmed:
  - `avb init <dev>`
  - `avb set-verity <enable|disable>`
  - `avb set-devicestate <0/1>` (`0=unlock`, `1=lock`)
  - `usb_partial_upgrade_to_emmc <file_name> <partition_name>`
  - `env set`, `saveenv`, `usb start/reset/storage`, `fatls`
- `mmc list` reported `mtk-mmc-fcie@1c660000: 0 (eMMC)`.

## Images and USB preparation

The offline Magisk boot verifier passed before flashing: Android boot v4, kernel byte-identical to stock, ramdisk changed, Magisk markers present.

| Image | Bytes | SHA-256 |
|---|---:|---|
| `c9k_v670_stock_boot.img` | 56,623,104 | `f4f5010d85c2b8d6bcda0670bdb781a51d7d509728e81b661988d2646cc9eaa1` |
| `c9k_v670_boot_b_magisk.img` | 56,623,104 | `c0d47a7485c97c2ad080a7f5d9f48ef3b9933283682b91d1f3cfb100335d7f73` |

The source images are in the git-ignored `private/c9k-stock-v670/magisk/` directory. The owner asked to format the connected USB stick. Windows identified one removable candidate, a 30 GB `Generic Flash Disk` on `D:` (previous label `ESD-USB`); it was formatted FAT32 as `TCL_C9K_V670`. Both images were copied to its root and destination SHA-256 values matched the source values above.

In U-Boot, the first `usb start` reported zero storage devices. `usb reset` rescanned both controllers and found one USB storage device. `fatls usb 0:1 /` listed:

```text
56623104  c9k_v670_boot_b_magisk.img
56623104  c9k_v670_stock_boot.img
```

The stick was removed from the TV before `reset`.

## Commands executed and observed results

Only `help`/inventory commands were used before the state changes: `help`, `help avb`, `help usb`, `help usb_partial_upgrade_to_emmc`, `help env`, `help saveenv`, `help mmc`, `help fatls`, `mmc list`, `usb start`, `usb storage`, `usb reset`, and `fatls usb 0:1 /`.

Then the following sequence was sent at the live U-Boot prompt:

```text
avb init mmc
env set devicestate unlock
avb set-devicestate 0
avb set-verity disable
saveenv
usb_partial_upgrade_to_emmc c9k_v670_boot_b_magisk.img boot_b
reset
```

Observed writes/results:

- `avb init mmc` returned to the prompt without an error message.
- `env set devicestate unlock` returned to the prompt without an error message.
- `avb set-devicestate 0` printed `Disable Flash Update Mode`, two 4,096-block MMC writes ending in `OK`, and `Saving Environment to EXT4... done / OK`.
- `avb set-verity disable` printed one MMC block write ending in `OK`.
- `saveenv` printed `Saving Environment to EXT4... done / OK`.
- Flash command: `usb_partial_upgrade_to_emmc c9k_v670_boot_b_magisk.img boot_b`.
  - U-Boot printed `** Unrecognized filesystem type **` and a one-byte probe, then identified image size `0x03600000` (56,623,104 bytes).
  - It read 52,428,800 bytes and wrote 102,400 MMC blocks (`OK`), then read 4,194,304 bytes and wrote 8,192 MMC blocks (`OK`). The prompt returned. The warning was not investigated further; the full image byte count and both writes were reported successfully.
- Target was `boot_b`, because the pre-flash active slot was `_b`. The forum example's `boot_a` was not used.
- The guide's `setenv -f EnableSelinux permissive` was not run. SELinux was not deliberately switched to permissive.

## Boot result and current state

After the USB stick was removed, `reset` was sent. The UART boot log included `Loading Kernel Image ... OK`, `Loading Ramdisk ... OK`, and `Loading Device Tree ... OK`. It also showed these warnings/errors before continuing: `Unrecognized filesystem type`, a partition-metadata read failure, and a crash-kernel/kdump read failure. No interpretation of those messages has been established.

The owner reports that Android started and now requires initial setup again, consistent with the unlock reset described in the guide. After the reset, `adb devices -l` showed no device and `adb mdns services` showed no Wireless debugging service.

## ADB and Magisk follow-up (2026-11-23)

The owner completed Android setup, enabled Wireless debugging, and approved the PC's ADB RSA key. Magisk v31.1 Manager was installed from the local git-ignored APK with `adb install -r` (`Success`) and launched. The owner accepted the app's first-run restart. After that restart, Wireless debugging was re-enabled and ADB reconnected on a new TLS port.

Read-only verification results after reconnect:

| Command/check | Result |
|---|---|
| `adb shell id` | `uid=2000(shell)` |
| `ps -A` | `magiskd` running as root; Magisk app and its root service process present |
| `/debug_ramdisk/magisk -v` | `31.1:MAGISK:R` |
| `/debug_ramdisk/su` | Exists as a link to `./magisk` |
| `/debug_ramdisk/su -c id` | `Permission denied`; UID 0 has not been established |
| `getenforce` | `Enforcing` |
| `ro.boot.vbmeta.device_state` | `unlocked` |
| `ro.boot.verifiedbootstate` | `orange` |
| `ro.boot.veritymode` | `disabled` |
| `ro.boot.slot_suffix` | `_b` |

The ordinary `adb shell su -c id` command reports that `su` is not on the shell PATH; the explicit Magisk path above reaches the binary but currently receives `Permission denied`. Magisk Superuser access for ADB/shell still needs to be enabled or approved in the TV app. SELinux has remained enforcing; no permissive setting was applied.

Current facts:

- AVB device state was set to unlocked; dm-verity was disabled.
- Magisk v31.1 patched boot was written to `boot_b`; Magisk Manager is installed and its first-run restart was accepted.
- This is a persistent Magisk boot modification, not a temporary GhostLock root; returning to stock requires restoring the stock boot, and the device must not be relocked while the patched boot is installed.
- Magisk daemon is running and identifies as version 31.1, but `/debug_ramdisk/su -c id` returned `Permission denied`; UID 0 remains unverified pending Superuser authorization for ADB/shell.
- SELinux is `Enforcing`; the permissive boot setting was omitted.
- Wireless debugging was re-enabled after Magisk's restart and ADB reconnected. Continue using the new mDNS TLS port if it changes again.
- The exact stock boot image is retained on the same USB stick and locally. The live recovery command syntax is known, but a stock reflash has not been tested. Do not relock while the patched image is installed.
- This Magisk route is separate from GhostLock. The V670 GhostLock profile remains `analysis_only`; no GhostLock exploit was run.

## Next steps at initial reconnect (superseded by follow-up below)

1. In Magisk, enable Superuser access for ADB/`shell` and grant the pending shell request if shown.
2. Re-run `/debug_ramdisk/su -c id`; proceed only after it returns `uid=0(root)`. Record `getenforce` again after the reboot.
3. Do not switch SELinux to permissive unless a specific AVC denial blocks the audio experiment.
4. Once rooted access is verified, continue with read-only audio-state collection before changing HAL/MS12/DSP behavior. Keep a stock-boot recovery path ready.

## Later Wireless ADB confirmation (2026-11-23)

A later connection over Wireless ADB succeeded on the TV's TLS ADB transport. `adb shell su -c id` returned `uid=0(root) gid=0(root) groups=0(root) context=u:r:magisk:s0`; the shell is rooted and SELinux remains `Enforcing`. This supersedes the initial root-unverified status above. Current ALSA/eARC observations are recorded in [the endpoint inventory](earc-endpoint-and-policy.md#live-wireless-adb-follow-up-2026-11-23). No playback, device-node open, mixer write, or setting change was performed during that inventory.
