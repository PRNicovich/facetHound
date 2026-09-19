# HUD, exact selection and card trial

RPM follow-up: hand-spin reports of 51200 and 66560 match the speed-register
byte-order error exactly (wire 14 00 / 1A 00). Register 0x8018 is now decoded
low-byte-first, producing 200 / 260 RPM under the existing 20/pole-pairs scale.
Raw diagnostic fields remain unchanged. Status/fault register byte order is NOT
swapped. Absolute scale still needs a tachometer check; motor shudder is not
fixed merely by correcting the display reading.

Upload baseChassisModule and displayModule_v6_gem. Mast is unchanged.

- Static/dynamic highlight uses the JOB tier/facet identity, not the nearest
  angle/index. Unknown identity has no highlight. All 128 built-in poses/IDs
  were compared between base and display and match.
- Dynamic rotation follows live index with short shortest-path easing (65 ms
  time constant, approximately 95% complete in 195 ms). Physical tip
  only affects HUD values/errors; model inclination comes from the selected tier.
- Both HUDs show index and Z LOCK/FREE state from existing TLK/ZLK telemetry.
  These are commanded engagement states, not driver electrical feedback.
- Flow always shows its setpoint. Running: cyan droplet and white text;
  paused: grey droplet and text. State is flow_dir 0/2 running, 1/3 paused,
  regardless of setpoint magnitude. RPM text/arrow are white running, grey paused.
- Animated title card runs every restart, with a 300 ms fade to black at the end.
  Credit footer remains a separate sprite and only changes during the fade.
- Tier navigation preserves ordinal position in the source tier list, with
  the first entry as fallback if the new tier is too short. No angle/distance
  matching is used. This applies to SD-loaded and built-in designs. Existing next/previous
  facet commands are unchanged. No motion limits/acceleration were increased.

## SD electrical test

The eBay listing offers multiple module variants. Its title alone and 3.3 V
across the module input do not establish the card's actual supply. Identify the
module/regulator before changing its supply; never apply 5 V to a bare card or
Pico signal. No automatic formatting or erasing is performed.

The supplied reader photo resolves the pinout: 3V3, CS, MOSI, CLK, MISO, GND.
For the straight-through v9 header this means CS10, MOSI11, CLK12, MISO13.
Firmware previously had MOSI/CLK reversed; now corrected. Keep 3.3 V supply.
SD PROBE is an SPI command, not a card-detect GPIO test.

Display now consumes the existing @A actual-index record directly. Target and
error records can no longer mix across frames to produce a false pose. Until A
arrives the model holds its index; physical TIP cannot set model orientation.
Base sends A in the normal telemetry cycle and display-test mode. Other PC
proxies should send @A,<actual-index> along with T/E. Selected tier sets inclination.
Axis-gutter indicators are filled dots only: grey unlocked, axis color locked.
HUD uses its own version counter so a run/pause change remains pending until
painted. Flow color depends exclusively on FLD (0/2 cyan, 1/3 grey), not FLW.
On a flow-state change the display emits @UIFLOW,dir=...,running=... on both
serial ports; base TRACE ON exposes it. Build banner: SPLASH-FADE-HUD3.

## Known lap motor and index polarity test

57BLR70-24-02 is 4-pole = 2 pole pairs, already configured correctly. Manufacturer
connection table: U/V/W yellow/green/blue, Hall A/B/C yellow/green/blue,
Hall +5V red and GND black. Map motor U/V/W to BLD MA/MB/MC and Hall A/B/C to
HA/HB/HC. A white lead differs from this drawing; identify it rather than guessing.
Source: https://www.omc-stepperonline.com/24v-3500rpm-0-47nm-172w-10-4a-57x69mm-brushless-dc-motor-57blr70-24-02

The current speed command is shaft RPM, not a fraction of a coil or a percentage.
No changes to current limits, pole pairs, Hall mode or RPM scaling made here.

For index direction leave the mechanism clear, switch tip-servo off, stop lap
and pump, unlock both axes and wait for valid encoders. Send INDEX PROBE. This
commands only 128 positive pulses (travel depends on actual microstep setup),
prints measured start/end/delta and a suggested sign, then releases the motor.
It is nonblocking, cancellable with STOP, times out at 2.5 s, and does not
automatically apply the suggested sign or start a target seek. Do not operate
other axis controls during this test. Send its result before another sign flip.

```text
STOP
SD PROBE
SD RETRY
SD LIST
```

CMD0 response 0x01 means the card answered in SPI idle state. 0xFF means no
answer to this test; investigate card power/CS/clock/MOSI/MISO and socket/card.
Other responses need interpretation rather than being called a filesystem fault.
If CMD0 works but mounting fails, also check card filesystem/partition support.
Do not format a card containing files to test this without backing it up.
PROBE resets protocol state, so RETRY is required afterward. Once mounted:

```text
GEM LOAD 0
MODE DYNAMIC
```

Use the desired file's index from SD LIST. Loading remains blocked during motion.

### Staged SD initialization trial

Repeated CMD0=01 with an inserted card and FF with no card establishes that
the card can answer, but intermittent 7F still means communication is not yet
reliable. MISSING is a generic mount failure, not a card-detect switch reading.
SD PROBE now follows a successful CMD0 with CMD8 (expected R1=1, R7=000001AA
for a v2 card), CMD55/ACMD41 (expected ready R1=0), and CMD58 (expected R1=0,
OCR printed). Ready polling is capped at one second. A CMD8 illegal-command
response takes the older SD initialization path. No sector writes or formatting.
SD RETRY transfer ceiling is reduced from 1 MHz to 250 kHz for this trial;
the underlying library still controls its own initialization clock.
If all protocol stages succeed but mounting fails, check filesystem/card
support and sector-read reliability rather than assuming an open wire.

Hall wiring photo correction: user confirmed red is in the Hall +5V terminal.
Do not confuse the adjacent logic-bank GND with the Hall-bank terminals.
The photos do not reliably establish all individual lead-to-terminal mappings;
do not recommend wire swaps based on those ambiguous images.

## Lap shudder / implausible RPM

Keep stopped while checking motor phase and Hall wiring against the actual
motor documentation. A valid Modbus reply or fault=0 does not prove commutation
is correct. Do not raise current or bypass Hall protection. The posted snapshot
is paused with zero RPM, so it does not show the implausible running value.

MOTOR STATUS includes speed_raw (wire big-endian interpretation of register
0x8018), speed_raw_swapped (the little-endian value used for RPM), and pole_pairs.
Current limits, motor mode and pole-pair configuration are unchanged.

## Index settling trial

Circular error now normalizes any number of whole turns, not just one. For the
reported target 3 / actual 1.9922 on a 96 wheel, error is +1.0078, not a wrap
ambiguity. The old controller latched on any target crossing beyond one count.
Correction gain is restored to 0.2. A crossing up to 2 index units may settle
for 100 ms with coils held, then correct; at most three such corrections are
allowed per target. Larger crossings/repeated hunting still fault. Wrong-way
and stale-encoder protections remain enabled. Fault telemetry preserves error.
Do not change motor polarity blindly: use the guarded INDEX PROBE above.

Source/diff and built-in identity checks performed; no IDE compile, hardware
upload, panel screenshot verification or physical card access claimed.
