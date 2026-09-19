# HUD, exact selection and card trial

## RPM factor-of-two correction

Base-only update. Captured reply 01-03-02-25-00-A2-D4 carries little-endian
speed raw=37. The 8018 manual specifies raw*20 divided by MOTOR POLES.
The four-pole motor therefore reports 185 RPM, not the previous 370. The
control word still uses 2 POLE PAIRS. Speed commands, 200 RPM cap, wiring,
ramp and current settings are unchanged. Both asynchronous telemetry and
the demo library now use the same conversion. @MOTOR adds rpm_divisor_poles=4.
At the same physical operating point, the HUD should show about 185..190
running and 200 paused (setpoint). Below-speed stalling remains a separate
operating issue; this display correction does not change motor torque/speed.
Source: https://ae01.alicdn.com/kf/Sdb18dfe1fd7741f7bf6f43a54702a5abB.pdf
Earlier notes describing raw*20/pole-pairs scaling are superseded here.

## Locked-only model motion and averaging follow-up

Upload display and mast for this follow-up (base still needs the preceding
3926f00 fractional-index conversion and 0.009 tolerance).
Unlocked: exact facet highlight/name changes, but rendered orientation freezes,
including an interpolation already in progress. Locked: actual index controls
rotation and selected tier controls tilt. Physical tip does not drive the model.
The first frame initializes once even if unlocked. Machine-wheel units are
converted to mesh index units before rendering.

Index averaging was NOT removed: it still uses 16 circular samples; tip uses 8.
However the final index average was rounded to one native encoder count.
It now stays floating point and @twist prints five decimal places in the same
4096-units/revolution scale. This preserves fractional sample estimates; it
does not prove sub-LSB accuracy in the absence of noise/dither and calibration.

Do not erase the populated SD card. Previous failures are at the command/read
layer, not evidence that formatting is needed. Back up before any later format.

Lap phase-matching trial: leave all Hall wires and drive settings fixed. Label
the three MOTOR-side phase leads A/B/C by their current MA/MB/MC connections,
not by assumed colors. Record baseline and test distinct MA/MB/MC assignments:
ABC, ACB, BAC, BCA, CAB, CBA. Disconnect 24 V and wait for the drive supply to
discharge before EVERY connector change; never hot-swap motor leads. Secure
the motor, preferably uncoupled from the lap, insulate all bullet connectors,
keep hands clear and have STOP/power isolation immediately available. Use the
same modest setpoint/direction each time; abort immediately on shudder, stall,
unexpected speed, fault or excessive current. Do not dwell stalled or raise
current limits. A candidate must start smoothly and then work in the opposite
commanded direction after a complete stop; rotation alone is not proof of a
correct match (incorrect timing can run with poor torque/high current). If no
combination is smooth, stop permutations and verify Hall state sequence and
Hall supply under load, controller outputs and mechanical load. Do not cycle
through Hall power-wire arrangements. Near-zero winding readings on a normal
meter can reflect low winding resistance; OL/open is a different failure.
Reference: https://e2e.ti.com/support/motor-drivers-group/motor-drivers/f/motor-drivers-forum/1102147/mct8316z-motor-lock-error-detected-everytime

## Full-resolution index / dedicated-SPI trial

Upload mast, base and display together for this trial. Mast now retains all
14 AMT232B position bits. @twist keeps its legacy 4096 units/revolution, but
uses fractional quarter counts (e.g. 245.25). Base converts the floating-point
value before truncating the legacy integer diagnostic/calibration field.
No bit-bang timing, pin, MODE wiring or parity changes. At wheel 96 the
measurement increment is 96/16384 = 0.005859375, rather than 0.0234375.
Stop tolerance is 0.009 index units, with no automatic coarse-count enlargement.
Settled holding remains passive: no feedback pulses until a fresh seek.
This is resolution, not a claim of calibrated mechanical accuracy. Larger
wheel resolutions may exceed this tolerance's quantization requirement.

Display drains up to 2048 bytes/128 lines per loop instead of 320/12, preserving
wire order, before the next redraw. A received changed selection emits
@JOBACK,tier,facet; base TRACE ON exposes it. This lets a missed key be separated
from delayed display receipt. Model rotation still follows actual index until
the user chooses preview-versus-live behavior; physical tip never drives pose.

Installed Arduino-Pico 5.6.0 SdFat error 0C is CMD18; data 04 is illegal command.
Sector 0 already read using that library, so this is not proof of a bad format.
Use the existing SDFS implementation with a small begin/end override selecting
DEDICATED_SPI for the card-only bus and actually closing SdFat on end. All
existing SD/File/cache APIs remain intact; no installed library files change.
Mount attempts print @SD_MOUNT with library error/data. This is a trial for
read/stop/restart reliability, not yet validated on hardware. No formatting.

BLD readback C02/1414/AA0F/C800 means brake, two pole pairs, 2 s ramps,
sensored mode, and 200 RPM setpoint. Do not increase current. With power off,
disconnect motor phase leads and compare the three pairwise winding resistances;
all should be similar, with none open. Account for test-lead resistance. Keep
phase leads isolated for powered Hall-only testing. Verify Hall supply at the
motor, then hand-turn and check all three Hall outputs switch. Valid switching
does not alone prove phase/Hall alignment. Do not infer wire mapping from the
ambiguous photos or randomly swap the Hall power leads.

## Settled hold / keyboard / SD / lap integration follow-up

Upload base and display; mast and keyboard unchanged. Index now latches settled
on the first in-tolerance feedback sample and stops queued pulses. Holding
current stays enabled, but encoder drift does NOT restart movement. Only a
new target, explicit seek, or re-engagement re-arms it. Final approach speed
is capped at 300 pulses/s per remaining index unit (100 minimum, existing
1800 maximum). Overshoot/wrong-direction/invalid-feedback protections remain.
Z has no position feedback correction loop: it holds after explicit jog pulses.
Physical vibration with no new jog would need electrical/driver investigation.

Flash autosave waits for 1.5 seconds without keyboard actions and no pending
index/Z motion. Facet/tier changes repaint model and HUD without waiting for
their normal frame intervals; selection still uses exact JOB identities.

SD PROBE additionally uses the installed SdFat library's SdInfo approach:
cardBegin, sector-0 read, volumeBegin. @SD_FS prints error/data codes and,
if readable, sector signature and partition types. No formatting/file writes.
CMD8=01/000001AA, ACMD41=00 and OCR=C0FF8000 indicate successful SDHC/SDXC
initialization for that transaction, not proof of reliable sector reads.
Use STOP, SD PROBE; paste all @SD_FS records. Card size and filesystem as
reported by the PC are also useful; do not reformat existing data for this test.

Lap comparison used origin/uiReno's BLD510B_Demo_copy_20260918125840 sketch.
It sets register 8003 to 1414 (2 seconds each) before speed/enable; integration
now restores that acknowledged configuration. Existing current limit, sensored
mode and pole pairs are not altered. Pause commands take precedence over ramp
configuration. MOTOR DEMOPROBE now also reads 8000..8005 via that same library
and prints @BLD_CONFIG (control, ramp, current_mode, speed_wire).

Lap trial: keep mechanism clear, stop other motion, send STOP and RPM 200.
Then send MOTOR CW, watching the shaft; stop immediately with STOP if it
shudders or stalls. Do not leave it energized stalled. If it turns smoothly,
stop fully before trying MOTOR CCW. Do not queue CW and STOP back-to-back in
one paste as that would not exercise startup. After stopping, MOTOR DEMOPROBE
reports drive configuration. The manual describes approximately 150 RPM minimum;
the proven demo used 500/1500 forward and 800 reverse, not the recent 12..92.
This trial does not assert that low speed is the sole cause. If still shuddering,
check @BLD_CONFIG mode byte against documented 0F sensored (10 sensorless)
and measure each Hall signal toggling with hand rotation before phase swaps.
Source: https://ae01.alicdn.com/kf/Sdb18dfe1fd7741f7bf6f43a54702a5abB.pdf

Source/regression checks only; no Arduino build or physical validation claimed.

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
