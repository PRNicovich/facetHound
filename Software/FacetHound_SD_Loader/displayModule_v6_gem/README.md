# Facet Hound RP2040 display v6

This Arduino-Pico firmware runs the 320 x 480 HX8357D display on the legacy
RP2040 Zero wiring. It provides three persisted boot modes:

- `CLASSIC`: the original machine-status UI and custom fonts.
- `DYNAMIC`: a smoothly animated gem wireframe driven by live machine pose.
- `STATIC`: TOP, BOTTOM, FRONT, and SIDE gem diagrams.

The firmware listens to the instrument UART and USB CDC simultaneously. Both
use the existing newline-delimited `@KEY,value` protocol, so the USB connection
is also the proxy/test input; no alternate firmware image is necessary.

A transient settings menu overlays any of the three screens. It currently edits
display mode and is structured for later base-module settings.

## Hardware and Arduino setup

Use Earle F. Philhower's **Arduino-Pico** core and install:

- TFT_eSPI
- PioEncoder

`SerialPIO` is supplied by Arduino-Pico. Select the RP2040 Zero-compatible
board definition and use the normal 133 MHz CPU setting initially.

The legacy pin assignment is unchanged:

| Function | GPIO |
| --- | ---: |
| TFT SCLK | 2 |
| TFT MOSI | 3 |
| TFT CS | 5 |
| TFT DC | 6 |
| TFT RESET | 7 |
| Instrument UART TX | 8 |
| Instrument UART RX | 9 |
| Encoder | 10 |

TFT_eSPI compiles its driver configuration as part of the library, rather than
from the sketch. Back up the library's current `User_Setup.h`, then use
`User_Setup_HX8357D_RP2040.h` from this directory as its setup. It selects the
HX8357D driver, these pins, and the proven 62.5 MHz SPI setting.

Open `displayModule_v6_gem.ino` in Arduino IDE and upload it. The first boot
uses `CLASSIC`; later selections are stored in flash.

The packaged project also includes `displayModule_v6_gem_HX8357D.uf2`, already
compiled for the Waveshare RP2040 Zero with the exact HX8357D settings above.
To use it, hold BOOT while connecting/resetting the RP2040, then copy the UF2
onto the `RPI-RP2` drive. Building from source is preferable after changing the
embedded gem or UI.

## Select a screen

Send one of these commands through either the instrument UART at 460800 baud or
the USB serial port:

```text
@MODE,CLASSIC
@MODE,DYNAMIC
@MODE,STATIC
```

A changed mode is committed to flash and the RP2040 reboots. Query the current
mode without rebooting with:

```text
@MODE,?
```

Numeric aliases `0`, `1`, and `2` mean classic, dynamic, and static.

## Settings menu

Open the menu and navigate it through either serial connection:

```text
@MENU,1
@MENUKEY,UP
@MENUKEY,DOWN
@MENUKEY,SELECT
@MENUKEY,BACK
```

The menu asks the base for one settings snapshot with `@CFGGET,ALL` when it
opens. Edits use `@CFGSET,id,value` or `@CFGACTION,id,value`, followed by a base
reply. Settings do not stream continuously. Long mark-point lists are fetched
in eight-value pages while that screen is visible. See `SETTINGS_PROTOCOL.md`
for the implemented base-module contract and physical control mapping.

The settings UI includes display mode, CW/CCW index direction, Table Adapter
(subtracts 45 degrees from the reported tip angle), automatic servo enable,
wheel index, Z polarity, flow conversion calibration, encoder-zero actions,
Special Commands with constant index spin, reset spacing, an SD design browser,
and direct numeric editing of a variable-length mark-point list. The card is attached to the base module;
the display requests six-name pages and loads a selected `.asc` or `.fct` file
through the settings protocol.

## USB proxy/testing

Install pyserial on the laptop:

```powershell
py -m pip install pyserial
```

Run animated test telemetry (replace `COM7`):

```powershell
py usb_proxy_test.py COM7 --demo 30
```

Change mode:

```powershell
py usb_proxy_test.py COM7 --mode dynamic
```

Or start an interactive protocol terminal:

```powershell
py usb_proxy_test.py COM7
```

The display sends encoder updates to both outputs as `@ZENC,count`.

For a visual test that runs directly in Spyder, open and run
`settings_menu_spyder.py`. It uses Matplotlib and needs no serial hardware by
default. Set `SERIAL_PORT = "COM7"` near the top to drive the RP2040 while using
the simulator. Arrow keys emulate twist-wheel rotation, Enter is wheel click,
Backspace is the top-left/coarser key, `F` is finer, Delete removes a mark point,
and `M` opens/closes the menu.

## Replace the embedded gem

`gem_data.h` contains the startup fallback mesh. When the base loads an ASC/FCT
design, the Pico 2 calculates the convex solid and streams its vertices, edges,
and facet ownership to this display. Dynamic mode rotates those runtime 3D
vertices; static mode projects the same runtime vertices into its four panels.

From a checkout containing `gemUtils/gem_generator.py`, run:

```powershell
py export_gem_data.py path\to\design.asc gem_data.h --gem-utils path\to\gemUtils
```

Rebuild and upload the sketch after regenerating the fallback header. The
supplied fallback was generated from `pc01391.asc` (`PC 01.391 Rings of Fire`).

Runtime SD jobs replace the displayed wireframe after a complete mesh transfer,
as well as replacing the machine cut sequence. GemCad center-to-facet distance
is used as a plane equation term during geometry construction, but it remains
unrelated to machine Z and is not shown as a Z target.

GemCad `n` names are treated as tier labels: the first name found in a tier is
shown for every facet in that tier beside the live tip angle (`C1`, `C2`, `P1`,
`G2`, and so on). An unnamed table plane is shown as `T`.

## Protocol mapping used by gem modes

- `T`: target index position.
- `E`: signed index error; live index is reconstructed as `T + E`.
- `TIP`: live tilt angle.
- `WIDX`: machine index-wheel resolution (normalized to the active runtime or
  fallback design).
- `ZMM`, `RPM`, `RPV`, `DIR`, `FLW`, `FLD`, and force `F`/`N`: lower HUD.

All other legacy fields continue to be parsed for classic mode.
