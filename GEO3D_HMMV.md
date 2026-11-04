# geo3d fill spans as HMMV (V9968_Cartridge v0.92) on blueMSX+

Local change on top of `experimental/v9968-geo3d-v2.0` (`da1c7bd`).
**Not sent upstream; under evaluation.**

HRA's V9968_Cartridge v0.92 (2026-11-01, commit `3d55af4`) speeds up
SCREEN 8 polygon drawing: the geo3d engine still emits a horizontal LINE
for an untextured filled-face span, but it also raises a tag
(`cmd_fill_span`), and the V9968, seeing the tag at command start, runs
the span as a one-row HMMV instead. The HMMV path writes the colour
directly, where LINE reads every destination pixel back first. This
change gives the emulator the same behaviour, in the same places the
FPGA has it:

| File | Change |
|---|---|
| `Geo3DCore.h`, `Geo3DCore.cpp` | `Command` gains a `fillSpan` tag, set only in `renderFaces` for untextured spans. The emitted bytes stay a LINE, bit for bit. Savestate layout 5 stores the tag; older layouts load with it clear. |
| `Geo3D.h`, `Geo3D.cpp` | `geo3dPeekFillSpan`, and `geo3dConvertFillSpan`: the rewrite itself, shared so a host test can drive it without the emulator. |
| `VDP.h`, `VDP.c` | `vdpIsV9968Screen8()`: the mode test. `screenMode == 8` here is Graphic7 with YJK off; the YJK screens are reported as 10/12, which excludes them just as the FPGA's YJK flag does. |
| `Geo3DDevice.c` | At issue time, a tagged command is converted before its registers are written. Device savestate version 5 (matching core layout 5). |

The conversion fires only when every condition the FPGA checks holds:
tagged span, LINE with LOP = IMP, ARG = 0 (rightward, downward, X-major,
no destination-memory bit), NY = 0, DX inside the 256-dot screen, and
SCREEN 8 at issue time. Then NX grows by 1 (LINE includes its endpoint,
HMMV counts pixels), NY becomes 1, and the command byte becomes HMMV.

Notes and differences from the FPGA:

- The command still lands as 11 register writes, so the device timing
  plan (`plan()` gives an 11-byte command 14 ec to land) is untouched.
  What changes is the consumer side: `V9938.c` already carries separate
  LINE and HMMV costs, fitted to the RTL for the V9968 high-speed model,
  so a converted span is charged the HMMV cost and a RUN ends sooner,
  as on a v0.92 cartridge.
- The FPGA keeps the post-command register values at their LINE values
  and falls back to LINE if the screen mode changes mid-span. Neither
  is observable here: the command registers are write-only from the
  CPU, geo3d rewrites all of them for the next span, and the command
  engine in this emulator keeps the mode a command started with.
- CPU-issued LINE commands are never converted: the tag exists only on
  geo3d commands, and only the device consults it.

Checks done so far (Linux, g++):

- `geo3d_hmmv_test/test_geo3d_hmmv.cpp`, built against `Geo3D.cpp` and
  `Geo3DCore.cpp` alone, passes: a flat quad gives 31 tagged spans with
  the expected LINE bytes; every span converts in SCREEN 8 and draws
  the same pixel set as its LINE; a wireframe edge whose bytes have the
  same shape is not tagged and not converted; textured (LRMM) commands
  are not tagged; LOP != IMP, DX >= 256, NY != 0, ARG != 0 are all
  refused; a savestate taken mid-RUN keeps the tags of the commands
  still to be issued.

  ```sh
  g++ -std=c++20 -O1 -I blueMSX/Src/VideoChips -I blueMSX/Src/Common \
      geo3d_hmmv_test/test_geo3d_hmmv.cpp \
      blueMSX/Src/VideoChips/Geo3D.cpp blueMSX/Src/VideoChips/Geo3DCore.cpp \
      -o test_geo3d_hmmv && ./test_geo3d_hmmv
  ```

- `Geo3DDevice.c` and `VDP.c` pass a C syntax check with the project's
  include paths, with no warnings beyond the ones the unmodified files
  already produce.

Still open: the full Windows build (Visual Studio 2022,
`blueMSX/Make/msvc2022/blueMSX.sln`, Release|x64, as in GEO3D_V2.md)
and on-screen checks against the demos, which is what this copy is for.
