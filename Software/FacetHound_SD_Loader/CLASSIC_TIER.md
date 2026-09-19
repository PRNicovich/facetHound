# Classic and Classic tier

Upload matching base and display sources. Settings → Display mode now includes
**Classic tier** (`MODE CLASSIC_TIER` over the console). Original Classic remains
the fast machine-status startup without model loading.

- With a loaded gem, plain Classic rebuilds the mark list as sorted unique
  machine index values across all tiers (including the existing pavilion
  half-turn conversion). Facet keys traverse that list; tier keys are ignored.
- Classic tier retains all cut/tier identities and uses the loaded design, or
  the compiled default gem when none is loaded. Tier keys select tiers; index
  keys select facets within a tier. It loads design metadata/mesh like the other
  tier-aware modes, rather than pretending the plain Classic list has tiers.
- The force-bar area becomes `previous tier < current / total > next tier`.
  Below it are target tip angle and signed actual-minus-target error, in degrees.
  Anonymous tiers use `T<number>`; available source names label the neighbors.
- Mode-list reconstruction preserves the current motor target and lock state.
  Ordinary motion fault protection and explicit Home retry remain unchanged.
- Settings configuration snapshots are painted once at END, not once per value.
- Dynamic rendering targets a 20 ms frame interval (up to 50 fps), skips unchanged
  geometry frames, and limits routine HUD refresh to 10 Hz. Actual frame rate
  depends on mesh complexity/SPI time; no clock overclock or baud change is made.

Validation: Arduino-header syntax checks and source-design regression tests.
Hardware frame-rate and UI-layout acceptance are still required.
