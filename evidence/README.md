# Evidence index

This directory intentionally contains only derived data and SHA-256 manifests. Original raw logs, firmware libraries, inventory dumps and proprietary demo media stay with the device owner. The manifest records filenames from the investigation, not a download location or a promise of public availability. Hashes permit independently verifying the exact files if shared privately. No whole proprietary binaries, configuration XML files or logcat archives are distributed.

- `source-hashes.csv`: original captured inputs and ZIP checksums, including policy snapshots, app logs, native logcat and firmware inventory stages.
- `firmware-binary-hashes.csv`: per-ELF hashes from three extraction archives (archive-relative names; some original `adb pull` commands flattened paths).
- `tunnel-results.csv`: extracted TUNNEL_END and OBSERVED records joined by session ID from reports_v021.zip / tunnel_v021.log, including the earlier retained v0.2.0 runs. A reported PCM beep may be the synthetic sine tone.
- `atmos-status.csv`: allowlisted MAT/Atmos message suffixes plus original 1-based line numbers from reports_v021.zip / diagnostic_v021.txt; no device identifiers.

The chronological interpretation is in docs/investigation.md. Static symbol offsets are in docs/native-analysis.md. Run tools/analyze_logs.py and tools/scan_firmware.py on your own copies to regenerate fuller private reports.
- `iec-string-matches.csv`: selected diagnostic string file offsets from actual extracted ELFs; these are file offsets, not virtual addresses or runtime addresses.
- `policy-profiles.csv`: profile attributes transcribed programmatically from the captured primary audio policy XML.
- `probe-observations.csv`: all recorded audio-only observations retained in probe_v011.log (including earlier sessions).
