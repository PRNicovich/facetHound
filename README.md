# Facet Hound

Open hardware and software for the Facet Hound digital lapidary faceting machine.

Facet Hound is a mast-style faceting machine with digital readouts on all three axes, electronic indexing precise to less than 0.01 degrees, and a direct-driven lap powered by a brushless DC motor. It is designed to be replicated with readily available components and a minimum of custom parts. In 2025, the included components cost approximately $1,600.

Complete v7 and v8 assemblies are included as `Hardware/FacetHoundv7.step` and `Hardware/FacetHoundv8.step`. Subfolders of `Hardware/` contain vendor files for machining (`.step`, `.pdf`), 3D printing (`.stl`), PCB fabrication (`.brd`), and laser/plasma/waterjet cutting (`.dxf`). Commercially available parts are identified in the main design.

Subfolders of `Software/` contain the Arduino C++ sketches and desktop Python tools. The main module is a Raspberry Pi Pico 2, while peripheral controllers use RP2040 Zero boards.

## Current firmware

The current Pico 2 base plus RP2040 Zero mast and display release is in
[`Software/FacetHound_SD_Loader`](Software/FacetHound_SD_Loader). It includes:

- Classic, Dynamic, and Static HX8357D display modes.
- A twist-wheel settings UI for machine and display configuration.
- Persistent CW/CCW indexing, Table Adapter, servo, calibration, and axis settings.
- Variable-length mark points with direct numeric editing.
- GemCad `.asc` and Facet Hound `.fct` loading from microSD.
- Source-validated `.fhc` geometry caching and a PC bulk converter.
- Runtime convex-gem mesh generation on the Pico 2 and streaming to the display.
- A documented USB CDC API and PC diagnostic/motion-test console.
- Matching streaming mast telemetry for tip, index, and force sensing.

See the [release README](Software/FacetHound_SD_Loader/README.md) for SD wiring, dependencies, flashing instructions, limits, and verification results.

## Repository map

- `Hardware/`: complete assemblies and fabrication files.
- `Software/FacetHound_SD_Loader/`: current three-controller firmware release.
- `Software/caseModule/`, `displayModule/`, and `usbToUART/`: earlier module sketches and hardware support firmware.
- `Software/gemUtils/`: GemCad parsing, display prototypes, and geometry utilities.
- `Software/engraveUtils/`: engraving experiments.

These designs remain preliminary and may contain incomplete or untested elements. Contributions and bug fixes are welcome as pull requests.
