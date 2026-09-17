# Base module wiring and bench test

The keyboard remains a dedicated UART module. The Pico is not a USB keyboard
host. Its native USB connector is used only for programming and the optional
PC diagnostic console.

## Signal map

All UART logic must be 3.3 V TTL and every module needs a common ground.

| Device / signal | Base GPIO | Notes |
|---|---:|---|
| Keyboard UART TX | 7 | Base TX to keyboard RX |
| Keyboard UART RX | 6 | Keyboard `D,<HID>` events enter here |
| Display UART TX | 5 | Base to display, 460800 baud |
| Display UART RX | 4 | Display to base, 460800 baud |
| Mast UART TX | 3 | Base to mast, 115200 baud |
| Mast UART RX | 2 | Mast to base, 115200 baud |
| SD CS | 8 | PIO-backed software SPI |
| SD SCK | 9 | PIO-backed software SPI |
| SD MISO | 10 | PIO-backed software SPI |
| SD MOSI | 11 | PIO-backed software SPI |
| Twist STEP / DIR / EN / UART TX | 1 / 0 / 14 / 28 | TMC2209 axis |
| Z STEP / DIR / EN / UART TX | 16 / 17 / 18 / 15 | TMC2209 axis |
| Pump STEP / DIR / EN / UART TX | 20 / 21 / 22 / 19 | TMC2209 axis |
| Lap RS-485 TX / RX | 26 / 27 | BLD-510B link, 9600 baud |
| PC diagnostics | Native USB | USB CDC only; not the keyboard path |

GPIO 8-11 were legacy lap-controller signals. The current RS-485 lap mode uses
GPIO 26/27, leaving 8-11 available for the SD reader. Verify the SD breakout's
supply requirement before connecting it; the GPIO signals themselves must not
exceed 3.3 V.

For the opposite end of the mast link, connect mast TX GPIO8 to base RX GPIO2
and mast RX GPIO9 to base TX GPIO3. The matching streaming records and numeric
ranges are documented in [the mast README](../mastModule/README.md).

## Keyboard protocol

The keyboard module sends one newline-terminated record per action:

```text
D,4
```

The second field is the HID usage number configured in
`keyboardSettings.png`. See [KEYBOARD_MAP.md](KEYBOARD_MAP.md) and
[keyboard_map.svg](keyboard_map.svg) for the physical layout and action map.

## USB bench console

`tools/base_module_console.py` talks to the base module's native USB CDC port.
It displays raw traffic, decoded state, I/O counters, and supports controlled
motor jogs. It does not intercept the keyboard UART.

The console is a reference client for a documented PC-control interface. See
[PC_USB_API.md](PC_USB_API.md) for framing, every command and response, state
field definitions, safety guidance, and a minimal client example.

For Spyder, open the script, set `SERIAL_PORT = "COM5"` near the top, and run
the file. `AUTO` also works when exactly one likely Pico serial port is present.
Install pyserial in Spyder's Python environment if needed:

```text
python -m pip install pyserial
```

Useful console commands:

```text
state
stream 250
stream off
key A
key 7
jog twist 1
jog z 200
rpm 20
motor cw
flow 100
pump fwd
stop
```

`quit`, Ctrl+C, and EOF send `STREAM OFF` and `STOP` before closing. Keep the
machine mechanically safe during bench tests: begin with motors unloaded or
disabled, use small jogs, and retain physical power cutoff access.
