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
| `KEY <0..255>` | Inject one configured HID usage through the same action router as the UART keyboard | `@ACK,KEY,<hid>` |
| `JOG TWIST <index-units>` | Set a relative index target and enable index lock | `@ACK,JOG,TWIST,<target>` |
| `JOG Z <signed-steps>` | Request a relative Z move in raw motor steps | `@ACK,JOG,Z,<steps>` |
| `RPM <0..200>` | Set lap-speed command in RPM | `@ACK,RPM,<rpm>` |
| `MOTOR CW` | Set lap direction clockwise | `@ACK,MOTOR` |
| `MOTOR CCW` | Set lap direction counter-clockwise | `@ACK,MOTOR` |
| `MOTOR OFF` | Set lap RPM command to zero | `@ACK,MOTOR` |
| `FLOW <0..750>` | Set raw pump velocity; displayed mL/min uses the configured conversion | `@ACK,FLOW,<value>` |
| `PUMP FWD` | Set forward pump direction | `@ACK,PUMP` |
| `PUMP REV` | Set reverse pump direction | `@ACK,PUMP` |
| `PUMP OFF` | Stop the pump direction command | `@ACK,PUMP` |
| `STOP` | Stop index, Z, lap, and pump commands | `@ACK,STOP` |

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
@IO,mast_rx=1234,key_rx=37,display_rx=8912,display_link=up,display_age_ms=237,usb_rx=44
```

The display sends an idle heartbeat once per second, so `display_rx` increases
even when its encoder is stationary. `display_link` is `up` when a byte arrived
from the display in the last 2.5 seconds; `display_age_ms` is the time since the
most recent byte (`0` means none has ever arrived).

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
