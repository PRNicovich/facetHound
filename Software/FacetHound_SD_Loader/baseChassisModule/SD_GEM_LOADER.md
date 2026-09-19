# SD GemCad loader

The base module owns the microSD card and parses designs. The display only
renders the file browser and the active cut information over the existing serial
settings link.

## Wiring

The loader follows the v9 BaseModule header through `GemSdSpi`, a bounded
GPIO SPI adapter for the existing Arduino SD/SdFat filesystem:

| microSD reader | Base GPIO |
| --- | ---: |
| CS | 10 |
| MOSI | 11 |
| SCK | 12 |
| MISO | 13 |
| VCC | Reader's documented supply |
| GND | GND |

This ordering is not a legal hardware-SPI mapping. The transport clocks GPIOs
in mode 0 at no more than 250 kHz, with interrupts enabled. No PIO state
machines are allocated, including on repeated retries. The pin constants are at the top of
`sdGemLoader.h`. GPIO 8/9 serve the v9 `ESC.TTL` header for the external
BLD-510B RS-485 interface; GPIO 26/27 are not connected on v9.

The Arduino-Pico 5.6.0 `SoftwareSPI` implementation allocated another PIO
state machine on each begin, did not reclaim it on end, and waited indefinitely
for FIFO data. It also dereferenced null source/destination buffer pointers;
SdFat uses such buffers for receive-only/send-only transfers. `GemSdSpi`
avoids those paths and sends 0xFF during receive-only SD transfers. Filesystem
mounting remains in SdFat, using shared transaction mode and explicit teardown.
No formatting or installed-library modifications are required.

After uploading the base sketch, run `STOP`, `SD RETRY`, `SD LIST`.
The mount diagnostic should now say `bus=gpio`; `bus=dedicated` means the old
firmware is still running. Repeat `SD RETRY` several times and issue `STATUS`
between attempts to check that serial remains responsive. An absent card can
still take the library's finite initialization timeout. With a mounted card,
use `GEM LOAD <index>` with an index returned by `SD LIST`, then choose dynamic
mode in settings. Loading can create a same-named geometry cache (see below).

Use a FAT16/FAT32 card. Put `.asc` or `.fct` files in the card root. The browser
intentionally ignores directories and other file types.

## Geometry cache

Before rebuilding a design, the base checks for a same-named `.fhc` file on the
same card (`stone.asc` becomes `stone.fhc`). The versioned binary cache holds
the parsed cuts and complete runtime planes, vertices, and edges. Its header
stores the source file's exact byte count and CRC32, so editing or replacing the
ASC automatically invalidates stale geometry. A missing, old, malformed, or
mismatched cache is ignored safely; the normal parser and convex builder run,
then the result is saved as a replacement `.fhc` when the card is writable.

To prepare a card on a PC, use the folder converter:

```text
python tools/bulk_convert_gems.py D:\\facet-files
python tools/bulk_convert_gems.py D:\\facet-files --recursive --force
```

By default each cache is written next to its `.asc`. `--output-dir PATH` puts
the generated caches in another folder. The converter uses the repository's
existing `gemUtils/gem_generator.py`, and continues through a bulk run while
reporting any files that fail.

## GemCad `.asc`

The on-Pico parser follows `gemLoader.py`: `g` sets the wheel resolution and
meridian, the first `H` sets the title, and every `a` record supplies signed
angle, center-to-facet distance, and one or more facet indexes. A line beginning
with whitespace continues the preceding tier. Fractional indexes and `n` tier
names are supported; the first name in an `a` tier is copied to every cut in
that tier. `G` cutting instructions are ignored after parsing the coordinates.

Loading a design replaces the mark-point list with one entry per cut, in file
order. Existing next/previous mark controls therefore step through the job and
home the index axis. The display receives the active tier, facet, signed angle,
center-to-facet distance, original GemCad index, and propagated tier label. The machine
uses the angle and index. GemCad's center-to-facet distance is retained only as
source metadata: it is not a Z coordinate, is not displayed as a machine target,
and never commands the Z or mast axes.

The Pico 2 uses each distance as the `d` term of a geometric half-space plane
`n dot p <= d`. Because GemCad jobs describe convex gems, the firmware starts
with a temporary cube and clips that convex mesh by each facet plane. A plane
whose current vertices are all inside is skipped immediately. Crossing edges
create the next face directly, so the firmware never enumerates all plane
triples. If an artificial cube face survives, the cube is expanded and the
build retried; no artificial boundary is accepted into the finished mesh.

The parser accepts up to 2,048 cuts and a 383-character physical line. Runtime
geometry is deliberately bounded at 512 facet planes, 1,024 vertices, and 2,048
edges. A bad header, malformed number, geometry over those bounds, oversized
job, unbounded design, or missing card is reported without replacing the
currently loaded job. Construction work follows the size of the current convex
boundary rather than the number of possible three-plane combinations.

## Flattened `.fct` coordinate files

`.fct` is an optional, directly editable Facet Hound coordinate format:

```text
FacetHound 1
g 96 0
H Standard Round Brilliant
c 1 1 -90.0 1.02653281 93 G
c 1 2 -90.0 1.02653281 87
c 2 1 -42.5 0.61819401 93 1
```

The `c` fields are:

```text
c <tier> <facet> <signed-angle-deg> <center-to-facet-distance> <index> [name]
```

Names are single tokens. The first nonempty name in a tier is used as that
tier's label for every coordinate in the tier. Lines beginning with `#` and
blank lines are ignored.

## Display protocol

The file browser uses paged requests so neither controller stores a duplicate
directory listing:

```text
display -> base: @CFGGET,SD_FILES,0,6
base -> display: @CFG,SD_STATUS,READY
base -> display: @CFG,SD_FILE_COUNT,3
base -> display: @CFG,SD_FILE_0,round.asc

display -> base: @CFGACTION,LOAD_SD_FILE,0
base -> display: @CFGACK,LOAD_SD_FILE,Standard Round Brilliant
base -> display: @JOB,1,1,-90.0000,1.0265328,93.0000,G
base -> display: @MESHBEGIN,146,272,128,96.0000,1.0297423
base -> display: @MESHV,0,0.9419794,-0.3901806,-0.0116945
base -> display: @MESHE,0,0,1,2,0,1
base -> display: @MESHP,0,-90.00000,93.00000,1,1,G
base -> display: @MESHEND,1
```

`@JOB` retains the GemCad distance field for protocol/file fidelity, but the
display deliberately does not present it as Z. The row is refreshed when the
selected cut changes and periodically so a display reboot recovers the active
cut.

`MESHE` carries edge id, vertex A, vertex B, support count, then supporting
plane ids. `MESHP` carries plane id, angle, index, tier, facet, and the imported
GemCad tier label. The display stores edge support ids compactly instead of
allocating a 512-bit mask for every edge. A rebooted display requests the
current runtime mesh with `@CFGGET,MESH`; the compiled `gem_data.h` mesh remains
only as a fallback before an SD design is loaded.
