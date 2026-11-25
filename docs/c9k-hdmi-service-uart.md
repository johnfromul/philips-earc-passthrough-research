# TCL C9K V670 HDMI service UART

Device: TCL 65C9K, chassis T653T01, product `G08_4K_GB`, MT5896, Android 14, firmware V8-T653T01-LF1V670 (GB AU04).

## Verified observations (2026-11-23)

- HDMI service pin 14 carried the bootloader console. A clean capture contained 11,060 bytes across two boot cycles.
- The live bootloader is U-Boot 2019.04. A PuTTY session on COM5 at 115200 8N1 reached the `=>` prompt, accepted commands, and returned output.
- Live help confirmed `avb init`, `avb set-devicestate`, `avb set-verity`, `env set`, `saveenv`, USB storage/listing commands, and `usb_partial_upgrade_to_emmc <file_name> <partition_name>`.
- **Update (2026-11-25):** The earlier 1.8 V logic-level claim is **not conclusively established**. The owner confirms a **USB–UART adapter configured for 3.3 V successfully exchanged commands and responses with U-Boot** on this exact C9K (PuTTY COM5, 115200 8N1). This proves that the particular hookup worked, **not** that the TV RX pad is specified to tolerate a direct 3.3 V signal; the precise TV-side signal voltage and whether the final setup included voltage translation were not independently documented.
- Pin 14 is TV TX; pin 2 is TV RX; pin 11 was used as the signal reference. HDMI pin 18 carries +5 V and must not be connected to UART equipment.
- The active slot was `_b`. The exact state-changing commands and their observed results are recorded in the [chronological UART/Magisk attempt log](c9k-v670-uart-root-attempt-2026-11-23.md).
- The passive capture utility is [tools/serial_capture.ps1](../tools/serial_capture.ps1). It only reads serial data and saves local captures under the git-ignored `private/` directory; interactive U-Boot commands in the attempt log were sent separately through PuTTY.

## Current device state

AVB was unlocked, dm-verity disabled, and the Magisk v31.1 patched image was written to `boot_b`. After Android setup and installing Magisk Manager, **root is confirmed**: `su -c id` returns `uid=0(root)` (verified repeatedly in the 2026-11-23/24 session). `ro.boot.vbmeta.device_state=unlocked`, `verifiedbootstate=orange`, `veritymode=disabled`, slot `_b`, SELinux `Enforcing`.

This is a persistent Magisk boot modification. Do not relock while the patched boot is installed; the stock boot image is retained for recovery.

## Electrical caution

**Do not interpret a working 3.3 V adapter as electrical proof that the TV's RX pin is 3.3 V tolerant.** Check the actual TV-side UART voltage and permitted input range before driving pin 2. Until confirmed, use an appropriate level-shifting interface rather than assuming direct 3.3 V is safe. Never drive 5 V into either UART signal pin or connect HDMI pin 18 (+5 V) to UART equipment. Make or break connections only with the TV powered off. The UART/Magisk attempt log is the source of truth for the commands used and recovery status.
