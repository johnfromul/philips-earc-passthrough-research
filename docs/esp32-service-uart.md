# ESP32 service UART preflight

On 2026-11-21, the Windows build, USB upload, Wi-Fi access and internal UART loopback passed with the ESP32 physically detached from both TVs. A separate logic analyzer captured C655 boot UART output from Delock terminal 14 with terminal 11 as reference. A later receive-only run verified readable C655 boot and wake output through the ESP32. [C655 bench results](c655-hdmi-service-uart-bench-log.md)

## Verified bridge setup

| Item | Result |
|---|---|
| Source | `lukasa1993/tcl-tv-toolkit` commit `d564848164a13ec49096500ee4bd3cf68098d6e5` |
| Build | Windows 11, Python 3.12.1, PlatformIO 6.2.0, `esp32dev`, pioarduino 55.03.38, Arduino 3.3.8 |
| USB identity | Silicon Labs CP210x, VID:PID `10C4:EA60`, driver 11.5.0.417. Unplugging removed the CP210x and left COM3 and COM4 present as Bluetooth. The driver later assigned COM5. |
| ESP32 upload | ESP32-D0WD-V3 revision 3.1, 4 MB flash. Esptool verified all four regions written to the ESP32 and the bridge boot message appeared on its USB serial port. |
| LAN | TCP 8080 reachable over 2.4 GHz Wi-Fi. `/status` returned 401 without a token and 200 with the local token. mDNS agreed with the LAN address printed on USB serial. |
| Internal UART2 | At 115200 8N1, a unique 47-byte marker including CR was written and read back exactly. |
| Handoff | `DISARMED`, idle, both boot triggers cleared, commissioning closed and loopback disabled. Authenticated status and automation checks confirmed those settings after an ESP32-only restart. |

Build and keep credentials in the toolkit checkout. Use its ignored private files and let the setup script generate the access token. Application binaries embed the Wi-Fi credentials and token. Keep those binaries, the generated configuration and raw captures local. [Pinned toolkit bridge guide](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/esp32/README.md)

```powershell
# Run from the pinned toolkit checkout.
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt platformio==6.2.0
.\.venv\Scripts\python.exe esp32/setup_bridge.py
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.local\platformio'
.\.venv\Scripts\python.exe -m platformio run -d esp32 -e esp32dev
Get-FileHash -Algorithm SHA256 esp32/.pio/build/esp32dev/firmware.bin
```

### Troubleshooting observed on Windows

- Windows initially reported Code 28 for the CP210x. Installing the signed driver corrected enumeration. Reconfirm the port by unplugging and reconnecting before any authorized ESP32 upload. [Signed Microsoft Update Catalog package](https://www.catalog.update.microsoft.com/ScopedViewInline.aspx?updateid=dea925e6-68b5-4568-9490-351d8621e7af)
- The first upload stopped before writing because the automatic reset left the ESP32 in normal boot mode (`0x13`). Holding BOOT, tapping EN and then releasing BOOT selected the ESP32 ROM download mode. [Espressif boot mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)
- Esptool 5.2.0 aborted while rendering its progress bar in a redirected Windows console using CP1252. The successful retry used `PYTHONIOENCODING=utf-8`, `--no-progress`, `--before no-reset` and 115200 baud, with the same images and offsets as the PlatformIO upload. All four region hashes verified. Commissioning persisted after reboot, so an additional OTA write was unnecessary.
- Initially, the configured SSID was absent from the Windows scan and ESP32 did not report a LAN address. The owner restarted the router. The 2.4 GHz SSID then appeared, the ESP32 joined and the bridge responded. If mDNS fails, use the current address reported by ESP32 on USB serial or confirmed by the router.

## Bridge behavior

The pinned firmware uses **UART2 at 115200 8N1**, with GPIO25 RX left floating and GPIO26 configured as TX. A task drains an 8 KiB UART driver buffer into a **32 KiB ring** even while Wi-Fi is down. Authenticated HTTP `GET /rx?since=<cursor>` on TCP 8080 returns the binary bytes and their start, end and ring-loss counts. The CP210x on USB connects to **UART0**, which outputs ESP32 startup and Wi-Fi diagnostics. UART2 capture bytes travel over the HTTP endpoint. Opening COM for a monitor is unnecessary and can toggle the ESP32 reset lines. [Pinned firmware source](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/esp32/src/main.cpp)

Fresh settings default to idle. A stored **ARM** flag can start a one-shot profile when the **ESP32** boots. The legacy stored Enter timer can also emit carriage returns after ESP32 boot. **Start now** saves and immediately starts the supplied timeline, which can send an Enter burst and profile commands. **DISARM** stops the active runner and live Enter timer. A successful response confirms that both stored boot flags were cleared. A dry run simulates the timeline without UART writes. GPIO26 remains configured as an output, and the firmware retains manual transmit endpoints. For strict receive-only on the TV side, keep GPIO26 physically disconnected and use the GET-only capture below. [Pinned bridge guide](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/esp32/README.md)

## Receive-only validation on C655

| Delock terminal | Connection for this test | Evidence |
|---|---|---|
| 14 | ESP32 GPIO25 RX after electrical checks | Boot and wake UART decoded on analyzer D0 at 115200 8N1 |
| 11 | ESP32 GND after verifying a safe common reference for this power and cable configuration | Analyzer reference in the recorded capture |
| 2 | Disconnected, including GPIO26 | Owner reports 0–3.3 V DC relative to 11. Direction, input limits and UART role remain unverified. |
| 17, 18 and separate shell GRND | Disconnected | No alternate ground or HDMI power feed is part of this setup |

1. With probes disconnected, run the default preflight below. It enumerates present CP210x COM devices and checks authenticated status and automation. The last confirmed ESP32 port was COM5. If more than one candidate appears, identify the ESP32 by unplug/replug and use the confirmed `--com COMx`. Use the ESP32's confirmed LAN IPv4 with `--host` if mDNS fails. Run on a trusted LAN. [C655 electrical bench log](c655-hdmi-service-uart-bench-log.md)
2. Confirm the Service connector, cable continuity and pin numbering with power removed. Verify a safe potential and return path between the two grounds in the actual setup and measure pin 14 peak levels with appropriately rated differential or isolated equipment. Compare with the ESP32 input limits. Prior analyzer success confirms readable data for its recorded setup. It does not establish electrical compatibility with the ESP32. Make the RX-only connections with power off, then power the ESP32 separately. [Espressif electrical characteristics](https://documentation.espressif.com/esp32_datasheet_en.pdf) · [Differential measurement guidance](https://www.tek.com/en/documents/whitepaper/abcs-probes-primer)
3. Leave the TV in standby and run the 100-second capture. Wait for `RECORDING`, then press Power on the TV remote once. Note when Android becomes visible. For a separate cold-boot trial, start capture before the owner restores normal TV power. Keep the ESP32 powered throughout and stop with Ctrl+C if needed.

```powershell
# Run from the research checkout. Defaults to the sibling tcl-tv-toolkit checkout.
py -3.12 .\tools\capture_c655_uart.py
py -3.12 .\tools\capture_c655_uart.py --seconds 100
```

Expected preflight output includes the current COM candidate, authenticated TCP 8080, 115200 8N1, GPIO25 RX, GPIO26 TX, disarmed and loopback off. At the end, the tool prints duration, byte count, ring loss, observed resets and connection errors, and saves a fresh UTC-named directory under ignored `private/uart_captures/`. `uart.bin` preserves the raw bytes, `uart.txt` escapes non-text bytes, `uart.hex.txt` gives file offsets, and `events.jsonl` associates byte ranges with **host receipt times**. The ESP32 firmware provides no per-byte timestamps or framing-error counter. `summary.json` records marker counts and possible signal-quality symptoms. The capture starts at the current ring cursor, preserving only new bytes.

The Windows preflight found COM5 and verified the authenticated live bridge state. A local simulated bridge exercised capture and error handling.

Compare with the earlier analyzer runs at the same baud and the same starting TV state. Look for `Hello world, RTD2875p`, `System Start`, `RELEASE kboot`, and `wake_source:` for a standby wake. The extended analyzer run decoded 7,026 bytes across its particular standby and wake sequence, with no D0 transitions for 54.651 seconds after the final burst. Byte counts can vary with timing and starting state. A common-start, simultaneous and electrically validated reference trace can support exact byte comparison. Otherwise, compare the ordered markers and text between them. Repeated coherent markers across equivalent boot or wake trials with zero reported ring loss give stronger reception evidence than one readable line.

- **Zero bytes:** check window timing, standby/wake state, GPIO25 continuity and the approved reference. The TV can reach Android after the serial output goes quiet.
- **Missing text:** check for ring loss, connection errors, resets and where the capture began. The ESP32 ring counters cannot count bytes lost before they reached the ring. A disconnected USB power cable can present as a LAN interruption. The recorder keeps the partial capture and retries GETs within the window. On a detectable restart it flags the discontinuity and starts a new bridge cursor. Separate epochs and gaps are recorded in `events.jsonl`.
- **Garbled text:** compare non-text counts, scope timing and voltage, and confirm idle polarity and framing against the analyzer. Binary data can also raise the non-text count. No framing error count is available.
- **401, unreachable or unsafe bridge state:** check the local token path, current LAN IP and current automation. The script exits instead of disarming or changing the firmware. Return code 0 means preflight passed or a capture collected bytes without a detected transport gap. Code 2 means an operational failure. Code 3 means a capture was empty or had a detected transport issue.

For a bounded capture packaged with an ADB manifest, the research repo also has an ESP-only collector mode. [Combined collector guide](combined-device-and-uart-collector.md)

### First C655 capture through ESP32 (2026-11-22 local time)

The owner confirmed the receive-only wiring and GPIO26 physically disconnected from terminal 2 before the capture. The 100.016 s run collected **7,033 bytes**. The bridge reported zero ring loss, resets, connection errors and unresolved cursor conflicts. The owner confirmed the Android interface appeared while recording.

The capture reproduced two `System Start` and `RELEASE kboot` sequences, standby and wake. **302 of 309 lines** matched the earlier independently decoded analyzer trace in order. These were separate runs. The differences do not by themselves demonstrate corruption. The final main burst was received about **31.313 s** after the recorder started. At about **97.234 s**, the recorder received three isolated bytes (`00 00 e8`) with no recognizable text. There were no decoded Linux console markers. The raw capture, host receipt timestamps and comparison are retained privately.

## Bidirectional test gate

Hold GPIO26 disconnected. The owner's pin 2 reading relative to terminal 11 provides a DC potential range. It leaves open whether pin 2 is a driven line, a passive strap, or a UART input. Establish its direction and routing on the **Realtek C655** from a matching schematic, documented service interface or verified board-level trace. Check the candidate line unloaded across standby, boot and Android, including idle level, swing, transients and driven states. Confirm the TV input thresholds and tolerances against GPIO26 output levels, check the shared reference and avoid contention or back-powering in either power order. Use appropriate isolation or level translation if those measurements require it. [Reference mapping from other TCL models](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/docs/rooting.md#service-uart-and-esp)

The captured Realtek `kboot` output is one-way, and no interactive prompt has been observed. A plaintext scan of the offline V461 `bootloader.img` found no `help` or `version` strings. This leaves the command set unresolved. Before any test, independently verify a naturally available prompt and a single non-mutating information command with its exact spelling and terminator from matching Realtek evidence. After separate explicit approval, the proposed test would be one manually initiated query, recording both TX and RX and checking the reply for the expected information. A bridge write count or echoed characters alone cannot confirm execution. The capture tool has no transmit action.
