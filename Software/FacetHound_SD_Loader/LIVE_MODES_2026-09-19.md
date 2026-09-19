# Live modes and Classic tier

Upload baseChassisModule and displayModule_v6_gem. No mast or keyboard changes.

All display mode changes now persist without restarting the display. View
sprites are released when switching rendering families; geometry and its RAM
cache stay resident. A Classic-only cold boot still skips model loading. Its
first switch into a tier-aware view requests geometry if none is resident.

Classic tier's index counter and previous/next values use only the selected
tier, sorted by machine index. Tier keys retain list position when possible;
facet keys wrap within that tier. The named tier selector sits below tip;
signed tip error is on its right, styled like the index error. File names for
tiers take precedence over inferred C/P/G/Table-style labels. No force bar.

Tier-aware mode switches preserve the selected facet instead of selecting a
different tier with the same index. Plain Classic remains a deduplicated index
list independent of tiers.

Validation: Arduino syntax checks and Python geometry/list regressions. Actual
display layout, memory headroom for large models, and live transitions require
hardware testing. No motor parameters changed in this update.
