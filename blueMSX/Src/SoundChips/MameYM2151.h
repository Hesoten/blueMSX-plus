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

#ifndef _H_YM2151_
#define _H_YM2151_

#include "MsxTypes.h"
#include "AudioMixer.h"
#include "DebugDeviceManager.h"

typedef struct MameYm2151 MameYm2151;

/* How the core reaches the unit it sits in; each call gets back Create's ref. */
typedef struct {
    void (*timerSet)(void* ref, int timer, int count);
    void (*timerStart)(void* ref, int timer, int start);
    void (*setIrq)(void* ref, int irq);
    void (*clearIrq)(void* ref, int irq);
    void (*writePort)(void* ref, UInt32 port, UInt8 value);
} YM2151Callbacks;

/* stateSection names the core's save state section; it must outlive the chip. */
MameYm2151* YM2151Create(void* ref, const YM2151Callbacks* callbacks,
                         const char* stateSection, int clock, int rate);
void YM2151Destroy(MameYm2151* chip);
void YM2151ResetChip(MameYm2151* chip);
void YM2151UpdateOne(MameYm2151* chip,Int16* bufL, Int16* bufR, int length);
/* Detailed mixer: lend the eight channels to the mixer channel handle, named
** 1-8; returns NULL when none is left. */
MixerVoices* YM2151AttachVoices(MameYm2151* chip, Mixer* mixer, Int32 handle, float scale);
/* Once per update, before the samples: read whether gains or levels are wanted. */
void YM2151SyncVoices(MameYm2151* chip);
void YM2151WriteReg(MameYm2151* chip, int r, int v);
int  YM2151ReadStatus(MameYm2151* chip);
void YM2151TimerCallback(MameYm2151* chip, int timer);
void YM2151LoadState(MameYm2151* chip);
void YM2151SaveState(MameYm2151* chip);
/* Adds the register bank: the last values written, 19h as AMD and PMD. */
void YM2151GetDebugInfo(MameYm2151* chip, DbgDevice* dbgDevice);
#endif /*_H_YM2151_*/
