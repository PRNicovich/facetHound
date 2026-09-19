# Gem loading and index seek trial — 2026-09-18

Upload both `baseChassisModule` and `displayModule_v6_gem`. No mast or keyboard
firmware change is required. Both report build `GEM-NORMAL-HOME-20260918`.

## Operator checks

1. Load an ASC through settings. Loading releases index/Z; a running lap is
   paused and its brake command/low speed must be confirmed before reading SD.
2. The loading screen shows stage text and a model-transfer progress bar. Each
   mesh record is acknowledged, with bounded record retries and whole-transfer
   retries. A transfer failure does not substitute the built-in geometry.
3. Press Home (K / key 14) to engage index and seek the selected mark. This is an
   explicit operator retry of a latched index fault; invalid/stale feedback still
   blocks motion. Facet changes seek automatically while locked. Unlocked facet
   changes only change selection. At rest the driver holds without idle hunting.
4. At the target, the selected face should face the viewer. Actual index drives
   yaw, selected tier sets inclination; live tip does not rotate the model.
5. Index/Z HUD indicators are grey outlines when unlocked, colored disks when
   locked. Long names wrap in the left half; classic counters fit three digits.
6. Settings → Loaded gem info shows two pages of source/geometry metadata. Turn
   the wheel to switch pages. The SD list shows title plus filename; unrecognized
   headers have a `null` title, not a fabricated design name.
7. Restart with the card inserted: `/facetHound.last` identifies the saved ASC.
   Splash/loading remains visible while restoring instead of showing a demo gem
   paired with the saved marks. A missing remembered file reports an error.

## Geometry/cache changes

Face orientation is derived from mesh edge geometry, including each source's
meridian and index convention. Static panels independently fit their projected
bounds. GemCad zero-angle culets with negative distance now clip the pavilion,
not the crown. Firmware and PC converter use cache version 2; version 1 caches
are rebuilt from the ASC automatically. Keep the ASC files; no card reformat.

Run `tools/test_gem_regressions.py` directly or from Spyder (numpy required).
It checks face-to-viewer transforms for pc04188, pc42011, pc01028c and pc01043,
and reconstructs the Dodecahedron to check its full vertical extent.

## Diagnostics and limitations

`@INDEX,SEEK` reports target, actual and UNLOCKED / WAIT_FEEDBACK /
FAULT_LATCHED / SEEKING. A new facet does not automatically clear a motion fault;
Home explicitly retries. Stop and investigate recurring moving-away faults.

BLD fault 0x01 is a locked-rotor report. Firmware now pauses on that fault instead
of repeatedly driving the stalled motor. The supplied failed-direction trace had
startup_boost=0 and a 560 RPM command; it does not establish that the kick caused
the stall. This patch does not claim to fix Hall/phase commutation or raise current.

Validation: changed firmware translation units pass Arduino-header syntax checks;
no full firmware build, upload, or physical-motion validation was performed.
