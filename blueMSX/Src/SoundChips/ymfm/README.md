# ymfm (OPNA subset)

These files are taken unmodified from [ymfm](https://github.com/aaronsgiles/ymfm)
by Aaron Giles, commit `81aec25ccbb98f4873a255f7551ac4dadac59b4a`.
They are licensed under the BSD 3-Clause License; see `LICENSE`.

Only what the YM2608 (`ym2608` in `ymfm_opn.h`) needs is included:

| File | From |
|---|---|
| `ymfm.h` | `src/ymfm.h` |
| `ymfm_fm.h`, `ymfm_fm.ipp` | `src/` |
| `ymfm_opn.h`, `ymfm_opn.cpp` | `src/` |
| `ymfm_adpcm.h`, `ymfm_adpcm.cpp` | `src/` |
| `ymfm_ssg.h`, `ymfm_ssg.cpp` | `src/` |
| `LICENSE` | `LICENSE` |

`ymfm_opn.cpp` also holds the other chips of the OPN family; they are
compiled but not used. To update, copy the same files from a newer commit
and change the commit above. The blueMSX side is `../YM2608.cpp`.
