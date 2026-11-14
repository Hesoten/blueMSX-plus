/*
**
** File: ym2151.h - header file for software implementation of YM2151
**                                            FM Operator Type-M(OPM)
**
** (c) 1997-2002 Jarek Burczynski (s0246@poczta.onet.pl, bujar@mame.net)
** Some of the optimizing ideas by Tatsuyuki Satoh
**
** Version 2.150 final beta May, 11th 2002
**
**
** I would like to thank following people for making this project possible:
**
** Beauty Planets - for making a lot of real YM2151 samples and providing
** additional informations about the chip. Also for the time spent making
** the samples and the speed of replying to my endless requests.
**
** Shigeharu Isoda - for general help, for taking time to scan his YM2151
** Japanese Manual first of all, and answering MANY of my questions.
**
** Nao - for giving me some info about YM2151 and pointing me to Shigeharu.
** Also for creating fmemu (which I still use to test the emulator).
**
** Aaron Giles and Chris Hardy - they made some samples of one of my favourite
** arcade games so I could compare it to my emulator.
**
** Bryan McPhail and Tim (powerjaw) - for making some samples.
**
** Ishmair - for the datasheet and motivation.
*/
/*
   Forked from blueMSX's MameYM2151.h for the Y8960 cartridge, 2026 by
   madscient. Renamed so that it links beside the original; see
   Y8960OpmCore.c.
*/

#ifndef Y8960_OPM_CORE_H
#define Y8960_OPM_CORE_H

#include "MsxTypes.h"

typedef struct Y8960OpmCore Y8960OpmCore;

Y8960OpmCore* Y8960OpmCoreCreate(void* ref, int clock, int rate);
void Y8960OpmCoreDestroy(Y8960OpmCore* chip);
void Y8960OpmCoreReset(Y8960OpmCore* chip);
void Y8960OpmCoreUpdateOne(Y8960OpmCore* chip,Int16* bufL, Int16* bufR, int length);
void Y8960OpmCoreWriteReg(Y8960OpmCore* chip, int r, int v);
int  Y8960OpmCoreReadStatus(Y8960OpmCore* chip);
void Y8960OpmCoreTimerCallback(Y8960OpmCore* chip, int timer);
void Y8960OpmCoreLoadState(Y8960OpmCore* chip);
void Y8960OpmCoreSaveState(Y8960OpmCore* chip);
#endif /*Y8960_OPM_CORE_H*/
