"""Headless state-transition checks for the Spyder settings simulator."""

import matplotlib
matplotlib.use("Agg")

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
    menu.cursor = 7
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
    menu.page, menu.cursor = "root", 5
    press(menu, "enter")
    press(menu, "enter")
    assert len(menu.positions) == 24
    assert "@CFGACTION,RESET_POSITIONS,4" in link.lines


if __name__ == "__main__":
    test_menu_transitions()
    print("settings menu simulator tests: PASS")
