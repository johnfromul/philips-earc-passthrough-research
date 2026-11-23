#!/usr/bin/env python3
"""Capture C655 UART RX over the commissioned ESP32 bridge using HTTP GET only."""
import argparse
from datetime import datetime, timezone
import http.client
import ipaddress
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
PORT = 8080
RING_BYTES = 32768
MAX_CAPTURE_BYTES = 8 * 1024 * 1024
MAX_SECONDS = 600
MARKERS = (
    b"Hello world, RTD2875p", b"System Start", b"RELEASE kboot",
    b"Standby mode", b"wake_source:", b"Linux version", b"earlycon:", b"init:",
)
READ_ERRORS = (OSError, http.client.HTTPException)


class CaptureError(Exception):
    pass


def now_utc():
    return datetime.now(timezone.utc)


def detect_port(requested):
    # Enumeration only. Opening the CP210x can toggle RTS/EN, and USB carries UART0.
    command = r"""
Get-PnpDevice -PresentOnly -Class Ports -ErrorAction Stop |
    Where-Object { $_.Status -eq 'OK' -and $_.InstanceId -like 'USB\VID_10C4&PID_EA60\*' } |
    ForEach-Object { if ($_.FriendlyName -match '\((COM[0-9]+)\)$') { $Matches[1] } }
"""
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
            capture_output=True, text=True, timeout=12, check=True,
        )
    except (OSError, subprocess.SubprocessError):
        raise CaptureError("Windows CP210x enumeration failed.") from None
    ports = sorted({line.strip() for line in result.stdout.splitlines()
                    if re.fullmatch(r"COM[0-9]+", line.strip())})
    if requested:
        if requested not in ports:
            raise CaptureError("The specified COM port is not a healthy CP210x device.")
        return requested
    if len(ports) != 1:
        raise CaptureError(
            f"CP210x candidates: {', '.join(ports) or 'none'}. "
            "Confirm the ESP32 by unplug/replug, then supply --com if needed."
        )
    return ports[0]


def lan_address(host):
    try:
        address = ipaddress.IPv4Address(host)
    except ipaddress.AddressValueError:
        if host.lower().rstrip(".") != "tcl-uart-bridge.local":
            raise CaptureError("Use tcl-uart-bridge.local or a confirmed private IPv4 address.") from None
        try:
            answers = {entry[4][0] for entry in socket.getaddrinfo(
                host, PORT, socket.AF_INET, socket.SOCK_STREAM)}
        except OSError:
            raise CaptureError("mDNS lookup failed. Use the ESP32's confirmed LAN IPv4 address.") from None
        if len(answers) != 1:
            raise CaptureError("mDNS resolved ambiguously. Use the ESP32's confirmed LAN IPv4 address.")
        address = ipaddress.IPv4Address(answers.pop())
    if not any(address in ipaddress.IPv4Network(net)
               for net in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")):
        raise CaptureError("The bridge address must be on a private IPv4 LAN.")
    return str(address)


class Bridge:
    def __init__(self, address, token):
        self.address = address
        self.token = token

    def _get(self, path, timeout):
        # Direct socket, with neither system proxies nor redirect following.
        client = http.client.HTTPConnection(self.address, PORT, timeout=timeout)
        try:
            client.request("GET", path, headers={"Authorization": "Bearer " + self.token})
            with client.getresponse() as response:
                if response.status == 409 and path.startswith("/rx?"):
                    return None, None
                if response.status != 200:
                    raise CaptureError(f"Bridge returned HTTP {response.status}.")
                body = response.read(RING_BYTES + 1)
                if len(body) > RING_BYTES:
                    raise CaptureError("Bridge response exceeded the expected buffer size.")
                if response.length:
                    raise http.client.IncompleteRead(body, response.length)
                return body, response.headers
        finally:
            client.close()

    def state(self, timeout=3):
        try:
            status = json.loads(self._get("/status", timeout)[0])
            automation = json.loads(self._get("/automation", timeout)[0])
            if not isinstance(status, dict) or not isinstance(automation, dict):
                raise ValueError
            identity = status["firmware"], status["baud"], status["rx_gpio"], status["tx_gpio"]
            if identity != ("tcl-uart-bridge-2", 115200, 25, 26):
                raise CaptureError("Unexpected bridge firmware, baud or GPIO assignment.")
            safe = (status["loopback"] is False and status["sequence_armed"] is False
                    and status["interrupt_active"] is False and status["next_boot_enter_seconds"] == 0
                    and automation["armed"] is False and automation["active"] is False)
            if not safe:
                raise CaptureError("Bridge is armed, active or in loopback. Capture stopped.")
            for name in ("received", "uptime_ms"):
                if type(status[name]) is not int or status[name] < 0:
                    raise ValueError
        except (ValueError, KeyError, UnicodeError):
            raise CaptureError("Invalid bridge status or automation response.") from None
        return status

    def rx(self, cursor, timeout):
        data, headers = self._get(f"/rx?since={cursor}", timeout)
        if data is None:
            return None
        try:
            start, end, lost = (int(headers["X-UART-" + name]) for name in ("Start", "End", "Lost"))
            if start < cursor or lost != start - cursor or end - start != len(data):
                raise ValueError
        except (TypeError, ValueError):
            raise CaptureError("Inconsistent bridge RX offsets or loss counters.") from None
        return data, end, lost


def capture(bridge, initial, port, seconds, directory):
    directory.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    deadline = started + seconds
    cursor = initial["received"]
    previous = initial
    next_check = started + 2
    byte_count = ring_lost = restarts = interruptions = 0
    outcome = "complete"
    error = None
    recheck = False
    unresolved_conflict = False
    started_utc = now_utc().isoformat()
    with (directory / "uart.bin").open("xb") as raw, (
            directory / "events.jsonl").open("x", encoding="utf-8", buffering=1) as events:
        def record(event, **details):
            events.write(json.dumps({"event": event, "utc": now_utc().isoformat(),
                                     "elapsed_s": round(time.monotonic() - started, 6),
                                     **details}) + "\n")

        record("start", cursor=cursor)
        print("RECORDING. Use the TV remote for the planned boot or wake now.", flush=True)
        try:
            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                timeout = max(0.05, min(2, remaining))
                try:
                    if recheck or time.monotonic() >= next_check:
                        status = bridge.state(timeout)
                        if status["uptime_ms"] < previous["uptime_ms"] or status["received"] < cursor:
                            restarts += 1
                            record("bridge_restart_or_counter_reset", previous_cursor=cursor)
                            cursor = 0
                        previous = status
                        next_check = time.monotonic() + 2
                        recheck = False
                    if time.monotonic() >= deadline:
                        break
                    batch = bridge.rx(cursor, max(0.05, min(2, deadline - time.monotonic())))
                    if batch is None:
                        record("rx_cursor_conflict", cursor=cursor)
                        recheck = True
                        unresolved_conflict = True
                    else:
                        data, end, lost = batch
                        unresolved_conflict = False
                        if lost:
                            ring_lost += lost
                            record("ring_loss", lost=lost, previous_cursor=cursor)
                        if data:
                            if byte_count + len(data) > MAX_CAPTURE_BYTES:
                                raise CaptureError("Capture reached the host byte limit.")
                            raw.write(data)
                            raw.flush()
                            record("rx", file_offset=byte_count, length=len(data),
                                   bridge_start=end - len(data), bridge_end=end)
                            byte_count += len(data)
                        cursor = end
                except READ_ERRORS as exc:
                    if not recheck:
                        print("Bridge connection interrupted. Retrying within the capture window.", flush=True)
                    interruptions += 1
                    recheck = True
                    record("connection_error", kind=type(exc).__name__)
                time.sleep(max(0, min(0.15, deadline - time.monotonic())))
        except KeyboardInterrupt:
            outcome = "stopped_by_operator"
        except CaptureError as exc:
            outcome, error = "aborted", str(exc)
        record("end", outcome=outcome, error=error)
    elapsed = time.monotonic() - started
    raw = (directory / "uart.bin").read_bytes()
    rendered = raw.decode("ascii", errors="backslashreplace")
    rendered = "".join(c if c in "\r\n\t" or 32 <= ord(c) <= 126
                       else f"\\x{ord(c):02x}" for c in rendered)
    (directory / "uart.txt").write_bytes(rendered.encode("ascii"))
    with (directory / "uart.hex.txt").open("x", encoding="ascii") as output:
        for offset in range(0, len(raw), 16):
            output.write(f"{offset:08x}  {raw[offset:offset+16].hex(' ')}\n")
    controls = sum(b not in (9, 10, 13) and not 32 <= b <= 126 for b in raw)
    marker_counts = {marker.decode(): raw.count(marker) for marker in MARKERS}
    if not raw:
        signal_note = "No new UART bytes. Check the boot window and RX path."
    elif not any(marker_counts.values()) and controls / len(raw) >= 0.25:
        signal_note = "High non-text fraction without known markers. Check 8N1 timing and signal quality."
    elif not any(marker_counts.values()):
        signal_note = "No known marker. Compare the text with the expected boot phase."
    else:
        signal_note = "Known boot or console markers found. Check counts and transport gaps against the reference."
    summary = {
        "started_utc": started_utc, "duration_s": round(elapsed, 3), "requested_s": seconds,
        "esp_usb_com_at_start": port, "transport": "HTTP GET, TCP 8080",
        "baud": 115200, "format": "8N1", "bytes": len(raw),
        "ring_loss_bytes": ring_lost, "observed_counter_resets": restarts,
        "unresolved_cursor_conflict": unresolved_conflict,
        "connection_errors": interruptions, "nontext_bytes": controls,
        "nontext_fraction": round(controls / len(raw), 4) if raw else None,
        "known_marker_counts": marker_counts, "signal_note": signal_note,
        "framing_errors_available": False, "outcome": outcome, "error": error,
    }
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"{outcome}: {len(raw)} bytes, {elapsed:.2f} s, 115200 8N1, "
          f"ring loss={ring_lost}, bridge resets={restarts}, connection errors={interruptions}, "
          f"unresolved cursor conflict={unresolved_conflict}")
    print(signal_note)
    print(f"Saved {directory}")
    if error:
        print(error, file=sys.stderr)
        return 2
    return 0 if raw and not (ring_lost or restarts or interruptions or unresolved_conflict) else 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp-toolkit", type=Path, default=ROOT.parent / "tcl-tv-toolkit")
    parser.add_argument("--host", default="tcl-uart-bridge.local")
    parser.add_argument("--com", type=str.upper, help="CP210x COM port already confirmed by unplug/replug")
    parser.add_argument("--seconds", type=int, default=0, help="capture time, 1-600 (default only checks state)")
    args = parser.parse_args()
    if not 0 <= args.seconds <= MAX_SECONDS:
        parser.error("--seconds must be 0-600")
    try:
        port = detect_port(args.com)
        try:
            token = (args.esp_toolkit / "esp32/private/access-token").read_text(encoding="ascii").strip()
        except (OSError, UnicodeError):
            raise CaptureError("Could not read the local toolkit access token.") from None
        if not re.fullmatch(r"[a-fA-F0-9]{64}", token):
            raise CaptureError("Unexpected toolkit access token format.")
        bridge = Bridge(lan_address(args.host), token)
        status = bridge.state()
        print(f"USB candidate {port} (enumeration only). Authenticated bridge on TCP 8080, "
              "115200 8N1, RX GPIO25, TX GPIO26, disarmed and loopback off.", flush=True)
        if args.seconds:
            directory = ROOT / "private/uart_captures" / now_utc().strftime("c655-uart-%Y%m%dT%H%M%S%fZ")
            return capture(bridge, status, port, args.seconds, directory)
    except (CaptureError, *READ_ERRORS) as exc:
        detail = str(exc) if isinstance(exc, CaptureError) else type(exc).__name__
        print(f"Capture unavailable: {detail}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
