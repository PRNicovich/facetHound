# Native GEM/GCS files and SD folders

Upload both `baseChassisModule/baseChassisModule.ino` and
`displayModule_v6_gem/displayModule_v6_gem.ino`, keeping their adjacent source
files. Mast and keyboard firmware do not change.

The SD menu now lists folders alongside ASC, FCT, Gem Cut Studio GCS and binary GemCAD GEM
files (case-insensitive extensions). Turn the wheel to choose, click to enter
a folder or load a design. The top-left/back key goes up one directory; at the
card root it returns to Settings. Folders are labelled `[Folder]`.

Folder browsing reads directory entries once and caches filenames/types. It
does not open design files for previews or embedded titles; the title and other
metadata appear after loading. Scrolling and repeated page requests use RAM.
Changing directory or `SD RETRY` invalidates the list. If the card is edited on
a PC, retry/reinsert it before relying on old menu ordinals. The cache is bounded
to 1024 disk entries / approximately 96 KiB of names and entry bookkeeping per
folder. A larger folder logs `@SD_LIST,TRUNCATED`; split it into subfolders.
SPI speed, geometry loading and saved-path handling are unchanged.

The selected design's full path is saved in `/facetHound.last`, so designs in
subfolders can be restored after restart. No formatting or relocation of
existing card contents is needed. Existing geometry caches remain compatible;
cache paths now follow the source folder. Avoid giving different source formats
the same basename in one folder, since they share the `.fhc` cache filename.

### Missing saved design recovery

A moved/deleted saved file, missing parent folder, absent card, or failed design
load opens the file picker instead of trapping startup on a loading error.
The first entry in every directory is **Built-in gem**, available without SD.
Selecting it clears stale loaded geometry and restore-failure state, unlocks
index/Z, restores built-in targets and returns to the main screen. With a card,
`/@builtin` is saved in `facetHound.last`; without one the choice lasts for the
current session. Selecting a replacement SD file remembers its new full path.
Classic mode retains its immediate startup behavior.

Failed loads keep the current picker folder/cursor instead of reopening it at
entry zero. `@GEM_SOURCE` reports the actual opened path and byte count;
`@GEM_PARSE` reports result, byte offset and recovered cut count. Binary short
reads are accumulated; a zero/error read emits `@GEM_READ_ERROR` and fails
without an unbounded retry. These diagnostics distinguish parser failures
from a reset or a different file being opened on the card.

`Tessellation_32_(J).gem` (7624 bytes) passes PC parsing/reconstruction with 48
planes, 38 vertices and 84 edges. Its reported on-device BAD_HEADER has not yet
been reproduced locally. A PC-generated `.fhc` beside the original is a cache
workaround; the firmware verifies the original size/CRC before accepting it.

Both base and display need this recovery update (`GEMPICKER` / `GEMSELECTED`).
The display splash must yield screen ownership if recovery opens the picker
during serial reception: no remaining animation frames, final black clear, or
setup HUD draw may overwrite it. Z encoder initialization precedes the splash,
and both animated and low-memory splash paths continue transmitting feedback.
Deferred HUD redraws wait until the menu closes. The Z-stale diagnostic remains
enabled; this removes the startup feedback gap rather than hiding the fault.
SD list indices now reserve zero for the built-in and start disk entries at one.
Files are not moved, deleted or reformatted by recovery.

Hardware acceptance checks:

1. Load a gem, power down, move it into a new folder on the card, reboot in
   Dynamic/Static: picker appears and can navigate to the replacement path.
2. Delete the saved gem's parent directory: root picker remains navigable.
3. Remove the card and reboot: choose Built-in gem, then navigate facets normally.
4. Choose Built-in with a card inserted and reboot: built-in returns without
   retrying the old missing path. Also verify a valid saved file still restores.

## Supported native GEM data

The reader recovers facet planes from the stored polygon normals and vertices,
then uses the existing geometry builder. It imports tier/facet names, cutting
comments, title, attribution, index gear, symmetry, mirror flag and refractive
index. It does not substitute the built-in gem.

The supplied `Tristano.gem` parsed as 16 tiers and 73 facets. PC reconstruction
produced 61 vertices and 132 edges. Existing geometry regressions and malformed
binary tests pass; firmware syntax checks pass. On-device SD/menu operation
still needs a hardware upload test.

Bounds: full paths up to 191 bytes; filename labels up to 63 bytes; GEM input
up to 2 MiB, 2048 cut records, 4096 polygon vertices per record. The existing
geometry limits still apply (512 planes, 1024 mesh vertices, 2048 mesh edges).
GEM variants containing preform sections are rejected explicitly; export those
to ASC instead. This is not a claim of support for every historical GEM variant.

## PC tools / Spyder

`gemUtils/gemcad_io.py` recognizes `.gem` and `.gcs` directly. The bulk converter accepts
ASC, GEM and GCS, including nested folders with `--recursive`:

```text
python tools/bulk_convert_gems.py "D:/Gem Designs" --recursive
python tools/test_binary_gem.py
python tools/test_gcs.py
python tools/test_gem_regressions.py
```

The test scripts can also be run directly in Spyder. The converter writes each
cache beside its source by default. The original source remains required for
source validation and metadata. Tests do not modify the supplied Tristano file.

## Protocol additions

Display: `@CFG,SD_DIR,<path>` accompanies directory listings.
`@CFGACTION,SD_UP,0` goes up; `LOAD_SD_FILE` enters directories or loads files.
`SD_FILE_COUNT` now counts all browsable entries, not only files. Directory rows
use `[Folder]` as their `SD_TITLE_n`. Folder changes are rejected during pending
loads so a queued file index cannot silently refer to a different directory.

USB commands are documented in `baseChassisModule/PC_USB_API.md`.

## Gem Cut Studio GCS

The version-1000 XML reader imports the index gear, tier names/instructions,
title/author and RI. Construction-guide tiers are excluded; hidden physical
tiers are retained. No file conversion is required on the card. Normal vectors
and facet vertices are converted with `(x,y,z) -> (-y,x,z)` to the existing
ASC coordinate convention. Stored normals take precedence over index-angle
labels: GCS changes index winding between crown and pavilion. The index targets
are derived from those normalized planes, not by blindly copying index_angle.
When normals are absent, tier angle and index_angle supply them. Depth may be
supplied on the tier or derived from vertices. Invalid planes are rejected.

GCS `base`, `symmetry`, and `mirror` are editor state, not verified design
symmetry or an index offset. They are not imported as those machine settings.
See the official [GCS format specification](https://www.gemcutstudio.com/app_download/UserManual_v100.pdf),
pages 56–58. Render colors, frosting and optical-simulation settings are not
machine cutting instructions and are not imported.

The firmware uses a bounded streaming XML reader (2 MiB file, 4095-byte tag,
16 nesting levels, 32 attributes per tag), supports quoted attributes across
lines and standard/numeric XML character references, and rejects DTD/entity
declarations. Unsupported file versions are rejected explicitly. It is not a
general-purpose XML processor.

The supplied Easy Octagon reconstructed as 6 tiers, 37 facets, 41 vertices and
76 edges. All face normals match the input after the coordinate conversion;
vertex error is below 0.000001 design units. Run an optional example check with:

```text
python tools/test_gcs.py "path/to/2007 - Novice - Easy Octagon.gcs"
```

Only the base requires new GCS functionality; the display update changes the
empty-folder extension hint. No changes to motor control, wiring or telemetry.

## Format reference

The native binary layout was informed by Mathew Parker's MIT-licensed
[gemcad-file-reader](https://github.com/mbparker/gemcad-file-reader), particularly
`LibGemcadFileReader/Concrete/GemCadGemImport.cs`. The firmware and Python readers
are bounded implementations for this project's data structures.

MIT License

Copyright (c) 2023 Mathew Parker

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
