# One-archive TV + ESP32 evidence bundle

`tools/collect_tv_research_bundle.py` reuses `tools/collect_tv_audio_audit.py` and optionally reads `lukasa1993/tcl-tv-toolkit/esp32/bridge.py` state and passive RX. It writes **one ZIP** under `private/research_bundles/<LABEL>_<UTC>.zip`. It does **not** run commands on the TV UART, arm ESP automation, power-cycle the TV, reboot, unlock, flash, patch or request root. The optional `--uart-seconds` reads UART **RX only**; no TV or ESP configuration changes are made. Network-based status/RX access needs the ESP bridge and its private token already provisioned in the toolkit checkout.

## Recommended C655 baseline (Windows PowerShell)

Use Python 3.9+ and up-to-date ADB. In the **research** repo root:

```powershell
git pull
adb devices -l
python .\tools\collect_tv_research_bundle.py --label C655-stock --serial "ACTUAL_C655_ADB_SERIAL" --no-esp
```

The `--serial` option can be omitted if exactly one authorized online TV is attached. This runs the full general audio collector (identity, security/slots, kernel config if readable, DT sysfs, driver binding, audio policy/ALSA, services, filtered existing audio logcat and permitted vendor binaries). Optional `--reports-only` disables binary pulls, and `--include-packages` adds package names/paths. No `adb root` or denied privileged probes are retried with escalation. The nested ADB ZIP is kept as `adb_audit.zip` inside the outer bundle, together with log files and a status manifest.

## ESP32 UART check (after commissioning and electrical verification)

The public toolkit checkout should be at `..\tcl-tv-toolkit` (or pass `--esp-toolkit PATH`). Provision / flash its bridge according to its own `esp32/README.md` **while TV UART wires are detached**. Verify voltage compatibility, the service-port pinout, continuity and common ground; initially connect only TV TX to ESP RX and GND. Power ESP independently. The script does not perform this electrical safety check for you.

For ESP-only capture, if C655 ADB is not reachable:

```powershell
python .\tools\collect_tv_research_bundle.py --label C655-uart --skip-adb --esp-toolkit "..\tcl-tv-toolkit" --esp-host "ESP_IP_OR_HOST" --uart-seconds 90
```

Start this from a host that can contact the ESP over a trusted LAN; **then manually power on the TV during the capture interval**. If the ESP is not connected/online, an archive still contains failure details. No `Enter`, `version`, automation, U-Boot command, TX or flashing is sent. `--uart-seconds` is 0 by default so an ordinary run never waits for a boot cycle. Max capture 600 s, bounded raw RX file 8 MiB, printable preview limited to 512 KiB. A local reader process is stopped after the requested time. This does not stop a TV boot or revoke any previously configured bridge automation; check its automation status separately.

To combine both ADB + ESP evidence in one ZIP:

```powershell
python .\tools\collect_tv_research_bundle.py --label C655-combined --serial "ACTUAL_C655_ADB_SERIAL" --esp-toolkit "..\tcl-tv-toolkit" --esp-host "ESP_IP_OR_HOST" --uart-seconds 90
```

ADB inventory runs first; after it finishes, the passive UART capture starts. This is best done before interrupting a normal TV boot. If C655 allows only USB debugging and ADB is not available, use `--skip-adb`; do not mistake an ADB connection problem for evidence that the ESP/UART is faulty.

To inventory a matching OTA alongside a live ADB audit, pass `--ota PATH` and optionally `--images PATH`. This adds metadata and hash-verified offline parsing via the existing collector; it does not copy full raw partition images into the ZIP. For offline-only or payload extraction workflows, use `collect_tv_audio_audit.py` directly as documented in `multi-device-audio-audit.md`.

## Bundle contents

- `manifest.json`: completion state for ADB, ESP status and UART, with missing parts indicated
- `adb_audit.zip`: inner ZIP produced by the existing multi-device collector (when successful/partial)
- `adb_audit.stdout.txt` / `adb_audit.stderr.txt`: diagnostic wrapper output
- `esp_status.stdout.txt`, `esp_automation.stdout.txt` and stderr: ESP bridge read-only state
- `uart_rx.raw`, `uart_rx_preview.txt`, `uart_rx.stdout.bin`, `uart_rx.stderr.txt`: only when capturing RX; raw bytes retained for decoding

Keep the ZIP **private**: device identifiers, firmware hashes, app paths, UART text, audio logs and proprietary TV binaries may be present. ESP token, Wi-Fi password, `config.h`, access-token file and firmware.bin are never archived by this wrapper. Review before sending anywhere.

Limitations: the wrapper has not been end-to-end tested on the owner's C655/ESP yet. An unreadable UART or denied path is a recorded diagnostic, not proof of an absent port, device or driver. The existing collector has limits of 64 MiB per selected file and 768 MiB total. ESP UART receive is only a transport test; the wrapper does not establish root eligibility, matching firmware or a safe recovery route.
