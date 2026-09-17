# Flashing the complete electronics set

These four sketches are the matching current source set. Build them from this
folder; older UF2 artifacts elsewhere in the repository predate the latest
integration changes.

| Controller | Sketch | Board target | Special settings |
|---|---|---|---|
| Base | `baseChassisModule/baseChassisModule.ino` | Raspberry Pi Pico 2 | Arduino-Pico 5.6.0, normal board defaults |
| Mast | `mastModule/mastModule.ino` | Waveshare RP2040 Zero | `RunningAverage` library |
| Display | `displayModule_v6_gem/displayModule_v6_gem.ino` | Waveshare RP2040 Zero | Install `PioEncoder`; force `User_Setup_HX8357D_RP2040.h` into TFT_eSPI |
| Keyboard USB host | `usbToUART/usbToUART.ino` | Waveshare RP2040 Zero | 240 MHz, Optimize `-O`, Adafruit TinyUSB stack |

## Arduino IDE checklist

1. Install Earle Philhower's Arduino-Pico core version 5.6.0.
2. Install `RunningAverage`, `TFT_eSPI`, `PioEncoder`, and
   `Adafruit TinyUSB Library` as required by the sketches above.
3. For the display only, configure TFT_eSPI with
   `displayModule_v6_gem/User_Setup_HX8357D_RP2040.h` before compiling.
4. Select the board and options from the table for each sketch. In particular,
   restore normal defaults after flashing the keyboard bridge so its 240 MHz
   overclock is not accidentally applied to another module.
5. Flash and smoke-test each controller by itself before connecting UARTs or
   motor power. Then follow `INTEGRATION_TEST.md`.

## Verified builds

| Controller | Flash | Static RAM |
|---|---:|---:|
| Base | 172,876 bytes | 16,392 bytes |
| Mast | 60,972 bytes | 9,672 bytes |
| Display | 392,832 bytes | 29,228 bytes |
| Keyboard USB host | 90,308 bytes | 33,004 bytes |

## Inter-controller links

All logic UARTs are 3.3 V TTL and require a shared ground.

| Link | Wiring | Baud |
|---|---|---:|
| Keyboard bridge to base | bridge TX0 -> base RX6; bridge RX1 <- base TX7 | 115200 |
| Mast to base | mast TX8 -> base RX2; mast RX9 <- base TX3 | 115200 |
| Display to base | display TX8 -> base RX4; display RX9 <- base TX5 | 460800 |

Do not join the modules through a PCB with a suspected rail short. Validate the
PCB rails first, then add these links one at a time as described in the
integration guide.
