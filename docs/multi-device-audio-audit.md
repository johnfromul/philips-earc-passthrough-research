# Multi-device TCL audio evidence collector

The C655 range has multiple board families; its commercial model name does not establish SoC or firmware compatibility with C9K. No rooting, flashing, rebooting, module loading, mixer writes, app installation, or settings changes are included. Run with normal authorized ADB shell access, no root required. Keep all artifacts private.

## Windows: live capture

From repository root (Python 3.9+, current adb on PATH):

    adb devices -l
    python tools/collect_tv_audio_audit.py --label C655-stock --logs

Specify --serial with the actual ADB device identifier when multiple TVs are connected. First use --reports-only if bandwidth is limited; omit it to collect readable candidate audio binaries/configuration/modules. --logs saves filtered existing logcat without clearing the buffer. Run again with labels C655-joc, C655-truehd, C655-hdmi while playing the same test samples manually if comparing routes. Optional --privacy-inventory adds installed package paths; no app-private data, credentials, raw device partition copies, or key extraction.

The output is a timestamped ZIP and an unpacked directory under private/device_audits. identity.json, reports/, live_candidates.json, live_files/, collection_status.json; each command has exit status and separate stderr. Some compound commands can exit 0 despite nested failures. Candidate selection is heuristic and size-limited (64 MiB/file, 768 MiB combined); denied/missing files are not hardware absence. Optional diagnostics and logs may contain device identifiers, network information, app names or media paths: review locally before sharing. Fingerprint checked again at end, but not proof of binary consistency across all readbacks.

## Offline stock package

    python tools/collect_tv_audio_audit.py --offline-only --label C655-ota --ota "C:\path\C655-stock.zip"

For a full CrAU v2 OTA, add --payload-dumper "C:\path\payload-dumper-go.exe" to extract available supported partitions to a new sibling _images directory, or add --images "C:\path\already_extracted" to inspect existing raw images. Supply a trusted local executable yourself; the collector does not download one. The image metadata and hash are compared against the supplied OTA manifest. No claim of signature authenticity, exact model/installed-build compatibility, or recovery support. Images are never included in the ZIP. Legacy ZIP/PKG without a supported full payload can only be inventoried; parsing proprietary formats needs separate work. Never use C9K images on C655.

For offline filesystem extraction (normal WSL/Linux user with erofs-utils and e2fsprogs; enough disk space), use:

    python3 tools/collect_tv_audio_audit.py --offline-only --label C655-stock-files --ota /mnt/c/path/C655-stock.zip --images /mnt/c/path/extracted --extract-filesystems

This reuses existing read-only parser/extraction helpers without their V670-specific hash gates. Individual images are verified against the supplied manifest before parsing. Boot kernel/config (raw or gzip; no general compressed kernel decoder), vendor_boot DT candidates, Android DTBO, audio-related ELF/KO/config files and selected APEX payloads are included when supported. No overlay application. stock_live_comparison.json only compares matching paths collected within the same run; use hashes for cross-session comparisons. No mount or device access in offline-only mode. Extraction can use substantial temporary disk space in the WSL home directory; keep original files intact. No unattended privileged steps.

## C655 versus C9K

Start with live capture and the exact model size, installed product firmware string from TV settings, and physical HDMI service label. Compare board, SoC, build/ABI, audio policy, HAL names and hashes, active ALSA/driver binding and TrueHD/JOC behavior. Do not transplant C9K patch offsets or assume HDMI service UART electrical compatibility. The script is an initial implementation inspected statically; no end-to-end device or image tests executed in authoring environment.
