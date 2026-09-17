#!/usr/bin/env python3
"""Interactive USB diagnostic console for the Facet Hound base module.

Spyder: set SERIAL_PORT below and Run File. A normal terminal may instead use
``python base_module_console.py --port COM5``.
"""

from __future__ import annotations

import argparse
import queue
import threading
import time


SERIAL_PORT = "AUTO"  # Set to "COM5", for example, when AUTO is ambiguous.
SERIAL_BAUD = 115200

KEY_CODES = {
    **{chr(ord("A") + offset): 4 + offset for offset in range(15)},
    **{str(number): 29 + number for number in range(1, 10)},
}


def choose_port(requested: str) -> str:
    from serial.tools import list_ports

    if requested.upper() != "AUTO":
        return requested
    ports = list(list_ports.comports())
    likely = [
        port
        for port in ports
        if any(
            word in f"{port.description} {port.manufacturer}".lower()
            for word in ("pico", "rp2350", "raspberry pi", "usb serial")
        )
    ]
    choices = likely or ports
    if len(choices) != 1:
        names = ", ".join(port.device for port in ports) or "none"
        raise RuntimeError(f"AUTO needs one likely port; found {names}. Set SERIAL_PORT='COMx'.")
    return choices[0].device


def pretty_state(line: str) -> str:
    fields = line.split(",")
    values = {}
    for field in fields[2:]:
        if "=" in field:
            key, value = field.split("=", 1)
            values[key] = value
    return (
        f"tip {values.get('tip', '?'):>8}  "
        f"index {values.get('actual', '?'):>8} -> {values.get('target', '?'):>8}  "
        f"err {values.get('error', '?'):>8}  z {values.get('z_mm', '?'):>8} mm\n"
        f"rpm {values.get('rpm_actual', '?'):>5}/{values.get('rpm_set', '?'):<5}  "
        f"flow {values.get('flow', '?'):>7}  force {values.get('force', '?'):>8}  "
        f"locks I={values.get('twist_lock', '?')} Z={values.get('z_lock', '?')}  "
        f"mark {values.get('mark', '?')}  gem {values.get('gem', '?')}"
    )


class Console:
    def __init__(self, port: str):
        import serial

        self.serial = serial.Serial(port, SERIAL_BAUD, timeout=0.1)
        self.lines: queue.Queue[str] = queue.Queue()
        self.running = True
        self.reader = threading.Thread(target=self._reader, daemon=True)
        self.reader.start()
        print(f"Connected to {port} at {SERIAL_BAUD} baud")

    def _reader(self):
        while self.running:
            try:
                raw = self.serial.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", "replace").rstrip()
                self.lines.put(line)
                stamp = time.strftime("%H:%M:%S")
                if line.startswith("@STATE,"):
                    print(f"\n[{stamp}] RX STATE\n{pretty_state(line)}")
                else:
                    print(f"\n[{stamp}] RX {line}")
                print("fh> ", end="", flush=True)
            except Exception as exc:  # device unplug or OS serial failure
                if self.running:
                    print(f"\nserial reader stopped: {exc}")
                self.running = False

    def send(self, command: str):
        command = command.strip()
        if not command:
            return
        print(f"TX {command}")
        self.serial.write((command + "\n").encode("ascii"))

    def close(self):
        self.running = False
        try:
            self.send("STREAM OFF")
            self.send("STOP")
            time.sleep(0.05)
        finally:
            self.serial.close()


def translate(command: str) -> str | None:
    words = command.strip().split()
    if not words:
        return None
    lower = [word.lower() for word in words]
    if lower[0] in ("quit", "exit", "q"):
        return "__QUIT__"
    if lower[0] == "state":
        return "STATUS"
    if lower[0] == "stream":
        if len(words) == 1:
            return "STREAM ON 250"
        if lower[1] == "off":
            return "STREAM OFF"
        if lower[1] == "on":
            return "STREAM ON " + (words[2] if len(words) > 2 else "250")
        return f"STREAM ON {words[1]}"
    if lower[0] == "key" and len(words) == 2:
        token = words[1].upper()
        code = KEY_CODES.get(token, token)
        return f"KEY {code}"
    return command.upper()


LOCAL_HELP = """
Local shortcuts (commands are logged in both directions):
  state                    one state/IO snapshot
  stream [milliseconds]    continuous state snapshots; stream off to stop
  key A .. O | key 1 .. 9  inject the configured HID key
  jog twist <units>        relative index jog; enables index lock
  jog z <steps>            relative raw-step jog; enables Z lock
  rpm <0..200>             set lap RPM command
  motor cw|ccw|off         lap direction/off
  motor status|probe       inspect/probe the BLD-510B Modbus link
  sd status|retry|list     inspect, reinitialize, or list the SD card
  flow <0..750>            pump raw velocity
  pump fwd|rev|off         pump direction/off
  stop                     immediately stop index, Z, lap, and pump commands
  help                     firmware help; quit exits after STOP
"""


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--port", default=SERIAL_PORT)
    args, _unknown = parser.parse_known_args()
    try:
        port = choose_port(args.port)
        console = Console(port)
    except Exception as exc:
        print(f"Unable to connect: {exc}")
        return 2

    print(LOCAL_HELP)
    console.send("STATUS")
    try:
        while console.running:
            translated = translate(input("fh> "))
            if translated == "__QUIT__":
                break
            if translated:
                console.send(translated)
    except (EOFError, KeyboardInterrupt):
        print()
    finally:
        console.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
