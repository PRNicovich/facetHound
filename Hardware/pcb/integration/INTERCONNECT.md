# Release module interconnect

Open **ModuleInterconnect.sch** in Eagle. It is a six-sheet, electrically wired
system/harness schematic, not a PCB layout. Module symbols expose connector pad
identities and MCU GPIO functions. No legacy board routing or manufacturing ZIP
was changed. Connector numbers below are PCB pad numbers, not an assumed view
of a cable plug; the old JST library has multiple gates each with pin name `1`.

| Base v9 header/pad | Peripheral connector/pad | Signal |
|---|---|---|
| MAST 1 / 2 / 3 / 4 | Mast J1 4 / 3 / 2 / 1 | 5V / mast TX8 to base RX2 / base TX3 to mast RX9 / GND |
| DISPLAY 1 / 2 / 3 / 4 | Display J3 4 / 3 / 2 / 1 | 5V / display TX8 to base RX4 / base TX5 to display RX9 / GND |
| KEYS 1 / 2 / 3 / 4 | USBHost J2 1 / 2 / 3 / 4 | 5V / host TX0 to base RX6 / base TX7 to host RX1 / GND |
| ESC.TTL 4 / 3 / 2 / 1 | Adapter VCC / RX input / TX output / GND | 3V3 / base TX8 / base RX9 / GND |
| MICROSD 1 / 2 / 3 / 4 / 5 / 6 | SD 3V3 / CS / MOSI / CLK / MISO / GND | 3V3 / GP10 / GP11 / GP12 / GP13 / GND |

Mast/keyboard UART: 115200. Display UART: 460800. All MCU UART signals are 3.3 V.
Adapter bus terminals connect A+ to BLD A+, B- to BLD B-, with shared ground.
The adapter is automatic-direction; no DE/RE pin is driven. Terminal functions,
not adapter LEDs, wire colors, or ambiguous TX/RX silkscreen, define connections.
Do not apply 24 V to any MCU/header logic supply. BLD motor power uses its own
24 V terminals. This drawing does not prescribe motor phase/Hall colors.

## Integration modifications to preserve

- **AMT MODE disconnected:** Mast J5 pad 2 and J2 pad 2 must NOT be connected to
  the encoders' MODE leads. The v8 PCB routes these to U2 B7/B8 (MCU GP26/27);
  the as-tested cable leaves them disconnected. This is a harness change, not
  a claim that those traces have been removed from the existing PCB artwork.
- Mast J5 tip: CS pad 1 → GP14, clock pad 4 → GP13, data pad 5 → GP12, through
  U2. J2 index: CS pad 1 → GP15, clock pad 4 → GP6, data pad 5 → GP7, through U2.
  Each uses pad 6 supply and pad 3 GND. Preserve the working MODE-disconnected
  harness. The mast level shifter is not a claim that the v9 base has one.
- Display J4 Z encoder: pad 1=5V, pad 2=GND, pad 3=A→R3/R6→GP10,
  pad 4=B→R4/R5→GP11. Preserve the repaired GP11 connection. Firmware disables
  internal pulls on GP10/11; the existing dividers remain in the board schematic.
- J1/J3 pads 5–8 and J6/J2 pass-through headers on the peripheral boards are
  not UART alternatives. Their cable routing is not inferred here.

## Authoritative sources

- `../v9/BaseModule.sch` (no base level shifter).
- `../v8/MastModule_v8.sch`.
- `../v8/Display-KeyboardModule_v8.sch`.
- `../v8/USBHostModule.sch`.
- Firmware: `Software/FacetHound_SD_Loader` base, mast, display, usbToUART.

The standalone interconnect prevents accidental schematic/board mismatches in
the existing manufactured designs. XML/net connectivity can be checked without
Eagle; native Eagle visual/ERC review and power-off continuity checks remain
required before building another harness. No manufacturing sign-off is implied.
