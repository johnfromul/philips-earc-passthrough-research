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

## Owner boot audit result — 2026-11-20

Received stock_kernel_config_audit.json (SHA-256 cb6f2aaa620c77f343679d59548c349edd79001bc2229b4d5c241d8f9cc14e38). The owner successfully ran the offline audit against their extracted stock image; the image itself was not supplied for independent verification.

- boot.img: 56,623,104 bytes, SHA-256 f4f5010d85c2b8d6bcda0670bdb781a51d7d509728e81b661988d2646cc9eaa1; Android header v4, 4096-byte page.
- Kernel payload: 42,891,776 bytes, SHA-256 a9a9c063a6eaacc90c8aac4688e9dd06de732b930e02007f856f4b3520e773c5; raw arm64 Image; embedded configuration successfully extracted.
- CONFIG_IKCONFIG=y, CONFIG_IKCONFIG_PROC=y, CONFIG_MODULES=y, CONFIG_MODULE_UNLOAD=y, CONFIG_MODVERSIONS=y, CONFIG_MODULE_SIG=y, CONFIG_MODULE_SIG_FORCE=n, CONFIG_MODULE_SIG_ALL=y, CONFIG_CFI_CLANG=y, CONFIG_CFI_PERMISSIVE=n, CONFIG_SHADOW_CALL_STACK=y, CONFIG_DEBUG_INFO_BTF=y, CONFIG_DEBUG_INFO_BTF_MODULES=y, CONFIG_SND=y, CONFIG_SND_SOC=y, CONFIG_SND_COMPRESS_OFFLOAD=y.
- Live ADB uname: 5.15.192-android14-11 #1 SMP PREEMPT Sat Apr 25 03:50:48 UTC 2026 armv8lToybox. The owner confirmed /proc/config.gz is readable by file mode (0444; 44,424 bytes), but actual read access and comparison are pending. armv8l is consistent with the 32-bit userspace and does not contradict the ARM64 stock image.

Interpretation: build-time mandatory module signing is off, but runtime enforcement, vendor policy and verified boot remain separate. CONFIG_MODULE_SIG_ALL enables signing during module installation at build time; it does not imply every shipped module has a standard appended signature. See [Linux 5.15 module signing](https://docs.kernel.org/5.15/admin-guide/module-signing.html). MODVERSIONS, strict CFI and SCS require preserving the matching ABI/toolchain constraints. Module unloading support is not proof that the active TV audio stack may be unloaded safely. No runtime replacement attempted.

Next collect the live configuration using `adb pull /proc/config.gz private/kernel_config.gz`, then compare its selected keys with the audit. Full configuration equality, even if established, would not prove byte-identical running kernel or drivers. Collect the live audio module listing read-only, if accessible. Do not unload or load drivers, reboot, flash, change security settings or infer exploit compatibility from these results.

## Live kernel configuration and module inventory — owner follow-up

Read and parsed both owner uploads directly. `kernel_config.gz`: 44,424 bytes, SHA-256 fde39c020eba440bd4c983ddb475267cf13c1986ac4c0c67cbee61c2b8c72338; decompressed config 203,071 bytes, SHA-256 2b1df3e8c81a2603e0fd771c32da2588149b4e5701cb82f2271109429bb93fbd. All 16 selected CONFIG values from the preceding stock boot report agree with the live config, including strict CFI, SCS, MODVERSIONS, MODULE_SIG=y, MODULE_SIG_FORCE=n and compressed ALSA. CONFIG_ARM64=y and CONFIG_COMPAT=y also confirmed live. This is selected-key equality, not full config or kernel binary equality.

`kernel_modules_live.txt`: SHA-256 81eeb41bd3adbc5139cfe80ccd540212f35ed0b6190ac4504a914b17be290a25; 182 entries, all Live. All six audited stock module names appear in the running inventory (hyphens normalized to underscores).

| Module | Resident bytes | Refcount | Named holders |
|---|---:|---:|---|
| mtk_earc | 65536 | 1 | none listed |
| mtk_tvsnd_machine | 106496 | 4 | none listed |
| mtk_tvsnd_platform | 192512 | 4 | mtk_tvsnd_machine |
| mtk_tvsnd_dsp_loader | 16384 | 0 | none listed |
| mtk_tvsnd_soc | 1228800 | 3 | mtk_tvsnd_machine, mtk_tvsnd_platform, mtk_tvsnd_dsp_loader |
| audio_public_if | 16384 | 2 | audio_ipauth, mtk_tvsnd_soc |

All six have (OE) flags. Under standard Linux 5.15 semantics O means out-of-tree and E means unsigned/untrusted signature accepted when loaded (see [taint documentation](https://docs.kernel.org/5.15/admin-guide/tainted-kernels.html) and [module signing](https://docs.kernel.org/5.15/admin-guide/module-signing.html)). The inventory has 127 OE, 39 E, 16 POE. This corroborates the previous observation that the six on-disk stock binaries lack an appended signature. It does not establish that arbitrary modified modules can load now, that an unprivileged process can load them, or that the stock and installed files are identical. Resident sizes are not on-disk ELF sizes. Zero references do not prove safe unload. Do not unload the active audio stack.

The runtime holder graph corroborates the static audio dependency graph, particularly soc -> machine/platform/loader consumers and platform -> machine consumer. mtk_memory and tee also have broad non-audio consumers. Two additional loaded audio modules, mtk_aloop (61,440 B, refcount 2) and snd_aloop (36,864 B, refcount 0), are possible diagnostics leads; neither demonstrates an IEC61937 or HDMI virtual input path. Investigate their supported audio formats before drawing conclusions. No changes to authorization or content protection proposed.

Next step: inspect the stock vendor_dlkm module load lists and init configuration to determine actual early/late ordering, and audit available ALSA endpoints read-only. Prioritize the userspace route; kernel replacements remain conditional on an established development/recovery path and proven transport need. No TV commands executed or changes made in this analysis.
