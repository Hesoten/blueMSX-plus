/*
   Modified 2026 by Hesoten for blueMSX+ fork.
   See https://github.com/Hesoten/blueMSX-plus for change history.
*/
/*
   Forked for the Y8960 cartridge, 2026 by madscient.

   The cartridge's OPLLEX block is a YM2413 that carries four preset ROMs and
   picks one per channel, so the core holds four patch tables instead of one
   and takes nine bank registers the YM2413 does not have, plus nine pan
   registers that place each channel in the stereo image. Every exported
   symbol is renamed with a Y8960 prefix because the unmodified emu2413 is
   linked into the same binary for the machine's own MSX-MUSIC.

   The preset tables are NOT Okazaki's; see the comment above the tables.

   Carried over from emu2413 v1.6.4 (by way of madscient/DSAemuEngine, whose
   fork of the same core took them first): the rate converter, the integer
   output timing and the 4.12 fixed-point pan_fine. The converter takes L and
   R at the same phase, so it also covers the Y8960OPLL_calcStereo fix of
   v1.6.3. The other changes of v1.6.x (save state, reset, tables, OPLL_calc
   speedup) are not carried over.
*/
#ifndef _Y8960_OPLL_CORE_H_
#define _Y8960_OPLL_CORE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y8960OPLL_DEBUG 0

/* The BANK register numbers the preset ROMs; these are its values, not
** emu2413's tone ids, which ran in a different order. */
enum Y8960OPLL_TONE_ENUM {
  Y8960OPLL_2413_TONE = 0, /* OPLL   */
  Y8960OPLL_2423_TONE = 1, /* OPLL-X */
  Y8960OPLL_281B_TONE = 2, /* OPLL-P */
  Y8960OPLL_VRC7_TONE = 3  /* VRC7   */
};

#define Y8960OPLL_BANK_COUNT 4

/* Bank registers 40h-48h and pan registers 50h-58h, one per channel each,
** sit above the YM2413's 00h-3Fh. */
#define Y8960OPLL_REG_COUNT 0x59
#define Y8960OPLL_BANK_REG_BASE 0x40
#define Y8960OPLL_PAN_REG_BASE 0x50

/* voice data */
typedef struct __Y8960OPLL_PATCH {
  uint32_t TL, FB, EG, ML, AR, DR, SL, RR, KR, KL, AM, PM, WS;
} Y8960OPLL_PATCH;

/* slot */
typedef struct __Y8960OPLL_SLOT {
  uint8_t number;

  /* type flags:
   * 000000SM
   *       |+-- M: 0:modulator 1:carrier
   *       +--- S: 0:normal 1:single slot mode (sd, tom, hh or cym)
   */
  uint8_t type;

  Y8960OPLL_PATCH *patch; /* voice parameter */

  /* slot output */
  int32_t output[2]; /* output value, latest and previous. */

  /* phase generator (pg) */
  uint16_t *wave_table; /* wave table */
  uint32_t pg_phase;    /* pg phase */
  uint32_t pg_out;      /* pg output, as index of wave table */
  uint8_t pg_keep;      /* if 1, pg_phase is preserved when key-on */
  uint16_t blk_fnum;    /* (block << 9) | f-number */
  uint16_t fnum;        /* f-number (9 bits) */
  uint8_t blk;          /* block (3 bits) */

  /* envelope generator (eg) */
  uint8_t eg_state;  /* current state */
  int32_t volume;    /* current volume */
  uint8_t key_flag;  /* key-on flag 1:on 0:off */
  uint8_t sus_flag;  /* key-sus option 1:on 0:off */
  uint16_t tll;      /* total level + key scale level*/
  uint8_t rks;       /* key scale offset (rks) for eg speed */
  uint8_t eg_rate_h; /* eg speed rate high 4bits */
  uint8_t eg_rate_l; /* eg speed rate low 2bits */
  uint32_t eg_shift; /* shift for eg global counter, controls envelope speed */
  uint32_t eg_out;   /* eg output */

  uint32_t update_requests; /* flags to debounce update */

#if Y8960OPLL_DEBUG
  uint8_t last_eg_state;
#endif
} Y8960OPLL_SLOT;

/* mask */
#define Y8960OPLL_MASK_CH(x) (1 << (x))
#define Y8960OPLL_MASK_HH (1 << (9))
#define Y8960OPLL_MASK_CYM (1 << (10))
#define Y8960OPLL_MASK_TOM (1 << (11))
#define Y8960OPLL_MASK_SD (1 << (12))
#define Y8960OPLL_MASK_BD (1 << (13))
#define Y8960OPLL_MASK_RHYTHM (Y8960OPLL_MASK_HH | Y8960OPLL_MASK_CYM | Y8960OPLL_MASK_TOM | Y8960OPLL_MASK_SD | Y8960OPLL_MASK_BD)

/* rate conveter */
typedef struct __Y8960OPLL_RateConv {
  int ch;
  uint32_t timer; /* fractional phase of the output in 0.32 fixed point */
  uint32_t timer_step; /* fractional part of f_ratio in 0.32 fixed point */
  double f_ratio;
  int16_t *sinc_table;
  int16_t **buf; /* ring buffer of LW samples per channel, stored twice so that the latest LW samples are contiguous */
  int *pos;
} Y8960OPLL_RateConv;

Y8960OPLL_RateConv *Y8960OPLL_RateConv_new(double f_inp, double f_out, int ch);
void Y8960OPLL_RateConv_reset(Y8960OPLL_RateConv *conv);
void Y8960OPLL_RateConv_putData(Y8960OPLL_RateConv *conv, int ch, int16_t data);
int16_t Y8960OPLL_RateConv_getData(Y8960OPLL_RateConv *conv, int ch);
void Y8960OPLL_RateConv_delete(Y8960OPLL_RateConv *conv);
/* sinc history length per channel (=LW). blueMSX addition for save-state. */
int Y8960OPLL_RateConv_getBufferLength(void);

typedef struct __Y8960OPLL {
  uint32_t clk;
  uint32_t rate;

  uint8_t chip_type;

  uint32_t adr;

  /* in units of 1/(clk * rate) sec, so that all steps are integers */
  uint32_t inp_step;
  uint32_t out_step;
  uint32_t out_time;
  /* inp_recip = ceil(2^inp_shift / inp_step), to get the rate converter phase without division */
  uint32_t inp_recip;
  uint32_t inp_shift;

  uint8_t reg[Y8960OPLL_REG_COUNT];
  uint8_t test_flag;
  uint32_t slot_key_status;
  uint8_t rhythm_mode;

  uint32_t eg_counter;

  uint32_t pm_phase;
  int32_t am_phase;

  uint8_t lfo_am;

  uint32_t noise;
  uint8_t short_noise;

  int32_t patch_number[9];
  /* Which preset ROM each channel reads its voice from. */
  uint8_t patch_bank[9];
  Y8960OPLL_SLOT slot[18];
  Y8960OPLL_PATCH patch[Y8960OPLL_BANK_COUNT][19 * 2];
  /* Voice 0 is written through registers 00h-07h and belongs to the chip, not
  ** to a bank, so switching banks must not change it. */
  Y8960OPLL_PATCH user_patch[2];

  uint8_t pan[16];
  int32_t pan_fine[16][2]; /* 4.12 fixed point */

  uint32_t mask;

  /* channel output */
  /* 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym */
  int16_t ch_out[14];

  int16_t mix_out[2];

  Y8960OPLL_RateConv *conv;
} Y8960OPLL;

Y8960OPLL *Y8960OPLL_new(uint32_t clk, uint32_t rate);
void Y8960OPLL_delete(Y8960OPLL *);

void Y8960OPLL_reset(Y8960OPLL *);
/* Reloads every bank from its preset ROM. */
void Y8960OPLL_resetPatch(Y8960OPLL *);

/* blueMSX addition: re-link Y8960OPLL_SLOT pointers after a bulk Y8960OPLL memcpy
** (save-state restore) leaves them dangling. */
void Y8960OPLL_relinkAfterRestore(Y8960OPLL *opll);

/**
 * Set output wave sampling rate.
 * @param rate sampling rate. If clock / 72 (typically 49716 or 49715 at 3.58MHz) is set, the internal rate converter is
 * disabled.
 */
void Y8960OPLL_setRate(Y8960OPLL *opll, uint32_t rate);

/**
 * Set internal calcuration quality. Currently no effects, just for compatibility.
 * >= v1.0.0 always synthesizes internal output at clock/72 Hz.
 */
void Y8960OPLL_setQuality(Y8960OPLL *opll, uint8_t q);

/**
 * Set pan pot (extra function - not YM2413 chip feature)
 * @param ch 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym 14,15:reserved
 * @param pan 0:mute 1:right 2:left 3:center
 * ```
 * pan: 76543210
 *            |+- bit 1: enable Left output
 *            +-- bit 0: enable Right output
 * ```
 */
void Y8960OPLL_setPan(Y8960OPLL *opll, uint32_t ch, uint8_t pan);

/**
 * Set fine-grained panning
 * @param ch 0..8:tone 9:bd 10:hh 11:sd 12:tom 13:cym 14,15:reserved
 * @param pan output strength of left/right channel.
 *            pan[0]: left, pan[1]: right. pan[0]=pan[1]=1.0f for center.
 */
void Y8960OPLL_setPanFine(Y8960OPLL *opll, uint32_t ch, float pan[2]);

/**
 * Set chip type. If vrc7 is selected, r#14 is ignored.
 * This method not change the current ROM patch set.
 * To change ROM patch set, use Y8960OPLL_resetPatch.
 * @param type 0:YM2413 1:VRC7
 */
void Y8960OPLL_setChipType(Y8960OPLL *opll, uint8_t type);

void Y8960OPLL_writeIO(Y8960OPLL *opll, uint32_t reg, uint8_t val);
void Y8960OPLL_writeReg(Y8960OPLL *opll, uint32_t reg, uint8_t val);

/**
 * Calculate one sample
 */
int16_t Y8960OPLL_calc(Y8960OPLL *opll);

/**
 * Calulate stereo sample
 */
void Y8960OPLL_calcStereo(Y8960OPLL *opll, int32_t out[2]);

void Y8960OPLL_setPatch(Y8960OPLL *, int32_t bank, const uint8_t *dump);
void Y8960OPLL_copyPatch(Y8960OPLL *, int32_t bank, int32_t, Y8960OPLL_PATCH *);

/**
 * Force to refresh.
 * External program should call this function after updating patch parameters.
 */
void Y8960OPLL_forceRefresh(Y8960OPLL *);

void Y8960OPLL_dumpToPatch(const uint8_t *dump, Y8960OPLL_PATCH *patch);
void Y8960OPLL_patchToDump(const Y8960OPLL_PATCH *patch, uint8_t *dump);
void Y8960OPLL_getDefaultPatch(int32_t type, int32_t num, Y8960OPLL_PATCH *);

/**
 *  Set channel mask
 *  @param mask mask flag: Y8960OPLL_MASK_* can be used.
 *  - bit 0..8: mask for ch 1 to 9 (Y8960OPLL_MASK_CH(i))
 *  - bit 9: mask for Hi-Hat (Y8960OPLL_MASK_HH)
 *  - bit 10: mask for Top-Cym (Y8960OPLL_MASK_CYM)
 *  - bit 11: mask for Tom (Y8960OPLL_MASK_TOM)
 *  - bit 12: mask for Snare Drum (Y8960OPLL_MASK_SD)
 *  - bit 13: mask for Bass Drum (Y8960OPLL_MASK_BD)
 */
uint32_t Y8960OPLL_setMask(Y8960OPLL *, uint32_t mask);

/**
 * Toggler channel mask flag
 */
uint32_t Y8960OPLL_toggleMask(Y8960OPLL *, uint32_t mask);

/* for compatibility */
#define Y8960OPLL_set_rate Y8960OPLL_setRate
#define Y8960OPLL_set_quality Y8960OPLL_setQuality
#define Y8960OPLL_set_pan Y8960OPLL_setPan
#define Y8960OPLL_set_pan_fine Y8960OPLL_setPanFine
#define Y8960OPLL_calc_stereo Y8960OPLL_calcStereo
#define Y8960OPLL_dump2patch Y8960OPLL_dumpToPatch
#define Y8960OPLL_patch2dump Y8960OPLL_patchToDump
#define Y8960OPLL_setChipMode Y8960OPLL_setChipType

#ifdef __cplusplus
}
#endif

#endif
