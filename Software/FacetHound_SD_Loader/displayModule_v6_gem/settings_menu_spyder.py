"""Spyder-friendly visual test for the Facet Hound settings menu.

Run directly in Spyder. Arrow keys emulate twist-wheel rotation; Enter emulates
the wheel click. Backspace is the top-left/former wheel-index key, F is the
former servo key (finer tier), and Delete removes a selected mark point.
Set SERIAL_PORT to mirror commands to a connected display; None is fully local.
"""

from __future__ import annotations

import math
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon

SERIAL_PORT = None  # Example: "COM7"
SERIAL_BAUD = 115200
START_MODE = "CLASSIC"


class OptionalSerial:
    def __init__(self, port_name):
        self.port = None
        if port_name:
            try:
                import serial
            except ImportError as exc:
                raise RuntimeError("Install pyserial or set SERIAL_PORT=None") from exc
            self.port = serial.Serial(port_name, SERIAL_BAUD, timeout=0.05)

    def send(self, line):
        print(">", line)
        if self.port:
            self.port.write((line + "\n").encode("ascii"))

    def close(self):
        if self.port:
            self.port.close()


class SettingsMenuSimulator:
    MODES = ("CLASSIC", "DYNAMIC", "STATIC")
    WHEELS = (1, 2, 6.2832, 32, 40, 48, 60, 64, 72, 77, 80, 81, 88,
              91, 96, 98, 99, 100, 102, 104, 110, 120, 128, 144, 192,
              256, 360, 400)
    RESET_STEPS = (4, 8, 12, 16, 32)
    TIERS = (10.0, 1.0, 0.1, 0.01)
    ROOT = ("Display mode", "Index direction", "Table adapter", "Servo control",
            "Wheel index", "Reset positions", "Load SD design", "Edit positions",
            "Close settings")

    def __init__(self, link):
        self.link = link
        self.open = True
        self.page = "root"
        self.cursor = 0
        self.mode = START_MODE.upper()
        self.direction = "CW"
        self.adapter = False
        self.servo = False
        self.wheel = 96.0
        self.positions = [float(value) for value in range(0, 96, 6)]
        self.edit_index = 0
        self.edit_value = 0.0
        self.tier = 1
        self.status = ""
        self.sd_files = ["pc01391.asc", "standard_round.asc", "test_job.fct"]
        self.active_design = "NONE"
        self.fig, self.ax = plt.subplots(figsize=(3.2, 4.8), dpi=100)
        self.fig.canvas.manager.set_window_title("Facet Hound settings simulator")
        self.fig.canvas.mpl_connect("key_press_event", self.on_key)
        self.fig.canvas.mpl_connect("close_event", lambda _event: self.link.close())
        self.link.send("@MENU,1")
        self.link.send("@CFGGET,ALL")
        self.draw()

    def emit_key(self, name):
        self.link.send(f"@MENUKEY,{name}")

    def choices(self):
        if self.page == "mode": return list(self.MODES)
        if self.page == "direction": return ["CW", "CCW"]
        if self.page in ("adapter", "servo"): return ["OFF", "ON"]
        if self.page == "wheel": return list(self.WHEELS)
        if self.page == "reset": return [f"Every {step}" for step in self.RESET_STEPS]
        return []

    def open_root_item(self):
        if self.cursor == 0:
            self.page, self.cursor = "mode", self.MODES.index(self.mode)
        elif self.cursor == 1:
            self.page, self.cursor = "direction", int(self.direction == "CCW")
        elif self.cursor == 2:
            self.page, self.cursor = "adapter", int(self.adapter)
        elif self.cursor == 3:
            self.page, self.cursor = "servo", int(self.servo)
        elif self.cursor == 4:
            self.page = "wheel"
            self.cursor = min(range(len(self.WHEELS)), key=lambda i: abs(self.WHEELS[i] - self.wheel))
        elif self.cursor == 5:
            self.page, self.cursor = "reset", 0
        elif self.cursor == 6:
            self.page, self.cursor = "sd", 0
            self.status = f"{len(self.sd_files)} design files"
            self.link.send("@CFGGET,SD_FILES,0,6")
        elif self.cursor == 7:
            self.page, self.cursor = "positions", 0
            self.link.send("@CFGGET,POSITIONS,0,8")
        else:
            self.open = False
            self.link.send("@MENU,CLOSED")

    def select_choice(self):
        if self.page == "mode":
            self.mode = self.MODES[self.cursor]
            self.link.send(f"@CFGSET,DISPLAY_MODE,{self.mode}")
            print("  a changed display mode persists and reboots on hardware")
        elif self.page == "direction":
            self.direction = ("CW", "CCW")[self.cursor]
            self.link.send(f"@CFGSET,INDEX_DIRECTION,{self.direction}")
        elif self.page == "adapter":
            self.adapter = bool(self.cursor)
            self.link.send(f"@CFGSET,TABLE_ADAPTER,{'ON' if self.adapter else 'OFF'}")
        elif self.page == "servo":
            self.servo = bool(self.cursor)
            self.link.send(f"@CFGSET,SERVO_ENABLED,{'ON' if self.servo else 'OFF'}")
        elif self.page == "wheel":
            self.wheel = float(self.WHEELS[self.cursor])
            self.positions = [value % self.wheel for value in self.positions]
            self.link.send(f"@CFGSET,WHEEL_INDEX,{self.wheel:.4f}")
        elif self.page == "reset":
            step = self.RESET_STEPS[self.cursor]
            self.positions = [float(value) for value in range(0, int(math.ceil(self.wheel)), step)
                              if value < self.wheel]
            self.link.send(f"@CFGACTION,RESET_POSITIONS,{step}")
        self.page, self.cursor = "root", 0

    def on_key(self, event):
        key = (event.key or "").lower()
        if key == "m":
            self.open = not self.open
            self.link.send(f"@MENU,{1 if self.open else 0}")
            if self.open:
                self.page, self.cursor = "root", 0
                self.link.send("@CFGGET,ALL")
        elif not self.open:
            return
        elif key in ("up", "down"):
            self.emit_key(key.upper())
            delta = -1 if key == "up" else 1
            if self.page == "edit":
                self.edit_value = (self.edit_value - delta * self.TIERS[self.tier]) % self.wheel
            elif self.page == "root":
                self.cursor = (self.cursor + delta) % len(self.ROOT)
            elif self.page == "positions":
                self.cursor = (self.cursor + delta) % (len(self.positions) + 1)
                if self.cursor < len(self.positions) and self.cursor % 8 == 0:
                    self.link.send(f"@CFGGET,POSITIONS,{self.cursor},8")
            elif self.page == "sd" and self.sd_files:
                self.cursor = (self.cursor + delta) % len(self.sd_files)
                if self.cursor % 6 == 0:
                    self.link.send(f"@CFGGET,SD_FILES,{self.cursor},6")
            elif self.choices():
                self.cursor = (self.cursor + delta) % len(self.choices())
        elif key in ("enter", "return"):
            self.emit_key("SELECT")
            if self.page == "root":
                self.open_root_item()
            elif self.page == "positions":
                if self.cursor == len(self.positions):
                    self.positions.append(0.0)
                    self.cursor = len(self.positions) - 1
                    self.link.send("@CFGACTION,ADD_POSITION,0")
                else:
                    self.edit_index = self.cursor
                    self.edit_value = self.positions[self.cursor]
                    self.tier = 1
                    self.page = "edit"
            elif self.page == "edit":
                self.positions[self.edit_index] = self.edit_value
                self.link.send(f"@CFGSET,POSITION_{self.edit_index},{self.edit_value:.4f}")
                self.page, self.cursor = "positions", self.edit_index
            elif self.page == "sd":
                if self.sd_files:
                    self.active_design = self.sd_files[self.cursor]
                    self.status = f"Loaded: {self.active_design}"
                    self.link.send(f"@CFGACTION,LOAD_SD_FILE,{self.cursor}")
            else:
                self.select_choice()
        elif key in ("backspace", "escape"):
            self.emit_key("BACK")
            if self.page == "edit":
                self.tier = max(0, self.tier - 1)
            elif self.page == "root":
                self.open = False
                self.link.send("@MENU,CLOSED")
            else:
                self.page, self.cursor = "root", 0
        elif key == "f" and self.page == "edit":
            self.emit_key("FINER")
            self.tier = min(len(self.TIERS) - 1, self.tier + 1)
        elif key == "delete" and self.page in ("positions", "edit") and self.positions:
            self.emit_key("DELETE")
            index = self.edit_index if self.page == "edit" else min(self.cursor, len(self.positions) - 1)
            self.link.send(f"@CFGACTION,DELETE_POSITION,{index}")
            del self.positions[index]
            self.page = "positions"
            self.cursor = min(index, len(self.positions))
        else:
            return
        self.draw()

    def row(self, y, label, value, selected, active=False):
        bg = "#10202b" if selected else "#000000"
        fg = "#40e8ff" if selected else "#e8edf2"
        self.ax.add_patch(FancyBboxPatch((.045, y), .91, .10,
                          boxstyle="round,pad=.004,rounding_size=.014",
                          facecolor=bg, edgecolor="none"))
        if selected:
            self.ax.add_patch(Polygon(((.066, y+.037), (.066, y+.063), (.087, y+.05)),
                              closed=True, facecolor=fg, edgecolor="none"))
        self.ax.text(.112, y+.05, label, va="center", color=fg, fontsize=9,
                     family="DejaVu Sans Mono")
        if value:
            self.ax.text(.91, y+.05, value, va="center", ha="right",
                         color="#40ff78" if active else fg, fontsize=8,
                         family="DejaVu Sans Mono")

    def root_value(self, index):
        return (self.mode, self.direction, "ON" if self.adapter else "OFF",
                "ON" if self.servo else "OFF", f"{self.wheel:g}", "", self.active_design,
                str(len(self.positions)), "")[index]

    def draw(self):
        self.ax.clear(); self.ax.set(xlim=(0, 1), ylim=(0, 1)); self.ax.axis("off")
        self.fig.patch.set_facecolor("#000"); self.ax.set_facecolor("#000")
        if not self.open:
            self.ax.text(.5, .52, "SETTINGS CLOSED\npress M to open", color="#77808a",
                         ha="center", va="center", family="DejaVu Sans Mono")
            self.fig.canvas.draw_idle(); return
        subtitle = {"root": "machine and display configuration", "positions": "numeric mark-point list",
                    "edit": "edit mark point", "sd": "load coordinates from SD"}.get(self.page, "choose value")
        self.ax.text(.056, .94, "SETTINGS", color="#e8edf2", fontsize=18,
                     va="center", family="DejaVu Sans Mono")
        self.ax.text(.058, .89, subtitle, color="#6e7882", fontsize=8,
                     va="center", family="DejaVu Sans Mono")
        self.ax.plot((.056, .944), (.84, .84), color="#20303b", lw=1)

        if self.page == "root":
            items = [(label, self.root_value(i)) for i, label in enumerate(self.ROOT)]
        elif self.page == "positions":
            items = [(f"Position {i+1}", f"{value:.4f}") for i, value in enumerate(self.positions)]
            items.append(("+ Add position", ""))
        elif self.page == "edit":
            items = [(f"Position {self.edit_index+1}", ""), ("Value", f"{self.edit_value:.4f}"),
                     ("Edit tier", f"Step {self.TIERS[self.tier]:.2f}")]
            self.cursor = 1
        elif self.page == "sd":
            items = [(name, "") for name in self.sd_files]
        else:
            items = [(str(value), "") for value in self.choices()]

        start = max(0, self.cursor - 5)
        for row, index in enumerate(range(start, min(len(items), start + 6))):
            self.row(.71 - row*.115, *items[index], index == self.cursor, True)
        footer = "WHEEL arrows/click   BACKSPACE back/coarse   F fine   DEL remove"
        self.ax.text(.055, .025, footer, color="#68727c", fontsize=5.5,
                     va="bottom", family="DejaVu Sans Mono")
        self.fig.canvas.draw_idle()


if __name__ == "__main__":
    serial_link = OptionalSerial(SERIAL_PORT)
    simulator = SettingsMenuSimulator(serial_link)
    plt.show()
