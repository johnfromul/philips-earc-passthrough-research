# TCL C9K V670 HDMI service UART

Device: TCL 65C9K, chassis T653T01, product `G08_4K_GB`, MT5896, Android 14, firmware V8-T653T01-LF1V670 (GB AU04).

## Verified observations (2026-11-23)

- HDMI service pin 14 carried the bootloader console. A clean capture contained 11,060 bytes across two boot cycles.
- The live bootloader is U-Boot 2019.04. A PuTTY session on COM5 at 115200 8N1 reached the `=>` prompt, accepted commands, and returned output.
- Live help confirmed `avb init`, `avb set-devicestate`, `avb set-verity`, `env set`, `saveenv`, USB storage/listing commands, and `usb_partial_upgrade_to_emmc <file_name> <partition_name>`.
- The service UART uses 1.8 V logic. Pin 14 is TV TX; pin 2 is TV RX; pin 11 was used as the signal reference. HDMI pin 18 carries +5 V and must not be connected to UART equipment.
- The active slot was `_b`. The exact state-changing commands and their observed results are recorded in the [chronological UART/Magisk attempt log](c9k-v670-uart-root-attempt-2026-11-23.md).
- The passive capture utility is [tools/serial_capture.ps1](../tools/serial_capture.ps1). It only reads serial data and saves local captures under the git-ignored `private/` directory; interactive U-Boot commands in the attempt log were sent separately through PuTTY.

## Current device state

AVB was unlocked, dm-verity disabled, and the Magisk v31.1 patched image was written to `boot_b`. The user completed Android setup, installed Magisk Manager, accepted its first-run restart, and re-enabled Wireless debugging. ADB is connected; SELinux is `Enforcing`, but `/debug_ramdisk/su -c id` returns `Permission denied`, so UID 0 is not yet confirmed. This is a separate, persistent Magisk route; the V670 GhostLock profile remains `analysis_only`.

The audio investigations dated 2026-11-19 describe the pre-unlock baseline. They are not measurements of the current boot state. No audio HAL or IEC61937 modification has been made.

## Electrical caution

Use a level interface appropriate for the TV-side 1.8 V UART. Never drive 3.3 V or 5 V directly into TV pin 2, and never connect HDMI pin 18 to UART equipment. Make or break connections only with the TV powered off. The UART/Magisk attempt log is the source of truth for the commands used and the recovery status.
