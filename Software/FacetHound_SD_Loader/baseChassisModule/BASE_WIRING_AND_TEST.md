# Base module wiring and bench test

The keyboard remains a dedicated UART module. The Pico is not a USB keyboard
host. Its native USB connector is used only for programming and the optional
PC diagnostic console.

## Signal map

All UART logic must be 3.3 V TTL and every module needs a common ground.

| Device / signal | Base GPIO | Notes |
|---|---:|---|
| Keyboard UART TX | 7 | Base TX to keyboard-host RX GPIO1 |
| Keyboard UART RX | 6 | Keyboard-host TX GPIO0 sends `D,<HID>` events here |
| Display UART TX | 5 | Base to display, 460800 baud |
| Display UART RX | 4 | Display to base, 460800 baud |
| Mast UART TX | 3 | Base to mast, 115200 baud |
| Mast UART RX | 2 | Mast to base, 115200 baud |
| Lap TTL TX / RX | 8 / 9 | Hardware UART1 at the v9 `ESC.TTL` header; external automatic-direction RS-485 module; BLD-510B, 9600 8N1 |
| SD CS | 10 | v9 microSD header pin 2; PIO-backed software SPI |
| SD SCK | 11 | v9 microSD header pin 3; PIO-backed software SPI |
| SD MOSI | 12 | v9 microSD header pin 4; PIO-backed software SPI |
| SD MISO | 13 | v9 microSD header pin 5; PIO-backed software SPI |
| Twist STEP / DIR / EN / shared UART | 1 / 0 / 14 / 28 | TMC2208-compatible axis; UART through 1 kΩ |
| Z STEP / DIR / EN / shared UART | 16 / 17 / 18 / 15 | TMC2208-compatible axis; UART through 1 kΩ |
| Pump STEP / DIR / EN / shared UART | 20 / 21 / 22 / 19 | TMC2208-compatible axis; UART through 1 kΩ |
| PC diagnostics | Native USB | USB CDC only; not the keyboard path |

This map comes from `Hardware/pcb/v9/BaseModule.sch`.
The v9 board does not connect GPIO 26/27. The microSD header is, in pin order,
3V3, CS, SCK, MOSI, MISO, GND. Do not use the old GPIO 8-11 SD map with v9.

For the opposite end of the mast link, connect mast TX GPIO8 to base RX GPIO2
and mast RX GPIO9 to base TX GPIO3. The matching streaming records and numeric
ranges are documented in [the mast README](../mastModule/README.md).

## Keyboard protocol

The keyboard module sends one newline-terminated record per action:

```text
D,4
```

The second field is the HID usage number configured in
[`../usbToUART/keyboardSettings.png`](../usbToUART/keyboardSettings.png). See
[KEYBOARD_MAP.md](KEYBOARD_MAP.md) and
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

For integration diagnostics, `MOTOR PROBE` requests BLD-510B actual-speed
register `0x8018` without starting the lap. `MOTOR STATUS` reports validated
Modbus replies, CRC errors, exceptions, timeouts, byte counts, and the last raw
reply. `STATUS` includes the same `@MOTOR` record. A valid Modbus exception
proves the electrical link even though it indicates a rejected request.

The v9 `ESC.TTL` header is pin 1 GND, pin 2 GP9/RX, pin 3 GP8/TX, pin 4
3.3 V. On automatic-direction converter boards whose UART pins are labelled
from the converter's perspective, connect converter `TXD` to header pin 2
(Pico RX) and converter `RXD` to header pin 3 (Pico TX). Do not connect the
two transmitters label-to-label. The lap port uses the Pico's native UART1 so
it does not consume two additional PIO state machines.

The v9 schematic ties both PDN/UART pads of each TMC2208-compatible carrier
together, then connects that node through 1 kΩ to one Pico GPIO (twist GP28,
Z GP15, pump GP19). The electrical routing therefore supports single-wire
half-duplex UART. Twist and Z release their transmit-only `SerialPIO` links
after startup configuration and run through STEP/DIR plus hardware EN. The
pump retains its transmit-only UART because its velocity and direction are
changed through TMC registers at runtime. Firmware does not claim TMC readback
until a tested single-pin turnaround implementation is added.

Genuine SilentStepStick-compatible carriers expose PDN/UART on the header and
need no carrier modification with this PCB. Some clone carriers require their
on-board UART solder jumper to be bridged. Verify continuity from the carrier's
PDN/UART connection to the PCB-side 1 kOhm UART resistor before modifying it;
the required jumper is carrier-specific.

Use `SD RETRY` after inserting a card, `SD STATUS` to inspect initialization,
and `SD LIST` to verify that the root directory and supported `.asc`/`.fct`
files are readable. Failed automatic initialization retries are limited to one
per second so a missing card does not stall the rest of the control loop.

`quit`, Ctrl+C, and EOF send `STREAM OFF` and `STOP` before closing. Keep the
machine mechanically safe during bench tests: begin with motors unloaded or
disabled, use small jogs, and retain physical power cutoff access.
