/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Emulator/Emulator.h,v $
**
** $Revision: 1.12 $
**
** $Date: 2008-03-30 18:38:40 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2006 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
******************************************************************************
*/
#ifndef EMULATOR_H
#define EMULATOR_H

#include "MsxTypes.h"
#include "Properties.h"
#include "AudioMixer.h"

typedef enum { EMU_RUNNING, EMU_PAUSED, EMU_STOPPED, EMU_SUSPENDED, EMU_STEP, EMU_STEP_BACK } EmuState;

void emulatorInit(Properties* properties, Mixer* mixer);
void emulatorExit();
/* Once at startup, before any plugin loads: the lock every change of the run state takes. */
void emulatorCreateLocks(void);

/* Goes up on every change of the run state, never to 0; the run generation agents compare. */
UInt32 emulatorGetStateSeq(void);
/* The seq of the last change of the run state made on the calling thread. */
UInt32 emulatorOwnStateSeq(void);

void emuEnableSynchronousUpdate(int enable);

void emulatorSetFrequency(int logFrequency, int* frequency);
/* emulation.speed (0..100, log scale) -> Hz; clamps out-of-range INI values. */
int  emulatorLogFrequencyToHz(int logFrequency);
/* Percent of the stock clock (10..1000) -> emulation.speed. */
int  emulatorPercentToLogFrequency(int percent);
void emulatorRestartSound();
/* Waits for the emulation thread to park on a paused machine; zero when it did not in time,
** or the machine is no longer paused. */
int  emulatorWaitParked(int timeoutMs);

/* Emulated frames run since the machine started. */
UInt32 emulatorGetFramesRun(void);
/* Frames drawn since the last one skipped while minimized; 0xFFFFFFFF if none was. */
UInt32 emulatorGetFramesDrawnSinceMinimized(void);

void emulatorSuspend();
void emulatorResume();
/* While held, emulatorResume does nothing: whoever suspended the machine to
** ask a question keeps it suspended until the answer is acted on. */
void emulatorHoldSuspended(int hold);
void emulatorDoResume();
/* Suspend-yield helper invoked from archWaitForAckOrSuspend: parks the
** emu thread until emuState returns to EMU_RUNNING. Returns non-zero
** when the wait was broken by an exit request. */
int  emuWaitForResume(void);
void emulatorRestart();
void emulatorStart(const char* stateName);
void emulatorStop();
void emulatorSetMaxSpeed(int enable);
int  emulatorGetMaxSpeed();
void emulatorPlayReverse(int enable);
int  emulatorGetPlayReverse();
int emulatorGetCpuOverflow();
int emulatorGetSyncPeriod();
EmuState emulatorGetState();
/* Non-zero while a single step runs its one instruction. */
int emulatorIsSingleStepping(void);
void emulatorSetState(EmuState state);
/* A pause only from running and a resume only from paused, each in one step under the state
** lock, with the sound following; zero when the machine was not in that state. Neither
** notifies; a resume is refused while emulatorHoldSuspended holds. */
int  emulatorPauseIfRunning(void);
int  emulatorResumeIfPaused(void);
UInt32 emulatorGetCpuSpeed();
UInt32 emulatorGetCpuUsage();
void emulatorResetMixer();
int emulatorGetCurrentScreenMode();
void emulatorRefreshFromSource();

/* A hook for tools that copy emulator state. FRAME comes once per VDP frame,
** also on the UI thread (soft reset): check emulatorIsEmuThread(). IDLE comes
** every few ms while paused. STOPPED: emulatorStop has ended the thread. */
typedef enum { EMU_TAP_FRAME, EMU_TAP_IDLE, EMU_TAP_STOPPED } EmuTapPoint;
typedef void (*EmuTapFn)(EmuTapPoint point);
void emulatorSetTap(EmuTapFn fn);
int  emulatorIsEmuThread(void);

#endif

