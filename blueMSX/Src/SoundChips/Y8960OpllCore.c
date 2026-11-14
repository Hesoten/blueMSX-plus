/**
 * emu2413 v1.5.9
 * https://github.com/digital-sound-antiques/emu2413
 * Copyright (C) 2020 Mitsutaka Okazaki
 *
 * This source refers to the following documents. The author would like to thank all the authors who have
 * contributed to the writing of them.
 * - [YM2413 notes](http://www.smspower.org/Development/YM2413) by andete
 * - ymf262.c by Jarek Burczynski
 * - [VRC7 presets](https://siliconpr0n.org/archive/doku.php?id=vendor:yamaha:opl2#opll_vrc7_patch_format) by Nuke.YKT
 * - YMF281B presets by Chabin
 */
/*
   Modified 2026 by Hesoten for blueMSX+ fork.
   See https://github.com/Hesoten/blueMSX-plus for change history.
*/
/*
   Forked for the Y8960 cartridge, 2026 by madscient.

   The cartridge's OPLLEX block is a YM2413 that carries four preset ROMs and
   picks one per channel, so the core holds four patch tables instead of one
   and takes nine bank registers the YM2413 does not have. Every exported
   symbol is renamed with a Y8960 prefix because the unmodified emu2413 is
   linked into the same binary for the machine's own MSX-MUSIC.

   The preset tables are NOT Okazaki's; see the comment above the tables.
*/
#include "Y8960OpllCore.h"
#include "Y8960Pan.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __inline
#elif defined(__GNUC__)
#define INLINE __inline__
#else
#define INLINE inline
#endif
#endif

#define _PI_ 3.14159265358979323846264338327950288

#define Y8960OPLL_TONE_NUM 4

/* Preset ROMs for the four banks the OPLLEX picks from, in the order its BANK
** register numbers them. Okazaki's own tables were dropped so that all four
** come from one source and stay comparable with each other.
**
** Source: "Copyright free OPLL(x) ROM patches"
**   https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches
**   David Viens, Hubert Lamontagne. Licensed CC BY-SA.
** Taken by way of madscient/Y8960emu (src/opllex.cpp), which carries the same
** attribution. Bank 0 therefore differs slightly from the table the machine's
** own MSX-MUSIC plays through the stock emu2413.
**
** The wiki gives melodic voices only. The three rhythm entries are the ones
** ymfm's ym2413 ships, which come from the same source; whether the real
** rhythm ROM differs per bank is unknown, so all four banks share them.
**
** Index 0 is the user voice, which no ROM supplies and register writes fill
** in. It is one voice for the whole chip, not one per bank (see set_patch).
*/
/* clang-format off */
static uint8_t default_inst[Y8960OPLL_TONE_NUM][(16 + 3) * 8] = {
{ /* BANK 0 - OPLL (YM2413) */
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 0: User
0x71,0x61,0x1e,0x17,0xef,0x7f,0x00,0x17, // 1: Violin
0x13,0x41,0x1a,0x0d,0xf8,0xf7,0x23,0x13, // 2: Guitar
0x13,0x01,0x99,0x00,0xf2,0xc4,0x11,0x23, // 3: Piano
0x31,0x61,0x0e,0x07,0x98,0x64,0x70,0x27, // 4: Flute
0x22,0x21,0x1e,0x06,0xbf,0x76,0x00,0x28, // 5: Clarinet
0x31,0x22,0x16,0x05,0xe0,0x71,0x0f,0x18, // 6: Oboe
0x21,0x61,0x1d,0x07,0x82,0x8f,0x10,0x07, // 7: Trumpet
0x23,0x21,0x2d,0x14,0xff,0x7f,0x00,0x07, // 8: Organ
0x41,0x61,0x1b,0x06,0x64,0x65,0x10,0x17, // 9: Horn
0x61,0x61,0x0b,0x18,0x85,0xff,0x81,0x07, // A: Synthesizer
0x13,0x01,0x83,0x11,0xfa,0xe4,0x10,0x04, // B: Harpsichord
0x17,0x81,0x23,0x07,0xf8,0xf8,0x22,0x12, // C: Vibraphone
0x61,0x50,0x0c,0x05,0xf2,0xf5,0x29,0x42, // D: Synthesizer Bass
0x01,0x01,0x54,0x03,0xc3,0x92,0x03,0x02, // E: Acoustic Bass
0x41,0x41,0x89,0x03,0xf1,0xe5,0x11,0x13, // F: Electric Guitar
0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, // R: Bass Drum
0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x48, // R: High-Hat(M) / Snare Drum(C)
0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, // R: Tom-tom(M) / Top Cymbal(C)
},
{ /* BANK 1 - OPLL-X (YM2423) */
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 0: User
0x61,0x61,0x1b,0x07,0x94,0x5f,0x10,0x06, // 1: Strings
0x93,0xb1,0x51,0x04,0xf3,0xf2,0x70,0xfb, // 2: Guitar
0x41,0x21,0x11,0x85,0xf2,0xf2,0x70,0x75, // 3: Electric Guitar
0x93,0xb2,0x28,0x07,0xf3,0xf2,0x70,0xb4, // 4: Electric Piano 2
0x72,0x31,0x97,0x05,0x51,0x6f,0x60,0x09, // 5: Flute
0x13,0x30,0x18,0x06,0xf7,0xf4,0x50,0x85, // 6: Marimba
0x51,0x31,0x1c,0x07,0x51,0x71,0x20,0x26, // 7: Trumpet
0x41,0xf4,0x1b,0x07,0x74,0x34,0x00,0x06, // 8: Harmonica
0x50,0x30,0x4d,0x03,0x42,0x65,0x20,0x06, // 9: Tuba
0x40,0x20,0x10,0x85,0xf3,0xf5,0x20,0x04, // A: Synth Brass 2
0x61,0x61,0x1b,0x07,0xc5,0x96,0xf3,0xf6, // B: Short Saw
0xf9,0xf1,0xdc,0x00,0xf5,0xf3,0x77,0xf2, // C: Vibraphone
0x60,0xa2,0x91,0x03,0x94,0xc1,0xf7,0xf7, // D: Electric Guitar 2
0x30,0x30,0x17,0x06,0xf3,0xf1,0xb7,0xfc, // E: Synth Bass 2
0x31,0x36,0x0d,0x05,0xf2,0xf4,0x27,0x9c, // F: Sitar
0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, // R: Bass Drum
0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x48, // R: High-Hat(M) / Snare Drum(C)
0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, // R: Tom-tom(M) / Top Cymbal(C)
},
{ /* BANK 2 - OPLL-P (YMF281) */
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 0: User
0x72,0x21,0x1a,0x07,0xf6,0x64,0x01,0x16, // 1: Clarinet / Electric String
0x00,0x10,0x45,0x00,0xf6,0x83,0x73,0x63, // 2: Synth Bass / Bow wow
0x13,0x01,0x96,0x00,0xf1,0xf4,0x31,0x23, // 3: Piano / Electric Guitar
0x71,0x21,0x0b,0x0f,0xf9,0x64,0x70,0x17, // 4: Flute / Organ
0x02,0x21,0x1e,0x06,0xf9,0x76,0x00,0x28, // 5: Square Wave / Clarinet
0x00,0x61,0x82,0x0e,0xf9,0x61,0x20,0x27, // 6: Space Oboe / Saxophone
0x21,0x61,0x1b,0x07,0x84,0x8f,0x10,0x07, // 7: Trumpet
0x37,0x32,0xca,0x02,0x66,0x64,0x47,0x29, // 8: Wow Bell / Street Organ
0x41,0x41,0x07,0x03,0xf5,0x70,0x51,0xf5, // 9: Electric Guitar / Synth Brass
0x36,0x01,0x5e,0x07,0xf2,0xf3,0xf7,0xf7, // A: Vibes / Electric Piano
0x00,0x00,0x18,0x06,0xc5,0xf3,0x20,0xf2, // B: Bass
0x17,0x81,0x25,0x07,0xf7,0xf3,0x21,0xf7, // C: Vibraphone
0x35,0x64,0x00,0x00,0xff,0xf3,0x77,0xf5, // D: Vibrato Bell / Chime
0x11,0x31,0x00,0x07,0xdd,0xf3,0xff,0xfb, // E: Click Sine / Tom Tom II
0x3a,0x21,0x00,0x07,0x95,0x84,0x0f,0xf5, // F: Noise and Tone
0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, // R: Bass Drum
0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x48, // R: High-Hat(M) / Snare Drum(C)
0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, // R: Tom-tom(M) / Top Cymbal(C)
},
{ /* BANK 3 - VRC7 (DS1001) */
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 0: User
0x03,0x21,0x05,0x06,0xc8,0x81,0x42,0x27, // 1: Buzzy Bell
0x13,0x41,0x14,0x0d,0xf8,0xf7,0x23,0x12, // 2: Guitar
0x31,0x11,0x08,0x08,0xfa,0xc2,0x28,0x22, // 3: Wurly
0x31,0x61,0x0c,0x07,0xf8,0x64,0x60,0x27, // 4: Flute
0x22,0x21,0x1e,0x06,0xff,0x76,0x00,0x28, // 5: Clarinet
0x02,0x01,0x05,0x00,0xac,0xf2,0x03,0x02, // 6: Synth
0x21,0x61,0x1d,0x07,0x82,0x8f,0x10,0x07, // 7: Trumpet
0x23,0x21,0x22,0x17,0xff,0x73,0x00,0x17, // 8: Organ
0x15,0x11,0x25,0x00,0x41,0x71,0x00,0xf1, // 9: Bells
0x95,0x01,0x10,0x0f,0xb8,0xaa,0x50,0x02, // A: Vibes
0x17,0xc1,0x5e,0x07,0xfa,0xf8,0x22,0x12, // B: Vibraphone
0x71,0x23,0x11,0x06,0x65,0x74,0x10,0x16, // C: Tutti
0x01,0x02,0xd3,0x05,0xf3,0x92,0x83,0xf2, // D: Fretless
0x61,0x63,0x0c,0x00,0xa4,0xff,0x30,0x06, // E: Synth Bass
0x21,0x62,0x0d,0x00,0xa1,0xff,0x50,0x08, // F: Sweep
0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, // R: Bass Drum
0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x48, // R: High-Hat(M) / Snare Drum(C)
0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, // R: Tom-tom(M) / Top Cymbal(C)
}
};
/* clang-format on */

/* phase increment counter */
#define DP_BITS 19
#define DP_WIDTH (1 << DP_BITS)
#define DP_BASE_BITS (DP_BITS - PG_BITS)

/* dynamic range of envelope output */
#define EG_STEP 0.375
#define EG_BITS 7
#define EG_MUTE ((1 << EG_BITS) - 1)
#define EG_MAX (EG_MUTE - 4)

/* dynamic range of total level */
#define TL_STEP 0.75
#define TL_BITS 6

/* dynamic range of sustine level */
#define SL_STEP 3.0
#define SL_BITS 4

/* damper speed before key-on. key-scale affects. */
#define DAMPER_RATE 12

#define TL2EG(d) ((d) << 1)

/* sine table */
#define PG_BITS 10 /* 2^10 = 1024 length sine table */
#define PG_WIDTH (1 << PG_BITS)

/* clang-format off */
/* exp_table[x] = round((exp2((double)x / 256.0) - 1) * 1024) */
static uint16_t exp_table[256] = {
0,    3,    6,    8,    11,   14,   17,   20,   22,   25,   28,   31,   34,   37,   40,   42,
45,   48,   51,   54,   57,   60,   63,   66,   69,   72,   75,   78,   81,   84,   87,   90,
93,   96,   99,   102,  105,  108,  111,  114,  117,  120,  123,  126,  130,  133,  136,  139,
142,  145,  148,  152,  155,  158,  161,  164,  168,  171,  174,  177,  181,  184,  187,  190,
194,  197,  200,  204,  207,  210,  214,  217,  220,  224,  227,  231,  234,  237,  241,  244,
248,  251,  255,  258,  262,  265,  268,  272,  276,  279,  283,  286,  290,  293,  297,  300,
304,  308,  311,  315,  318,  322,  326,  329,  333,  337,  340,  344,  348,  352,  355,  359,
363,  367,  370,  374,  378,  382,  385,  389,  393,  397,  401,  405,  409,  412,  416,  420,
424,  428,  432,  436,  440,  444,  448,  452,  456,  460,  464,  468,  472,  476,  480,  484,
488,  492,  496,  501,  505,  509,  513,  517,  521,  526,  530,  534,  538,  542,  547,  551,
555,  560,  564,  568,  572,  577,  581,  585,  590,  594,  599,  603,  607,  612,  616,  621,
625,  630,  634,  639,  643,  648,  652,  657,  661,  666,  670,  675,  680,  684,  689,  693,
698,  703,  708,  712,  717,  722,  726,  731,  736,  741,  745,  750,  755,  760,  765,  770,
774,  779,  784,  789,  794,  799,  804,  809,  814,  819,  824,  829,  834,  839,  844,  849,
854,  859,  864,  869,  874,  880,  885,  890,  895,  900,  906,  911,  916,  921,  927,  932,
937,  942,  948,  953,  959,  964,  969,  975,  980,  986,  991,  996, 1002, 1007, 1013, 1018
};
/* fullsin_table[x] = round(-log2(sin((x + 0.5) * PI / (PG_WIDTH / 4) / 2)) * 256) */
static uint16_t fullsin_table[PG_WIDTH] = {
2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979,  949,  920,  894,  869, 
846,  825,  804,  785,  767,  749,  732,  717,  701,  687,  672,  659,  646,  633,  621,  609, 
598,  587,  576,  566,  556,  546,  536,  527,  518,  509,  501,  492,  484,  476,  468,  461,
453,  446,  439,  432,  425,  418,  411,  405,  399,  392,  386,  380,  375,  369,  363,  358,  
352,  347,  341,  336,  331,  326,  321,  316,  311,  307,  302,  297,  293,  289,  284,  280,
276,  271,  267,  263,  259,  255,  251,  248,  244,  240,  236,  233,  229,  226,  222,  219, 
215,  212,  209,  205,  202,  199,  196,  193,  190,  187,  184,  181,  178,  175,  172,  169, 
167,  164,  161,  159,  156,  153,  151,  148,  146,  143,  141,  138,  136,  134,  131,  129,  
127,  125,  122,  120,  118,  116,  114,  112,  110,  108,  106,  104,  102,  100,  98,   96,   
94,   92,   91,   89,   87,   85,   83,   82,   80,   78,   77,   75,   74,   72,   70,   69,
67,   66,   64,   63,   62,   60,   59,   57,   56,   55,   53,   52,   51,   49,   48,   47,  
46,   45,   43,   42,   41,   40,   39,   38,   37,   36,   35,   34,   33,   32,   31,   30,  
29,   28,   27,   26,   25,   24,   23,   23,   22,   21,   20,   20,   19,   18,   17,   17,   
16,   15,   15,   14,   13,   13,   12,   12,   11,   10,   10,   9,    9,    8,    8,    7,    
7,    7,    6,    6,    5,    5,    5,    4,    4,    4,    3,    3,    3,    2,    2,    2,
2,    1,    1,    1,    1,    1,    1,    1,    0,    0,    0,    0,    0,    0,    0,    0,
};
/* clang-format on */

static uint16_t halfsin_table[PG_WIDTH];
static uint16_t *wave_table_map[2] = {fullsin_table, halfsin_table};

/* pitch modulator */
/* offset to fnum, rough approximation of 14 cents depth. */
static int8_t pm_table[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},    // fnum = 000xxxxxx
    {0, 0, 1, 0, 0, 0, -1, 0},   // fnum = 001xxxxxx
    {0, 1, 2, 1, 0, -1, -2, -1}, // fnum = 010xxxxxx
    {0, 1, 3, 1, 0, -1, -3, -1}, // fnum = 011xxxxxx
    {0, 2, 4, 2, 0, -2, -4, -2}, // fnum = 100xxxxxx
    {0, 2, 5, 2, 0, -2, -5, -2}, // fnum = 101xxxxxx
    {0, 3, 6, 3, 0, -3, -6, -3}, // fnum = 110xxxxxx
    {0, 3, 7, 3, 0, -3, -7, -3}, // fnum = 111xxxxxx
};

/* amplitude lfo table */
/* The following envelop pattern is verified on real YM2413. */
/* each element repeates 64 cycles */
static uint8_t am_table[210] = {0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  //
                                2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  //
                                4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  //
                                6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  //
                                8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  //
                                10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, //
                                12, 12, 12, 12, 12, 12, 12, 12,                                 //
                                13, 13, 13,                                                     //
                                12, 12, 12, 12, 12, 12, 12, 12,                                 //
                                11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, //
                                9,  9,  9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  //
                                7,  7,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6,  6,  6,  //
                                5,  5,  5,  5,  5,  5,  5,  5,  4,  4,  4,  4,  4,  4,  4,  4,  //
                                3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  2,  //
                                1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0};

/* envelope decay increment step table */
/* based on andete's research */
static uint8_t eg_step_tables[4][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1},
    {0, 1, 0, 1, 1, 1, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 1},
};

enum __Y8960OPLL_EG_STATE { ATTACK, DECAY, SUSTAIN, RELEASE, DAMP, UNKNOWN };

static uint32_t ml_table[16] = {1,     1 * 2, 2 * 2,  3 * 2,  4 * 2,  5 * 2,  6 * 2,  7 * 2,
                                8 * 2, 9 * 2, 10 * 2, 10 * 2, 12 * 2, 12 * 2, 15 * 2, 15 * 2};

#define dB2(x) ((x)*2)
static double kl_table[16] = {dB2(0.000),  dB2(9.000),  dB2(12.000), dB2(13.875), dB2(15.000), dB2(16.125),
                              dB2(16.875), dB2(17.625), dB2(18.000), dB2(18.750), dB2(19.125), dB2(19.500),
                              dB2(19.875), dB2(20.250), dB2(20.625), dB2(21.000)};

static uint32_t tll_table[8 * 16][1 << TL_BITS][4];
static int32_t rks_table[8 * 2][2];

static Y8960OPLL_PATCH null_patch = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static Y8960OPLL_PATCH default_patch[Y8960OPLL_TONE_NUM][(16 + 3) * 2];

/* don't forget min/max is defined as a macro in stdlib.h of Visual C. */
#ifndef min
static INLINE int min(int i, int j) { return (i < j) ? i : j; }
#endif
#ifndef max
static INLINE int max(int i, int j) { return (i > j) ? i : j; }
#endif

/***************************************************

           Internal Sample Rate Converter

****************************************************/
/* Note: to disable internal rate converter, set clock/72 to output sampling rate. */

/*
 * LW is truncate length of sinc(x) calculation.
 * Lower LW is faster, higher LW results better quality.
 * LW must be a non-zero positive even number, no upper limit.
 * LW=16 or greater is recommended when upsampling.
 * LW=8 is practically okay for downsampling.
 */
#define LW 16

/* number of phases between two input samples. must be a power of 2. */
#define SINC_RESO_BITS 8
#define SINC_RESO (1 << SINC_RESO_BITS)
#define SINC_AMP_BITS 12

// double hamming(double x) { return 0.54 - 0.46 * cos(2 * PI * x); }
static double blackman(double x) { return 0.42 - 0.5 * cos(2 * _PI_ * x) + 0.08 * cos(4 * _PI_ * x); }
static double sinc(double x) { return (x == 0.0 ? 1.0 : sin(_PI_ * x) / (_PI_ * x)); }
static double windowed_sinc(double x) { return blackman(0.5 + 0.5 * x / (LW / 2)) * sinc(x); }

/* f_inp: input frequency. f_out: output frequencey, ch: number of channels */
Y8960OPLL_RateConv *Y8960OPLL_RateConv_new(double f_inp, double f_out, int ch) {
  Y8960OPLL_RateConv *conv = malloc(sizeof(Y8960OPLL_RateConv));
  int i;

  conv->ch = ch;
  conv->f_ratio = f_inp / f_out;
  conv->timer_step = (uint32_t)((conv->f_ratio - floor(conv->f_ratio)) * 4294967296.0);
  conv->buf = malloc(sizeof(conv->buf[0]) * ch);
  conv->pos = malloc(sizeof(conv->pos[0]) * ch);
  for (i = 0; i < ch; i++) {
    conv->buf[i] = malloc(sizeof(conv->buf[0][0]) * LW * 2);
  }

  /* sinc_table[p * LW + k] is the coefficient for buf[k] at phase dn = p / SINC_RESO (0 <= p <= SINC_RESO).
     The coefficients for p and SINC_RESO - p are the same in reverse order, but both are kept to avoid a branch. */
  conv->sinc_table = malloc(sizeof(conv->sinc_table[0]) * (SINC_RESO + 1) * LW);
  for (i = 0; i < (SINC_RESO + 1) * LW; i++) {
    const double x = fabs((i % LW) - (LW / 2 - 1) - (double)(i / LW) / SINC_RESO);
    if (f_out < f_inp) {
      /* for downsampling */
      conv->sinc_table[i] = (int16_t)((1 << SINC_AMP_BITS) * windowed_sinc(x / conv->f_ratio) / conv->f_ratio);
    } else {
      /* for upsampling */
      conv->sinc_table[i] = (int16_t)((1 << SINC_AMP_BITS) * windowed_sinc(x));
    }
  }

  return conv;
}

/* p is the output phase in SINC_RESO units (0 <= p <= SINC_RESO). */
static INLINE int16_t rateconv_get(Y8960OPLL_RateConv *conv, int ch, uint32_t p) {
  const int16_t *buf = conv->buf[ch] + conv->pos[ch];
  const int16_t *coef = conv->sinc_table + p * LW;
  int32_t sum = 0;
  int k;
  for (k = 0; k < LW; k++) {
    sum += buf[k] * coef[k];
  }
  return sum >> SINC_AMP_BITS;
}

void Y8960OPLL_RateConv_reset(Y8960OPLL_RateConv *conv) {
  int i;
  conv->timer = 0;
  for (i = 0; i < conv->ch; i++) {
    conv->pos[i] = 0;
    memset(conv->buf[i], 0, sizeof(conv->buf[i][0]) * LW * 2);
  }
}

int Y8960OPLL_RateConv_getBufferLength(void) { return LW; }

/* put original data to this converter at f_inp. */
void Y8960OPLL_RateConv_putData(Y8960OPLL_RateConv *conv, int ch, int16_t data) {
  int16_t *buf = conv->buf[ch];
  int pos = conv->pos[ch];
  buf[pos] = buf[pos + LW] = data;
  conv->pos[ch] = (pos + 1 == LW) ? 0 : pos + 1;
}

/* get resampled data from this converter at f_out. */
/* this function must be called f_out / f_inp times per one putData call. */
int16_t Y8960OPLL_RateConv_getData(Y8960OPLL_RateConv *conv, int ch) {
  conv->timer += conv->timer_step;
  return rateconv_get(conv, ch, (uint32_t)(((uint64_t)conv->timer + (1u << (31 - SINC_RESO_BITS))) >> (32 - SINC_RESO_BITS)));
}

void Y8960OPLL_RateConv_delete(Y8960OPLL_RateConv *conv) {
  int i;
  for (i = 0; i < conv->ch; i++) {
    free(conv->buf[i]);
  }
  free(conv->buf);
  free(conv->pos);
  free(conv->sinc_table);
  free(conv);
}

/***************************************************

                  Create tables

****************************************************/

static void makeSinTable(void) {
  int x;

  for (x = 0; x < PG_WIDTH / 4; x++) {
    fullsin_table[PG_WIDTH / 4 + x] = fullsin_table[PG_WIDTH / 4 - x - 1];
  }

  for (x = 0; x < PG_WIDTH / 2; x++) {
    fullsin_table[PG_WIDTH / 2 + x] = 0x8000 | fullsin_table[x];
  }

  for (x = 0; x < PG_WIDTH / 2; x++)
    halfsin_table[x] = fullsin_table[x];

  for (x = PG_WIDTH / 2; x < PG_WIDTH; x++)
    halfsin_table[x] = 0xfff;
}

static void makeTllTable(void) {

  int32_t tmp;
  int32_t fnum, block, TL, KL;

  for (fnum = 0; fnum < 16; fnum++) {
    for (block = 0; block < 8; block++) {
      for (TL = 0; TL < 64; TL++) {
        for (KL = 0; KL < 4; KL++) {
          if (KL == 0) {
            tll_table[(block << 4) | fnum][TL][KL] = TL2EG(TL);
          } else {
            tmp = (int32_t)(kl_table[fnum] - dB2(3.000) * (7 - block));
            if (tmp <= 0)
              tll_table[(block << 4) | fnum][TL][KL] = TL2EG(TL);
            else
              tll_table[(block << 4) | fnum][TL][KL] = (uint32_t)((tmp >> (3 - KL)) / EG_STEP) + TL2EG(TL);
          }
        }
      }
    }
  }
}

static void makeRksTable(void) {
  int fnum8, block;
  for (fnum8 = 0; fnum8 < 2; fnum8++)
    for (block = 0; block < 8; block++) {
      rks_table[(block << 1) | fnum8][1] = (block << 1) + fnum8;
      rks_table[(block << 1) | fnum8][0] = block >> 1;
    }
}

static void makeDefaultPatch(void) {
  int i, j;
  for (i = 0; i < Y8960OPLL_TONE_NUM; i++)
    for (j = 0; j < 19; j++)
      Y8960OPLL_getDefaultPatch(i, j, &default_patch[i][j * 2]);
}

static uint8_t table_initialized = 0;

static void initializeTables(void) {
  makeTllTable();
  makeRksTable();
  makeSinTable();
  makeDefaultPatch();
  table_initialized = 1;
}

/*********************************************************

                      Synthesizing

*********************************************************/
#define SLOT_BD1 12
#define SLOT_BD2 13
#define SLOT_HH 14
#define SLOT_SD 15
#define SLOT_TOM 16
#define SLOT_CYM 17

/* utility macros */
#define MOD(o, x) (&(o)->slot[(x) << 1])
#define CAR(o, x) (&(o)->slot[((x) << 1) | 1])
#define BIT(s, b) (((s) >> (b)) & 1)

#if Y8960OPLL_DEBUG
static void _debug_print_patch(Y8960OPLL_SLOT *slot) {
  Y8960OPLL_PATCH *p = slot->patch;
  printf("[slot#%d am:%d pm:%d eg:%d kr:%d ml:%d kl:%d tl:%d ws:%d fb:%d A:%d D:%d S:%d R:%d]\n", slot->number, //
         p->AM, p->PM, p->EG, p->KR, p->ML,                                                                     //
         p->KL, p->TL, p->WS, p->FB,                                                                            //
         p->AR, p->DR, p->SL, p->RR);
}

static char *_debug_eg_state_name(Y8960OPLL_SLOT *slot) {
  switch (slot->eg_state) {
  case ATTACK:
    return "attack";
  case DECAY:
    return "decay";
  case SUSTAIN:
    return "sustain";
  case RELEASE:
    return "release";
  case DAMP:
    return "damp";
  default:
    return "unknown";
  }
}

static INLINE void _debug_print_slot_info(Y8960OPLL_SLOT *slot) {
  char *name = _debug_eg_state_name(slot);
  printf("[slot#%d state:%s fnum:%03x rate:%d-%d]\n", slot->number, name, slot->blk_fnum, slot->eg_rate_h,
         slot->eg_rate_l);
  _debug_print_patch(slot);
  fflush(stdout);
}
#endif

static INLINE int get_parameter_rate(Y8960OPLL_SLOT *slot) {

  if ((slot->type & 1) == 0 && slot->key_flag == 0) {
    return 0;
  }

  switch (slot->eg_state) {
  case ATTACK:
    return slot->patch->AR;
  case DECAY:
    return slot->patch->DR;
  case SUSTAIN:
    return slot->patch->EG ? 0 : slot->patch->RR;
  case RELEASE:
    if (slot->sus_flag) {
      return 5;
    } else if (slot->patch->EG) {
      return slot->patch->RR;
    } else {
      return 7;
    }
  case DAMP:
    return DAMPER_RATE;
  default:
    return 0;
  }
}

enum SLOT_UPDATE_FLAG {
  UPDATE_WS = 1,
  UPDATE_TLL = 2,
  UPDATE_RKS = 4,
  UPDATE_EG = 8,
  UPDATE_ALL = 255,
};

static INLINE void request_update(Y8960OPLL_SLOT *slot, int flag) { slot->update_requests |= flag; }

static void commit_slot_update(Y8960OPLL_SLOT *slot) {

#if Y8960OPLL_DEBUG
  if (slot->last_eg_state != slot->eg_state) {
    _debug_print_slot_info(slot);
    slot->last_eg_state = slot->eg_state;
  }
#endif

  if (slot->update_requests & UPDATE_WS) {
    slot->wave_table = wave_table_map[slot->patch->WS];
  }

  if (slot->update_requests & UPDATE_TLL) {
    if ((slot->type & 1) == 0) {
      slot->tll = tll_table[slot->blk_fnum >> 5][slot->patch->TL][slot->patch->KL];
    } else {
      slot->tll = tll_table[slot->blk_fnum >> 5][slot->volume][slot->patch->KL];
    }
  }

  if (slot->update_requests & UPDATE_RKS) {
    slot->rks = rks_table[slot->blk_fnum >> 8][slot->patch->KR];
  }

  if (slot->update_requests & (UPDATE_RKS | UPDATE_EG)) {
    int p_rate = get_parameter_rate(slot);

    if (p_rate == 0) {
      slot->eg_shift = 0;
      slot->eg_rate_h = 0;
      slot->eg_rate_l = 0;
      return;
    }

    slot->eg_rate_h = min(15, p_rate + (slot->rks >> 2));
    slot->eg_rate_l = slot->rks & 3;
    if (slot->eg_state == ATTACK) {
      slot->eg_shift = (0 < slot->eg_rate_h && slot->eg_rate_h < 12) ? (13 - slot->eg_rate_h) : 0;
    } else {
      slot->eg_shift = (slot->eg_rate_h < 13) ? (13 - slot->eg_rate_h) : 0;
    }
  }

  slot->update_requests = 0;
}

static void reset_slot(Y8960OPLL_SLOT *slot, int number) {
  slot->number = number;
  slot->type = number % 2;
  slot->pg_keep = 0;
  slot->wave_table = wave_table_map[0];
  slot->pg_phase = 0;
  slot->output[0] = 0;
  slot->output[1] = 0;
  slot->eg_state = RELEASE;
  slot->eg_shift = 0;
  slot->rks = 0;
  slot->tll = 0;
  slot->key_flag = 0;
  slot->sus_flag = 0;
  slot->blk_fnum = 0;
  slot->blk = 0;
  slot->fnum = 0;
  slot->volume = 0;
  slot->pg_out = 0;
  slot->eg_out = EG_MUTE;
  slot->patch = &null_patch;
}

static INLINE void slotOn(Y8960OPLL *opll, int i) {
  Y8960OPLL_SLOT *slot = &opll->slot[i];
  slot->key_flag = 1;
  slot->eg_state = DAMP;
  request_update(slot, UPDATE_EG);
}

static INLINE void slotOff(Y8960OPLL *opll, int i) {
  Y8960OPLL_SLOT *slot = &opll->slot[i];
  slot->key_flag = 0;
  if (slot->type & 1) {
    slot->eg_state = RELEASE;
    request_update(slot, UPDATE_EG);
  }
}

static INLINE void update_key_status(Y8960OPLL *opll) {
  const uint8_t r14 = opll->reg[0x0e];
  const uint8_t rhythm_mode = BIT(r14, 5);
  uint32_t new_slot_key_status = 0;
  uint32_t updated_status;
  int ch;

  for (ch = 0; ch < 9; ch++)
    if (opll->reg[0x20 + ch] & 0x10)
      new_slot_key_status |= 3 << (ch * 2);

  if (rhythm_mode) {
    if (r14 & 0x10)
      new_slot_key_status |= 3 << SLOT_BD1;

    if (r14 & 0x01)
      new_slot_key_status |= 1 << SLOT_HH;

    if (r14 & 0x08)
      new_slot_key_status |= 1 << SLOT_SD;

    if (r14 & 0x04)
      new_slot_key_status |= 1 << SLOT_TOM;

    if (r14 & 0x02)
      new_slot_key_status |= 1 << SLOT_CYM;
  }

  updated_status = opll->slot_key_status ^ new_slot_key_status;

  if (updated_status) {
    int i;
    for (i = 0; i < 18; i++)
      if (BIT(updated_status, i)) {
        if (BIT(new_slot_key_status, i)) {
          slotOn(opll, i);
        } else {
          slotOff(opll, i);
        }
      }
  }

  opll->slot_key_status = new_slot_key_status;
}

static INLINE void set_patch(Y8960OPLL *opll, int32_t ch, int32_t num) {
  Y8960OPLL_PATCH *base = (num == 0) ? opll->user_patch : opll->patch[opll->patch_bank[ch]] + num * 2;
  opll->patch_number[ch] = num;
  MOD(opll, ch)->patch = &base[0];
  CAR(opll, ch)->patch = &base[1];
  request_update(MOD(opll, ch), UPDATE_ALL);
  request_update(CAR(opll, ch), UPDATE_ALL);
}

static INLINE void set_patch_bank(Y8960OPLL *opll, int32_t ch, uint8_t bank) {
  opll->patch_bank[ch] = bank;
  set_patch(opll, ch, opll->patch_number[ch]);
}

static INLINE void set_sus_flag(Y8960OPLL *opll, int ch, int flag) {
  CAR(opll, ch)->sus_flag = flag;
  request_update(CAR(opll, ch), UPDATE_EG);
  if (MOD(opll, ch)->type & 1) {
    MOD(opll, ch)->sus_flag = flag;
    request_update(MOD(opll, ch), UPDATE_EG);
  }
}

/* set volume ( volume : 6bit, register value << 2 ) */
static INLINE void set_volume(Y8960OPLL *opll, int ch, int volume) {
  CAR(opll, ch)->volume = volume;
  request_update(CAR(opll, ch), UPDATE_TLL);
}

static INLINE void set_slot_volume(Y8960OPLL_SLOT *slot, int volume) {
  slot->volume = volume;
  request_update(slot, UPDATE_TLL);
}

/* set f-Nnmber ( fnum : 9bit ) */
static INLINE void set_fnumber(Y8960OPLL *opll, int ch, int fnum) {
  Y8960OPLL_SLOT *car = CAR(opll, ch);
  Y8960OPLL_SLOT *mod = MOD(opll, ch);
  car->fnum = fnum;
  car->blk_fnum = (car->blk_fnum & 0xe00) | (fnum & 0x1ff);
  mod->fnum = fnum;
  mod->blk_fnum = (mod->blk_fnum & 0xe00) | (fnum & 0x1ff);
  request_update(car, UPDATE_EG | UPDATE_RKS | UPDATE_TLL);
  request_update(mod, UPDATE_EG | UPDATE_RKS | UPDATE_TLL);
}

/* set block data (blk : 3bit ) */
static INLINE void set_block(Y8960OPLL *opll, int ch, int blk) {
  Y8960OPLL_SLOT *car = CAR(opll, ch);
  Y8960OPLL_SLOT *mod = MOD(opll, ch);
  car->blk = blk;
  car->blk_fnum = ((blk & 7) << 9) | (car->blk_fnum & 0x1ff);
  mod->blk = blk;
  mod->blk_fnum = ((blk & 7) << 9) | (mod->blk_fnum & 0x1ff);
  request_update(car, UPDATE_EG | UPDATE_RKS | UPDATE_TLL);
  request_update(mod, UPDATE_EG | UPDATE_RKS | UPDATE_TLL);
}

static INLINE void update_rhythm_mode(Y8960OPLL *opll) {
  const uint8_t new_rhythm_mode = (opll->reg[0x0e] >> 5) & 1;

  if (opll->rhythm_mode != new_rhythm_mode) {

    if (new_rhythm_mode) {
      opll->slot[SLOT_HH].type = 3;
      opll->slot[SLOT_HH].pg_keep = 1;
      opll->slot[SLOT_SD].type = 3;
      opll->slot[SLOT_TOM].type = 3;
      opll->slot[SLOT_CYM].type = 3;
      opll->slot[SLOT_CYM].pg_keep = 1;
      set_patch(opll, 6, 16);
      set_patch(opll, 7, 17);
      set_patch(opll, 8, 18);
      set_slot_volume(&opll->slot[SLOT_HH], ((opll->reg[0x37] >> 4) & 15) << 2);
      set_slot_volume(&opll->slot[SLOT_TOM], ((opll->reg[0x38] >> 4) & 15) << 2);
    } else {
      opll->slot[SLOT_HH].type = 0;
      opll->slot[SLOT_HH].pg_keep = 0;
      opll->slot[SLOT_SD].type = 1;
      opll->slot[SLOT_TOM].type = 0;
      opll->slot[SLOT_CYM].type = 1;
      opll->slot[SLOT_CYM].pg_keep = 0;
      set_patch(opll, 6, opll->reg[0x36] >> 4);
      set_patch(opll, 7, opll->reg[0x37] >> 4);
      set_patch(opll, 8, opll->reg[0x38] >> 4);
    }
  }

  opll->rhythm_mode = new_rhythm_mode;
}

static void update_ampm(Y8960OPLL *opll) {
  if (opll->test_flag & 2) {
    opll->pm_phase = 0;
    opll->am_phase = 0;
  } else {
    opll->pm_phase += (opll->test_flag & 8) ? 1024 : 1;
    opll->am_phase += (opll->test_flag & 8) ? 64 : 1;
  }
  opll->lfo_am = am_table[(opll->am_phase >> 6) % sizeof(am_table)];
}

static void update_noise(Y8960OPLL *opll, int cycle) {
  int i;
  for (i = 0; i < cycle; i++) {
    if (opll->noise & 1) {
      opll->noise ^= 0x800200;
    }
    opll->noise >>= 1;
  }
}

static void update_short_noise(Y8960OPLL *opll) {
  const uint32_t pg_hh = opll->slot[SLOT_HH].pg_out;
  const uint32_t pg_cym = opll->slot[SLOT_CYM].pg_out;

  const uint8_t h_bit2 = BIT(pg_hh, PG_BITS - 8);
  const uint8_t h_bit7 = BIT(pg_hh, PG_BITS - 3);
  const uint8_t h_bit3 = BIT(pg_hh, PG_BITS - 7);

  const uint8_t c_bit3 = BIT(pg_cym, PG_BITS - 7);
  const uint8_t c_bit5 = BIT(pg_cym, PG_BITS - 5);

  opll->short_noise = (h_bit2 ^ h_bit7) | (h_bit3 ^ c_bit5) | (c_bit3 ^ c_bit5);
}

static INLINE void calc_phase(Y8960OPLL_SLOT *slot, int32_t pm_phase, uint8_t reset) {
  const int8_t pm = slot->patch->PM ? pm_table[(slot->fnum >> 6) & 7][(pm_phase >> 10) & 7] : 0;
  if (reset) {
    slot->pg_phase = 0;
  }
  slot->pg_phase += (((slot->fnum & 0x1ff) * 2 + pm) * ml_table[slot->patch->ML]) << slot->blk >> 2;
  slot->pg_phase &= (DP_WIDTH - 1);
  slot->pg_out = slot->pg_phase >> DP_BASE_BITS;
}

static INLINE uint8_t lookup_attack_step(Y8960OPLL_SLOT *slot, uint32_t counter) {
  int index;

  switch (slot->eg_rate_h) {
  case 12:
    index = (counter & 0xc) >> 1;
    return 4 - eg_step_tables[slot->eg_rate_l][index];
  case 13:
    index = (counter & 0xc) >> 1;
    return 3 - eg_step_tables[slot->eg_rate_l][index];
  case 14:
    index = (counter & 0xc) >> 1;
    return 2 - eg_step_tables[slot->eg_rate_l][index];
  case 0:
  case 15:
    return 0;
  default:
    index = counter >> slot->eg_shift;
    return eg_step_tables[slot->eg_rate_l][index & 7] ? 4 : 0;
  }
}

static INLINE uint8_t lookup_decay_step(Y8960OPLL_SLOT *slot, uint32_t counter) {
  int index;

  switch (slot->eg_rate_h) {
  case 0:
    return 0;
  case 13:
    index = ((counter & 0xc) >> 1) | (counter & 1);
    return eg_step_tables[slot->eg_rate_l][index];
  case 14:
    index = ((counter & 0xc) >> 1);
    return eg_step_tables[slot->eg_rate_l][index] + 1;
  case 15:
    return 2;
  default:
    index = counter >> slot->eg_shift;
    return eg_step_tables[slot->eg_rate_l][index & 7];
  }
}

static INLINE void start_envelope(Y8960OPLL_SLOT *slot) {
  if (min(15, slot->patch->AR + (slot->rks >> 2)) == 15) {
    slot->eg_state = DECAY;
    slot->eg_out = 0;
  } else {
    slot->eg_state = ATTACK;
  }
  request_update(slot, UPDATE_EG);
}

static INLINE void calc_envelope(Y8960OPLL_SLOT *slot, Y8960OPLL_SLOT *buddy, uint16_t eg_counter, uint8_t test) {

  uint32_t mask = (1 << slot->eg_shift) - 1;
  uint8_t s;

  if (slot->eg_state == ATTACK) {
    if (0 < slot->eg_out && 0 < slot->eg_rate_h && (eg_counter & mask & ~3) == 0) {
      s = lookup_attack_step(slot, eg_counter);
      if (0 < s) {
        slot->eg_out = max(0, ((int)slot->eg_out - (slot->eg_out >> s) - 1));
      }
    }
  } else {
    if (slot->eg_rate_h > 0 && (eg_counter & mask) == 0) {
      slot->eg_out = min(EG_MUTE, slot->eg_out + lookup_decay_step(slot, eg_counter));
    }
  }

  switch (slot->eg_state) {
  case DAMP:
    // DAMP to ATTACK transition is occured when the envelope reaches EG_MAX (max attenuation but it's not mute).
    // Do not forget to check (eg_counter & mask) == 0 to synchronize it with the progress of the envelope.
    if (slot->eg_out >= EG_MAX && (eg_counter & mask) == 0) {
      start_envelope(slot);
      if (slot->type & 1) {
        if (!slot->pg_keep) {
          slot->pg_phase = 0;
        }
        if (buddy && !buddy->pg_keep) {
          buddy->pg_phase = 0;
        }
      }
    }
    break;

  case ATTACK:
    if (slot->eg_out == 0) {
      slot->eg_state = DECAY;
      request_update(slot, UPDATE_EG);
    }
    break;

  case DECAY:
    // DECAY to SUSTAIN transition must be checked at every cycle regardless of the conditions of the envelope rate and
    // counter. i.e. the transition is not synchronized with the progress of the envelope.
    if ((slot->eg_out >> 3) == slot->patch->SL) {
      slot->eg_state = SUSTAIN;
      request_update(slot, UPDATE_EG);
    }
    break;

  case SUSTAIN:
  case RELEASE:
  default:
    break;
  }

  if (test) {
    slot->eg_out = 0;
  }
}

static void update_slots(Y8960OPLL *opll) {
  int i;
  opll->eg_counter++;

  for (i = 0; i < 18; i++) {
    Y8960OPLL_SLOT *slot = &opll->slot[i];
    Y8960OPLL_SLOT *buddy = NULL;
    if (slot->type == 0) {
      buddy = &opll->slot[i + 1];
    }
    if (slot->type == 1) {
      buddy = &opll->slot[i - 1];
    }
    if (slot->update_requests) {
      commit_slot_update(slot);
    }
    calc_envelope(slot, buddy, opll->eg_counter, opll->test_flag & 1);
    calc_phase(slot, opll->pm_phase, opll->test_flag & 4);
  }
}

/* output: -4095...4095 */
static INLINE int16_t lookup_exp_table(uint16_t i) {
  /* from andete's expression */
  int16_t t = (exp_table[(i & 0xff) ^ 0xff] + 1024);
  int16_t res = t >> ((i & 0x7f00) >> 8);
  return ((i & 0x8000) ? ~res : res) << 1;
}

static INLINE int16_t to_linear(uint16_t h, Y8960OPLL_SLOT *slot, int16_t am) {
  uint16_t att;
  if (slot->eg_out > EG_MAX)
    return 0;

  att = min(EG_MUTE, (slot->eg_out + slot->tll + am)) << 4;
  return lookup_exp_table(h + att);
}

static INLINE int16_t calc_slot_car(Y8960OPLL *opll, int ch, int16_t fm) {
  Y8960OPLL_SLOT *slot = CAR(opll, ch);

  uint8_t am = slot->patch->AM ? opll->lfo_am : 0;

  slot->output[1] = slot->output[0];
  slot->output[0] = to_linear(slot->wave_table[(slot->pg_out + 2 * (fm >> 1)) & (PG_WIDTH - 1)], slot, am);

  return slot->output[0];
}

static INLINE int16_t calc_slot_mod(Y8960OPLL *opll, int ch) {
  Y8960OPLL_SLOT *slot = MOD(opll, ch);

  int16_t fm = slot->patch->FB > 0 ? (slot->output[1] + slot->output[0]) >> (9 - slot->patch->FB) : 0;
  uint8_t am = slot->patch->AM ? opll->lfo_am : 0;

  slot->output[1] = slot->output[0];
  slot->output[0] = to_linear(slot->wave_table[(slot->pg_out + fm) & (PG_WIDTH - 1)], slot, am);

  return slot->output[0];
}

static INLINE int16_t calc_slot_tom(Y8960OPLL *opll) {
  Y8960OPLL_SLOT *slot = MOD(opll, 8);

  return to_linear(slot->wave_table[slot->pg_out], slot, 0);
}

/* Specify phase offset directly based on 10-bit (1024-length) sine table */
#define _PD(phase) ((PG_BITS < 10) ? (phase >> (10 - PG_BITS)) : (phase << (PG_BITS - 10)))

static INLINE int16_t calc_slot_snare(Y8960OPLL *opll) {
  Y8960OPLL_SLOT *slot = CAR(opll, 7);

  uint32_t phase;

  if (BIT(slot->pg_out, PG_BITS - 2))
    phase = (opll->noise & 1) ? _PD(0x300) : _PD(0x200);
  else
    phase = (opll->noise & 1) ? _PD(0x0) : _PD(0x100);

  return to_linear(slot->wave_table[phase], slot, 0);
}

static INLINE int16_t calc_slot_cym(Y8960OPLL *opll) {
  Y8960OPLL_SLOT *slot = CAR(opll, 8);

  uint32_t phase = opll->short_noise ? _PD(0x300) : _PD(0x100);

  return to_linear(slot->wave_table[phase], slot, 0);
}

static INLINE int16_t calc_slot_hat(Y8960OPLL *opll) {
  Y8960OPLL_SLOT *slot = MOD(opll, 7);

  uint32_t phase;

  if (opll->short_noise)
    phase = (opll->noise & 1) ? _PD(0x2d0) : _PD(0x234);
  else
    phase = (opll->noise & 1) ? _PD(0x34) : _PD(0xd0);

  return to_linear(slot->wave_table[phase], slot, 0);
}

#define _MO(x) (-(x) >> 1)
#define _RO(x) (x)

static void update_output(Y8960OPLL *opll) {
  int16_t *out;
  int i;

  update_ampm(opll);
  update_short_noise(opll);
  update_slots(opll);

  out = opll->ch_out;

  /* CH1-6 */
  for (i = 0; i < 6; i++) {
    if (!(opll->mask & Y8960OPLL_MASK_CH(i))) {
      out[i] = _MO(calc_slot_car(opll, i, calc_slot_mod(opll, i)));
    }
  }

  /* CH7 */
  if (!opll->rhythm_mode) {
    if (!(opll->mask & Y8960OPLL_MASK_CH(6))) {
      out[6] = _MO(calc_slot_car(opll, 6, calc_slot_mod(opll, 6)));
    }
  } else {
    if (!(opll->mask & Y8960OPLL_MASK_BD)) {
      out[9] = _RO(calc_slot_car(opll, 6, calc_slot_mod(opll, 6)));
    }
  }
  update_noise(opll, 14);

  /* CH8 */
  if (!opll->rhythm_mode) {
    if (!(opll->mask & Y8960OPLL_MASK_CH(7))) {
      out[7] = _MO(calc_slot_car(opll, 7, calc_slot_mod(opll, 7)));
    }
  } else {
    if (!(opll->mask & Y8960OPLL_MASK_HH)) {
      out[10] = _RO(calc_slot_hat(opll));
    }
    if (!(opll->mask & Y8960OPLL_MASK_SD)) {
      out[11] = _RO(calc_slot_snare(opll));
    }
  }
  update_noise(opll, 2);

  /* CH9 */
  if (!opll->rhythm_mode) {
    if (!(opll->mask & Y8960OPLL_MASK_CH(8))) {
      out[8] = _MO(calc_slot_car(opll, 8, calc_slot_mod(opll, 8)));
    }
  } else {
    if (!(opll->mask & Y8960OPLL_MASK_TOM)) {
      out[12] = _RO(calc_slot_tom(opll));
    }
    if (!(opll->mask & Y8960OPLL_MASK_CYM)) {
      out[13] = _RO(calc_slot_cym(opll));
    }
  }
  update_noise(opll, 2);
}

INLINE static void mix_output(Y8960OPLL *opll) {
  int16_t out = 0;
  int i;
  for (i = 0; i < 14; i++) {
    out += opll->ch_out[i];
  }
  if (opll->conv) {
    Y8960OPLL_RateConv_putData(opll->conv, 0, out);
  } else {
    opll->mix_out[0] = out;
  }
}

INLINE static void mix_output_stereo(Y8960OPLL *opll) {
  int16_t *out = opll->mix_out;
  int i;
  out[0] = out[1] = 0;
  for (i = 0; i < 14; i++) {
    if (opll->pan[i] & 2)
      out[0] += (int16_t)(opll->ch_out[i] * opll->pan_fine[i][0] / 4096);
    if (opll->pan[i] & 1)
      out[1] += (int16_t)(opll->ch_out[i] * opll->pan_fine[i][1] / 4096);
  }
  if (opll->conv) {
    Y8960OPLL_RateConv_putData(opll->conv, 0, out[0]);
    Y8960OPLL_RateConv_putData(opll->conv, 1, out[1]);
  }
}

/***********************************************************

                   External Interfaces

***********************************************************/

Y8960OPLL *Y8960OPLL_new(uint32_t clk, uint32_t rate) {
  Y8960OPLL *opll;
  int i;

  if (!table_initialized) {
    initializeTables();
  }

  opll = (Y8960OPLL *)calloc(1, sizeof(Y8960OPLL));
  if (opll == NULL)
    return NULL;

  for (i = 0; i < 19 * 2; i++) {
    int b;
    for (b = 0; b < Y8960OPLL_BANK_COUNT; b++)
      memcpy(&opll->patch[b][i], &null_patch, sizeof(Y8960OPLL_PATCH));
  }
  memcpy(&opll->user_patch[0], &null_patch, sizeof(Y8960OPLL_PATCH));
  memcpy(&opll->user_patch[1], &null_patch, sizeof(Y8960OPLL_PATCH));

  opll->clk = clk;
  opll->rate = rate;
  opll->mask = 0;
  opll->conv = NULL;
  opll->mix_out[0] = 0;
  opll->mix_out[1] = 0;

  Y8960OPLL_reset(opll);
  Y8960OPLL_setChipType(opll, 0);
  Y8960OPLL_resetPatch(opll);
  return opll;
}

void Y8960OPLL_delete(Y8960OPLL *opll) {
  if (opll->conv) {
    Y8960OPLL_RateConv_delete(opll->conv);
    opll->conv = NULL;
  }
  free(opll);
}

static void reset_rate_conversion_params(Y8960OPLL *opll) {
  const double f_out = opll->rate;
  const double f_inp = opll->clk / 72.0;

  opll->out_time = 0;
  opll->out_step = opll->clk;
  opll->inp_step = opll->rate * 72;
  /* the largest inp_shift such that inp_recip fits in 32 bits */
  opll->inp_shift = 32;
  while ((((uint64_t)1 << (opll->inp_shift + 1)) - 1) / opll->inp_step < 0xffffffff) {
    opll->inp_shift++;
  }
  opll->inp_recip = (uint32_t)((((uint64_t)1 << opll->inp_shift) - 1) / opll->inp_step + 1);

  if (opll->conv) {
    Y8960OPLL_RateConv_delete(opll->conv);
    opll->conv = NULL;
  }

  if (floor(f_inp) != f_out && floor(f_inp + 0.5) != f_out) {
    opll->conv = Y8960OPLL_RateConv_new(f_inp, f_out, 2);
  }

  if (opll->conv) {
    Y8960OPLL_RateConv_reset(opll->conv);
  }
}

void Y8960OPLL_reset(Y8960OPLL *opll) {
  int i;

  if (!opll)
    return;

  opll->adr = 0;

  opll->pm_phase = 0;
  opll->am_phase = 0;

  opll->noise = 0x1;
  opll->mask = 0;

  opll->rhythm_mode = 0;
  opll->slot_key_status = 0;
  opll->eg_counter = 0;

  reset_rate_conversion_params(opll);

  for (i = 0; i < 18; i++)
    reset_slot(&opll->slot[i], i);

  for (i = 0; i < 9; i++) {
    opll->patch_bank[i] = 0;
    set_patch(opll, i, 0);
  }

  for (i = 0; i < 15; i++) {
    opll->pan[i] = 3;
    opll->pan_fine[i][1] = opll->pan_fine[i][0] = 4096;
  }

  for (i = 0; i < Y8960OPLL_REG_COUNT; i++)
    Y8960OPLL_writeReg(opll, i, 0);

  /* The pan registers reset to the centre, which is what the specification
  ** names as their default; the zero just written would sound the same, but
  ** the register itself is to read 128. */
  for (i = 0; i < 9; i++)
    Y8960OPLL_writeReg(opll, Y8960OPLL_PAN_REG_BASE + i, Y8960_PAN_CENTER);

  for (i = 0; i < 14; i++) {
    opll->ch_out[i] = 0;
  }
}

void Y8960OPLL_forceRefresh(Y8960OPLL *opll) {
  int i;

  if (opll == NULL)
    return;

  for (i = 0; i < 9; i++) {
    set_patch(opll, i, opll->patch_number[i]);
  }

  for (i = 0; i < 18; i++) {
    request_update(&opll->slot[i], UPDATE_ALL);
  }
}

void Y8960OPLL_setRate(Y8960OPLL *opll, uint32_t rate) {
  opll->rate = rate;
  reset_rate_conversion_params(opll);
}

void Y8960OPLL_setQuality(Y8960OPLL *opll, uint8_t q) {}

void Y8960OPLL_setChipType(Y8960OPLL *opll, uint8_t type) { opll->chip_type = type; }

/* The pan register of a melody channel, 50h-58h; the law is Y8960Pan.h.
**
** In rhythm mode the five rhythm sounds keep the pan of the channel they are
** generated on, as the YM2413 assigns them: BD on channel 7, HH and SD on
** channel 8, TOM and CYM on channel 9, counting from 1 as the registers do. */
static void set_pan_reg(Y8960OPLL *opll, int ch, uint8_t data) {
  Int32 gain[2];
  float pan[2];
  y8960PanGain(data, gain);
  pan[0] = (float)gain[0] / Y8960_GAIN_ONE;
  pan[1] = (float)gain[1] / Y8960_GAIN_ONE;
  Y8960OPLL_setPanFine(opll, ch, pan);
  switch (ch) {
  case 6:
    Y8960OPLL_setPanFine(opll, 9, pan);   /* BD */
    break;
  case 7:
    Y8960OPLL_setPanFine(opll, 10, pan);  /* HH */
    Y8960OPLL_setPanFine(opll, 11, pan);  /* SD */
    break;
  case 8:
    Y8960OPLL_setPanFine(opll, 12, pan);  /* TOM */
    Y8960OPLL_setPanFine(opll, 13, pan);  /* CYM */
    break;
  }
}

void Y8960OPLL_writeReg(Y8960OPLL *opll, uint32_t reg, uint8_t data) {
  int ch, i;

  if (reg >= Y8960OPLL_REG_COUNT)
    return;

  /* 49h-4Fh lie between the bank and pan blocks and are nothing; keeping them
  ** out of the mirror keeps a save state from carrying them. */
  if (Y8960OPLL_BANK_REG_BASE + 9 <= reg && reg < Y8960OPLL_PAN_REG_BASE)
    return;

  /* mirror registers */
  if ((0x19 <= reg && reg <= 0x1f) || (0x29 <= reg && reg <= 0x2f) || (0x39 <= reg && reg <= 0x3f)) {
    reg -= 9;
  }

  opll->reg[reg] = (uint8_t)data;

  switch (reg) {
  case 0x00:
    opll->user_patch[0].AM = (data >> 7) & 1;
    opll->user_patch[0].PM = (data >> 6) & 1;
    opll->user_patch[0].EG = (data >> 5) & 1;
    opll->user_patch[0].KR = (data >> 4) & 1;
    opll->user_patch[0].ML = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(MOD(opll, i), UPDATE_RKS | UPDATE_EG);
      }
    }
    break;

  case 0x01:
    opll->user_patch[1].AM = (data >> 7) & 1;
    opll->user_patch[1].PM = (data >> 6) & 1;
    opll->user_patch[1].EG = (data >> 5) & 1;
    opll->user_patch[1].KR = (data >> 4) & 1;
    opll->user_patch[1].ML = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(CAR(opll, i), UPDATE_RKS | UPDATE_EG);
      }
    }
    break;

  case 0x02:
    opll->user_patch[0].KL = (data >> 6) & 3;
    opll->user_patch[0].TL = (data)&63;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(MOD(opll, i), UPDATE_TLL);
      }
    }
    break;

  case 0x03:
    opll->user_patch[1].KL = (data >> 6) & 3;
    opll->user_patch[1].WS = (data >> 4) & 1;
    opll->user_patch[0].WS = (data >> 3) & 1;
    opll->user_patch[0].FB = (data)&7;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(MOD(opll, i), UPDATE_WS);
        request_update(CAR(opll, i), UPDATE_WS | UPDATE_TLL);
      }
    }
    break;

  case 0x04:
    opll->user_patch[0].AR = (data >> 4) & 15;
    opll->user_patch[0].DR = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(MOD(opll, i), UPDATE_EG);
      }
    }
    break;

  case 0x05:
    opll->user_patch[1].AR = (data >> 4) & 15;
    opll->user_patch[1].DR = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(CAR(opll, i), UPDATE_EG);
      }
    }
    break;

  case 0x06:
    opll->user_patch[0].SL = (data >> 4) & 15;
    opll->user_patch[0].RR = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(MOD(opll, i), UPDATE_EG);
      }
    }
    break;

  case 0x07:
    opll->user_patch[1].SL = (data >> 4) & 15;
    opll->user_patch[1].RR = (data)&15;
    for (i = 0; i < 9; i++) {
      if (opll->patch_number[i] == 0) {
        request_update(CAR(opll, i), UPDATE_EG);
      }
    }
    break;

  case 0x0e:
    if (opll->chip_type == 1)
      break;
    update_rhythm_mode(opll);
    update_key_status(opll);
    break;

  case 0x0f:
    opll->test_flag = data;
    break;

  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
    ch = reg - 0x10;
    set_fnumber(opll, ch, data + ((opll->reg[0x20 + ch] & 1) << 8));
    break;

  case 0x20:
  case 0x21:
  case 0x22:
  case 0x23:
  case 0x24:
  case 0x25:
  case 0x26:
  case 0x27:
  case 0x28:
    ch = reg - 0x20;
    set_fnumber(opll, ch, ((data & 1) << 8) + opll->reg[0x10 + ch]);
    set_block(opll, ch, (data >> 1) & 7);
    set_sus_flag(opll, ch, (data >> 5) & 1);
    update_key_status(opll);
    break;

  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
  case 0x34:
  case 0x35:
  case 0x36:
  case 0x37:
  case 0x38:
    if ((opll->reg[0x0e] & 32) && (reg >= 0x36)) {
      switch (reg) {
      case 0x37:
        set_slot_volume(MOD(opll, 7), ((data >> 4) & 15) << 2);
        break;
      case 0x38:
        set_slot_volume(MOD(opll, 8), ((data >> 4) & 15) << 2);
        break;
      default:
        break;
      }
    } else {
      set_patch(opll, reg - 0x30, (data >> 4) & 15);
    }
    set_volume(opll, reg - 0x30, (data & 15) << 2);
    break;

  case 0x40:
  case 0x41:
  case 0x42:
  case 0x43:
  case 0x44:
  case 0x45:
  case 0x46:
  case 0x47:
  case 0x48:
    set_patch_bank(opll, reg - Y8960OPLL_BANK_REG_BASE, data & 3);
    break;

  case 0x50:
  case 0x51:
  case 0x52:
  case 0x53:
  case 0x54:
  case 0x55:
  case 0x56:
  case 0x57:
  case 0x58:
    set_pan_reg(opll, reg - Y8960OPLL_PAN_REG_BASE, data);
    break;

  default:
    break;
  }
}

void Y8960OPLL_writeIO(Y8960OPLL *opll, uint32_t adr, uint8_t val) {
  if (adr & 1)
    Y8960OPLL_writeReg(opll, opll->adr, val);
  else
    opll->adr = val;
}

void Y8960OPLL_setPan(Y8960OPLL *opll, uint32_t ch, uint8_t pan) { opll->pan[ch & 15] = pan; }

void Y8960OPLL_setPanFine(Y8960OPLL *opll, uint32_t ch, float pan[2]) {
  opll->pan_fine[ch & 15][0] = (int32_t)floor(pan[0] * 4096.0 + 0.5);
  opll->pan_fine[ch & 15][1] = (int32_t)floor(pan[1] * 4096.0 + 0.5);
}

void Y8960OPLL_dumpToPatch(const uint8_t *dump, Y8960OPLL_PATCH *patch) {
  patch[0].AM = (dump[0] >> 7) & 1;
  patch[1].AM = (dump[1] >> 7) & 1;
  patch[0].PM = (dump[0] >> 6) & 1;
  patch[1].PM = (dump[1] >> 6) & 1;
  patch[0].EG = (dump[0] >> 5) & 1;
  patch[1].EG = (dump[1] >> 5) & 1;
  patch[0].KR = (dump[0] >> 4) & 1;
  patch[1].KR = (dump[1] >> 4) & 1;
  patch[0].ML = (dump[0]) & 15;
  patch[1].ML = (dump[1]) & 15;
  patch[0].KL = (dump[2] >> 6) & 3;
  patch[1].KL = (dump[3] >> 6) & 3;
  patch[0].TL = (dump[2]) & 63;
  patch[1].TL = 0;
  patch[0].FB = (dump[3]) & 7;
  patch[1].FB = 0;
  patch[0].WS = (dump[3] >> 3) & 1;
  patch[1].WS = (dump[3] >> 4) & 1;
  patch[0].AR = (dump[4] >> 4) & 15;
  patch[1].AR = (dump[5] >> 4) & 15;
  patch[0].DR = (dump[4]) & 15;
  patch[1].DR = (dump[5]) & 15;
  patch[0].SL = (dump[6] >> 4) & 15;
  patch[1].SL = (dump[7] >> 4) & 15;
  patch[0].RR = (dump[6]) & 15;
  patch[1].RR = (dump[7]) & 15;
}

void Y8960OPLL_getDefaultPatch(int32_t type, int32_t num, Y8960OPLL_PATCH *patch) {
  Y8960OPLL_dumpToPatch(default_inst[type] + num * 8, patch);
}

void Y8960OPLL_setPatch(Y8960OPLL *opll, int32_t bank, const uint8_t *dump) {
  Y8960OPLL_PATCH patch[2];
  int i;
  for (i = 0; i < 19; i++) {
    Y8960OPLL_dumpToPatch(dump + i * 8, patch);
    memcpy(&opll->patch[bank][i * 2 + 0], &patch[0], sizeof(Y8960OPLL_PATCH));
    memcpy(&opll->patch[bank][i * 2 + 1], &patch[1], sizeof(Y8960OPLL_PATCH));
  }
}

void Y8960OPLL_patchToDump(const Y8960OPLL_PATCH *patch, uint8_t *dump) {
  dump[0] = (uint8_t)((patch[0].AM << 7) + (patch[0].PM << 6) + (patch[0].EG << 5) + (patch[0].KR << 4) + patch[0].ML);
  dump[1] = (uint8_t)((patch[1].AM << 7) + (patch[1].PM << 6) + (patch[1].EG << 5) + (patch[1].KR << 4) + patch[1].ML);
  dump[2] = (uint8_t)((patch[0].KL << 6) + patch[0].TL);
  dump[3] = (uint8_t)((patch[1].KL << 6) + (patch[1].WS << 4) + (patch[0].WS << 3) + patch[0].FB);
  dump[4] = (uint8_t)((patch[0].AR << 4) + patch[0].DR);
  dump[5] = (uint8_t)((patch[1].AR << 4) + patch[1].DR);
  dump[6] = (uint8_t)((patch[0].SL << 4) + patch[0].RR);
  dump[7] = (uint8_t)((patch[1].SL << 4) + patch[1].RR);
}

void Y8960OPLL_copyPatch(Y8960OPLL *opll, int32_t bank, int32_t num, Y8960OPLL_PATCH *patch) {
  memcpy(&opll->patch[bank][num], patch, sizeof(Y8960OPLL_PATCH));
}

void Y8960OPLL_resetPatch(Y8960OPLL *opll) {
  int bank, i;
  for (bank = 0; bank < Y8960OPLL_BANK_COUNT; bank++)
    for (i = 0; i < 19 * 2; i++)
      Y8960OPLL_copyPatch(opll, bank, i, &default_patch[bank][i]);
}

/* output phase of the rate converter: round(dn * SINC_RESO), where dn = (inp_step - out_time) / inp_step
   (or 0 if out_time is 0) is the position of the output sample between the last two input samples.
   It is derived from out_time every time, so that it never drifts. */
static INLINE uint32_t get_conv_phase(Y8960OPLL *opll) {
  uint32_t d = opll->out_time ? opll->inp_step - opll->out_time : 0;
  /* dn in 0.32 fixed point */
  uint32_t dn = (uint32_t)(((uint64_t)d * opll->inp_recip) >> (opll->inp_shift - 32));
  return (uint32_t)(((uint64_t)dn + (1u << (31 - SINC_RESO_BITS))) >> (32 - SINC_RESO_BITS));
}

int16_t Y8960OPLL_calc(Y8960OPLL *opll) {
  while (opll->out_step > opll->out_time) {
    opll->out_time += opll->inp_step;
    update_output(opll);
    mix_output(opll);
  }
  opll->out_time -= opll->out_step;
  if (opll->conv) {
    opll->mix_out[0] = rateconv_get(opll->conv, 0, get_conv_phase(opll));
  }
  return opll->mix_out[0];
}

void Y8960OPLL_calcStereo(Y8960OPLL *opll, int32_t out[2]) {
  while (opll->out_step > opll->out_time) {
    opll->out_time += opll->inp_step;
    update_output(opll);
    mix_output_stereo(opll);
  }
  opll->out_time -= opll->out_step;
  if (opll->conv) {
    uint32_t p = get_conv_phase(opll);
    out[0] = rateconv_get(opll->conv, 0, p);
    out[1] = rateconv_get(opll->conv, 1, p);
  } else {
    out[0] = opll->mix_out[0];
    out[1] = opll->mix_out[1];
  }
}

uint32_t Y8960OPLL_setMask(Y8960OPLL *opll, uint32_t mask) {
  uint32_t ret;

  if (opll) {
    ret = opll->mask;
    opll->mask = mask;
    return ret;
  } else
    return 0;
}

uint32_t Y8960OPLL_toggleMask(Y8960OPLL *opll, uint32_t mask) {
  uint32_t ret;

  if (opll) {
    ret = opll->mask;
    opll->mask ^= mask;
    return ret;
  } else
    return 0;
}

/* blueMSX addition: see header comment.  Re-link the slot pointers that
** a verbatim memcpy of Y8960OPLL would otherwise leave dangling. */
void Y8960OPLL_relinkAfterRestore(Y8960OPLL *opll) {
  int ch, i;
  if (opll == NULL) return;
  for (ch = 0; ch < 9; ch++) {
    int num = opll->patch_number[ch];
    Y8960OPLL_PATCH *base;
    if (num < 0 || num >= 19) num = 0;
    if (opll->patch_bank[ch] >= Y8960OPLL_BANK_COUNT) opll->patch_bank[ch] = 0;
    base = (num == 0) ? opll->user_patch : opll->patch[opll->patch_bank[ch]] + num * 2;
    MOD(opll, ch)->patch = &base[0];
    CAR(opll, ch)->patch = &base[1];
  }
  for (i = 0; i < 18; i++) {
    uint32_t ws = opll->slot[i].patch ? (opll->slot[i].patch->WS & 1) : 0;
    opll->slot[i].wave_table = wave_table_map[ws];
  }
}
