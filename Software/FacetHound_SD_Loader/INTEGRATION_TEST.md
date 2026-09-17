# Three-module integration test

Bring the machine up in layers. A suspected PCB short is a power fault first,
not a firmware or communications problem.

## 1. Isolate the short before connecting controllers

1. Disconnect USB, external power, motors, drivers, display, mast, keyboard,
   SD reader, and every inter-board cable from the suspect PCB.
2. Inspect both sides under magnification for solder bridges, reversed parts,
   conductive debris, damaged vias, and connector pins touching planes.
3. With power removed, measure resistance from each power rail to ground in
   both probe polarities. Record the readings rather than relying only on the
   continuity beeper; capacitors may initially look like a short while charging.
4. Split the board electrically wherever connectors, jumpers, ferrites, or
   zero-ohm links permit, then repeat the measurements to narrow the section.
5. Power only a rail whose voltage and polarity are known, using a bench supply
   with a conservative current limit. Stop if the supply immediately current
   limits or a component heats unexpectedly. A thermal camera, careful IPA
   evaporation, or freeze spray can identify the heating part without raising
   the current aggressively.
6. Verify every required rail at its test point and confirm no unpowered rail
   is being back-fed through a UART, USB cable, or GPIO before reconnecting a
   controller.

Do not use resistance mode on a powered circuit. Do not inject voltage into an
unknown signal net, and do not connect USB and external supplies together until
their grounding and back-feed paths are understood.

## 2. Controller-only smoke tests

Test each controller separately before joining them:

- **Display:** power and restart it several times. Confirm the complete splash,
  persisted UI mode, and no reset loop.
- **Mast:** verify 3.3 V and sensor supplies, then confirm encoder clock/CS
  activity. Open the mast's USB serial port during reset to enable automatic
  diagnostic mirroring, then confirm `@tip`, `@twist`, and `@force` records.
  No special bench build is required.
- **Base:** leave motor power disabled. Connect its native USB and run
  `tools/base_module_console.py`; `state` must return a valid snapshot and
  `stop` must acknowledge.
- **Keyboard host:** power its protected keyboard VBUS without the base UART
  connected. Confirm 5 V VBUS and 3.3 V controller rails remain stable, then
  attach a keyboard and check for USB-host resets or excess supply current.

## 3. Base plus mast

Wire common ground first, then mast TX8 to base RX2 and base TX3 to mast RX9.
Both sides use 115200 baud, 3.3 V TTL.

Run the PC console:

```text
stream 250
```

Pass criteria:

- `@IO,mast_rx=...` increases continuously.
- `mast_link=up` and `mast_age_ms` remains well below 500.
- `tip` changes smoothly through mast motion.
- `actual` follows twist motion and crosses the wheel rollover without a large
  false jump.
- `force` increases with sensor load and stays below 140000.
- The base does not repeatedly reset or print malformed-line errors.

If `mast_rx` stays zero, check crossed TX/RX, shared ground, 115200 baud, and
3.3 V idle-high levels. If bytes rise but values do not change, capture the raw
UART and verify each line begins with `@` and ends with newline.

## 4. Add display, keyboard, and SD

1. Add the display UART: base TX5 to display RX9, base RX4 from display TX8,
   plus common ground. The display link is 460800 baud. On the v8 PCBs the
   four-conductor cable is fully reversed: base connector pins 1/2/3/4 land on
   display connector pins 4/3/2/1. Confirm that mapping with continuity before
   applying power.
2. Confirm telemetry appears in Classic, Dynamic, and Static modes.
3. Add the dedicated keyboard bridge UART: bridge TX0 to base RX6 and bridge
   RX1 to base TX7. Verify A opens Settings, B/C change tiers, H offsets marks,
   and the twist wheel operates the menu. `STATUS` should show
   `keyboard_link=up` even before a key is pressed.
4. Add the SD reader on base GPIO8-11 and load a known ASC/FHC pair. Confirm the
   loaded facet, target tip/index, dynamic highlight, and tier buttons agree.

With the base USB console open, send `STATUS`. A healthy display return path
reports `display_link=up`; repeated `STATUS` calls about a second apart show
`display_rx` increasing because the display sends a heartbeat. If the display
updates but `display_link=down`, only the base-to-display direction works. If
the display stays stale but `display_link=up`, only display-to-base works or the
base TX/display RX conductor is wrong.

The current base also reports `display_roundtrip`. When this is `up`, the base
has sent a query which the display parsed and answered, proving both UART
directions. With sensors disconnected, ordinary values remain constant even on
a healthy display. Send `TEST DISPLAY ON` to animate synthetic tip, index, Z,
RPM, and flow values; send `TEST DISPLAY OFF` afterward.

For mast or keyboard connector trials that would otherwise require reflashing
several modules, the two `*_UART_SWAP_TRIAL` constants at the top of the base
sketch reverse only the selected base GPIO pair. Upload only the base and test
one link at a time. The normal v8 settings are both `false`.

## 5. Add motion last

Keep mechanical loads clear and use the lowest practical driver current during
first motion tests. Add one subsystem at a time: index, Z, pump, then lap RS-485.
After each connection, repeat `state`, a small jog, and `stop`. Do not proceed
to the next motor if supply current, direction, encoder feedback, or emergency
stop behavior is wrong.

The USB console API and telemetry definitions are in
`baseChassisModule/PC_USB_API.md`. Full base wiring is in
`baseChassisModule/BASE_WIRING_AND_TEST.md`.
