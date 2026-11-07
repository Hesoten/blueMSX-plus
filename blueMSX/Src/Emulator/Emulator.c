/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Emulator/Emulator.c,v $
**
** $Revision: 1.67 $
**
** $Date: 2009-07-18 14:35:59 $
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
#include "Emulator.h"
#include "MsxTypes.h"
#include "Debugger.h"
#include "Board.h"
#include "Disk.h"
#include "FileHistory.h"
#include "Switches.h"
#include "Led.h"
#include "Machine.h"
#include "InputEvent.h"
#include "Actions.h"

#include "ArchThread.h"
#include "ArchEvent.h"
#include "ArchTimer.h"
#include "ArchSound.h"
#include "ArchMidi.h"

#include "JoystickPort.h"
#include "ArchInput.h"
#include "ArchDialog.h"
#include "ArchNotifications.h"
#include "YM2413.h"
#include "VideoManager.h"
#include "FrameBuffer.h"
#include "VDP.h"
#include "RaceTest.h"
#include <math.h>
#include <string.h>

static int WaitForSync(int maxSpeed, int stopCause);

#ifdef WII
#define EMU_THREAD_LOCAL
#elif defined(_MSC_VER)
#define EMU_THREAD_LOCAL __declspec(thread)
#else
#define EMU_THREAD_LOCAL __thread
#endif

static EMU_THREAD_LOCAL int emuOnThisThread;
static volatile EmuTapFn emuTap;
static volatile EmuRewindLeadFn emuRewindLead;

/* Reverse play, on the emulation thread: 0 idle, 1 running a snapshot on to the frame
** before the one shown, 2 arrived. Board times: rwShownVint the end of active display of
** the frame on screen, emuLastVint the latest one run, rwLo/rwBudgetEnd the replay's window. */
static int    rwPhase;
static UInt32 rwShownVint;
static UInt32 emuLastVint;
static UInt32 rwLo;
static UInt32 rwBudgetEnd;
static UInt32 rwDue;          /* host time the next step is due, in ms times the refresh rate */
/* A replay was cut short, so the screen still shows the frame at rwCutVint, until one is shown. */
static int    rwCut;
static UInt32 rwCutVint;

void emulatorSetTap(EmuTapFn fn) {
    emuTap = fn;
}

void emulatorSetRewindLead(EmuRewindLeadFn fn) {
    emuRewindLead = fn;
}

int emulatorIsEmuThread(void) {
    return emuOnThisThread;
}

int emulatorRewindReplaying(void) {
    return rwPhase == 1 && emuOnThisThread;
}

void emulatorTap(EmuTapPoint point) {
    EmuTapFn tap = emuTap;
    /* Of a replay only the frame it stops at is copied. */
    if ((point == EMU_TAP_VDP_SHOWN || point == EMU_TAP_V9990_SHOWN) && emulatorRewindReplaying() &&
        (Int32)(boardSystemTime() - rwLo) < 0) {
        return;
    }
    if (tap) {
        tap(point);
    }
}

void emulatorTapBarrier(void) {
    EmuTapFn tap = emuTap;
    if (tap && !emuOnThisThread) {
        tap(EMU_TAP_BARRIER);
    }
}

/* Paused only: running, these waits sit mid-frame; suspended, the UI thread may be
** changing the machine. A debugger or agent may still write VRAM or VDP registers
** while paused, so a copy can be torn; the first after the writes stop is whole. */
#define EMU_TAP_IF_IDLE() \
    do { EmuTapFn tap = emuTap; if (tap && emuState == EMU_PAUSED) tap(EMU_TAP_IDLE); } while (0)

static void*  emuThread;
#ifndef WII
static void*  emuSyncEvent;
#endif
static void*  emuStartEvent;
#ifndef WII
static void*  emuTimer;
#endif
static volatile int emuExitFlag;
static UInt32 emuSysTime = 0;
static UInt32 emuFrequency = 3579545;
int           emuMaxSpeed = 0;
volatile int  emuPlayReverse = 0;
int           emuMaxEmuSpeed = 0; // Max speed issued by emulation

/* Fast-forward multiplier (max-speed and FDC/HDD boost); higher =
** faster, capped by host emu throughput.  Stock blueMSX used 10. */
#define EMU_MAXSPEED_FACTOR 15

/* Emulated ms handed out per iteration during a tape boost. Not a speed
** limit: that path does not queue for a sync tick, so the host's throughput
** sets the rate and this only decides how often the loop comes up for air
** (suspend latency, input polling, per-iteration overhead). */
#define EMU_TAPE_SLICE_MS   60

/* What a rewind does once no snapshot is left: 1 ends it and lets play
** resume, 0 stays on the oldest frame until the key is let go. */
#define EMU_REVERSE_STOP_AT_RING_END 1
static char   emuStateName[512];
static volatile int      emuSuspendFlag;
static volatile EmuState emuState = EMU_STOPPED;
static volatile int      emuSingleStep = 0;
/* A leaf lock, held only while emuState is written; under it only the debugger's state is set,
** which takes no lock. */
static void*             emuStateLock;
/* Goes up on every write of emuState, never to 0, so one transition is told from a later one. */
static volatile UInt32   emuStateSeq = 1;
/* Goes up when a step leaves a pause, so a view can light only what the step changed. */
static volatile UInt32   emuStepSeq = 1;
/* The seq of the last transition this thread made, for the notify that follows it. */
static ARCH_THREAD_LOCAL UInt32 emuOwnStateSeq;
/* Set while a question about the media is open: nothing may resume the machine meanwhile. */
static int               emuHoldSuspended;
/* A stop asked for at the next frame end, and that frame ended; both written under
** emuStateLock and cleared by a pause or a stop, not by a suspend. */
static volatile int      frameStopReq;
static volatile int      frameStopNow;
/* Machine VDP frames since the request while a pacing source is shown, under
** emuStateLock; after a few the step ends anyway. */
static int               frameStopWait;
/* Set while a step runs on to the first shown frame that differs; machine VDP frames it
** has run, and whether the last such step gave up. All under emuStateLock. */
#define CHANGE_STEP_FRAMES 300
static volatile int      frameStopChange;
static int               changeFrames;
static volatile int      changeGaveUp;
/* A max speed or reverse play hold began from a pause, and its end asks reverse play to
** pause at the frame it shows; under emuStateLock, cleared by a pause or a stop. */
static volatile int      holdFromPause;
static volatile int      rwPauseReq;
/* A step back from a pause: 1 one frame, 2 on to another picture; the frames it went back,
** why the last one gave up (1 nothing older to stop at, 2 no other picture within the limit),
** that a replay of it arrived, and that it starts from a replay cut short. */
static volatile int      backStep;
static int               backFrames;
static volatile int      backGaveUp;
static int               backArrived;
static int               backFromCut;
static Properties* properties;
static Mixer* mixer;
static BoardDeviceInfo deviceInfo;
static Machine* machine;
static int lastScreenMode;

static int emuFrameskipCounter = 0;

static UInt32 emuTimeIdle       = 0;
static UInt32 emuTimeTotal      = 1;
static UInt32 emuTimeOverflow   = 0;
static UInt32 emuUsageCurrent   = 0;
static UInt32 emuCpuSpeed       = 0;
static UInt32 emuCpuUsage       = 0;
static int    enableSynchronousUpdate = 1;

#if 0

#define LOG_SIZE (10 * 1000000)
UInt32 logentry[LOG_SIZE];

int logindex;
int logwrapped;

void dolog(int slot, int sslot, int wr, UInt16 addr, UInt8 val)
{
    logentry[logindex++] = (slot << 26) | (sslot << 24) | ((UInt32)val << 16) | addr | (wr ? (1 << 31) : 0);
    if (logindex == LOG_SIZE) {
        logindex = 0;
        logwrapped++;
    }
}

void clearlog()
{
    logwrapped = 0;
    logindex = 0;
}

void savelog()
{
    int totalSize = LOG_SIZE;
    int lastPct = -1;
    int cnt = 0;
    FILE * f = fopen("bluemsxlog.txt", "w+");
    int i = 0;
    if (logwrapped == 0 && logindex == 0) {
        return;
    }

    if (logwrapped) {
        i = logindex;
    }
    else {
        totalSize = logindex;
    }

    printf("Saving log for slot 1\n");

    do {
        UInt32 v = logentry[i];
        int newPct = ++cnt * 100 / totalSize;
        char rw = (v >> 31) ? 'W' : 'R';

        if (newPct != lastPct) {
            printf("\r%d%%",newPct);
            lastPct = newPct;
        }
        fprintf(f, "%c(%d:%d) %.4x: %.2x\n", rw, (v>>26)&3, (v>>24)&3,v & 0xffff, (v >> 16) & 0xff);

        if (++i == LOG_SIZE) {
            i = 0;
        }
    } while (i != logindex);
    printf("\n");
    fclose(f);
}
#else
#define clearlog()
#define savelog()
#endif

static void emuCalcCpuUsage() {
    static UInt32 oldSysTime = 0;
    static UInt32 oldAverage = 0;
    static UInt32 cnt = 0;
    UInt32 newSysTime;
    UInt32 emuTimeAverage;

    if (emuTimeTotal < 10) {
        return;
    }
    newSysTime = archGetSystemUpTime(1000);
    emuTimeAverage = 100 * (emuTimeTotal - emuTimeIdle) / (emuTimeTotal / 10);

    emuTimeOverflow = emuTimeAverage > 940;

    if ((cnt++ & 0x1f) == 0 && newSysTime != oldSysTime) {
        UInt32 usageAverage = emuUsageCurrent * 100 / (newSysTime - oldSysTime) * emuFrequency / 3579545;
        if (usageAverage > 98 && usageAverage < 102) {
            usageAverage = 100;
        }

        if (usageAverage >= 10000) {
            usageAverage = 0;
        }

        emuCpuSpeed = usageAverage;
        emuCpuUsage = emuTimeAverage;
    }

    oldSysTime      = newSysTime;
    emuUsageCurrent = 0;
    emuTimeIdle     = 0;
    emuTimeTotal    = 1;
}

static int emuUseSynchronousUpdate()
{
    if (properties->emulation.syncMethod == P_EMU_SYNCIGNORE) {
        return properties->emulation.syncMethod;
    }

    if (properties->emulation.speed == 50 &&
        enableSynchronousUpdate &&
        emulatorGetMaxSpeed() == 0 &&
        !boardGetFdcActive() &&
        !boardGetCasActive())
    {
        return properties->emulation.syncMethod;
    }
    /* During boost / max-speed, fall back to async AUTO so the emu
    ** thread doesn't block on per-frame present-ack. */
    return P_EMU_SYNCAUTO;
}


UInt32 emulatorGetCpuSpeed() {
    return emuCpuSpeed;
}

UInt32 emulatorGetCpuUsage() {
    return emuCpuUsage;
}

void emuEnableSynchronousUpdate(int enable)
{
    enableSynchronousUpdate = enable;
}

void emulatorInit(Properties* theProperties, Mixer* theMixer)
{
    properties = theProperties;
    mixer      = theMixer;
}

void emulatorExit()
{
    properties = NULL;
    mixer      = NULL;
}


EmuState emulatorGetState() {
    return emuState;
}

void emulatorCreateLocks(void)
{
    if (emuStateLock == NULL) {
        emuStateLock = archSemaphoreCreate(1);
    }
}

/* With emuStateLock held: a step waiting for a change becomes a plain frame step. */
static void disarmChange(void)
{
    if (frameStopChange) {
        frameStopChange = 0;
        frameBufferWatchShown(0, 0);
    }
}

/* With emuStateLock held: a step back ends, with its watch for another picture. */
static void endBackStep(void)
{
    if (backStep == 2) {
        frameBufferWatchShown(0, 0);
    }
    backStep = 0;
}

/* The one place emuState is written, with emuStateLock held; with setsDebugger the
** debugger's state follows in the same step. */
static void writeHeld(EmuState state, int setsDebugger)
{
    /* Before the state: the woken thread must not draw a step's frame under the old count. */
    if (emuState == EMU_PAUSED && state == EMU_RUNNING && (frameStopReq || emuSingleStep || backStep)) {
        emuStepSeq++;
    }
    emuState = state;
    emuStateSeq = emuStateSeq + 1 != 0 ? emuStateSeq + 1 : 1;
    emuOwnStateSeq = emuStateSeq;
    /* A suspend (menu, window move, mode switch) keeps a frame stop: the machine
    ** runs on to it after the resume rather than running free. */
    if (state != EMU_RUNNING && state != EMU_SUSPENDED) {
        frameStopReq = 0;
        frameStopNow = 0;
        frameStopWait = 0;
        disarmChange();
        holdFromPause = 0;
        rwPauseReq = 0;
        endBackStep();
    }
    else if (state == EMU_RUNNING) {
        changeGaveUp = 0;
        backGaveUp = 0;
    }
    if (setsDebugger) {
        dbgSetRunState(state == EMU_PAUSED ? DBG_PAUSED : DBG_RUNNING);
    }
}

/* setsDebugger for a pause or a resume: a notify that comes late cannot leave the debugger's
** state stale then. */
static void writeState(EmuState state, int setsDebugger)
{
    archSemaphoreWait(emuStateLock, -1);
    writeHeld(state, setsDebugger);
    archSemaphoreSignal(emuStateLock);
}

/* A change only from the state named, decided and made in one locked step; zero when the
** machine was not in from. Nothing goes to running while a suspend is held. */
static int switchState(EmuState from, EmuState to, int setsDebugger)
{
    int switched;

    archSemaphoreWait(emuStateLock, -1);
    switched = emuState == from && !(to == EMU_RUNNING && emuHoldSuspended);
    if (switched) {
        writeHeld(to, setsDebugger);
    }
    archSemaphoreSignal(emuStateLock);
    return switched;
}

/* Pauses for a frame stop that is still due, checked and made in one locked step so
** a pause and resume in between cannot leave a stale stop behind. */
static int takeFrameStop(void)
{
    int taken;

    archSemaphoreWait(emuStateLock, -1);
    taken = frameStopNow && emuState == EMU_RUNNING;
    if (taken) {
        writeHeld(EMU_PAUSED, 1);
    }
    archSemaphoreSignal(emuStateLock);
    return taken;
}

int emulatorBackStepStopSoon(void)
{
    int taken;

    archSemaphoreWait(emuStateLock, -1);
    taken = backStep && (emuState == EMU_RUNNING || emuState == EMU_SUSPENDED);
    if (taken && backStep == 2) {
        endBackStep();
        backStep = 1;
    }
    archSemaphoreSignal(emuStateLock);
    return taken;
}

int emulatorPauseIfRunning(void)
{
    if (!switchState(EMU_RUNNING, EMU_PAUSED, 1)) {
        return 0;
    }
    archSoundSuspend();
    archMidiEnable(0);
    return 1;
}

int emulatorResumeIfPaused(void)
{
    /* Leaving a pause, as in emulatorSetState. */
    dbgChangeBegin();
    if (!switchState(EMU_PAUSED, EMU_RUNNING, 1)) {
        return 0;
    }
    archSoundResume();
    archMidiEnable(1);
    return 1;
}

void emulatorRequestFrameStop(void)
{
    int resumed = 0;

    dbgChangeBegin();
    archSemaphoreWait(emuStateLock, -1);
    if (backStep) {
        /* A step back under way ends as it is. */
    }
    else if (frameStopChange) {
        disarmChange();
    }
    else if (emuState == EMU_RUNNING) {
        frameStopReq = 1;
        frameStopWait = 0;
    }
    else if (emuState == EMU_PAUSED && !emuHoldSuspended) {
        frameStopReq = 1;
        frameStopWait = 0;
        writeHeld(EMU_RUNNING, 1);
        resumed = 1;
    }
    archSemaphoreSignal(emuStateLock);

    /* Sound stays off: one frame at a time would only stutter. */
    if (resumed) {
        debuggerNotifyEmulatorResume();
    }
}

void emulatorRequestChangeStop(void)
{
    int resumed = 0;
    int fromShown = -1;

    dbgChangeBegin();
    archSemaphoreWait(emuStateLock, -1);
    if (frameStopChange) {
        disarmChange();
    }
    else if (frameStopReq || frameStopNow || backStep) {
        /* A plain frame step or a step back is under way; it ends as it is. */
    }
    else if (emuState == EMU_RUNNING) {
        fromShown = 0;
    }
    else if (emuState == EMU_PAUSED && !emuHoldSuspended) {
        /* Parked, the picture on screen is still and is the reference; paused a moment ago,
        ** the thread may still draw, so it takes the next frame it completes, as when running. */
        fromShown = emuSuspendFlag ? 1 : 0;
    }
    if (fromShown >= 0) {
        frameStopChange = 1;
        changeFrames = 0;
        changeGaveUp = 0;
        frameBufferWatchShown(1, fromShown);
        frameStopReq = 1;
        frameStopWait = 0;
        if (emuState == EMU_PAUSED) {
            writeHeld(EMU_RUNNING, 1);
            resumed = 1;
        }
    }
    archSemaphoreSignal(emuStateLock);

    if (resumed) {
        debuggerNotifyEmulatorResume();
    }
}

void emulatorRequestBackStep(int picture)
{
    int resumed = 0;

    dbgChangeBegin();
    archSemaphoreWait(emuStateLock, -1);
    if (backStep) {
        /* Again while it runs: it stops at the next frame it shows. */
        endBackStep();
        backStep = 1;
    }
    else if (emuState == EMU_PAUSED && !emuHoldSuspended && !emuPlayReverse &&
             properties->emulation.reverseEnable) {
        if (picture) {
            /* Paused, the picture on screen is still and is the reference. */
            frameBufferWatchShown(1, 1);
        }
        backFromCut = rwCut;
        backArrived = 0;
        backFrames = 0;
        backGaveUp = 0;
        /* Set before the state, so the thread woken never runs on forward. */
        backStep = picture ? 2 : 1;
        writeHeld(EMU_RUNNING, 1);
        resumed = 1;
    }
    archSemaphoreSignal(emuStateLock);

    /* Sound stays off: frames replayed would only be heard again. */
    if (resumed) {
        debuggerNotifyEmulatorResume();
    }
}

int emulatorBackStepPending(void)
{
    return backStep;
}

int emulatorBackStepGaveUp(void)
{
    return backGaveUp;
}

int emulatorFrameStopPending(void)
{
    return frameStopReq || frameStopNow;
}

int emulatorHoldFromPause(void)
{
    return holdFromPause || rwPauseReq;
}

int emulatorChangeStepArmed(void)
{
    return frameStopChange;
}

int emulatorChangeStepGaveUp(void)
{
    return changeGaveUp;
}

int emulatorChangeStepLimit(void)
{
    return CHANGE_STEP_FRAMES;
}

UInt32 emulatorGetStateSeq(void)
{
    return emuStateSeq;
}

UInt32 emulatorGetStepSeq(void)
{
    return emuStepSeq;
}

UInt32 emulatorOwnStateSeq(void)
{
    return emuOwnStateSeq;
}

int emulatorIsSingleStepping(void) {
    return emuSingleStep;
}

void emulatorSetState(EmuState state) {
    int pauseOrResume = state == EMU_PAUSED || state == EMU_RUNNING;

    /* Leaving a pause: an agent may be reading the parked machine. */
    if (emuState == EMU_PAUSED && state != EMU_PAUSED) {
        dbgChangeBegin();
    }
    if (state == EMU_RUNNING) {
        archSoundResume();
        archMidiEnable(1);
    }
    else {
        archSoundSuspend();
        archMidiEnable(0);
    }
    if (state == EMU_STEP) {
        state = EMU_RUNNING;
        emuSingleStep = 1;
    }
    if (state == EMU_STEP_BACK) {
        /* Fall back to the state we came from when there is nothing to rewind
        ** to. EMU_STEP_BACK is a request, not a state the rest of the code
        ** knows how to leave: emuState would never reach EMU_PAUSED again. */
        EmuState oldState = emuState;
        state = EMU_RUNNING;
        /* A step too, counted before the restore so a view never sees it under the old count. */
        if (oldState == EMU_PAUSED) {
            emuStepSeq++;
        }
        if (!boardRewindOne()) {
            state = oldState;
        }
        
    }
    writeState(state, pauseOrResume);
}


int emulatorGetSyncPeriod() {
#ifdef NO_HIRES_TIMERS
    return 10;
#else
    return properties->emulation.syncMethod == P_EMU_SYNCAUTO ||
           properties->emulation.syncMethod == P_EMU_SYNCNONE ? 2 : 1;
#endif
}

#ifndef WII
static int timerCallback(void* timer) {
    if (properties == NULL) {
        return 1;
    }
    else {
        static UInt32 frameCount = 0;
        static UInt32 oldSysTime = 0;
        static UInt32 refreshRate = 50;
        UInt32 framePeriod = (properties->video.frameSkip + 1) * 1000;
        UInt32 syncPeriod = emulatorGetSyncPeriod();
        UInt32 sysTime = archGetSystemUpTime(1000);
        UInt32 diffTime = sysTime - oldSysTime;
        int syncMethod = emuUseSynchronousUpdate();

        if (diffTime == 0) {
            return 0;
        }

        oldSysTime = sysTime;

        // Update display
        frameCount += refreshRate * diffTime;
        if (frameCount >= framePeriod) {
            frameCount %= framePeriod;
            if (emuState == EMU_RUNNING) {
                int rate = boardGetRefreshRate();
                if (rate > 0) {
                    refreshRate = rate;
                }

                /* Reverse play leaves WaitForSync at its head, before the present the
                ** vblank mode relies on; a frame step back presents itself, and this
                ** keeps the screen up for the steps of the old kind. */
                if (syncMethod == P_EMU_SYNCAUTO || syncMethod == P_EMU_SYNCNONE ||
                    (syncMethod == P_EMU_SYNCTOVBLANK && emuPlayReverse &&
                     properties->emulation.reverseEnable)) {
                    archUpdateEmuDisplay(0);
                }
            }
        }

        if (syncMethod == P_EMU_SYNCTOVBLANKASYNC) {
            archUpdateEmuDisplay(syncMethod);
        }

        // Update emulation
        archEventSet(emuSyncEvent);
    }

    return 1;
}
#endif

static void getDeviceInfo(BoardDeviceInfo* deviceInfo)
{
    int i;

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        strcpy(properties->media.carts[i].fileName, deviceInfo->carts[i].name);
        strcpy(properties->media.carts[i].fileNameInZip, deviceInfo->carts[i].inZipName);
        /* Keep type in sync with fileName so .sta restore + re-insert
        ** doesn't feed a stale type into updateFileHistory. */
        properties->media.carts[i].type = deviceInfo->carts[i].type;
        updateExtendedRomName(i, properties->media.carts[i].fileName, properties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        strcpy(properties->media.disks[i].fileName, deviceInfo->disks[i].name);
        strcpy(properties->media.disks[i].fileNameInZip, deviceInfo->disks[i].inZipName);
        updateExtendedDiskName(i, properties->media.disks[i].fileName, properties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        strcpy(properties->media.tapes[i].fileName, deviceInfo->tapes[i].name);
        strcpy(properties->media.tapes[i].fileNameInZip, deviceInfo->tapes[i].inZipName);
        updateExtendedCasName(i, properties->media.tapes[i].fileName, properties->media.tapes[i].fileNameInZip);
    }

    properties->emulation.vdpSyncMode      = deviceInfo->video.vdpSyncMode;

}

static void setDeviceInfo(BoardDeviceInfo* deviceInfo)
{
    int i;

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        deviceInfo->carts[i].inserted =  (int)strlen(properties->media.carts[i].fileName);
        deviceInfo->carts[i].type = properties->media.carts[i].type;
        strcpy(deviceInfo->carts[i].name, properties->media.carts[i].fileName);
        strcpy(deviceInfo->carts[i].inZipName, properties->media.carts[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        deviceInfo->disks[i].inserted =  (int)strlen(properties->media.disks[i].fileName);
        strcpy(deviceInfo->disks[i].name, properties->media.disks[i].fileName);
        diskSetDirFormat(i, properties->media.disks[i].dirFormat);
        strcpy(deviceInfo->disks[i].inZipName, properties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        deviceInfo->tapes[i].inserted =  (int)strlen(properties->media.tapes[i].fileName);
        strcpy(deviceInfo->tapes[i].name, properties->media.tapes[i].fileName);
        strcpy(deviceInfo->tapes[i].inZipName, properties->media.tapes[i].fileNameInZip);
    }

    deviceInfo->video.vdpSyncMode = properties->emulation.vdpSyncMode;
}

static int emulationStartFailure = 0;

static void emulatorPauseCb(void)
{
    emulatorSetState(EMU_PAUSED);
    debuggerNotifyEmulatorPause();
}

static void emulatorThread() {
    int frequency;
    int success = 0;
    int reversePeriod = 0;
    int reverseBufferCnt = 0;
    void* mmcssHandle;

    emuOnThisThread = 1;
    emulatorSetFrequency(properties->emulation.speed, &frequency);

    switchSetFront(properties->emulation.frontSwitch);
    switchSetPause(properties->emulation.pauseSwitch);
    switchSetAudio(properties->emulation.audioSwitch);

    if (properties->emulation.reverseEnable && properties->emulation.reverseMaxTime > 0) {
        reversePeriod = 50;
        reverseBufferCnt = properties->emulation.reverseMaxTime * 1000 / reversePeriod;
    }

    mmcssHandle = archThreadBeginEmulationProfile(properties->emulation.priorityBoost);

    success = boardRun(machine,
                       &deviceInfo,
                       mixer,
                       *emuStateName ? emuStateName : NULL,
                       frequency, 
                       reversePeriod,
                       reverseBufferCnt,
                       WaitForSync);

    archThreadEndEmulationProfile(mmcssHandle);

    /* The recorder may have rendered on this thread; its buffers die with it. */
    frameBufferClearDeinterlace();

    ledSetAll(0);
    /* Already so when emulatorStop ended the thread; a second write would date its stop. */
    if (emuState != EMU_STOPPED) {
        writeState(EMU_STOPPED, 0);
    }

#ifndef WII
    archTimerDestroy(emuTimer);
#endif

    if (!success) {
        emulationStartFailure = 1;
    }

    archEventSet(emuStartEvent);
}
//extern int xxxx;

void emulatorStart(const char* stateName) {
    /* A start builds the machine an agent's hold would read. */
    dbgChangeBegin();
    /* stateName may point to extractToken()'s static argBuf via the
    ** /onearg -> tryLaunchUnknownFile path; machineCreate() below calls
    ** extractToken on machine config and clobbers that buffer, so copy
    ** the path into emuStateName up-front before any such call. */
    strcpy(emuStateName, stateName ? stateName : "");

    /* A state brings back what the MSX remembered of a disk, and that must
    ** not be written to a host medium. */
    if (emuStateName[0] != 0) {
        diskWithdrawHostMedia();
    }
    diskDirStart(emuStateName[0] != 0, emuState == EMU_STOPPED);
        dbgEnable();

    archEmulationStartNotification();
//xxxx = 0;
    emulatorResume();

    emuExitFlag = 0;

    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MOONSOUND, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_YAMAHA_SFG, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXAUDIO, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXMUSIC, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_SCC, 1);


    properties->emulation.pauseSwitch = 0;
    switchSetPause(properties->emulation.pauseSwitch);

    machine = machineCreate(properties->emulation.machineName);

    if (machine == NULL) {
        /* archShowStartEmuFailDialog shows the reason; a follow-up
        ** archEmulationStartFailure would stack a second identical dialog. */
        archShowStartEmuFailDialog(properties->emulation.machineName);
        archEmulationStopNotification();
        writeState(EMU_STOPPED, 0);
        return;
    }

    boardSetMachine(machine);

#ifndef NO_TIMERS
#ifndef WII
    emuSyncEvent  = archEventCreate(0);
#endif
    emuStartEvent = archEventCreate(0);
#ifndef WII
    emuTimer = archCreateTimer(emulatorGetSyncPeriod(), timerCallback);
#endif
#endif

    setDeviceInfo(&deviceInfo);

    inputEventReset();

    archSoundResume();
    archMidiEnable(1);

    /* The emulation thread is not running: a reverse play step of the last machine ends. */
    rwPhase = 0;
    rwCut = 0;
    writeState(EMU_PAUSED, 0);
    emulationStartFailure = 0;

    clearlog();

#ifdef SINGLE_THREADED
    emuState = EMU_RUNNING;
    emulatorThread();

    if (emulationStartFailure) {
        archEmulationStopNotification();
        emuState = EMU_STOPPED;
        archEmulationStartFailure();
    }
#else
    emuThread = archThreadCreate(emulatorThread, THREAD_PRIO_HIGH);

    archEventWait(emuStartEvent, 3000);

    if (emulationStartFailure) {
        archEmulationStopNotification();
        writeState(EMU_STOPPED, 0);
        archEmulationStartFailure();
    }
    if (emuState != EMU_STOPPED) {
        getDeviceInfo(&deviceInfo);

        boardSetYm2413Oversampling(properties->sound.chip.ym2413Oversampling);
        boardSetY8950Oversampling(properties->sound.chip.y8950Oversampling);
        boardSetMoonsoundOversampling(properties->sound.chip.moonsoundOversampling);
        /* Push the OPLL analog filter cutoffs into the YM2413 global so the
        ** chip picks them up at create time and Hot-applied changes survive
        ** machine restarts.  Preset modes resolve to documented Hz values;
        ** Custom uses the explicit fields the user edited. */
        {
            int lpf = 0, hpf = 0;
            propertiesGetOpllFilterHz(properties->sound.chip.ym2413AnalogFilterMode,
                                      &properties->sound.chip, &lpf, &hpf);
            ym2413AnalogFilterSet(lpf, hpf);
        }

        strcpy(properties->emulation.machineName, machine->name);

        debuggerNotifyEmulatorStart();

        writeState(EMU_RUNNING, 0);
    }
#endif
}

void emulatorStop() {
    /* The machine is about to be destroyed: no agent may be reading it. */
    dbgChangeBegin();
    if (emuState == EMU_STOPPED) {
        return;
    }

    /* Stopped first, so the stop posted below carries the seq of this transition. */
    writeState(EMU_STOPPED, 0);

    debuggerNotifyEmulatorStop();

    /* Same modal-loop-aware wake as emulatorSuspend: if the emu thread
    ** is parked in archWaitForAckOrSuspend, signal it so it observes
    ** the new emuState without running out the wrapper's safety timeout. */
    archEmuSuspendSignal();
    while (!emuSuspendFlag) {
        archThreadSleep(1);
    }

    emuExitFlag = 1;
#ifndef WII
    archEventSet(emuSyncEvent);
#endif
    archSoundSuspend();
    archThreadJoin(emuThread, 3000);
    /* emu thread's boardRun exit ran boardCaptureDestroy -> boardCaptureStop;
    ** surface the record-end toast the menu Stop path already shows. */
    actionReplayFlushCompletionToast();
    archMidiEnable(0);
    machineDestroy(machine);
    archThreadDestroy(emuThread);
    {
        EmuTapFn tap = emuTap;
        if (tap) {
            tap(EMU_TAP_STOPPED);
        }
    }
#ifndef WII
    archEventDestroy(emuSyncEvent);
#endif
    archEventDestroy(emuStartEvent);

    // Reset active indicators in mixer
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MOONSOUND, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_YAMAHA_SFG, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXAUDIO, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXMUSIC, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_SCC, 1);

    archEmulationStopNotification();

    dbgDisable();
    dbgPrint();
    savelog();
}



/* emulation.speed is a log scale: 0 = 10%, 50 = 3.579MHz (100%), 100 =
   1000%.  Shared so the property page and theme tooltips cannot drift
   from the clock the board is actually given. */
int emulatorLogFrequencyToHz(int logFrequency) {
    if (logFrequency < 0)   logFrequency = 0;
    if (logFrequency > 100) logFrequency = 100;
    return (int)(3579545 * pow(2.0, (logFrequency - 50) / 15.0515));
}

/* Percent of the stock clock, clamped to the 10 to 1000 the property accepts. */
int emulatorPercentToLogFrequency(int percent) {
    if (percent < 10)   percent = 10;
    if (percent > 1000) percent = 1000;
    return (int)(50.0 + 15.0515 * log((double)percent / 100.0) / log(2.0) + 0.5);
}

void emulatorSetFrequency(int logFrequency, int* frequency) {
    /* Max speed counts from 100%: a slower speed set while it lasts waits for its end. */
    int normal = emulatorPercentToLogFrequency(100);
    if (emuMaxSpeed && logFrequency < normal) {
        logFrequency = normal;
    }
    emuFrequency = emulatorLogFrequencyToHz(logFrequency);

    if (frequency != NULL) {
        *frequency  = emuFrequency;
    }

    boardSetFrequency(emuFrequency);
}

/* A pauser writes the state under the lock, a full fence, before it reads the flag; this
** clears the flag, fences and reads the state, so one of the two sees the other. Non-zero,
** with the flag set again, when the thread is to stay parked. */
static int stayParked(void)
{
    emuSuspendFlag = 0;
    archMemoryBarrier();
    if (emuExitFlag || emuState == EMU_RUNNING) {
        return 0;
    }
    emuSuspendFlag = 1;
    return 1;
}

int emulatorWaitParked(int timeoutMs)
{
    UInt32 start = archGetSystemUpTime(1000);

    while (!emuSuspendFlag) {
        if (emuState != EMU_PAUSED || (int)(archGetSystemUpTime(1000) - start) >= timeoutMs) {
            return 0;
        }
        archThreadSleep(1);
    }
    return emuState == EMU_PAUSED;
}

void emulatorSuspend() {
    /* Whatever the state: what follows a suspend often changes the machine. */
    dbgChangeBegin();
    if (emuState != EMU_RUNNING) {
        return;
    }
    RACE_TEST_POINT(RACE_SUSPEND_CHECK);
    /* Locked: a pause made since the look above stays, and nothing is suspended. */
    if (!switchState(EMU_RUNNING, EMU_SUSPENDED, 0)) {
        return;
    }
    /* Wake the emu thread if it is mid-wait inside
    ** archWaitForAckOrSuspend so it observes the suspend immediately
    ** instead of running out the wrapper's safety timeout. The hook
    ** is a no-op on platforms without modal-loop coupling. */
    archEmuSuspendSignal();
    while (!emuSuspendFlag) {
        archThreadSleep(1);
    }
    archSoundSuspend();
    archMidiEnable(0);
}

/* Cancel-cooperative wait used by archWaitForAckOrSuspend. Mirrors the
** suspend tail of WaitForSync (set emuSuspendFlag, archEventWait on
** emuSyncEvent until emuState == EMU_RUNNING) so the emu thread can
** honour a suspend that arrives while it is blocked on a platform
** event wait. */
int emuWaitForResume(void) {
    int wasParked = emuSuspendFlag;
    emuSuspendFlag = 1;
    /* Inside WaitForSync the thread is still parked when this returns, so the flag stays. */
    do {
        while (emuState != EMU_RUNNING && !emuExitFlag) {
            archEventWait(emuSyncEvent, -1);
            EMU_TAP_IF_IDLE();
        }
    } while (!wasParked && stayParked());
    return emuExitFlag;
}

void emulatorHoldSuspended(int hold) {
    emuHoldSuspended = hold;
}

void emulatorResume() {
    /* The suspended thread is parked and does not read it; its clock starts over as it runs. */
    if (emuState == EMU_SUSPENDED && !emuHoldSuspended) {
        emuSysTime = 0;
    }
    /* Locked: only a suspend ends here, not a state another thread set since. */
    if (switchState(EMU_SUSPENDED, EMU_RUNNING, 0)) {
        /* A step or reverse play stays silent as it began; a step begun while running ends in a
        ** pause soon anyway. */
        if (!backStep && !frameStopReq && !frameStopNow && !rwPauseReq && !emuPlayReverse) {
            archSoundResume();
            archMidiEnable(1);
        }
        archUpdateEmuDisplay(0);
    }
}

int emulatorGetCurrentScreenMode()
{
    return lastScreenMode;
}

void emulatorRestart() {
    Machine* machine = machineCreate(properties->emulation.machineName);

    emulatorStop();
    if (machine != NULL) {
        boardSetMachine(machine);
        machineDestroy(machine);
    }
}

void emulatorRestartSound() {
    /* 2ch unconditionally; mono/stereo is a mixer flag, so toggling it
    ** does not restart the driver. */
    emulatorSuspend();
    archSoundDestroy();
    archSoundCreate(mixer, 44100, properties->sound.bufSize, 2);
    emulatorResume();
}

int emulatorGetCpuOverflow() {
    int overflow = emuTimeOverflow;
    emuTimeOverflow = 0;
    return overflow;
}

void emulatorSetMaxSpeed(int enable) {
    int was = emuMaxSpeed;

    emuMaxSpeed = enable;
    /* The speed setting applies again, with or without the floor max speed sets on it. */
    if (!was != !enable) {
        emulatorSetFrequency(properties->emulation.speed, NULL);
    }
}

int  emulatorGetMaxSpeed() {
    return emuMaxSpeed;
}

void emulatorPlayReverse(int enable)
{
    /* MIDI too: the frames replayed would send their messages again. */
    if (enable) {
        archSoundSuspend();
        archMidiEnable(0);
    }
    else if (emuState == EMU_RUNNING) {
        /* A rewind while paused leaves the machine paused, and silent. */
        archSoundResume();
        archMidiEnable(1);
    }
    emuPlayReverse = enable;
    /* A rewind does not reach the sync that takes a frame stop, so a pending one
    ** pauses here instead: a step must never end up running free. */
    if (enable) {
        int pending;
        archSemaphoreWait(emuStateLock, -1);
        /* Suspended, it is kept for after the resume and the rewind. */
        pending = (frameStopReq || frameStopNow || backStep) && emuState != EMU_SUSPENDED;
        if (pending) {
            frameStopReq = 0;
            frameStopNow = 0;
            frameStopWait = 0;
            disarmChange();
            endBackStep();
        }
        archSemaphoreSignal(emuStateLock);
        if (pending) {
            emulatorPauseIfRunning();
        }
    }
}

int  emulatorGetPlayReverse()
{
    return emuPlayReverse;
}

void emulatorHoldBegin(int reverse)
{
    int resumed = 0;

    dbgChangeBegin();
    if (reverse) {
        archSoundSuspend();
        archMidiEnable(0);
        emuPlayReverse = 1;
    }
    archSemaphoreWait(emuStateLock, -1);
    if (emuState == EMU_RUNNING && (frameStopReq || frameStopNow || backStep)) {
        /* A frame step under way is taken over, and ends as a hold from a pause does. */
        frameStopReq = 0;
        frameStopNow = 0;
        frameStopWait = 0;
        disarmChange();
        endBackStep();
        holdFromPause = 1;
        resumed = !reverse;
    }
    else if (emuState == EMU_PAUSED && !emuHoldSuspended) {
        writeHeld(EMU_RUNNING, 1);
        holdFromPause = 1;
        resumed = 2;
    }
    archSemaphoreSignal(emuStateLock);

    /* Max speed is heard as when running; reverse play stays silent. */
    if (resumed && !reverse) {
        archSoundResume();
        archMidiEnable(1);
    }
    if (resumed == 2) {
        debuggerNotifyEmulatorResume();
    }
}

void emulatorHoldEnd(int reverse)
{
    int fromPause;

    archSemaphoreWait(emuStateLock, -1);
    /* A suspend keeps the stop for after the resume, as it keeps a frame stop. */
    fromPause = holdFromPause && (emuState == EMU_RUNNING || emuState == EMU_SUSPENDED);
    holdFromPause = 0;
    if (fromPause && reverse) {
        rwPauseReq = 1;
    }
    else if (fromPause) {
        frameStopReq = 1;
        frameStopWait = 0;
    }
    archSemaphoreSignal(emuStateLock);

    if (!reverse) {
        emulatorSetMaxSpeed(0);
    }
    else if (!fromPause) {
        emulatorPlayReverse(0);
    }
    else {
        /* Set after the request, so the run never goes on past the frame shown. */
        emuPlayReverse = 0;
    }
}

void emulatorResetMixer() {
    // Reset active indicators in mixer
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MOONSOUND, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_YAMAHA_SFG, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXAUDIO, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_MSXMUSIC, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_SCC, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_PCM, 1);
    mixerIsChannelTypeActive(mixer, MIXER_CHANNEL_IO, 1);
}

/* Counted in RefreshScreen, once per VDP frame. Not in emulatorSyncScreen,
** which runs once per sync slice on the sync-to-vblank path. */
static UInt32 emuFramesRun;
/* The last frame that ran while the minimized window skipped drawing; 0 if none. */
static UInt32 emuLastFrameNotDrawn;

UInt32 emulatorGetFramesRun(void)
{
    return emuFramesRun;
}

UInt32 emulatorGetMachineFrame(void)
{
    return vdpGetMachineFrame();
}

UInt32 emulatorGetFramesDrawnSinceMinimized(void)
{
    return emuLastFrameNotDrawn != 0 ? emuFramesRun - emuLastFrameNotDrawn : 0xFFFFFFFF;
}

int emulatorSyncScreen()
{
    int rv = 0;
    emuFrameskipCounter--;
    if (emuFrameskipCounter < 0) {
        rv = archUpdateEmuDisplay(properties->emulation.syncMethod);
        if (rv) {
            emuFrameskipCounter = properties->video.frameSkip;
        }
    }
    return rv;
}


void emulatorRefreshFromSource() {
    if (emuUseSynchronousUpdate() == P_EMU_SYNCFRAMES && emuState == EMU_RUNNING) {
        emulatorSyncScreen();
    }
}

/* At a shown frame end, or a machine VDP frame end (machineFrame; shown when it presents
** the frame): a step ends at a shown one, unchanged ones pass while waiting for a
** change, and a few machine ones without a shown one end it. The sync is brought forward. */
static void frameStepAtFrameEnd(int shown, int machineFrame)
{
    int ended = 0;

    if (!frameStopReq || !emulatorIsEmuThread() || rwPhase == 1) {
        return;
    }
    archSemaphoreWait(emuStateLock, -1);
    if (frameStopReq) {
        /* Waiting for a change that does not come ends as a plain frame step. */
        if (frameStopChange && machineFrame && ++changeFrames >= CHANGE_STEP_FRAMES &&
            !(shown && frameBufferShownChanged())) {
            disarmChange();
            changeGaveUp = 1;
        }
        if (shown && frameStopChange && !frameBufferShownChanged()) {
            frameStopWait = 0;
        }
        else if (shown || ++frameStopWait >= 3) {
            frameStopReq  = 0;
            frameStopNow  = 1;
            frameStopWait = 0;
            disarmChange();
            ended = 1;
        }
    }
    archSemaphoreSignal(emuStateLock);
    if (ended) {
        boardSyncSoon();
    }
}

/* Called by the VDP every frame; presented: emulatorShownDisplayEnded showed it already;
** vintShows: it will show the next one at its end of active display. A shown source that
** paces the screen calls emulatorRefreshFromSource instead; the mode still follows the VDP. */
void RefreshScreen(int screenMode, int presented, int vintShows) {
    EmuTapFn tap = emuTap;

    lastScreenMode = screenMode;
    /* A frame replayed on the way back was run before; it is not shown here either. */
    if (emulatorRewindReplaying()) {
        return;
    }
    if (tap) {
        tap(EMU_TAP_FRAME);
    }

    emuFramesRun++;
    if (!vdpGetDisplayEnable()) {
        emuLastFrameNotDrawn = emuFramesRun;
    }
    if (!presented && vintShows && emuOnThisThread) {
        /* Just flipped: the frame on screen is the one that ended its active display last. */
        rwShownVint = emuLastVint;
        rwCut = 0;
    }

    if (!videoManagerActivePacesScreen() && !presented) {
        emulatorRefreshFromSource();
        /* A step asked for after the end of active display runs on to the next one. */
        frameStepAtFrameEnd(!vintShows, 1);
    }
    else {
        frameStepAtFrameEnd(0, 1);
    }
}

void emulatorShownFrameEnded(void)
{
    frameStepAtFrameEnd(1, 0);
}

int emulatorShownDisplayEnded(int ready, int shown, void (*show)(void* ref), void* ref)
{
    if (!emulatorIsEmuThread()) {
        return 0;
    }
    /* Only the source on screen sets the frames reverse play steps by. */
    if (shown) {
        emuLastVint = boardSystemTime();
    }
    if (rwPhase == 1) {
        /* A frame begun after the load, at or past the earliest one to stop at. */
        if (!ready || !shown || (Int32)(emuLastVint - rwLo) < 0) {
            return 0;
        }
        show(ref);
        /* Presented as the sync mode does; the timer only repaints in some of them. */
        if (emuUseSynchronousUpdate() == P_EMU_SYNCFRAMES) {
            emulatorRefreshFromSource();
        }
        else {
            archUpdateEmuDisplay(0);
        }
        rwShownVint = emuLastVint;
        rwCut = 0;
        rwPhase = 2;
        backArrived = backStep != 0;
        boardSyncSoon();
        return 1;
    }
    if (!frameStopReq || !ready || !shown) {
        return 0;
    }
    show(ref);
    emulatorRefreshFromSource();
    rwShownVint = emuLastVint;
    rwCut = 0;
    frameStepAtFrameEnd(1, 0);
    return 1;
}

#ifndef NO_TIMERS

#ifdef WII

static int WaitForSync(int maxSpeed, int breakpointHit)
{
    UInt32 diffTime;

    emuMaxEmuSpeed = maxSpeed;

    emuSuspendFlag = 1;

    archPollInput();

    if (emuState != EMU_RUNNING) {
        archEventSet(emuStartEvent);
        archThreadSleep(100);
        emuSuspendFlag = 0;
        return emuExitFlag ? -1 : 0;
    }

    emuSuspendFlag = 0;

    if (emuSingleStep) {
        diffTime = 0;
    }else{
        diffTime = 20;
    }

    if (emuMaxSpeed || emuMaxEmuSpeed) {
        diffTime *= 10;
    }

    return emuExitFlag ? -1 : diffTime;
}

#else

/* WaitReverse returns it to have WaitForSync go on as it does when running. */
#define RW_RUN_ON 0x7fffffff

/* Board ticks as the ms a negative WaitForSync result covers, rounded up: the sync
** converts them with the board clock, which follows the speed setting. */
static int rwTicksToMs(UInt32 ticks)
{
    UInt64 freq = (UInt64)emuFrequency * (boardFrequency() / 3579545);
    return (int)(((UInt64)ticks * 1000 + freq - 1) / freq);
}

/* A reverse play hold that began from a pause pauses at the sync after it ends. */
static void takeReversePause(void)
{
    archSemaphoreWait(emuStateLock, -1);
    if (rwPauseReq && emuState == EMU_RUNNING) {
        rwPauseReq = 0;
        frameStopNow = 1;
    }
    archSemaphoreSignal(emuStateLock);
}

/* A step back that cannot go on pauses at the sync, on the frame shown. */
static void backStepGivesUp(void)
{
    archSemaphoreWait(emuStateLock, -1);
    if (backStep && emuState == EMU_RUNNING) {
        backGaveUp = 1;
        frameStopNow = 1;
        endBackStep();
    }
    archSemaphoreSignal(emuStateLock);
}

/* A step back went a frame further, to one it showed or, ranOut, to one it could not reach.
** Non-zero when it ends there, pausing at the sync. */
static int backStepEnds(int ranOut)
{
    int changed, ends = 0;

    archSemaphoreWait(emuStateLock, -1);
    if (backStep && emuState == EMU_RUNNING) {
        backFrames++;
        changed = backStep == 2 && !ranOut && frameBufferShownChanged();
        ends = backStep == 1 || changed || backFrames >= CHANGE_STEP_FRAMES;
        if (ends && !changed && backStep == 2 && backFrames >= CHANGE_STEP_FRAMES) {
            backGaveUp = 2;
        }
        else if (ends && !changed && ranOut) {
            backGaveUp = 1;
        }
        if (ends) {
            frameStopNow = 1;
            endBackStep();
        }
    }
    archSemaphoreSignal(emuStateLock);
    return ends;
}

/* The board time a step stopping at stopAt loads at or before: usual, or earlier when the
** lead hook asks, by at most a second. */
static UInt32 rewindLoadAt(UInt32 stopAt, UInt32 usual)
{
    EmuRewindLeadFn fn = emuRewindLead;
    UInt32 r, lead;

    if (fn == NULL) {
        return usual;
    }
    r = fn(stopAt);
    if ((Int32)(r - stopAt) >= 0) {
        return usual;
    }
    lead = r - boardFrequency() / 1000;
    if ((Int32)(lead - usual) > 0) {
        return usual;
    }
    /* Measured as a distance, so a lead from long ago cannot wrap past the limit. */
    if ((UInt32)(usual - lead) > boardFrequency()) {
        lead = usual - boardFrequency();
    }
    return lead;
}

/* A replay runs in slices of at most this many ms, so the sync can take a suspend between. */
#define RW_SLICE_MS 20

static int rwSlice(UInt32 ticks)
{
    int ms = rwTicksToMs(ticks);
    return ms > RW_SLICE_MS ? -RW_SLICE_MS : -ms;
}

/* One step of reverse play a frame back, at the refresh rate: loads a snapshot from before
** the frame on screen and runs it on to the end of active display of the frame before it,
** where emulatorShownDisplayEnded shows it. */
static int WaitReverse(void)
{
    UInt32 period, now, step, due, usual;
    int rate;
    int fallback;
    int ranOut = 0;
    int backKind;

    boardEnableSnapshots(0);
    if (emuState != EMU_RUNNING || emuExitFlag) {
        /* Stopped mid-replay: the next step starts from here, never runs past the target.
        ** Suspended (a menu, a window move), the replay goes on after the resume. */
        if (rwPhase == 1 && !(emuState == EMU_SUSPENDED && !emuExitFlag)) {
            rwPhase = 2;
            rwShownVint = rwLo;
            rwCut = 1;
            rwCutVint = rwBudgetEnd;
            /* An early load keeps snapshots that may now lie ahead of the machine. */
            boardRewindForget(boardSystemTime());
        }
        return RW_RUN_ON;
    }
    /* A breakpoint or another sync before the replay ends: run on to the frame. */
    if (rwPhase == 1 && (Int32)(rwBudgetEnd - boardSystemTime()) > 0) {
        emulatorTap(EMU_TAP_REPLAY);
        return rwSlice(rwBudgetEnd - boardSystemTime());
    }
    /* Arrived: the machine is at the frame on screen, so tools copy it as they do when paused. */
    if (rwPhase == 2) {
        emulatorTap(EMU_TAP_IDLE);
    }

    rate = boardGetRefreshRate();
    if (rate <= 0) {
        rate = 60;
    }
    period = boardFrequency() / rate;
    if (rwPhase == 1) {
        /* Ran out before the frame: never on into the future, but a frame back from here. */
        rwShownVint = rwLo;
        rwPhase = 2;
        ranOut = 1;
    }
    if (backStep && (backArrived || ranOut) && backStepEnds(ranOut)) {
        rwPhase = 0;
        return RW_RUN_ON;
    }
    backArrived = 0;

    /* Real time in ms times the rate, so a frame is 1000; the speed setting has no say. */
    step = 1000;
    if (!backStep) {
        now = archGetSystemUpTime(1000) * rate;
        /* More than a frame late, or a time left from an earlier rewind: from now. */
        if ((Int32)(now - rwDue) > (Int32)step || (Int32)(rwDue - now) > 50 * rate) {
            rwDue = now;
        }
        while ((Int32)(rwDue - now) > 0 && emuPlayReverse) {
            archEventWait(emuSyncEvent, -1);
            if (emuState != EMU_RUNNING || emuExitFlag) {
                return RW_RUN_ON;
            }
            now = archGetSystemUpTime(1000) * rate;
        }
    }
    emuSysTime = archGetSystemUpTime(1000);

    if (!emuPlayReverse && !backStep) {
        rwPhase = 0;
        return RW_RUN_ON;
    }

    /* The source on screen has no end of active display to stop at (it does not tell one,
    ** as a V9990 does not): 50 ms steps, of which its own frames come out as they may. */
    now = boardSystemTime();
    fallback = (Int32)(now - emuLastVint) < 0 || now - emuLastVint > 2 * period;
    if (backStep) {
        /* A step back needs a frame to stop at. */
        if (fallback) {
            backStepGivesUp();
            rwPhase = 0;
            return RW_RUN_ON;
        }
    }
    else {
        rwDue += fallback ? 50 * rate : step;
    }

    if (backStep && backFromCut) {
        /* The cut replay stopped short of the frame on screen, which may lie ahead of the board
        ** time; taken once, so a step that runs out goes on back from rwLo. */
        rwShownVint = rwCutVint;
        rwCut = 0;
    }
    else if ((Int32)(now - rwShownVint) < 0 || now - rwShownVint > 2 * period) {
        rwShownVint = (Int32)(now - emuLastVint) >= 0 && now - emuLastVint <= 2 * period ? emuLastVint : now;
    }
    rwLo = rwShownVint - period * 3 / 2;
    rwPhase = 0;
    backFromCut = 0;

    /* From before the start of the frame to stop at, so it is drawn whole. Shift+F3 runs
    ** many steps, so only the others may ask to start earlier. */
    usual = rwShownVint - period * 5 / 2;
    backKind = backStep;
    if (fallback ? !boardRewind() :
        !boardRewindTo(backKind != 2 ? rewindLoadAt(rwLo, usual) : usual, usual)) {
        /* No snapshot left to restore. Granting emulated time here is what
        ** used to run the guest forward, muted, for as long as the key was
        ** held. Begun from a pause, it pauses there until the key is let go. */
        int paused = 0;
        if (backStep) {
            backStepGivesUp();
            return RW_RUN_ON;
        }
        archSemaphoreWait(emuStateLock, -1);
        if (holdFromPause && emuState == EMU_RUNNING) {
            frameStopNow = 1;
            paused = 1;
        }
        archSemaphoreSignal(emuStateLock);
#if EMU_REVERSE_STOP_AT_RING_END
        if (!paused) {
            emulatorPlayReverse(0);
        }
#endif
        return RW_RUN_ON;
    }
    if (fallback) {
        return -60;
    }

    due = rwShownVint;
    rwBudgetEnd = due;
    rwPhase = 1;
    /* Tools drain what the load logged before a long replay can fill their logs. */
    emulatorTap(EMU_TAP_REPLAY);
    return rwSlice(due - boardSystemTime());
}

static int WaitForSync(int maxSpeed, int stopCause) {
    UInt32 li1;
    UInt32 li2;
    static UInt32 tmp = 0;
    static UInt32 cnt = 0;
    UInt32 sysTime;
    UInt32 diffTime;
    UInt32 syncPeriod;
    static int overflowCount = 0;
    static UInt32 kbdPollCnt = 0;

    if ((emuPlayReverse && properties->emulation.reverseEnable) || rwPhase != 0 || backStep) {
        int reverseTime = WaitReverse();
        if (reverseTime != RW_RUN_ON) {
            return reverseTime;
        }
        /* Breakpoints and watches hit on the way back are passed over, as ever. */
        stopCause = DBG_STOP_NONE;
    }
    if (rwPauseReq) {
        takeReversePause();
    }

    /* Never a snapshot of a replay under way, nor of a frame reverse play shows. */
    if (!(emuPlayReverse && properties->emulation.reverseEnable) && rwPhase == 0 && !backStep) {
        boardEnableSnapshots(1);
    }

    emuMaxEmuSpeed = maxSpeed;

    syncPeriod = emulatorGetSyncPeriod();
    li1 = archGetHiresTimer();

    RACE_TEST_POINT(RACE_PARK_DELAY);
    emuSuspendFlag = 1;

    /* A breakpoint or watch hit during a step is the one stop to report. */
    if (stopCause == DBG_STOP_NONE && emuSingleStep) {
        stopCause = DBG_STOP_STEP;
    }
    if (stopCause != DBG_STOP_NONE) {
        /* Paused before the notify posts the stop, so whoever it wakes can step again. */
        emuSingleStep = 0;
        writeState(EMU_PAUSED, 1);
        archSoundSuspend();
        archMidiEnable(0);
        debuggerNotifyEmulatorPauseFor(stopCause);
    }
    /* A frame stop only from running: during a suspend it waits for the resume. */
    else if (frameStopNow && takeFrameStop()) {
        archSoundSuspend();
        archMidiEnable(0);
        debuggerNotifyEmulatorPauseFor(DBG_STOP_FRAME);
    }

    if (emuState != EMU_RUNNING) {
        archEventSet(emuStartEvent);
        emuSysTime = 0;
    }

#ifdef SINGLE_THREADED
    emuExitFlag |= archPollEvent();
#endif

    if (((++kbdPollCnt & 0x03) >> 1) == 0) {
       archPollInput();
    }

    if (emuUseSynchronousUpdate() == P_EMU_SYNCTOVBLANK) {
        overflowCount += emulatorSyncScreen() ? 0 : 1;
        while ((!emuExitFlag && emuState != EMU_RUNNING) || overflowCount > 0) {
            archEventWait(emuSyncEvent, -1);
            EMU_TAP_IF_IDLE();
#ifdef NO_TIMERS
            while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
            overflowCount--;
        }
    }
    else if (emuMaxEmuSpeed == BOARD_BOOST_TAPE && emuState == EMU_RUNNING && !emuExitFlag) {
        /* Do not queue for a sync tick. One tick is handed out every syncPeriod
        ** ms whatever the host can do, so waiting for one pins the rate at
        ** slice-per-tick; skipping it lets the tape run out as fast as the host
        ** manages, which is the whole point of boosting a load. */
        overflowCount = 0;
    }
    else {
        do {
#ifdef NO_TIMERS
            while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
            archEventWait(emuSyncEvent, -1);
            EMU_TAP_IF_IDLE();
            if (((emuMaxSpeed || emuMaxEmuSpeed) && !emuExitFlag) || overflowCount > 0) {
#ifdef NO_TIMERS
                while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
                archEventWait(emuSyncEvent, -1);
            }
            overflowCount = 0;
        } while (!emuExitFlag && emuState != EMU_RUNNING);
    }

    RACE_TEST_POINT(RACE_PARK_TAIL);
    /* A pause that came after the loop last looked at the state, or on the tape boost
    ** branch that does not wait, is waited out here. */
    while (stayParked()) {
        archEventWait(emuSyncEvent, -1);
        EMU_TAP_IF_IDLE();
    }
    li2 = archGetHiresTimer();

    emuTimeIdle  += li2 - li1;
    emuTimeTotal += li2 - tmp;
    tmp = li2;

    sysTime = archGetSystemUpTime(1000);
    diffTime = sysTime - emuSysTime;
    emuSysTime = sysTime;

    if (emuSingleStep) {
        diffTime = 0;
    }

    if ((++cnt & 0x0f) == 0) {
        emuCalcCpuUsage(NULL);
    }

    overflowCount = emulatorGetCpuOverflow() ? 1 : 0;
#ifdef NO_HIRES_TIMERS
    if (diffTime > 50U) {
        overflowCount = 1;
        diffTime = 0;
    }
#else
    if (diffTime > 100U) {
        overflowCount = 1;
        diffTime = 0;
    }
#endif
    if (emuMaxEmuSpeed == BOARD_BOOST_TAPE && !emuSingleStep) {
        /* The measured elapsed time is no use without the tick wait: an
        ** iteration can be shorter than the 1 ms clock resolution and would
        ** read as zero. A fixed slice is what the loop runs on instead. */
        diffTime = EMU_TAPE_SLICE_MS;
    }
    else if (emuMaxSpeed || emuMaxEmuSpeed) {
        diffTime *= EMU_MAXSPEED_FACTOR;
        if (diffTime > 2 * EMU_MAXSPEED_FACTOR * syncPeriod) {
            diffTime =  2 * EMU_MAXSPEED_FACTOR * syncPeriod;
        }
    }

    emuUsageCurrent += diffTime;

    return emuExitFlag ? -99 : diffTime;
}
#endif

#else
#include <windows.h>

UInt32 getHiresTimer() {
    static LONGLONG hfFrequency = 0;
    LARGE_INTEGER li;

    if (!hfFrequency) {
        if (QueryPerformanceFrequency(&li)) {
            hfFrequency = li.QuadPart;
        }
        else {
            return 0;
        }
    }

    QueryPerformanceCounter(&li);

    return (DWORD)(li.QuadPart * 1000000 / hfFrequency);
}

static UInt32 busy, total, oldTime;

static int WaitForSync(int maxSpeed, int breakpointHit) {
    emuSuspendFlag = 1;

    busy += getHiresTimer() - oldTime;

    emuExitFlag |= archPollEvent();

    archPollInput();

    do {
        for (;;) {
            UInt32 sysTime = archGetSystemUpTime(1000);
            UInt32 diffTime = sysTime - emuSysTime;
            emuExitFlag |= archPollEvent();
            if (diffTime < 10) {
                continue;
            }
            emuSysTime += 10;
            if (diffTime > 30) {
                emuSysTime = sysTime;
            }
            break;
        }
    } while (!emuExitFlag && emuState != EMU_RUNNING);


    emuSuspendFlag = 0;

    total += getHiresTimer() - oldTime;
    oldTime = getHiresTimer();
#if 0
    if (total >= 1000000) {
        UInt32 pct = 10000 * busy / total;
        printf("CPU Usage = %d.%d%%\n", pct / 100, pct % 100);
        total = 0;
        busy = 0;
    }
#endif

    return emuExitFlag ? -1 : 10;
}

#endif

