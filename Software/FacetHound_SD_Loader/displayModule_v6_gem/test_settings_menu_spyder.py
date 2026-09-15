"""Headless state-transition checks for the Spyder settings simulator."""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from types import SimpleNamespace

from settings_menu_spyder import SettingsMenuSimulator


class CaptureLink:
    def __init__(self):
        self.lines = []

    def send(self, line):
        self.lines.append(line)

    def close(self):
        pass


def press(menu, key):
    menu.on_key(SimpleNamespace(key=key))


def test_menu_transitions():
    link = CaptureLink()
    menu = SettingsMenuSimulator(link)

    # Table adapter ON.
    press(menu, "down")
    press(menu, "down")
    press(menu, "enter")
    press(menu, "down")
    press(menu, "enter")
    assert menu.adapter
    assert "@CFGSET,TABLE_ADAPTER,ON" in link.lines

    # Variable position editor, tier keys, save, add, and delete.
    menu.cursor = 11
    press(menu, "enter")
    assert menu.page == "positions"
    press(menu, "enter")
    assert menu.page == "edit"
    original = menu.edit_value
    press(menu, "up")
    assert menu.edit_value != original
    press(menu, "backspace")
    press(menu, "f")
    press(menu, "enter")
    assert any(line.startswith("@CFGSET,POSITION_0,") for line in link.lines)

    menu.cursor = len(menu.positions)
    old_count = len(menu.positions)
    press(menu, "enter")
    assert len(menu.positions) == old_count + 1
    press(menu, "delete")
    assert len(menu.positions) == old_count

    # Reset every four on a 96 wheel creates 24 points.
    menu.page, menu.cursor = "root", 9
    press(menu, "enter")
    press(menu, "enter")
    assert len(menu.positions) == 24
    assert "@CFGACTION,RESET_POSITIONS,4" in link.lines

    # Simulator menu tree and wheel choices stay identical to the firmware.
    assert len(menu.ROOT) == 13
    assert 4 in menu.WHEELS
    assert all(float(value).is_integer() for value in menu.WHEELS)

    # The same window can close Settings and stream motion telemetry.
    menu.toggle_demo()
    assert menu.demo_running and not menu.open
    menu.demo_tick()
    assert any(line.startswith("@TIP,") for line in link.lines)
    assert any(line.startswith("@TIDX,") for line in link.lines)
    menu.toggle_demo()
    plt.close(menu.fig)


if __name__ == "__main__":
    test_menu_transitions()
    print("settings menu simulator tests: PASS")
