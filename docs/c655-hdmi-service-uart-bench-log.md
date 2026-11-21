# TCL C655 HDMI Service UART — owner bench log (2026-11-21)

**State. The logic analyzer captured boot UART output with D0 on Delock terminal 14 and analyzer GND on terminal 11. Electrical validation of that connection is pending.** The ESP32 was commissioned separately with all TV wiring detached. [Verified ESP32 setup and passive capture](esp32-service-uart.md)

This records physical observations reported by the device owner in the ongoing research session, rather than reconstructing hardware measurements from firmware. It is intended as a handoff for future work and for Codex. Unlike a wiring tutorial, **measured data**, **toolkit reference mapping**, and **unverified assumptions** are kept separate.

## Device and existing software evidence

The TV under test is the owner's second TV, identified as a TCL **C655 series**, exact retail size/physical label **not yet recorded**. Its *already collected ADB evidence* is documented at [c655-realtek-baseline.md](c655-realtek-baseline.md):

| ADB identity (already verified from separate bundle) | Value |
|---|---|
| Board/device | `G10` |
| SoC | **Realtek RTD2875P**, hardware `rtd6748` |
| Firmware | `V8-R75PT01-LF1V461` |
| Android / kernel | Android 12, `5.4.274+` |
| Active slot | `_a` |
| Security | AVB locked/green, SELinux Enforcing |

The prior C655 bundle contained **no ESP32 status or UART capture**. It also does **not** demonstrate a working TrueHD playback test. Realtek HAL configuration and recovery requirements must be investigated independently; this model is **not** the C9K `MT5896/T653T01/V670` platform. Do not reuse C9K firmware, root commands, binary offsets or boot images.

The service port is believed to be **HDMI 2 (SERVICE)** based on C655 documentation and the planned procedure; the owner's exact chassis port label has **not** been independently photographed/confirmed in this record. Confirm it physically before connecting.

## Equipment and host state reported

| Component | Identification / observed state |
|---|---|
| TV | TCL C655 series; Realtek R75PT01 V461 per ADB baseline |
| Service connector adapter | **Delock 65168**, HDMI-A **female** to **20 screw terminals** (HDMI contacts 1–19 plus a separate `GND`/`GRND` shell terminal) |
| Cable | Passive copper HDMI male–male cable (owner explicitly confirmed **passive**, not optical/active); one end attached to the Delock during tests |
| ESP | **ESP32-WROOM-32** development board running the UART bridge, powered independently via micro-USB |
| Jumper wires | Dupont leads used/planned to access the screw terminals with a meter |
| Multimeter | **Lexman**, exact meter model not supplied; probes black `COM`, red `VΩ`; continuity/beeper and DC-volts ranges |
| PC | Windows PowerShell. ESP32 confirmed on **COM5** via Silicon Labs CP210x USB. COM3 and COM4 are Bluetooth. |
| Optional peripherals | Separate Silicon Labs CP210x USB–UART adapter on **COM6**. An 8-channel logic analyzer has yet to be identified. |
| Bridge firmware | `lukasa1993/tcl-tv-toolkit` ESP32 Wi-Fi UART bridge (`esp32dev`). USB upload and internal loopback verified. |

Do not assume the Delock adapter has a male HDMI connector; it does not. Neither the color of individual HDMI wires nor order of test-pad solder joints may be used as proof of connector pin assignment.

## Reference UART pin map — *not verified for Realtek C655*

The [upstream TCL TV Toolkit rooting notes](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/docs/rooting.md) document these contacts for the author's **other TCL models** (e.g. 55C6K on MediaTek):

| Nominal HDMI contact / Delock terminal | Upstream *service-mode* interpretation | Planned ESP32 connection | Current status |
|---|---|---|---|
| **14** | TV UART **TX** | ESP32 UART2 **RX = GPIO25** | Boot UART decoded at **115200 8N1** in `test2.sr`, with analyzer D0 on terminal 14 and GND on terminal 11 |
| **2** | TV UART **RX** | ESP32 UART2 **TX = GPIO26** | No voltage / TV-RX function verified; **do not connect for passive capture** |
| **11** | UART **GND** in upstream example | ESP32 **GND** | Appears near 0 V to pin 17, but isolated in continuity tests; **ground/reference not proven** |
| **17** | Standard HDMI DDC/CEC ground | None proposed | Distinct from terminal 11 and shell in unpowered continuity measurements |
| **GRND / GND** (20th terminal) | Connector shell / shield, not one of 19 signal pins | None proposed | Owner confirmed continuity to metal HDMI shell only |
| **18** | Standard HDMI +5 V | **Never connect to ESP32 3.3 V / power rail** | Not probed in this session |

In normal HDMI, pin 11 is TMDS clock shielding, pin 17 is DDC/CEC ground, pin 14 is a reserved/utility signal depending on mode, and pin 2 is TMDS Data2 shielding. The UART role is a **vendor service-mode repurposing**, not generic HDMI. Electrical checks on C655 so far are compatible with, **but do not establish**, that repurposing.

Upstream UART setting is **115200 8N1** on ESP32 UART2 (GPIO25 RX / GPIO26 TX); this is the *reference configuration*, not a confirmed C655 serial baud rate.

## Original unpowered continuity results

Owner used the Lexman in beeper/continuity mode, with TV and ESP32 **disconnected**; later tests had the **passive HDMI cable attached to the Delock with its far end unplugged**.

| Probe points (Delock screw terminals unless indicated) | Owner observation | Interpretation / limitation |
|---|---|---|
| `GRND` ↔ metal shell of the HDMI female connector | **Beeps** | Extra terminal connected to metal connector shell |
| `GRND` ↔ pin **11** | **Does not beep** | No low-ohmic path detectable by continuity beeper |
| `GRND` ↔ pin **17** | **Does not beep** | No low-ohmic path detectable |
| Pin **11** ↔ pin **17** | **Does not beep** | Distinct contacts under this test; not a ground verification |
| Pin **14** ↔ pin **11** | **Does not beep** | No detected short between these contacts, with passive cable attached |
| Pin **14** ↔ pin **2** | **Does not beep** | No detected short between these contacts, with passive cable attached |

Owner reported that the continuity test from the separate `GRND` terminal to connector shell worked; meter self-test (probe-to-probe), threshold in ohms, actual HDMI-cable pin-to-pin mapping, and oscilloscope/logic-analyzer validation were **not recorded**.

**Critical interpretation:** `11` and `17` are *not* proven electrically the same ground merely because a later voltmeter measurement between them was near zero. A high-impedance DC meter can show 0 V across distinct or floating nodes; lack of continuity is a caution for shared-ground assumptions. Do **not** replace pin 11 with the extra `GRND` terminal by guesswork, nor short 11, 17 and shell together.

## Powered DC voltage observations (ESP32 disconnected)

Owner attached the passive cable/Delock to the presumed C655 Service HDMI socket, powered TV and used Lexman **DC voltage** mode. Polarity is stated as **red = first, black = second**:

| Red probe | Black probe | Reading reported | Evidence strength |
|---|---|---|---|
| HDMI terminal **14** | terminal **11** | approximately **+3.3 V** | User measurement |
| HDMI terminal **14** | terminal **17** | approximately **+3.3 V** | User measurement |
| HDMI terminal **17** | terminal **11** | approximately **0 V**, fluctuating to around **0.1 V** when probes/wires moved | User measurement; contact/noise possible |

No scope traces, UART decoding, reference-to-chassis voltage, GPIO input current or independent cable continuity measurements were recorded. The **3.3 V reading is a useful candidate idle UART-high voltage**, *not* proof of UART or final authorization to connect an ESP32 GPIO. The difference between pins 11 and 17 is low in DC potential, but they had no measured continuity when unpowered.

The ESP32 was detached during these measurements. The [structured readings](../evidence/c655-hdmi-service-electrical-2026-11-21.json) preserve the equipment state as reported during the electrical checks.

## ESP32 commissioning

COM5, the USB upload and authenticated access on port 8080 were verified while the ESP32 was detached from the TV. The bridge echoed the internal test marker, commissioning was closed, and an ESP32-only reboot confirmed it remained disarmed. The next step is to verify the common reference and waveform before any passive hookup. [Commissioning results and passive capture](esp32-service-uart.md) · [Combined collector guide](combined-device-and-uart-collector.md)

## USB-only check for PulseView

Unplugging the second device removed **COM6** and left the ESP32 on **COM5**. Windows identified COM6 as a Silicon Labs CP210x USB–UART bridge (`10C4:EA60`). The owner reported pin labels `DCD`, `DSR`, `RTS`, `CTS` and `RST`. The first four are serial control lines. PulseView 0.4.2 x64 showed only `Demo device`. The Windows driver stayed in place and the test used USB only. [Silicon Labs CP210x driver reference](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers)

The USB–UART adapter can receive decoded bytes once a compatible signal and reference ground are established. To inspect digital transitions and bit timing on the candidate C655 UART pin in PulseView, use a supported USB logic analyzer, for example an 8-channel 24 MHz FX2 model with inputs marked `CH0–CH7` or `D0–D7` and sigrok `fx2lafw` support. Confirm the exact model, input-voltage limits and ground reference before connecting probes to the TV. [sigrok FX2 device list](https://sigrok.org/wiki/Fx2lafw)

## Pending questions / explicit stop conditions

- [ ] Exact retail chassis label and physical HDMI **SERVICE** marking photographed/recorded.
- [ ] Delock terminal ↔ actual HDMI male cable contact mapping verified using a safe mating breakout or HDMI cable tester (no thick meter tips forced into connector).
- [ ] Shared reference/return ground for this **Realtek R75** service UART demonstrated by a safe measurement; do not infer from 0 V between distinct isolated nodes.
- [ ] Measure the waveform peaks at the observed pin 14 TX connection and verify the analyzer and ESP32 input-voltage limits.
- [ ] TV RX pin 2 voltage/role measured and checked separately *before any later bidirectional mode*.
- [x] First passive RX capture obtained and boot output decoded. An interactive prompt remains unverified.
- [ ] Matching C655 `R75PT01` V461 stock/recovery documentation located **before considering any root, Magisk, AVB or flashing operation**.

**No TV write path is authorized by this log.** In particular do not apply the MediaTek `boot_a` command sequence, C9K patch profile or upstream 55C6K boot image to this Realtek C655. Do not send even benign diagnostic commands until the physical UART path is validated.

## Public references

- Delock **65168**, [official product specification](https://www.delock.com/produkt/65168/merkmale.html). Product data describes HDMI female to 20-pin terminal block; it is not an authenticated electrical pin-by-pin test of the individual owner's adapter/cable.
- TCL TV Toolkit, [service UART/rooting reference](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/docs/rooting.md) and [ESP32 bridge README](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/esp32/README.md). Tested in upstream context on different TCL models; *reference only* for C655.
- Owner's captured [C655 ADB baseline](c655-realtek-baseline.md), including original private evidence hashes and known collection limitations.

### Integrity and provenance

Date: **2026-11-21**, owner-reported real-world electrical observations from the conversation. Unless noted as ADB evidence or cited upstream reference, these are **not independently reproduced laboratory measurements**. No raw meter photos, HDMI pinout photograph, ESP console log or scope traces are attached. This markdown intentionally contains no identifying serials, IP addresses, SSIDs, tokens, proprietary ROM dumps or private binaries.


## Update: Saleae-compatible logic capture and successful full TV boot (2026-11-22)

**Supersedes earlier statements above that no C655 UART output has been captured.** The owner supplied PulseView/sigrok recordings `test.sr`/`test.pvs` and `test2.sr`/`test2.pvs` in the research conversation. They are **not committed** to this public repository. The assistant produced local derived text `c655_uart_boot_decoded.txt` and `c655_uart_test2_decoded.txt`; the extracts below were checked against the local decoded text. Raw capture should be retained privately and re-decoded independently before making security or pinout conclusions.

### Instruments and USB enumeration

- A separate **8-channel 24 MHz Saleae Logic-compatible USB logic analyzer** was confirmed by the owner and configured in PulseView (distinct from the **CP210x USB–UART adapter on COM6**, which PulseView showed only as a demo device).
- The analyzer enumerated as USB VID/PID `0925:3881`. Installing WinUSB cleared Windows Code 28. PulseView 0.4.2 identified it as **Saleae Logic** via `fx2lafw` and completed a five-second USB-only capture at 2 MHz before any TV probes were connected.
- PulseView initially froze on Start Acquisition; a smaller USB-only test subsequently worked.
- Analyzer inputs are physically numbered **1–8**; PulseView records them as **D0–D7**. First input was used for the capture. Do not assume a universal clone pinout without checking the actual hardware.
- Capture metadata in `test2.sr` gives a **2 MHz sample rate**, **8 digital probes** and **one byte per sample**. The raw logic chunks total **80,793,600 samples** (40.3968 s). D0 carried the boot output.
- Prior suggestions of 40 M samples / 20 s describe a *recommended configuration*, **not** the actual length of `test2.sr`.

### Ground-reference / safety observations (owner-reported)

The laptop was initially connected to its charger. With TV on, owner measured between analyzer GND and the Delock `GRND` shell terminal:
- On DC 20 V range, unstable readings around 5 V then `OL` with charger attached.
- With laptop charger removed, a small negative DC reading below 1 V was observed.
- With laptop on battery, AC 200 V range initially showed **54.5 V AC** while other TV connections were still present.
- After unplugging other TV HDMI/coax/Ethernet connections, leaving only power and the Service adapter, AC was **0.5 V** and DC was **0.6 V**.
- These high-impedance multimeter readings leave safe shared ground, allowable ground current and analyzer input-voltage tolerance unverified. The owner confirmed the analyzer connections for `test2.sr`. Isolation and peak waveform voltage remain undocumented.

### Capture results and decoded boot sequence

For `test2.sr`, the owner confirmed **analyzer D0 to Delock terminal 14** and **analyzer GND to Delock terminal 11**. Independent decoding of the raw D0 samples at **115200 8N1** recovered the boot messages, standby and the wake sequence. This establishes boot UART output at breakout terminal 14 for the recorded setup. The electrical suitability of terminal 11 as a shared ground and the mapping to a Linux tty remain open.

First recording `test.sr`: initial ROM/auxcode/kboot messages followed by `****Standby mode****`. Second recording `test2.sr`: ROM/auxcode/kboot, standby, IR wake event, then another ROM/auxcode/kboot sequence. Selected exact text from the supplied derived logs:

```text
Post romcode git ver:128eb2380694b93f (Apr  1 2024 22:52:16)
Hello world, RTD2875p_CN2_TV030!
****System Start****
****RT2875P_GMC****
Auxcode git ver:488467e316058bc4 (May 15 2025 11:15:40)
flash_type = 2, secure_type = 2
choose active index 0
part name bootloader ative_slot _a real part name bootloader_a
---RELEASE kboot 11:15:30 May 15 2025 ---
get FW_TZFW ... ok!
get FW_VKERNEL ... ok!
get FW_VKERNEL2 ... ok!
get FW_AKERNEL ... ok!
****Standby mode****
IR(0xab054f00)
>Ir:0xab054f00
Ir_match.
wake
non-STR, led off
EMCU final!
wake_source:0xab050003
```

The shortened `get FW_... ... ok!` lines above are **illustrative abridgments** of longer flash-offset and image-size lines in the decoded text, not verbatim lines. The derived text also contains `[INFO]RPMB auth key is ready`, `ATF` messages and repeated DRAM/device-tree selections. Avoid treating the serial log as proof that any privileged command interface exists.

Independent decoding directly from `test2.sr` placed the first and last decoded bytes at **3.220321 s** and **11.583718 s**. The `Standby mode` message began at **8.183063 s**, and the first decoded IR wake event at **9.963453 s**.

**Owner explicitly confirmed the C655 fully reached its normal Android/Google TV interface after pressing power on the remote.** Lack of later kernel output on the observed channel is therefore not evidence of a failed boot. Kernel console could be disabled, redirected or filtered; U-Boot prompt, interactive input and Android console have **not** been demonstrated.

### Next actions for Codex / handoff

1. Retain the original `test.sr` and `test2.sr` recordings privately alongside the capture settings.
2. Read-only ADB on the running C655 returned `Permission denied` for both `/proc/cmdline` and `/proc/consoles`. The Android property `ro.boot.console` returned `ttyS1`. The active kernel consoles and their wiring remain unknown.
3. Confirm peak signal levels, analyzer input ratings and a safe reference before comparing a longer passive capture spanning standby, remote wake and Android boot. Keep TV RX and ESP32 TX disconnected.
4. If ADB shows no accessible kernel console, investigate stock Realtek R75PT01 boot image / DT / kernel cmdline **offline** using matching firmware; do not reuse MediaTek C9K methods.
5. No UART writes, boot interruption, root attempt, U-Boot commands, AVB changes, firmware flashing or pin-2 hookup are authorized. The ESP32 bridge remains a separate commissioned but not yet TV-validated path; do not infer that its RX connection was tested by the Saleae capture.

**Current status.** Boot UART output was decoded with D0 on terminal 14 and analyzer GND on terminal 11. Normal Android boot was confirmed. The active Linux console and safe shared ground still need verification.

### Extended passive capture through Android startup (2026-11-22)

The owner confirmed the setup was suitable for repeating the passive test. PulseView recorded **67.6864 s** at **2 MHz** with UART RX on D0. The owner woke the TV with the remote and confirmed the Android interface appeared while recording. The exact screen-appearance timestamp was not logged.

Independent decoding at **115200 8N1** recovered **7,026 bytes**. Standby output began at **6.779 s**, the IR wake event at **11.418 s**, and the second `kboot` banner at **12.114 s**. The last D0 transition was at **13.035 s**, followed by **54.651 s** with no D0 transitions before the capture stopped. The decoded output contained no `Linux version`, `earlycon:`, `printk:` or `init:` markers. This trace establishes a silent tail on the observed service channel while Android reached its UI. The routing and active state of the kernel console remain open.

The original recording, PulseView setup and timestamped decode are retained privately. The `.sr` archive SHA-256 is `7315c413335eddda84354eb39c540f6cb26de1cb91f92f98a825967592e9a69d`.

A follow-up read-only ADB check resolved `/sys/class/tty/ttyS1/device` to `../../../1801b100.serial`. This matches the controller address in the stock `vendor_boot` device tree's `stdout-path` (`/serial@1801B100`). Reading `/sys/class/tty/console/active` returned `Permission denied`. The controller mapping is established for the running C655. Its active Linux console state and connection to HDMI terminal 14 remain unverified.

### Offline comparison with the R75PT01 V461 OTA

The analyzed BLOCK OTA has SHA-256 `c7d3d8866c49a6084ceb4d710d28f1382f299b23a4c59b3933777cd3cc3bce5d`. Its metadata names software version `V8-R75PT01-LF1V461` and target fingerprint `G10_4K_US_NF/AS50`. The updater's product list includes the TV's `G10_4K_GB`. The installed fingerprint was recorded as `G10_4K_GB/AS12`. Vendor signature and byte-for-byte identity of the installed images remain unverified.

| Evidence | Console configuration |
|---|---|
| `boot.img` | Android v4 header cmdline is empty. Embedded kernel config contains `CONFIG_CMDLINE="console=ttyS0,115200 earlyprintk"` and `CONFIG_CMDLINE_FROM_BOOTLOADER=y`. Forcing the built-in cmdline is disabled. |
| `vendor_boot.img` | Base `rtk,rtd6748` DT has `earlycon` in `/chosen/bootargs` and `stdout-path=/serial@1801B100`. |
| `dtbo.img` | Its single archived FDT names `rtk,rtd299s` and includes `androidboot.console=ttyS0 console=ttyS0,115200`. Runtime selection or application on this C655 has not been established. |
| Live C655 | `ro.boot.console=ttyS1`, with `ttyS1` linked to `1801b100.serial`. Shell access to the effective cmdline and active console lists was denied. |

These observations identify the Linux controller used by `ttyS1` and the service terminal carrying the observed bootloader output. The physical link between the controller and that HDMI terminal remains unverified.
