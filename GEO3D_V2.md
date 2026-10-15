# geo3d v2.0 on blueMSX+ (branch `geo3d-v2.0`)

This branch, by Alex Moncks, is proposed to Hesoten's blueMSX+ as a pull
request on top of `experimental/v9968-geo3d` (2090cd26). It brings
**geo3d v2.0**, the development version of the geo3d coprocessor described in
[geo3d/docs/geo3d_v2.md](https://github.com/alexmoncks/V9968_Cartridge/blob/geo3d-v2.0/geo3d/docs/geo3d_v2.md).
geo3d v2.0 is not the version in HRA!'s official V9968 distribution (that is
v1.0.0) and has not run on hardware yet.

## What changed

Three files of `blueMSX/Src/VideoChips`:

| File | Change |
|---|---|
| `Geo3DCore.h`, `Geo3DCore.cpp` | the core is geo3d v2.0 |
| `Geo3DDevice.c` | the savestate version (4) |

- **1023 vertices, edges and faces** (v1.0.0: 255): 10-bit counts and write
  pointers, their high bits at 68h-6Eh; the model RAMs have 1024 entries.
- **XCTRL (6Fh) bit 0, the extended mode:** the write pointers count on
  their 10 bits, an edge takes 3 bytes and a face 12, with 10-bit indices.
  Read, bits 7-4: 10.
- **VFIRST (54h), FFIRST (56h), EFIRST (66h):** where the transform, face
  and edge phases of a RUN start.
- **Index 49h reads 20h** (v1.0.0: FFh).
- **The engine's time is v2.0's.** The core counts the RTL's states to give
  each command its clock. v2.0 keeps the visible faces in a heap (v1.0.0
  scanned the whole list for every face), divides two bits a clock and reads
  the four vertices of a face back to back, so the counts of those parts
  are new: the heap is the RTL's own, moved entry by entry, to count what it
  takes. The transform and the wireframe are as before.
- **Savestates:** a state saved by `experimental/v9968-geo3d` (versions 1 to
  3) still loads; the new one is version 4.

A geo3d v1.0.0 program gets the same V9968 commands as before, in the same
order: every new register resets to 0. What it gets differently is time: a
RUN of filled faces ends sooner, as it does on a v2.0 FPGA.

## Checks

The core has no blueMSX dependency, so it was checked alone
(`geo3d/sim/blue_replay.cc` in V9968_Cartridge, branch `geo3d-v2.0`), built
with g++ in C++20:

- the V9968 command logs of the reference model for the six v1.0.0 test
  sets (354,131 lines) and for what v2.0 adds (77,390 lines, with the
  registers read back; 44,185 more in the benchmark's three logs), byte for
  byte;
- the clock of every command and of the end of every RUN against the v2.0
  RTL in simulation (`geo3d/sim/tb_engine.v +ec`, `check_ec.py`): the
  testbench counts the clocks since RUN that the producer does not spend
  stalled on the holding register, and that count minus the core's clock
  must be one constant. It is -4 on every line of the three v1.0.0 sets
  tried (random edges, faces, textures), of the v2.0 scenes and of the two
  v2.0 benchmarks: 191,959 commands and 674 RUN ends. The method was
  calibrated first on `experimental/v9968-geo3d`'s core against the v1.0.0
  RTL, where it gives the same -4.

One difference came out of that calibration, in the v1.0.0 core too: a RUN
in face mode with NFACE = 0 (a transform-only RUN, as resident scenes use)
ends one engine clock earlier in the RTL than in the core, because the RTL
goes from F_START straight to R_DRAIN and the core still counted D_START
(184 such RUN ends in a benchmark against the v1.0.0 RTL). This branch
counts it as the RTL does. It is 23 ns per RUN, and
`experimental/v9968-geo3d` still has it.

blueMSX+ itself was then built from this branch with the Visual Studio 2022
Build Tools (`msbuild blueMSX.sln /t:blueMSX /p:Configuration=Release
/p:Platform=x64`), without errors and with no warning in the geo3d files.
On the `MSX2+ - C-BIOS V9968` machine it runs GEO FIGHTER (a geo3d v1.0.0
program with a resident scene) at 100 % and draws its fight. No geo3d v2.0
program has run in it yet: none exists.
