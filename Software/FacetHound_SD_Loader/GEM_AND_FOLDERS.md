# Native GEM files and SD folders

Upload both `baseChassisModule/baseChassisModule.ino` and
`displayModule_v6_gem/displayModule_v6_gem.ino`, keeping their adjacent source
files. Mast and keyboard firmware do not change.

The SD menu now lists folders alongside ASC, FCT and native binary GemCAD GEM
files (case-insensitive extensions). Turn the wheel to choose, click to enter
a folder or load a design. The top-left/back key goes up one directory; at the
card root it returns to Settings. Folders are labelled `[Folder]`.

The selected design's full path is saved in `/facetHound.last`, so designs in
subfolders can be restored after restart. No formatting or relocation of
existing card contents is needed. Existing geometry caches remain compatible;
cache paths now follow the source folder. Avoid giving different source formats
the same basename in one folder, since they share the `.fhc` cache filename.

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

`gemUtils/gemcad_io.py` now recognizes `.gem` directly. The bulk converter accepts
ASC and GEM, including nested folders with `--recursive`:

```text
python tools/bulk_convert_gems.py "D:/Gem Designs" --recursive
python tools/test_binary_gem.py
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
