# TCL C655 HDMI Service UART — owner bench log (2026-11-21)

**State: pre-connection electrical checks only. TV UART not yet observed. No ESP32 TX, U-Boot access, bootloader interrupt, root, TV firmware write or TV configuration change is claimed.**

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
| ESP | **ESP32-WROOM-32** development board with USB connection (planned UART bridge), powered independently via micro-USB |
| Jumper wires | Dupont leads used/planned to access the screw terminals with a meter |
| Multimeter | **Lexman**, exact meter model not supplied; probes black `COM`, red `VΩ`; continuity/beeper and DC-volts ranges |
| PC | Windows PowerShell; serial port enumeration showed **COM3 and COM4**, but which one is the ESP32 **remains unverified** |
| Optional peripherals | Separate USB-UART adapter/logic analyzer may be available from the broader hardware plan, **not confirmed used** in this measurement session |
| Bridge firmware | Candidate: `lukasa1993/tcl-tv-toolkit` ESP32 Wi-Fi UART bridge (`esp32dev`); installation/build/USB upload and loopback **not yet confirmed by the owner** |

Do not assume the Delock adapter has a male HDMI connector; it does not. Neither the color of individual HDMI wires nor order of test-pad solder joints may be used as proof of connector pin assignment.

## Reference UART pin map — *not verified for Realtek C655*

The [upstream TCL TV Toolkit rooting notes](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/docs/rooting.md) document these contacts for the author's **other TCL models** (e.g. 55C6K on MediaTek):

| Nominal HDMI contact / Delock terminal | Upstream *service-mode* interpretation | Planned ESP32 connection | Current status |
|---|---|---|---|
| **14** | TV UART **TX** | ESP32 UART2 **RX = GPIO25** | DC voltage observed; UART signal **not decoded** |
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

No connection was reported from TV to ESP32 during these measurements. No ESP firmware flash was reported.

## ESP32 / Windows bring-up: planned, NOT observed

The owner reported two Windows serial port names (**COM3**, **COM4**) and asked which bridge firmware to install. The ESP was **not yet matched** to a particular COM port by a before/after unplug test. This is the current USB-identification blocker, not a failure of the ESP.

Upstream code/reference: [tcl-tv-toolkit ESP32 bridge](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/esp32/README.md), `esp32dev`, PlatformIO, `esp32/setup_bridge.py`, `esp32/bridge.py`.

Recommended host-only verification order:
1. Identify which COM port disappears/reappears with **ESP USB** unplug/replug; do not select COM3/COM4 at random.
2. Inspect the actual checked-out upstream README, `platformio.ini`, private Wi-Fi configuration handling and firmware build requirements. Confirm build target is `esp32dev`.
3. Program **ESP only** (explicit owner confirmation), while **all HDMI/TV wires are detached**.
4. Verify network bridge `status`, UART internal commissioning loopback, close commissioning and check `DISARMED` plus automation state. Never put credentials, bearer token or compiled credential-bearing firmware in the public repo.
5. **Hold before electrical hookup** until cable-to-HDMI-contact continuity and a defensible common-ground reference are established. Strongly prefer passive logic-analyzer/scope verification of TV output signal, with correct voltage/rating.
6. If safely verified, the *proposed* first live connection is **TV TX candidate pin 14 → ESP RX GPIO25** and **verified TV ground → ESP GND**, with **ESP GPIO26 TX not connected**; ESP powered via USB, no HDMI pin 18 power. Start RX capture before normal manual TV boot.
7. Record raw UART bytes, time, baud, detected boot stage and errors. **Unreadable/no data ≠ absent UART.** Do not transmit, ARM, interrupt U-Boot or run bootloader commands at this stage.

Existing read-only integrated collector: [collect_tv_research_bundle.py](../tools/collect_tv_research_bundle.py), [usage](combined-device-and-uart-collector.md). It can save an ESP-only passive UART trace after wiring is verified, but does not verify electrical connections automatically.

## Pending questions / explicit stop conditions

- [ ] Exact retail chassis label and physical HDMI **SERVICE** marking photographed/recorded.
- [ ] Delock terminal ↔ actual HDMI male cable contact mapping verified using a safe mating breakout or HDMI cable tester (no thick meter tips forced into connector).
- [ ] Shared reference/return ground for this **Realtek R75** service UART demonstrated by a safe measurement; do not infer from 0 V between distinct isolated nodes.
- [ ] TX signal on pin 14 confirmed as UART (prefer logic analyzer or oscilloscope) and levels safe for **3.3 V** ESP32 GPIO.
- [ ] TV RX pin 2 voltage/role measured and checked separately *before any later bidirectional mode*.
- [ ] Correct Windows COM port determined; USB/serial driver healthy.
- [ ] Toolkit configured/built/uploaded **to ESP32 only**; loopback works and TX automation is disarmed.
- [ ] First passive RX capture obtained and interpreted. No readable U-Boot prompt or TV boot log has been obtained so far.
- [ ] Matching C655 `R75PT01` V461 stock/recovery documentation located **before considering any root, Magisk, AVB or flashing operation**.

**No TV write path is authorized by this log.** In particular do not apply the MediaTek `boot_a` command sequence, C9K patch profile or upstream 55C6K boot image to this Realtek C655. Do not send even benign diagnostic commands until the physical UART path is validated.

## Public references

- Delock **65168**, [official product specification](https://www.delock.com/produkt/65168/merkmale.html). Product data describes HDMI female to 20-pin terminal block; it is not an authenticated electrical pin-by-pin test of the individual owner's adapter/cable.
- TCL TV Toolkit, [service UART/rooting reference](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/docs/rooting.md) and [ESP32 bridge README](https://github.com/lukasa1993/tcl-tv-toolkit/blob/main/esp32/README.md). Tested in upstream context on different TCL models; *reference only* for C655.
- Owner's captured [C655 ADB baseline](c655-realtek-baseline.md), including original private evidence hashes and known collection limitations.

### Integrity and provenance

Date: **2026-11-21**, owner-reported real-world electrical observations from the conversation. Unless noted as ADB evidence or cited upstream reference, these are **not independently reproduced laboratory measurements**. No raw meter photos, HDMI pinout photograph, ESP console log or scope traces are attached. This markdown intentionally contains no identifying serials, IP addresses, SSIDs, tokens, proprietary ROM dumps or private binaries.
