# Mast module

This is the current mast firmware packaged beside the matching
`baseChassisModule` and `displayModule_v6_gem` projects. It is built for the
Waveshare RP2040 Zero with the Arduino-Pico core and the `RunningAverage`
library.

## Pin map

| Function | Mast GPIO |
|---|---:|
| Force sensor analog input | 28 |
| Tip encoder CS / DATA / CLOCK | 14 / 12 / 13 |
| Twist encoder CS / DATA / CLOCK | 15 / 7 / 6 |
| UART TX to base | 8 |
| UART RX from base | 9 |

The UART is 3.3 V TTL at 115200 baud. Connect mast TX GPIO8 to base RX GPIO2,
mast RX GPIO9 to base TX GPIO3, and connect grounds. Do not connect either UART
signal to RS-232 voltage levels.

## Base protocol compatibility

The current base consumes newline-terminated ASCII records:

```text
@tip,47850
@twist,2048
@force,3200
```

The mast streams all three records every 20 ms. A received `?` also produces
an immediate complete snapshot for compatibility with the older polling base.
The active base does not need to poll.

| Record | Mast range | Base expectation | Result |
|---|---:|---:|---|
| `@tip` | 0-131064 | 131072 counts/revolution | Compatible |
| `@twist` | 0-4095 | 4096 counts/revolution | Compatible |
| `@force` | 0-131040 | accepted through 140000 | Compatible |

Twist uses a circular average, so samples around the 4095/0 boundary do not
average incorrectly through the middle of the wheel. Tip and force retain the
classic oversampled ranges used by existing calibration values. The machine
UART does not depend on USB; optional USB mirroring is disabled by default with
`MIRROR_TELEMETRY_TO_USB`.

The obsolete mast protocol used space-delimited `i`, `l`, and `e` records only
after receiving `?`. That format is not accepted by the current base and is no
longer emitted.
