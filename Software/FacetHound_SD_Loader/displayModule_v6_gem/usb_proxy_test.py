#!/usr/bin/env python3
"""Exercise displayModule_v6_gem through the RP2040 USB serial port."""

from __future__ import annotations

import argparse
import math
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


def available_ports():
    return [port.device for port in list_ports.comports()]


def send_line(port, line: str):
    line = line.strip()
    if not line:
        return
    if not line.startswith("@"):
        line = "@" + line
    port.write((line + "\n").encode("ascii"))


def reader(port, stopped: threading.Event):
    while not stopped.is_set():
        try:
            line = port.readline()
        except serial.SerialException:
            stopped.set()
            return
        if line:
            print("<", line.decode("ascii", errors="replace").rstrip())


def demo(port, seconds: float):
    start = time.monotonic()
    frame = 0
    while time.monotonic() - start < seconds:
        t = time.monotonic() - start
        target = (3.0 + frame * 0.18) % 96.0
        error = 1.8 * math.sin(t * 1.7)
        tip = 47.0 + 18.0 * math.sin(t * 0.65)
        force = round(10.0 + 9.0 * math.sin(t * 0.9))
        values = (
            ("T", target),
            ("E", error),
            ("TIP", tip),
            ("ZMM", 1.25 * math.sin(t * 0.4)),
            ("F", max(0, force) * 25600 / 20),
            ("RPM", 1200),
            ("RPV", 1175 + 30 * math.sin(t)),
            ("DIR", 2),
            ("FLW", 3.5 + 0.4 * math.sin(t * 0.7)),
            ("FLD", 2),
            ("WIDX", 96),
        )
        for key, value in values:
            send_line(port, f"{key},{value:.4f}")
        frame += 1
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(
        description="USB proxy/test source for the Facet Hound RP2040 display"
    )
    parser.add_argument("port", nargs="?", help="COM port, for example COM7")
    parser.add_argument("--mode", choices=("classic", "dynamic", "static"))
    parser.add_argument("--demo", type=float, metavar="SECONDS")
    args = parser.parse_args()

    if not args.port:
        ports = available_ports()
        if len(ports) == 1:
            args.port = ports[0]
        else:
            choices = ", ".join(ports) if ports else "none found"
            raise SystemExit(f"specify a COM port (detected: {choices})")

    # USB CDC ignores the numeric baud rate on RP2040, but 115200 is a useful
    # conventional value for terminal programs.
    with serial.Serial(args.port, 115200, timeout=0.1) as port:
        time.sleep(0.4)
        port.reset_input_buffer()
        stopped = threading.Event()
        thread = threading.Thread(target=reader, args=(port, stopped), daemon=True)
        thread.start()

        if args.mode:
            send_line(port, f"MODE,{args.mode.upper()}")
            print("Mode changes persist and reboot the RP2040; reconnect after it returns.")
            time.sleep(0.5)
            stopped.set()
            return

        if args.demo is not None:
            demo(port, max(0.0, args.demo))
            stopped.set()
            return

        print("Enter KEY,value (or @KEY,value). Commands: demo, mode ?, quit")
        while not stopped.is_set():
            try:
                line = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if line.lower() in ("quit", "exit"):
                break
            if line.lower() == "demo":
                demo(port, 15.0)
            elif line.lower() == "mode ?":
                send_line(port, "MODE,?")
            else:
                send_line(port, line)

        stopped.set()
        thread.join(timeout=0.5)


if __name__ == "__main__":
    main()
