"""Spyder-friendly visual test for the Facet Hound settings menu.

Run directly in Spyder. Arrow keys emulate twist-wheel rotation; Enter emulates
the wheel click. Backspace is the top-left key, F/C change edit tier, and Delete
removes a selected mark point. Press D (or click LIVE DEMO) to close Settings
and animate Dynamic mode over USB. Set SERIAL_PORT to the RP2040 COM port.
"""

from __future__ import annotations

import math
import time
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon
from matplotlib.widgets import Button

SERIAL_PORT = "AUTO"  # Or an explicit port such as "COM5"; None is local-only.
SERIAL_BAUD = 115200
START_MODE = "DYNAMIC"


class OptionalSerial:
    def __init__(self, port_name):
        self.port = None
        self.message = "Local-only mode"
        if port_name:
            try:
                import serial
                from serial.tools import list_ports
            except ImportError:
                print("Serial disabled: install pyserial, or set SERIAL_PORT=None")
                return
            if str(port_name).upper() == "AUTO":
                candidates = list(list_ports.comports())
                likely = [item for item in candidates if any(
                    token in f"{item.description} {item.manufacturer}".lower()
                    for token in ("rp2040", "raspberry pi", "pico", "usb serial"))]
                if len(likely) == 1:
                    port_name = likely[0].device
                elif len(candidates) == 1:
                    port_name = candidates[0].device
                else:
                    found = ", ".join(item.device for item in candidates) or "none"
                    print(f"Serial disabled: AUTO found {found}; set SERIAL_PORT='COMx'")
                    return
                print(f"Using RP2040 USB port {port_name}")
            try:
                self.port = serial.Serial(port_name, SERIAL_BAUD, timeout=0.05)
                self.message = f"Connected to {port_name}"
            except Exception as exc:
                print(f"Serial disabled: could not open {port_name}: {exc}")

    def send(self, line):
        print(">", line)
        self.send_quiet(line)

    def send_quiet(self, line):
        if self.port:
            try:
                self.port.write((line + "\n").encode("ascii"))
            except Exception as exc:
                print(f"serial write failed: {exc}")
                self.close()

    def service(self):
        """Drain display replies so its USB transmit side never backs up."""
        if self.port:
            try:
                waiting = self.port.in_waiting
                if waiting:
                    self.port.read(min(waiting, 4096))
            except Exception as exc:
                print(f"serial read failed: {exc}")
                self.close()

    def close(self):
        port, self.port = self.port, None
        if port:
            port.close()


class SettingsMenuSimulator:
    MODES = ("CLASSIC", "DYNAMIC", "STATIC")
    WHEELS = (1, 2, 4, 32, 40, 48, 60, 64, 72, 77, 80, 81, 88,
              91, 96, 98, 99, 100, 102, 104, 110, 120, 128, 144, 192,
              256, 360, 400)
    RESET_STEPS = (4, 8, 12, 16, 32)
    TIERS = (10.0, 1.0, 0.1, 0.01)
    FLOW_TIERS = (0.01, 0.001, 0.0001, 0.00001)
    SPIN_TIERS = (1.0, 0.1, 0.01)
    ROOT = ("Display mode", "Index direction", "Table adapter", "Servo control",
            "Wheel index", "Z axis polarity", "Flow calibration", "Encoder zero",
            "Special commands", "Reset positions", "Load SD design", "Edit positions",
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
        self.z_polarity = "NORMAL"
        self.flow_conversion = 0.052
        self.flow_tier = 0
        self.spin_rpm = 1.0
        self.spin_tier = 0
        self.spin_running = False
        self.positions = [float(value) for value in range(0, 96, 6)]
        self.edit_index = 0
        self.edit_value = 0.0
        self.tier = 1
        self.status = ""
        self.sd_files = ["pc01391.asc", "standard_round.asc", "test_job.fct"]
        self.active_design = "NONE"
        self.demo_running = False
        self.demo_started = 0.0
        self.demo_frame = 0
        self.demo_segment = -1
        self.demo_values = (0.0, 0.0, 0.0)
        self.fig, self.ax = plt.subplots(figsize=(3.2, 4.8), dpi=100)
        self.fig.subplots_adjust(left=.04, right=.96, top=.98, bottom=.13)
        self.fig.canvas.manager.set_window_title("Facet Hound settings simulator")
        self.fig.canvas.mpl_connect("key_press_event", self.on_key)
        self.fig.canvas.mpl_connect("close_event", self.on_close)
        self.menu_button = Button(self.fig.add_axes((.08, .025, .38, .065)), "MENU [M]")
        self.demo_button = Button(self.fig.add_axes((.54, .025, .38, .065)), "LIVE DEMO [D]")
        self.menu_button.on_clicked(lambda _event: self.toggle_menu())
        self.demo_button.on_clicked(lambda _event: self.toggle_demo())
        self.timer = self.fig.canvas.new_timer(interval=120)
        self.timer.add_callback(self.demo_tick)
        self.timer.start()
        self.link.send("@MENU,1")
        self.link.send("@CFGGET,ALL")
        self.draw()

    def emit_key(self, name):
        self.link.send(f"@MENUKEY,{name}")

    def on_close(self, _event):
        self.demo_running = False
        self.timer.stop()
        self.link.close()

    def toggle_menu(self):
        self.demo_running = False
        self.open = not self.open
        self.link.send(f"@MENU,{1 if self.open else 0}")
        if self.open:
            self.page, self.cursor = "root", 0
            self.link.send("@CFGGET,ALL")
        self.draw()

    def toggle_demo(self):
        self.demo_running = not self.demo_running
        if self.demo_running:
            self.open = False
            self.page, self.cursor = "root", 0
            self.link.send("@MENU,0")
            self.demo_started = time.monotonic()
            self.demo_frame = 0
            self.demo_segment = -1
            setup_lines = ("@RPM,1200", "@DIR,2", "@FLD,2", "@WIDX,96",
                           "@TIDX,1", "@ZIDX,1")
            sender = getattr(self.link, "send_quiet", self.link.send)
            for line in setup_lines:
                sender(line)
            self.status = "Live USB telemetry running"
        else:
            self.status = "Live USB telemetry stopped"
        self.draw()

    def demo_tick(self):
        service = getattr(self.link, "service", None)
        if service:
            service()
        if not self.demo_running:
            return
        t = time.monotonic() - self.demo_started
        # Every fallback facet, in the exact tier/facet order of gem_data.h.
        tier_specs = ((90.0, 27.0, 6.0, "G"),
                      (132.0, 27.0, 6.0, "P1"),
                      (135.0, 24.0, 6.0, "P2"),
                      (138.0, 27.0, 6.0, "P3"),
                      (65.0, 69.0, -6.0, "C1"),
                      (47.0, 69.0, -6.0, "C2"),
                      (29.0, 69.0, -6.0, "C3"),
                      (12.0, 69.0, -6.0, "C4"))
        poses = []
        for tier, (raw_tip, start, increment, name) in enumerate(tier_specs, 1):
            for facet in range(1, 17):
                raw_index = (start + increment * (facet - 1)) % 96.0
                pavilion = raw_tip > 90.0
                machine_tip = 180.0 - raw_tip if pavilion else raw_tip
                machine_index = (raw_index + (48.0 if pavilion else 0.0)) % 96.0
                poses.append((machine_tip, machine_index, tier, facet,
                              raw_tip, raw_index, name))
        # Selection and targets change at the boundary. Actual pose moves for
        # one second, then holds still for one second.
        cycle = 2.0
        segment = int(t / cycle) % len(poses)
        phase = (t % cycle) / cycle
        target_tip, target, tier, facet, raw_tip, raw_index, name = poses[segment]
        previous_tip = poses[(segment - 1) % len(poses)][0]
        previous_index = poses[(segment - 1) % len(poses)][1]
        if phase < 0.50:
            move = phase / 0.50
            ease = move * move * (3.0 - 2.0 * move)
            delta = ((target - previous_index + 48.0) % 96.0) - 48.0
            actual_index = (previous_index + delta * ease) % 96.0
            tip = previous_tip + (target_tip - previous_tip) * ease
        else:
            actual_index = target
            tip = target_tip
        index_error = ((actual_index - target + 48.0) % 96.0) - 48.0
        z_value = 0.6 * math.sin(t * 0.25)
        values = (("T", target), ("E", index_error), ("TIP", tip), ("ZMM", z_value),
                  ("F", max(0, round(10.0 + 9.0 * math.sin(t * 0.9))) * 1280),
                  ("RPV", 1175 + 35 * math.sin(t * 4.3) + 9 * math.sin(t * 11.0)),
                  ("FLW", 3.5 + 0.4 * math.sin(t * 0.7)))
        lines = [f"@{key},{value:.4f}" for key, value in values]
        if segment != self.demo_segment:
            lines.insert(0, f"@JOB,{tier},{facet},{raw_tip},0,{raw_index},{name}")
            self.demo_segment = segment
        sender = getattr(self.link, "send_quiet", self.link.send)
        for line in lines:
            sender(line)
        self.demo_values = (tip, actual_index, z_value)
        self.demo_frame += 1
        if self.demo_frame % 4 == 0:
            self.draw()

    def choices(self):
        if self.page == "mode": return list(self.MODES)
        if self.page == "direction": return ["CW", "CCW"]
        if self.page in ("adapter", "servo"): return ["OFF", "ON"]
        if self.page == "wheel": return list(self.WHEELS)
        if self.page == "z_polarity": return ["NORMAL", "REVERSED"]
        if self.page == "encoder": return ["Zero index encoder", "Zero tip encoder",
                                            "Zero Z encoder", "Zero all encoders"]
        if self.page == "special": return ["Index continuous spin"]
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
            self.page, self.cursor = "z_polarity", int(self.z_polarity == "REVERSED")
        elif self.cursor == 6:
            self.page, self.cursor = "flow", 0
            self.flow_tier = 0
        elif self.cursor == 7:
            self.page, self.cursor = "encoder", 0
        elif self.cursor == 8:
            self.page, self.cursor = "special", 0
        elif self.cursor == 9:
            self.page, self.cursor = "reset", 0
        elif self.cursor == 10:
            self.page, self.cursor = "sd", 0
            self.status = f"{len(self.sd_files)} design files"
            self.link.send("@CFGGET,SD_FILES,0,6")
        elif self.cursor == 11:
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
        elif self.page == "z_polarity":
            self.z_polarity = ("NORMAL", "REVERSED")[self.cursor]
            self.link.send(f"@CFGSET,Z_POLARITY,{self.z_polarity}")
        elif self.page == "encoder":
            encoder = ("INDEX", "TIP", "Z", "ALL")[self.cursor]
            self.link.send(f"@CFGACTION,ZERO_ENCODER,{encoder}")
        elif self.page == "special":
            self.page, self.cursor = "spin", 0
            self.spin_tier = 0
            self.spin_running = False
            return
        elif self.page == "reset":
            step = self.RESET_STEPS[self.cursor]
            self.positions = [float(value) for value in range(0, int(math.ceil(self.wheel)), step)
                              if value < self.wheel]
            self.link.send(f"@CFGACTION,RESET_POSITIONS,{step}")
        self.page, self.cursor = "root", 0

    def on_key(self, event):
        key = (event.key or "").lower()
        if key == "m":
            self.toggle_menu()
            return
        elif key == "d":
            self.toggle_demo()
            return
        elif not self.open:
            return
        elif key in ("up", "down"):
            self.emit_key(key.upper())
            delta = -1 if key == "up" else 1
            if self.page == "edit":
                self.edit_value = (self.edit_value - delta * self.TIERS[self.tier]) % self.wheel
            elif self.page == "flow":
                self.flow_conversion = min(10.0, max(0.000001,
                    self.flow_conversion - delta * self.FLOW_TIERS[self.flow_tier]))
            elif self.page == "spin":
                self.spin_rpm = min(10.0, max(-10.0,
                    self.spin_rpm - delta * self.SPIN_TIERS[self.spin_tier]))
                if abs(self.spin_rpm) < .01:
                    self.spin_rpm = .01 if key == "up" else -.01
                if self.spin_running:
                    self.link.send(f"@CFGACTION,INDEX_SPIN_START,{self.spin_rpm:.2f}")
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
            elif self.page == "flow":
                self.link.send(f"@CFGSET,FLOW_CONVERSION,{self.flow_conversion:.6f}")
                self.page, self.cursor = "root", 0
            elif self.page == "spin":
                self.spin_running = not self.spin_running
                action = "INDEX_SPIN_START" if self.spin_running else "INDEX_SPIN_STOP"
                value = f"{self.spin_rpm:.2f}" if self.spin_running else "0"
                self.link.send(f"@CFGACTION,{action},{value}")
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
            elif self.page == "spin":
                self.spin_running = False
                self.link.send("@CFGACTION,INDEX_SPIN_STOP,0")
                self.page, self.cursor = "special", 0
            elif self.page == "root":
                self.open = False
                self.link.send("@MENU,CLOSED")
            else:
                self.page, self.cursor = "root", 0
        elif key == "f" and self.page in ("edit", "flow", "spin"):
            self.emit_key("FINER")
            if self.page == "edit": self.tier = min(len(self.TIERS) - 1, self.tier + 1)
            elif self.page == "flow": self.flow_tier = min(len(self.FLOW_TIERS) - 1, self.flow_tier + 1)
            else: self.spin_tier = min(len(self.SPIN_TIERS) - 1, self.spin_tier + 1)
        elif key == "c" and self.page in ("edit", "flow", "spin"):
            self.emit_key("COARSER")
            if self.page == "edit": self.tier = max(0, self.tier - 1)
            elif self.page == "flow": self.flow_tier = max(0, self.flow_tier - 1)
            else: self.spin_tier = max(0, self.spin_tier - 1)
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
                "ON" if self.servo else "OFF", f"{self.wheel:g}", self.z_polarity,
                f"{self.flow_conversion:.5f}", "", "", "", self.active_design,
                str(len(self.positions)), "")[index]

    def draw(self):
        self.ax.clear(); self.ax.set(xlim=(0, 1), ylim=(0, 1)); self.ax.axis("off")
        self.fig.patch.set_facecolor("#000"); self.ax.set_facecolor("#000")
        if not self.open:
            if self.demo_running:
                tip, index, z_value = self.demo_values
                message = ("LIVE USB DEMO\n\n"
                           f"TIP   {tip:+7.2f}\nINDEX {index:+7.2f}\nZ     {z_value:+7.3f} mm\n\n"
                           "press D to stop")
            else:
                message = "SETTINGS CLOSED\npress M to open\npress D for live motion"
            self.ax.text(.5, .52, message, color="#40e8ff" if self.demo_running else "#77808a",
                         ha="center", va="center", family="DejaVu Sans Mono")
            self.demo_button.label.set_text("STOP DEMO [D]" if self.demo_running else "LIVE DEMO [D]")
            self.fig.canvas.draw_idle(); return
        subtitles = {"root": "", "mode": "display mode", "direction": "positive index direction",
                     "adapter": "subtract 45 degrees from tip", "servo": "automatic spin servo",
                     "wheel": "wheel index", "z_polarity": "Z readout and motor direction",
                     "flow": "mL/min displayed per flow tick", "encoder": "reset current position to zero",
                     "special": "operations not mapped to keys", "spin": "constant open-loop index rotation",
                     "reset": "position spacing on wheel", "positions": "numeric mark-point list",
                     "edit": "edit mark point", "sd": f"active: {self.active_design}"}
        subtitle = subtitles.get(self.page, "choose value")
        self.ax.text(.056, .94, "SETTINGS", color="#e8edf2", fontsize=18,
                     va="center", family="DejaVu Sans Mono")
        if subtitle:
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
        elif self.page == "flow":
            items = [("Conversion", f"{self.flow_conversion:.6f}"),
                     ("Edit step", f"{self.FLOW_TIERS[self.flow_tier]:.5f}")]
            self.cursor = 0
        elif self.page == "spin":
            items = [("Signed rate", f"{self.spin_rpm:+.2f} rpm"),
                     ("Edit step", f"{self.SPIN_TIERS[self.spin_tier]:.2f} rpm"),
                     ("Motor", "RUNNING" if self.spin_running else "STOPPED")]
            self.cursor = 0
        elif self.page == "sd":
            items = [(name, "") for name in self.sd_files]
        else:
            items = [(str(value), "") for value in self.choices()]

        start = (self.cursor // 6) * 6
        for row, index in enumerate(range(start, min(len(items), start + 6))):
            self.row(.71 - row*.115, *items[index], index == self.cursor, True)
        footer = "ARROWS wheel   ENTER click   BACK back   F/C tier   DEL"
        self.ax.text(.055, .025, footer, color="#68727c", fontsize=5.5,
                     va="bottom", family="DejaVu Sans Mono")
        self.demo_button.label.set_text("LIVE DEMO [D]")
        self.fig.canvas.draw_idle()


if __name__ == "__main__":
    serial_link = OptionalSerial(SERIAL_PORT)
    simulator = SettingsMenuSimulator(serial_link)
    plt.show()
