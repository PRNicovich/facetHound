# USB keyboard-to-UART module

This dedicated RP2040 Zero module is the machine's USB host for the physical
keyboard. It keeps USB host behavior off the base Pico and translates new HID
key presses into the base module's simple 3.3 V UART protocol.

## Wiring

| Function | Bridge GPIO | Connects to |
|---|---:|---|
| PIO USB host D+ | 2 | Keyboard USB D+ |
| PIO USB host D- | 3 | Keyboard USB D- |
| UART TX | 0 | Base keyboard RX GPIO6 |
| UART RX | 1 | Base keyboard TX GPIO7 |
| Ground | GND | Base and keyboard-supply ground |

The keyboard also needs a correctly protected 5 V VBUS supply from the host
hardware. D+/D- are not power pins. Confirm VBUS polarity, grounding, current
protection, and the PCB's USB connector orientation before attaching a keyboard.
The UART side is 3.3 V TTL at 115200 baud, not RS-232.

## UART protocol

Each newly pressed HID usage is emitted on its own newline-terminated record:

```text
D,4
D,17
```

The active base consumes this format directly in `keyboardTask()`. Releases
are not sent. Already-held keys are suppressed when another key changes, and a
multi-key report produces one record for each newly pressed key. Non-keyboard
HID interfaces are ignored without stalling the USB host endpoint.

The physical layout and HID assignments are in
[keyboardSettings.png](keyboardSettings.png). Operator functions are mapped in
[`../baseChassisModule/KEYBOARD_MAP.md`](../baseChassisModule/KEYBOARD_MAP.md).

## Required Arduino settings

Use Arduino-Pico 5.6.0 with these board selections:

| Setting | Value |
|---|---|
| Board | Waveshare RP2040 Zero |
| CPU speed | 240 MHz (Overclock) |
| Optimize | Optimize (`-O`) |
| USB stack | Adafruit TinyUSB |
| Flash | 2 MB, no filesystem |

The PIO USB host was originally validated with the 240 MHz and `-O` settings;
do not silently substitute the board defaults. The firmware uses core 1 for
the host task and restarts that core if its heartbeat stops for three seconds.

Verified Arduino CLI target:

```text
rp2040:rp2040:waveshare_rp2040_zero:freq=240,opt=Optimize,usbstack=tinyusb
```

Current compile size: 90,308 bytes flash and 33,004 bytes static RAM.
