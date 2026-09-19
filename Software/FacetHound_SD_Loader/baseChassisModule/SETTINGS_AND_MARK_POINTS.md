# Base settings and mark points

This base firmware implements the display settings contract documented
in the display project's `SETTINGS_PROTOCOL.md`.

Notable behavior:

- Mark points are a dynamically sized `std::vector<float>` rather than a
  16-element array.
- The version-4 raw-flash record migrates version-2 and version-3 records and
  persists up to 16,320 positions in the final 64 KiB of a 2 MiB flash device.
- Index direction is persistent and changes both encoder and motor correction
  signs. A direction change stops and reacquires twist position before motion.
- Table Adapter is persistent and subtracts 45 degrees from calibrated tip.
- Automatic spin-servo enable is persistent and is controlled from the menu.
- Index, tip, Z, or all encoder readouts can be reset to zero from Settings.
  Index/tip zero offsets persist; the display-side incremental Z count is reset
  directly on the display controller.
- Z polarity and the flow display conversion (`mL/min per flow tick`) are
  persistent settings.
- Special Commands includes signed 0.01-10 RPM continuous index rotation. It
  is open-loop, suspends normal index locking, stops on page exit, and has a
  1.5-second command-lease timeout if display keepalives disappear.
- Position values are returned to the display in eight-value pages.
- SD coordinate loading uses bounded GPIO SPI on GPIO 10/11/12/13. It browses
  root-level `.asc`/`.fct` files and loads flattened cuts into the mark sequence.
- Pico 2 constructs the loaded design's convex mesh at runtime and streams up
  to 512 planes, 1,024 vertices, and 2,048 edges to the display.

See `SD_GEM_LOADER.md` for wiring, formats, limits, and the runtime protocol.

The top-level key constants in `baseChassisModule.ino` make the HID mapping
easy to change. Current menu mapping is former wheel-index/top-left = open/back
or coarser numeric tier, twist wheel = up/down, twist click = select, former
servo key = finer tier, and former cheat key = delete.

The firmware compiled successfully with Arduino-Pico 5.6.0 for the Raspberry
Pi Pico 2 base module.
