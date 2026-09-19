# HUD, exact selection and card trial

Upload baseChassisModule and displayModule_v6_gem. Mast is unchanged.

- Static/dynamic highlight uses the JOB tier/facet identity, not the nearest
  angle/index. Unknown identity has no highlight. All 128 built-in poses/IDs
  were compared between base and display and match.
- Dynamic rotation follows live index on the next rendered frame (33 ms target,
  actual rate depends on drawing time), without interpolation lag. Physical tip
  only affects HUD values/errors; model inclination comes from the selected tier.
- Both HUDs show index and Z LOCK/FREE state from existing TLK/ZLK telemetry.
  These are commanded engagement states, not driver electrical feedback.
- Flow always shows its setpoint. Droplet and value are blue for flow_dir 0/2
  (running) and grey for 1/3 (paused), regardless of setpoint magnitude.
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
L/U axis-gutter indicators and filled/empty lamps are drawn last in font 2.
HUD uses its own version counter so a run/pause change remains pending until
painted. Flow color depends exclusively on FLD (0/2 blue, 1/3 grey), not FLW.
On a flow-state change the display emits @UIFLOW,dir=...,running=... on both
serial ports; base TRACE ON exposes it. Build banner: SDPIN-ACTUALINDEX-HUD2.

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

## Lap shudder / implausible RPM

Keep stopped while checking motor phase and Hall wiring against the actual
motor documentation. A valid Modbus reply or fault=0 does not prove commutation
is correct. Do not raise current or bypass Hall protection. The posted snapshot
is paused with zero RPM, so it does not show the implausible running value.

MOTOR STATUS now adds speed_raw (register 0x8018), speed_raw_swapped (diagnostic
byte-swapped interpretation only), and pole_pairs. RPM decoding, current limits,
motor mode and pole-pair configuration are intentionally unchanged pending
motor identification and a captured abnormal reading.

Source/diff and built-in identity checks performed; no IDE compile, hardware
upload, panel screenshot verification or physical card access claimed.
