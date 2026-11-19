# V670 audio kernel module compatibility preflight

Date: 2026-11-19. Scope: audio development compatibility, not privilege escalation.

## Latest owner preflight

firmware_preflight.zip SHA-256 ee2968ca3a5e428dcada9377322782a2a441996260e42bc89bfaa1603441b00f. AU04 fingerprint, bootloader 01.01.261024, locked/green/enforcing, active slot b, Virtual A/B. Both client hashes, primary HAL and primary policy hashes match previous stock analysis. product_b-base, product_b-cow-img and product_b-src remain present; bootctl, snapshotctl and update_engine_client queries are unavailable. Actual merge state unknown. Do not change slots or flash.

## Module metadata rechecked from original stock archive

All six individual hashes agree with [kernel audit](kernel-audio-driver-audit.md). Each contains __versions and .BTF. None ends with the standard Linux appended module signature marker. This is not proof that modified modules are accepted: AVB, kernel enforcement, kABI/CRC, CFI, dependencies and early load order remain independent.

| Module | depends (modinfo) |
|---|---|
| audio-public-if | none listed |
| mtk-earc | mtktv-public-if |
| mtk-tvsnd-dsp-loader | mtk-tvsnd-soc, mtk-memory |
| mtk-tvsnd-machine | mtk-tvsnd-soc, mtk-tvsnd-platform |
| mtk-tvsnd-platform | mtk-tvsnd-soc, mtk-memory |
| mtk-tvsnd-soc | clk_dtv_util, tee, audio-public-if, mtk-memory |

All report mod_unload and modversions in vermagic; mtk-earc and the other five have different release strings (see previous audit). Exact live module hash and active kernel config remain unverified. Replacing the on-disk file after loading will not replace the loaded driver.

## Offline steps

From repository root in PowerShell, if the boot audit images and verified manifest do not already exist:

    powershell -ExecutionPolicy Bypass -File .\scripts\extract_stock_ota.ps1 -BootAuditOnly

Supply actual original V670 OTA path at prompt. Preserve existing output; extractor refuses to overwrite it. Then:

    python .\tools\audit_stock_boot.py

Output private/stock_kernel_config_audit.json. It verifies the image against the extraction report, parses standard Android boot headers v0–4, reports payload hash and format, and reads selected configuration keys from a raw or gzip payload containing IKCONFIG. LZ4/XZ detection only, not decompression. Unknown wrappers or missing config need separate investigation. This checks consistency against a local manifest, not cryptographic OTA signing or installed kernel identity. Tested synthetic v0/v3/v4 raw and gzip cases, plus mismatch rejection; not tested on actual V670 boot image. No raw kernel is emitted.

Optional read-only device check: adb shell ls -l /proc/config.gz and adb shell uname -a. If config.gz is accessible, retrieve locally via adb pull /proc/config.gz private/kernel_config.gz.

No live calls performed during this audit, no kernel changes, no device reboot or root, no exploit profile. Keep proprietary images private. The previously documented [virtual source fallback](virtual-hdmi-audio-source.md) is still a hypothesis.
