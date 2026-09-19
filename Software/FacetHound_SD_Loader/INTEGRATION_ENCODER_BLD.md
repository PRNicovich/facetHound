# Encoder startup, powered hold, and BLD isolation trial

## Latest bench results and trial

- Removing MODE conductors fixed encoder cold start. Leave encoder MODE pin 5
  permanently unconnected; retain the other five conductors.
- Replacing the RS485 adapter produced valid BLD reads/writes without CRC errors.
  Fault 0x04 is Hall value abnormal, NOT current limiting (overcurrent is 0x02).
  Verify motor Hall +5V/GND and HA/HB/HC against motor documentation, with power
  off for rewiring. Do not raise current limits or switch to sensorless blindly.
- Provisional tip down zero is 82832; horizontal sample 115896 yields 90.813 deg.
  The old default 47850 migrates; custom stored zeros remain. Range wraps at
  -90/270, outside down=0, horizontal=90, up=180. Table adapter subtracts 45.
- Paused BLD control now matches the original library's 0x0C brake word instead
  of 0x0D (enable+brake). Status faults refresh every fourth telemetry poll.
  The legacy DEMOPROBE `run` field is the status register's low byte; the manual
  calls this byte reserved, so do not interpret 21 as a running speed/state.

BLD source: https://ae01.alicdn.com/kf/Sdb18dfe1fd7741f7bf6f43a54702a5abB.pdf

SD uses a conservative 1 MHz SPI transfer rate for this trial. From base USB:

```text
STOP
SD RETRY
SD LIST
GEM LOAD 0
MODE DYNAMIC
```

Replace 0 with the desired file index in SD LIST. If RETRY is not READY, stop
there: firmware cannot load an unmounted card. Report that result and card
format/capacity; do not format or erase it. Root-level ASC/FCT sources are listed;
matching FHC caches are loaded automatically, or built/saved when absent. This
trial does not claim SD hardware success before the user's bench test.

Flash baseChassisModule and mastModule for this trial. Display and keyboard
firmware are unchanged. No encoder zero-setting or homing command is sent.

## Encoder cold start

The mast releases GPIO26/27 (MODE translator inputs) and all DATA pulls before
reading. CS output latches are set high before output enable. The existing
500 ms startup wait and legacy clock edges remain. Bad checksum transactions
are retried up to three times; bad data never becomes a position measurement.

The AMT23 datasheet requires MODE to be left open in normal operation and
the shaft stationary during its 200 ms startup. Releasing GPIO26/27 does NOT
disconnect the board's TXB0108 from MODE. If a cold power cycle still produces
encoder_fault=3 until reseating, with power OFF reversibly isolate the MODE
conductor on both encoder cables, leaving the other five connections intact.
IMPORTANT: MODE is pin 5 at the AMT232 encoder, but pin 2 at the v8 mast PCB
J5/J2 connectors. These numbering systems are reversed; do not confuse them.
Confirm continuity before removing a contact: J5.2 -> U2 B7 -> A7 -> GP26;
J2.2 -> U2 B8 -> A8 -> GP27 (the translator is not a direct continuity path).
Remove/insulate a connector contact to test first; do not cut traces yet.
Pin 2 AT THE ENCODER is DATA and must remain connected. Do not hot-plug.
Test a full cold start with shafts stationary, before engaging motor locks.

Reference: https://www.sameskydevices.com/product/resource/amt23.pdf

## Motion and holding

Z now gets software enable before its configuration UART is released, just as
index does. Both use active-low hardware EN and NORMAL standstill mode for
powered holding when locked; do not physically short motor coils.

A rejected index sample stops pending pulses without releasing lock. A fresh
valid sample permits movement again unless a sustained feedback, moving-away,
or overshoot fault has latched. Those faults keep powered hold, print @FAULT,
and require unlocking then locking again. Fix the cause before re-arming.
Explicit unlock/emergency stop still releases the driver. The target tolerance
is at least one transmitted index count (about 0.02344 on a 96 wheel), not an
unreachable fraction of one count. Settled position is checked again for drift.

## BLD: isolate the actual GP8/GP9 UART path

1. Stop lap, pump and both axes. Power OFF before touching connectors.
2. Disconnect the TTL-to-RS485 adapter from base TX/RX. Jumper base GP8 (TX)
   to GP9 (RX), preferably at the PCB adapter connector to include its traces.
   Do NOT jumper RS485 A/B, power pins, or two connected output drivers.
3. Power the base; leave index/Z unlocked and lap paused. Send `MOTOR LOOPBACK`.
4. Expect `@BLD_LOOPBACK,match=1,rx_bytes=8,hex=...`.
   - No bytes: test the same jumper directly at GP8/GP9. A direct pass but
     connector failure isolates the PCB/connector path.
   - Bytes but mismatch: investigate UART timing/clock and signal integrity.
   - Exact match at the connector: base UART path works. Power OFF, remove
     jumper, reconnect adapter; then `MOTOR DEMOPROBE` tests the downstream
     path with the exact uiReno library. Loopback alone does not test RS485.

LOOPBACK blocks for approximately 110 ms, only on explicit command. It sends a
read-only Modbus status request, not a speed, run, or address-change command.
It does not interpret an echo as proof that the BLD responded. This test does
not claim the silent drive is fixed; it produces a discriminating next result.

SD remains reported missing; no SD or pin-assignment changes are part of this
trial. These edits were source-reviewed, not hardware-verified or IDE-compiled.

## Index seek/hold and loopback follow-up

Index lock means seek the selected target then maintain holding torque; it
does not mean simply stop wherever the axis happens to be. With menu closed,
L (`KEY 15`) toggles it. A state with twist_lock=0 explicitly disables seeking.
If tip-servo is enabled it releases index above 120 degrees, recaptures below
105 degrees. For a manual index test turn Servo OFF in Settings. New STATUS
fields twist_ready, index_fault_latched, tip_servo distinguish the motion gates.
Runtime keyboard/servo paths now use hardware EN only, never the released
one-time TMC configuration UART. Sustained faults still require unlock/relock.

After a passing loopback remove the GP8/GP9 jumper with power off, reconnect
the adapter, then power up and run MOTOR DEMOPROBE. An echoed 8-byte request
01-03-80-1B-00-01-DD-CD is NOT a drive response. The normal parser expects a
7-byte response, so loopback requests appearing there produce CRC failures;
this alone does not indicate wrong CRC generation or a BLD fault.
