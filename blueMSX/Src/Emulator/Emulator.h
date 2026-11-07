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
/* Goes up when a frame, screen-update, back or debugger step starts from a pause. */
UInt32 emulatorGetStepSeq(void);

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

/* Machine VDP frames run since the program started, never going back; frames a rewind
** replays are not counted. */
UInt32 emulatorGetFramesRun(void);
/* The machine frame number: VDP frames since power-on, part of the machine state, so it
** goes back with a rewind or a state load. Any thread; current as of the last frame start. */
UInt32 emulatorGetMachineFrame(void);
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
/* The first max speed (reverse 0) or reverse play hold begins: from a pause, or a frame step
** under way, the machine runs in that mode while held. Neither stopped nor suspended. */
void emulatorHoldBegin(int reverse);
/* The last hold ends and clears its mode; begun from a pause, max speed pauses at the next end
** of active display, reverse play at the frame it shows. */
void emulatorHoldEnd(int reverse);
/* Non-zero while a hold begun from a pause runs, or its rewind runs on to the frame to pause at. */
int  emulatorHoldFromPause(void);
/* Non-zero on the emulation thread while reverse play runs a snapshot on to a frame: the
** frames it passes are not flipped, counted or tapped. */
int  emulatorRewindReplaying(void);
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
/* Running, the machine pauses at the end of the current shown frame; paused, it runs one frame
** and pauses there, with the sound kept off. Anything else does nothing. */
void emulatorRequestFrameStop(void);
/* Non-zero from a frame stop request until the stop; another pause, a stop or a rewind
** clears it, a suspend does not. */
int  emulatorFrameStopPending(void);
/* As emulatorRequestFrameStop, but on to the first shown frame that differs
** from the picture shown, giving up after a few seconds; again while it waits, or a frame stop
** request, stops it at the next frame end. */
void emulatorRequestChangeStop(void);
int  emulatorChangeStepArmed(void);
/* Non-zero when the last step waiting for a change stopped without seeing one, until
** the machine runs again; the limit is in machine VDP frames. */
int  emulatorChangeStepGaveUp(void);
int  emulatorChangeStepLimit(void);
/* Paused, rewind on, once parked (picture 0: or no replay under way): replays the frame before
** the one shown (picture 0) or the last of the picture before it, within the change step limit.
** Again while one runs it stops at the next frame shown, except a lead replay or rewind to a frame. */
void emulatorRequestBackStep(int picture);
/* UI thread, paused: replays one snapshot to the display end at vintTime (board time, low 32 bits),
** shows it and pauses, as a step back does. 0, changing nothing, when not before the frame shown,
** not within the snapshots, or another chip paces the screen; 1 when started. */
int  emulatorRewindToVint(UInt32 vintTime);
/* While paused: emulatorRewindToVint(vintTime) would start now, by the same checks but
** without waiting for the thread to park; 0 until it has. */
int  emulatorCanRewindToVint(UInt32 vintTime);
/* 1 while a step back one frame runs, 2 while one back to another picture runs (its replay for
** the lead too), 3 while emulatorRewindToVint runs, else 0. */
int  emulatorBackStepPending(void);
/* Why the last step back gave up, until the machine runs again: 1 nothing older to stop at,
** 2 no other picture within emulatorChangeStepLimit frames; 0 when it did not. */
int  emulatorBackStepGaveUp(void);
/* The user's pause during a step back: it stops at the next frame it shows rather than
** mid-replay. Zero, doing nothing, when no step back runs. */
int  emulatorBackStepStopSoon(void);
/* Called by a source that paces the screen at the end of each frame it shows, after its
** flip. A frame step ends there; calls off the emulation thread are ignored. */
void emulatorShownFrameEnded(void);
/* Called by a VDP at each of its ends of active display; ready: show(ref) can complete and
** flip the frame, shown: it is on screen. Ending a frame step or a reverse play step there,
** it shows and presents the frame and returns 1; else returns 0. */
int  emulatorShownDisplayEnded(int ready, int shown, void (*show)(void* ref), void* ref);
UInt32 emulatorGetCpuSpeed();
UInt32 emulatorGetCpuUsage();
void emulatorResetMixer();
int emulatorGetCurrentScreenMode();
void emulatorRefreshFromSource();

/* A hook for tools that copy emulator state. FRAME comes once per VDP frame,
** also on the UI thread (soft reset): check emulatorIsEmuThread(). IDLE comes every few
** ms while paused, and once per reverse play step. STOPPED: emulatorStop ended the thread. */
typedef enum { EMU_TAP_FRAME, EMU_TAP_IDLE, EMU_TAP_STOPPED, EMU_TAP_BARRIER, EMU_TAP_V9990_SHOWN,
               EMU_TAP_VDP_SHOWN,
               EMU_TAP_REPLAY, /* between slices of a reverse play replay: drain logs, copy nothing */
               EMU_TAP_VDP_SECONDARY_SHOWN
             } EmuTapPoint;
typedef void (*EmuTapFn)(EmuTapPoint point);
void emulatorSetTap(EmuTapFn fn);
/* Asked on the emu thread, outside the suspend barrier, before a reverse play step to the display
** end at or after stopAt (board time, low 32 bits) loads: when to start, stopAt or later for none.
** Never hold a lock the hook takes while waiting for the emulation thread. */
typedef UInt32 (*EmuRewindLeadFn)(UInt32 stopAt);
void emulatorSetRewindLead(EmuRewindLeadFn fn);
int  emulatorIsEmuThread(void);
/* Calls the hook. On the emulator thread, the V9990 calls it with V9990_SHOWN once it has drawn
** a frame's last displayed line, a VDP with VDP_SHOWN (on a cartridge VDP_SECONDARY_SHOWN) at
** the end of its active display, inside their own access or timer: the hook must not sync them. */
void emulatorTap(EmuTapPoint point);

/* Waits for a capture running on the emulator thread to end. Call it after
** unpublishing a chip and before freeing it, never while the tap's lock is
** held by this thread. Does nothing on the emulator thread. */
void emulatorTapBarrier(void);

#endif

