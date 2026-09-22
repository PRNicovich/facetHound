# Base-module PC USB API

The base firmware exposes a line-oriented control and telemetry API over the
Pico's native USB CDC serial port. `tools/base_module_console.py` is a reference
client, not a required intermediary: another PC program can open the same COM
port and send these commands directly.

This API is separate from the dedicated keyboard UART on GPIO 6/7 and from the
base-to-display UART. Using USB CDC does not reconfigure either hardware link.

## Transport and framing

- USB CDC serial, conventionally opened as 115200 baud, 8-N-1. USB CDC does
  not physically depend on that baud rate, but clients should use 115200 for
  consistency with the supplied console.
- Commands and responses are printable ASCII terminated by `\n`.
- The firmware accepts `\r\n` and ignores `\r`.
- Commands are case-insensitive and fields are separated by spaces.
- Maximum accepted command line is 127 bytes, excluding the terminator.
- Replies beginning with `@ACK` indicate acceptance, `@ERR` indicates a bad
  command or argument, and `@STATE` / `@IO` are telemetry.

The port also carries boot diagnostics, cache messages, and asynchronous
`@KEY,<hid>` records. Clients should therefore parse by prefix and ignore
unknown lines for forward compatibility.

## Commands

| Command | Meaning | Typical reply |
|---|---|---|
| `HELP` | List commands supported by the running firmware | One or more `@HELP,...` lines |
| `STATUS` | Emit one complete state and I/O snapshot | `@STATE,...` then `@IO,...` |
| `STREAM ON [ms]` | Stream snapshots every 50-5000 ms; default 250 ms | `@ACK,STREAM,ON` |
| `STREAM OFF` | Stop periodic snapshots | `@ACK,STREAM,OFF` |
| `PROBE` | Immediately query the mast, display, and lap Modbus links | `@ACK,PROBE,MAST_DISPLAY_AND_LAP` |
| `TRACE ON` / `TRACE OFF` | Mirror complete incoming peripheral UART lines to PC USB | `@ACK,TRACE,ON/OFF` |
| `TEST DISPLAY ON` | Send changing synthetic values to the display without changing machine state | `@ACK,TEST,DISPLAY,ON` |
| `TEST DISPLAY OFF` | Restore normal machine telemetry | `@ACK,TEST,DISPLAY,OFF` |
| `TEST DISPLAY LOOPBACK` | Five-second base GPIO4/5 loopback test | `@ACK,TEST,DISPLAY,LOOPBACK,5_SECONDS` |
| `KEY <0..255>` | Inject one configured HID usage through the same action router as the UART keyboard | `@ACK,KEY,<hid>` |
| `JOG TWIST <index-units>` | Set a relative index target and enable index lock | `@ACK,JOG,TWIST,<target>` |
| `INDEX PROBE` | Explicit motion test: 128 positive STEP pulses at 300 pulses/s maximum, then sample encoder delta and release; requires fresh valid encoders, axes unlocked and lap paused | `@INDEX_PROBE,...,suggested_motor_sign=...` (does not change calibration) |
| `JOG Z <signed-steps>` | Request a relative Z move in raw motor steps | `@ACK,JOG,Z,<steps>` |
| `RPM <0..1500>` | Set lap-speed command in RPM (no automatic startup boost) | `@ACK,RPM,<rpm>` |
| `MOTOR CW` | Set lap direction clockwise | `@ACK,MOTOR` |
| `MOTOR CCW` | Set lap direction counter-clockwise | `@ACK,MOTOR` |
| `MOTOR OFF` | Set lap RPM command to zero | `@ACK,MOTOR` |
| `MOTOR PROBE` | Queue a non-motion Modbus read of lap fault/status | `@ACK,MOTOR,PROBE` |
| `MOTOR DEMOPROBE` | One read-only status request using the exact uiReno BLD510B library; stop motion and unlock Z first | `@ACK,MOTOR,DEMOPROBE`, then `@BLD_DEMO,ok=...,error=...,fault=...,run=...` |
| `MOTOR LOOPBACK` | Adapter disconnected, GP8 TX jumpered to GP9 RX; lap paused and index/Z unlocked; see `../INTEGRATION_ENCODER_BLD.md` | `@BLD_LOOPBACK,match=0/1,rx_bytes=...,hex=...` |
| `MOTOR STATUS` | Report Modbus validation counters and last raw reply | `@MOTOR,...` |
| `SD STATUS` | Report SD initialization and root-directory state | `@SD,...` |
| `SD PROBE` | With axes unlocked and lap paused, test CMD0 and initialization, then use SdFat for card initialization, sector-0 read and volume mounting. No formatting/file writes; follow with SD RETRY | `@SD_PROBE,...`, `@SD_INIT,...`, `@SD_FS,...` including library error/data codes |
| `SD RETRY` | With axes unlocked and lap paused, force SD reinitialization after insertion/wiring changes | `@SD_MOUNT,bus=gpio,...`, `@ACK,SD,RETRY,READY/MISSING` |
| `SD LIST` | List folders and `.asc`, `.gem`, `.gcs`, `.fct` files in the current directory | `@SD_DIR,<path>`, `@SD_FILE,<index>,<name>`, `@SD_TYPE,<index>,DIR/FILE` |
| `SD UP` | Go to the parent directory; root stays root. Rejected while a gem load is pending | `@SD_DIR,<path>` and refreshed display browser |
| `GEM LOAD <index>` | Load the index returned by SD LIST; axes unlocked and lap paused. Builds geometry or reads cache, then transfers it asynchronously | `@GEM,LOADED,<path>`, `@GEM,GEOMETRY,<cache-status>,vertices=...,edges=...,planes=...`, then `@GEM,DISPLAY_READY` or `@GEM,DISPLAY_ERROR,...` |
| `MODE CLASSIC\|STATIC\|DYNAMIC` | Select display mode without changing motor locks | `@ACK,MODE,<mode>` |
| `FLOW <0..750>` | Set raw pump velocity; displayed mL/min uses the configured conversion | `@ACK,FLOW,<value>` |
| `PUMP FWD` | Set forward pump direction | `@ACK,PUMP` |
| `PUMP REV` | Set reverse pump direction | `@ACK,PUMP` |
| `PUMP OFF` | Stop the pump direction command | `@ACK,PUMP` |
| `STOP` | Stop index, Z, lap, and pump commands | `@ACK,STOP` |

`GEM LOAD <index>` enters a folder when the selected SD LIST entry is a directory;
it does not start motion. Run `SD LIST` again after entering or leaving a folder.
Indices are relative to the current directory, shared with the display browser.
Native binary GEM support and limits: [GEM_AND_FOLDERS.md](../GEM_AND_FOLDERS.md).

Malformed values receive a specific `@ERR,<command>,<reason>` reply. Unknown
commands receive `@ERR,UNKNOWN,use HELP`.

`KEY` uses the HID usages in [KEYBOARD_MAP.md](KEYBOARD_MAP.md). For example,
`KEY 4` opens Settings, `KEY 5` selects the next loaded gem tier, and `KEY 6`
selects the previous tier. Menu suppression and menu-specific key meanings are
identical to the physical UART keyboard path.

## State telemetry

`@STATE` is a comma-separated set of `name=value` fields:

```text
@STATE,123456,tip=42.500,target=12.0000,actual=11.9980,error=0.0020,z_mm=1.2500,rpm_set=20,rpm_actual=19,flow=100.00,force=514.0,twist_lock=1,z_lock=0,motor_dir=1,flow_dir=2,mark=3/96,sd=ready,gem=round.asc
```

The second field is milliseconds since boot. Fields may be added later, so a
client should read values by name rather than fixed column position.

| Field | Meaning |
|---|---|
| `tip` | Actual calibrated tip angle in degrees |
| `target` | Target index in current wheel units |
| `actual` | Actual index in current wheel units |
| `error` | Current signed index error |
| `z_mm` | Actual Z position in millimetres |
| `rpm_set` / `rpm_actual` | Commanded and reported lap RPM |
| `flow` | Raw pump setpoint; not converted mL/min |
| `force` | Current raw force reading |
| `twist_lock` / `z_lock` | Axis lock state, `0` or `1` |
| `motor_dir` / `flow_dir` | Internal direction state |
| `mark` | One-based selected mark and total mark count |
| `sd` | `ready` or `missing` |
| `gem` | Active source filename or `none` |

The following `@IO` record contains cumulative received-byte counters:

```text
@IO,mast_rx=1234,mast_link=up,mast_age_ms=4,key_rx=37,keyboard_link=up,keyboard_age_ms=183,display_rx=8912,display_link=up,display_age_ms=237,usb_rx=44
```

`mast_link=up` requires a valid `tip`, `twist`, or `force` protocol record in
the last 3.5 seconds; random bytes do not count. The deliberately generous
integration timeout also supports older mast firmware that answers the base's
periodic `?` poll instead of streaming continuously. `mast_age_ms` is the time
since the last valid mast record (`0` means none has ever arrived).

The keyboard bridge publishes an idle health record once per second while its
USB-host core is responsive. `keyboard_link` uses a 2.5-second timeout, so the
link can be verified without pressing a key. Actual presses still produce the
asynchronous `@KEY,<hid>` record.

The display sends an idle heartbeat once per second, so `display_rx` increases
even when its encoder is stationary. `display_link` is `up` when a byte arrived
from the display in the last 2.5 seconds; `display_age_ms` is the time since the
most recent byte (`0` means none has ever arrived).

`display_roundtrip=up` is the stronger display test. The base sends
`@PING,BASE` and requires the display to echo `@PONG,BASE`, proving that both
UART directions and the display parser work. `display_link=up` with
`display_roundtrip=down` means only display-to-base has been proven.
An explicit `TEST DISPLAY ON` bypasses the handshake gate so it can test the
base-to-display direction even when the display-to-base direction is dead.

Display telemetry is serialized as one short protocol record every 10 ms.
This avoids overflowing the display controller's 32-byte SerialPIO receive
FIFO while it is rendering; older base builds sent the entire screen as one
large burst and could appear one-way even with correct wiring.

The base now uses larger receive queues and sends only `@PING,BASE` until a valid
display reply establishes the round trip. Normal or synthetic screen telemetry
does not start on a one-way link. `rx_overflow=MxKxDx` reports and clears each
SerialPIO overflow latch whenever `STATUS` is emitted.

For a base-side electrical loopback, power down, disconnect the display cable,
and jumper only signal pins 2 and 3 of the base `DISPLAY` connector. Do not
jumper either power pin. Power the base from USB, send
`TEST DISPLAY LOOPBACK`, then `STATUS` within five seconds. A
`display_loopback=up` result proves base GPIO5 TX, GPIO4 RX, and the base UART
software; the fault is then beyond the base connector.

`rx_levels=M1K1D1` reports the instantaneous mast, keyboard, and display RX pin
levels. UART idle is normally high (`1`); a persistent zero suggests a short,
unpowered transmitter, or incorrect connector mapping. `uart_map` reports the
compiled mast and keyboard trial mapping.

For a base-only pin-direction trial, change `MAST_UART_SWAP_TRIAL` or
`KEYBOARD_UART_SWAP_TRIAL` at the top of `baseChassisModule.ino`, then upload
only the base. Leave both `false` for the documented v8 wiring. Set only one at
a time so the result is unambiguous.

`@MOTOR` distinguishes a stopped motor from a dead Modbus link. `lap=up`
requires a correctly addressed, CRC-valid reply within 1.5 seconds. The record
also reports transmit count, received bytes, timeouts, CRC failures, Modbus
exceptions, and the last reply in hexadecimal. `lap_uart=pio` confirms the
BLD-510B is on the proven GP8/GP9 SerialPIO link; `tmc_uart=setup_only` describes
the three on-board stepper-driver configuration links; it is not the lap-motor
RS-485 link.

`@SD` reports card initialization attempts separately from root-directory
readability. STATUS, STREAM and settings readiness queries use cached state;
they never retry card initialization. After inserting a card use `SD RETRY`.
Explicit file operations can also attempt initialization, throttled to once
per second while the card is missing. These operations can block on SD timeouts.

`@KEY,<hid>` is emitted whenever a key arrives from the physical keyboard
UART. It allows a PC supervisory application to observe operator input without
intercepting the keyboard connection.

## Control guidance

- Send `STATUS` after connecting instead of assuming previous machine state.
- Treat jog acknowledgements as command acceptance, not proof that motion has
  completed; observe `actual`, `target`, and `error` in subsequent telemetry.
- Send `STOP` on application shutdown, communications failure, or operator
  abort. A physical power cutoff remains the authoritative safety mechanism.
- Only one PC process should own the USB CDC port at a time.
- Ignore response prefixes and fields that the client does not recognize.
- Do not use this diagnostic/control API as a hard real-time motion bus.

## Minimal Python example

```python
import serial

with serial.Serial("COM5", 115200, timeout=1) as base:
    base.write(b"STATUS\n")
    print(base.readline().decode("ascii", "replace").strip())
    print(base.readline().decode("ascii", "replace").strip())

    base.write(b"JOG TWIST 1\n")
    print(base.readline().decode("ascii", "replace").strip())

    base.write(b"STOP\n")
```

## Lap startup and diagnostics

The keyboard speed knob changes the saved/user setpoint by 5 RPM per detent.
On an explicit start with a positive setpoint below 300 RPM, the controller is
temporarily commanded to 300 RPM for 2.5 seconds after its enable-write ACK,
then returns to the setpoint. This includes the existing two-second ramp.
An absolute five-second limit from the start request also bounds the boost if
ACKs are lost. Pause/STOP cancels it; no automatic stall retries or current-limit
changes are made. A request at or above 300 RPM gets no boost.

`MOTOR STATUS`/`STATUS` includes `startup_boost`, `rpm_command_ack`,
`control_ack` (09 forward, 0B reverse, 0C brake), and the raw status `run` and
`fault` bytes. These distinguish the saved setpoint, acknowledged temporary
command, and measured speed. Command ACKs now require an exact register/value
echo, not merely any CRC-valid function-06 frame. For a direction that clicks
and stops, capture these fields before clearing the fault. A working forward
direction does not establish correct Hall/phase pairing for both directions.
