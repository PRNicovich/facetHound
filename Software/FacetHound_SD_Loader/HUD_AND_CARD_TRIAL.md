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
- Tier navigation retains the lowest-index facet as fallback; a facet within
  one wheel-index unit of current measured index wins by closest wrapped
  distance. This applies to SD-loaded and built-in designs. Existing next/previous
  facet commands are unchanged. No motion limits/acceleration were increased.

## SD electrical test

The eBay listing offers multiple module variants. Its title alone and 3.3 V
across the module input do not establish the card's actual supply. Identify the
module/regulator before changing its supply; never apply 5 V to a bare card or
Pico signal. No automatic formatting or erasing is performed.

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
