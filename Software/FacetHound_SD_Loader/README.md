# Facet Hound SD loader build

This package contains the paired base and display firmware for loading GemCad
designs from a microSD card through Display Settings.

## Flash targets

- `firmware/baseChassisModule_Pico2_SD.uf2` is compiled for **Raspberry Pi
  Pico 2 (RP2350)**.
- `firmware/displayModule_Waveshare_RP2040_Zero_SD.uf2` is compiled for the
  existing **Waveshare RP2040 Zero** display module with the HX8357D pin/setup
  flags used by the source project.

The complete Arduino sources are in `baseChassisModule/` and
`displayModule_v6_gem/`.

## SD hookup

| microSD reader | Base GPIO |
| --- | ---: |
| CS | 8 |
| SCK | 9 |
| MISO | 10 |
| MOSI | 11 |

These are the retired legacy lap-motor/level-shifter signals. Because this
requested ordering is not a legal hardware-SPI1 mapping, the firmware uses the
Arduino-Pico PIO-backed `SoftwareSPI` implementation. The active motor RS-485
link remains on GPIO 26/27.

Use a FAT16/FAT32 card. Put `.asc` or `.fct` files in its root, insert it, then
open **Settings -> Load SD design**. Scroll with the twist wheel and click to
load. The existing next/previous mark controls then walk the file's cuts in
GemCad order and home the index axis to the selected cut.

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

See `baseChassisModule/SD_GEM_LOADER.md` for the parser behavior, `.fct`
coordinate format, limits, and serial protocol.

## Verification

- Base source compiled with Arduino-Pico 5.6.0 for Raspberry Pi Pico 2:
  165,708 bytes flash; 16,224 bytes static RAM.
- Display source compiled with Arduino-Pico 5.6.0 and the existing HX8357D
  flags: 406,464 bytes flash; 25,120 bytes static RAM.
- The reference `gemLoader.py` was exercised against nine supplied ASC
  fixtures (22 to 240 cuts; 64/80/96 wheels), including continuation lines,
  fractional indexes, and named facets.
- The double-precision convex-clipper mirror reproduced all nine reference
  fixtures exactly in topology. The largest had 240 planes, 466 vertices, and
  704 edges; maximum nearest-vertex error was `3.44e-14` model units.
