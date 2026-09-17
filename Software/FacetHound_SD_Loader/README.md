# Facet Hound SD loader build

This package contains the matching base, mast, display, and USB keyboard-host
firmware for loading GemCad designs from a microSD card through Display
Settings and operating the Facet Hound hardware as one system.

## Source targets

The complete Arduino sources are in `baseChassisModule/` for the Raspberry Pi
Pico 2 base, `mastModule/` for the Waveshare RP2040 Zero sensor module, and
`displayModule_v6_gem/` for the Waveshare RP2040 Zero display with the existing
HX8357D wiring. The dedicated keyboard host is in `usbToUART/`. Build and board
settings for all four controllers are collected in `FLASHING.md`.

See `INTEGRATION_TEST.md` for the staged three-module bring-up procedure and
short-circuit isolation checklist. The mast's exact UART contract and sensor
ranges are documented in `mastModule/README.md`.

## SD hookup

| microSD reader | Base GPIO |
| --- | ---: |
| CS | 10 |
| SCK | 11 |
| MOSI | 12 |
| MISO | 13 |

This is the connector order routed by `Hardware/pcb/v9/BaseModule.sch`.
Because it is not a legal hardware-SPI mapping, the firmware
uses Arduino-Pico's PIO-backed `SoftwareSPI`. The active motor link uses GPIO
8/9 at the v9 `ESC.TTL` header and an external automatic-direction RS-485
module; GPIO 26/27 are not routed on the v9 board.

Use a FAT16/FAT32 card. Put `.asc` or `.fct` files in its root, insert it, then
open **Settings -> Load SD design**. Scroll with the twist wheel and click to
load. The existing next/previous mark controls then walk the file's cuts in
GemCad order and home the index axis to the selected cut.

On first load the base looks for a same-named `.fhc` geometry cache. A cache is
used only when its format version, source byte count, and source CRC32 match.
Otherwise the Pico rebuilds the geometry and writes a fresh cache beside the
source. For example, `round.asc` produces `round.fhc`. Prebuild one file or a
whole folder on a PC with `tools/bulk_convert_gems.py` so the Pico only needs to
validate and load the cache.

Classic mode shows `T<tier> F<facet>`, signed target angle, and index in the
mark row. Dynamic/static gem modes show the loaded title and target angle/index
in the header/HUD. The first imported GemCad name in a tier is propagated to
all of that tier's facets, so labels such as `C1`, `C2`, `P1`, and `G2` remain
beside the tip angle throughout the tier; an unnamed table cut is shown as `T`.
GemCad center-to-facet distance is used only as the plane equation distance
during mesh construction; it is not machine Z and is never shown or commanded
as a Z target.

The Pico 2 now rebuilds the loaded design's 3D wireframe by incrementally
clipping a convex mesh against the GemCad half-space planes, then streams it to
both dynamic and static display modes. Redundant planes are skipped without
testing plane triples, and an adaptive temporary cube is never retained in the
result. The compiled `gem_data.h` design remains only as a startup/failure
fallback. Geometry is bounded at 512 planes, 1,024 vertices, and 2,048 edges.
Loading still does not command mast or Z motion; index selection uses the
existing twist control and safety behavior.

Settings also now includes encoder-zero actions (index, tip, Z, or all), Z-axis
polarity, flow conversion calibration in mL/min per raw flow tick, and a Special
Commands page for signed 0.01-10 RPM constant index spin. Spin stops on page
exit and times out if the display keepalive disappears.

See `baseChassisModule/SD_GEM_LOADER.md` for the parser behavior, cache format,
`.fct` coordinate format, limits, and serial protocol. See
`baseChassisModule/BASE_WIRING_AND_TEST.md` for the full pin map, dedicated
keyboard UART, and USB CDC bench console. The underlying PC control protocol is
documented in `baseChassisModule/PC_USB_API.md` so future software can drive the
base directly without depending on the supplied console UI.

## Verification

- Base source compiled with Arduino-Pico 5.6.0 for Raspberry Pi Pico 2:
  172,876 bytes flash; 16,392 bytes static RAM.
- Mast source compiled with Arduino-Pico 5.6.0 for Waveshare RP2040 Zero:
  60,972 bytes flash; 9,672 bytes static RAM.
- Keyboard USB-host source compiled with Arduino-Pico 5.6.0 for Waveshare
  RP2040 Zero at 240 MHz, `-O`, and Adafruit TinyUSB: 90,308 bytes flash;
  33,004 bytes static RAM.
- Display source compiled with Arduino-Pico 5.6.0 and the existing HX8357D
  flags: 392,832 bytes flash; 29,228 bytes static RAM.
- The reference `gemLoader.py` was exercised against nine supplied ASC
  fixtures (22 to 240 cuts; 64/80/96 wheels), including continuation lines,
  fractional indexes, and named facets.
- The double-precision convex-clipper mirror reproduced all nine reference
  fixtures exactly in topology. The largest had 240 planes, 466 vertices, and
  704 edges; maximum nearest-vertex error was `3.44e-14` model units.
