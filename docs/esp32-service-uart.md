# ESP32 service UART preflight

On 2026-11-21, the Windows build, USB upload, Wi-Fi access and internal UART loopback passed with the ESP32 physically detached from both TVs. A separate logic analyzer later captured C655 boot UART output with D0 on Delock terminal 14 and its reference on terminal 11. The ESP32-to-TV path remains untested. [C655 bench results](c655-hdmi-service-uart-bench-log.md)

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

## C655 receive-only checklist

1. Review the Delock 65168 measurements. Approximately 3.3 V from pin 14 to pins 11 and 17 supports checking for an idle-high signal. The lack of unpowered continuity between 11 and 17 and to the shell terminal leaves the signal-ground reference undetermined. DC readings also leave signal direction and framing undetermined. [C655 electrical bench log](c655-hdmi-service-uart-bench-log.md)
2. Confirm the physical Service port, pin numbering from the mating face, end-to-end cable continuity and absence of shorts with power removed. Confirm the signal ground and waveform with an appropriate high-impedance probe. Check voltage peaks against the ESP32 input specifications before wiring.
3. Once those checks support the reference mapping, power down before connecting HDMI 14 to ESP32 GPIO25 RX and HDMI 11 to ESP32 GND. Leave GPIO26 TX, HDMI 2 and HDMI 18 disconnected. Keep pin 17 and the connector shell separate from pin 11. Power the ESP32 by USB. The upstream mapping comes from other TCL models and reliable readable TV UART was not established in those tests. [Reference wiring](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/docs/rooting.md#service-uart-and-esp) · [Upstream hardware test scope](https://github.com/lukasa1993/tcl-tv-toolkit/blob/d564848164a13ec49096500ee4bd3cf68098d6e5/docs/tested-devices.md)
4. With the bridge disarmed and TX physically disconnected, run the receive-only capture from the toolkit checkout. Stop with Ctrl+C. Use a fresh filename for each run and keep the raw bytes under ignored `.local/`. If the capture stays empty, record the TV state, observation interval and received-byte count. Check the reference ground, waveform and timing with TX still disconnected.

```powershell
$capture = ".local\c655-uart-$(Get-Date -Format yyyyMMdd-HHmmss).bin"
.\.venv\Scripts\python.exe esp32\bridge.py --host tcl-uart-bridge.local read --follow --save $capture
```

For a bounded capture packaged with a manifest, use the research repository's ESP-only collector mode after the same electrical checks. [Combined collector guide](combined-device-and-uart-collector.md)
