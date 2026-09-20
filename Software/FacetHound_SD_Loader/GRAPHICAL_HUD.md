# Dynamic / Static HUD update

Upload both complete sketch folders with Arduino IDE:

- `baseChassisModule/baseChassisModule.ino` — base Pico 2, using the existing board/clock settings.
- `displayModule_v6_gem/displayModule_v6_gem.ino` — display RP2040 Zero, using the existing board/clock and HX8357D settings.

No mast or USB-to-UART keyboard update is needed. Keep all companion files in each sketch folder. No wiring or motor-driver configuration changes are part of this update.

## Graphical controls

In Dynamic and Static, twist-wheel events (16/18) adjust the selected facet's temporary index cheat using the existing index step selector. H/apply-all (11) commits that adjustment to the current crown or pavilion side, across its tiers. Moving to another facet discards the temporary component. Nominal indices and geometry remain unchanged. Applied side offsets accumulate; switching between Dynamic and Static retains the temporary adjustment.

The cheat is white while temporary and cyan when applied. The displayed index is nominal; index error compares actual position with the corrected motor target. Classic controls remain unchanged.

Applied offsets are stored with the current design identity. Loading a different design clears them; this is not a multi-design persistent cheat database. State format 5 reads existing version 2/3/4 state; older firmware cannot read newly saved version 5 state. Temporary cheat is not saved.

## Views

Static shows the selected tier's crown (T) or pavilion (B) cap plus a fixed side (S) projection. Each projection fits all vertices into its own bounds. Rear-facing side highlights are dark green; rear side edges are dim. The cap includes girdle edges.

Both modes share decimal-aligned values, cheat/error readouts, open-gray/filled-color lock dots and cumulative step symbols. Flow remains its setpoint: cyan drop and white text running, gray paused. RPM is measured while running and setpoint while paused. Red fault notices occupy the spare Z row.

The vertical tip-error gauge retains ten received samples. Latest is yellow; older ticks fade by a factor of 0.60 per newer sample, with overlapping history brightening. Labels: 0, ±0.1, ±1, ±10 degrees. Unlabeled minor ticks: ±0.05, ±0.5, ±5. The inner ±0.1 region is linear, spanning 42 pixels; outer regions are logarithmic. End arrows indicate overflow. The compact bottom label is ΔΘ; only the main tip value carries a degree symbol.

## Base → display records

Existing records are unchanged. New newline-delimited records:

```text
@CHEAT,nominal_index,total_cheat,temporary,target_index
@HUDFAULT,mask
@TIERCOMMENT,tier,text
```

Index values use the current wheel's units; `temporary` is 0/1. The fault mask is encoder stale/fault=1, index motion fault=2, lap fault=4, Z stale=8. These records describe state, not PC motion commands.

Tier comments come from GemCAD ASC `G` cutting instructions (not an `n G` tier name). They are retained per tier, up to 96 characters, and sent after the selected JOB. The graphical header shows one abbreviated line; tiers without comments leave it blank. Reload an already loaded gem after updating to populate this metadata.

## Bench verification after uploading

After a base-only restart, reporting a non-Classic mode now restores the saved SD design even if the display retains its mesh. Previously only a fresh mesh request triggered restoration, allowing built-in facet IDs to address a retained loaded model (Princess showed 9..24 of 48). Built-in JOB/navigation is suppressed while restoration is pending or failed; unrelated controls remain available. Test with the display left powered during a base reset, then cycle all 48 Princess girdle facets with keys 12/13.

Check crown/pavilion tier changes in Static, fixed side rear shading, and the smallest/longest loaded gems. Check temporary cheat clears on facet change; H applies only to that side. Verify nominal index stays fixed while corrected target/error changes. Verify lock dots, step changes, flow pause colors, and gauge history. Syntax/regression checks cannot substitute for this TFT and motion-hardware check.
