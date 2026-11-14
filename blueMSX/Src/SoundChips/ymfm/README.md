# ymfm (OPNA subset)

These files are [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles:
commit `81aec25ccbb98f4873a255f7551ac4dadac59b4a` with its pull request
[#40](https://github.com/aaronsgiles/ymfm/pull/40), "Improvements to ADPCM-B
implementation" (head `37d01124a83abd05f894df2824494b9d8b720365`), merged
into it. The pull request is ymfm's own, written by its author after
measurements on a real YM2608, and was still open when it was taken. It
merges without conflict and changes `ymfm_adpcm.h`, `ymfm_adpcm.cpp` and
`ymfm_opn.cpp`; the other files are that commit's, unmodified. Nothing else
has been changed here.

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
and change the commit above; once the pull request is part of that commit,
the note about it goes.

The blueMSX side is `../YM2608.cpp`. Four things there depend on ymfm's
insides:

- It clocks the FM and the SSG part of the chip separately, makes up for
  the behaviour listed below, and shows the registers to the debugger,
  through protected members of `ym2608` (`update_prescale`, `m_address`,
  `m_last_fm`, `m_ssg`, `m_fm`, `m_adpcm_a`, `m_adpcm_b`, `m_irq_enable`,
  `m_flag_control`), so an update that hides them stops the build there.
- The ADPCM-B the CPU sees, `m_adpcm_b`, runs on board time. What is heard
  comes from a second `adpcm_b_engine` that `Ym2608Chip::clockFm` clocks in
  a copy of `clock_fm_and_adpcm`, and that `Ym2608Chip::writeHeard` gives
  the writes `write_data_hi` passes to `m_adpcm_b`. An update that changes
  either function has to be carried over there. The data bytes reach that
  copy on the rule that, inside `clock()`, BRDY rises from 0 to 1 only when
  CPU-driven playback consumes a data byte; an update that changes this
  changes the sound without stopping the build.
- It makes up for four things this ymfm does otherwise than the chip, in
  `Ym2608Chip`: `reset()` leaves the prescaler and the IRQ mask as they
  were; ADPCM-B flags reach the IRQ line only when the upper status is
  read; a CSM key-on lasts until the channels are next prepared; and an
  ADPCM-B access does not say whether the ROM or the RAM is addressed
  (ymfm issue #76). An update that fixes one of them makes that part of
  `Ym2608Chip` redundant, and for #76 the glue should use what ymfm then
  offers.
- ymfm's saved state carries no mark of its layout. `YM2608.cpp` writes one
  beside it (`CHIP_STATE_LAYOUT`) and has to be told when an update changes
  what ymfm saves, even at the same size, as the pull request did for the
  ADPCM-B channel.
