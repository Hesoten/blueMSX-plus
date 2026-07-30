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
#include <math.h>
#include <string.h>

static int WaitForSync(int maxSpeed, int breakpointHit);

static void*  emuThread;
#ifndef WII
static void*  emuSyncEvent;
#endif
static void*  emuStartEvent;
#ifndef WII
static void*  emuTimer;
#endif
static int    emuExitFlag;
static UInt32 emuSysTime = 0;
static UInt32 emuFrequency = 3579545;
int           emuMaxSpeed = 0;
int           emuPlayReverse = 0;
int           emuMaxEmuSpeed = 0; // Max speed issued by emulation

/* Fast-forward multiplier (max-speed and FDC/HDD boost); higher =
** faster, capped by host emu throughput.  Stock blueMSX used 10. */
#define EMU_MAXSPEED_FACTOR 15
static char   emuStateName[512];
static volatile int      emuSuspendFlag;
static volatile EmuState emuState = EMU_STOPPED;
static volatile int      emuSingleStep = 0;
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

    if ((cnt++ & 0x1f) == 0) {
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
        !boardGetFdcActive())
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

void emulatorSetState(EmuState state) {
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
        EmuState oldState = state;
        state = EMU_RUNNING;
        if (!boardRewindOne()) {
            state = oldState;
        }
        
    }
    emuState = state;
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
                refreshRate = boardGetRefreshRate();

                if (syncMethod == P_EMU_SYNCAUTO || syncMethod == P_EMU_SYNCNONE) {
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

    ledSetAll(0);
    emuState = EMU_STOPPED;

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
    /* stateName may point to extractToken()'s static argBuf via the
    ** /onearg -> tryLaunchUnknownFile path; machineCreate() below calls
    ** extractToken on machine config and clobbers that buffer, so copy
    ** the path into emuStateName up-front before any such call. */
    strcpy(emuStateName, stateName ? stateName : "");
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
        emuState = EMU_STOPPED;
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

    emuState = EMU_PAUSED;
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
        emuState = EMU_STOPPED;
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

        emuState = EMU_RUNNING;
    }
#endif
}

void emulatorStop() {
    if (emuState == EMU_STOPPED) {
        return;
    }

    debuggerNotifyEmulatorStop();

    emuState = EMU_STOPPED;

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

void emulatorSetFrequency(int logFrequency, int* frequency) {
    emuFrequency = emulatorLogFrequencyToHz(logFrequency);

    if (frequency != NULL) {
        *frequency  = emuFrequency;
    }

    boardSetFrequency(emuFrequency);
}

void emulatorSuspend() {
    if (emuState == EMU_RUNNING) {
        emuState = EMU_SUSPENDED;
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
}

/* Cancel-cooperative wait used by archWaitForAckOrSuspend. Mirrors the
** suspend tail of WaitForSync (set emuSuspendFlag, archEventWait on
** emuSyncEvent until emuState == EMU_RUNNING) so the emu thread can
** honour a suspend that arrives while it is blocked on a platform
** event wait. */
int emuWaitForResume(void) {
    emuSuspendFlag = 1;
    while (emuState != EMU_RUNNING && !emuExitFlag) {
        archEventWait(emuSyncEvent, -1);
    }
    emuSuspendFlag = 0;
    return emuExitFlag;
}

void emulatorResume() {
    if (emuState == EMU_SUSPENDED) {
        emuSysTime = 0;

        archSoundResume();
        archMidiEnable(1);
        emuState = EMU_RUNNING;
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
    emuMaxSpeed = enable;
}

int  emulatorGetMaxSpeed() {
    return emuMaxSpeed;
}

void emulatorPlayReverse(int enable)
{
    if (enable) {   
        archSoundSuspend();
    }
    else {
        archSoundResume();
    }
    emuPlayReverse = enable;
}

int  emulatorGetPlayReverse()
{
    return emuPlayReverse;
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


void RefreshScreen(int screenMode) {

    lastScreenMode = screenMode;

    if (emuUseSynchronousUpdate() == P_EMU_SYNCFRAMES && emuState == EMU_RUNNING) {
        emulatorSyncScreen();
    }
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

int WaitReverse()
{
    boardEnableSnapshots(0);

    for (;;) {
        UInt32 sysTime = archGetSystemUpTime(1000);
        UInt32 diffTime = sysTime - emuSysTime;
        if (diffTime >= 50) {
            emuSysTime = sysTime;
            break;
        }
        archEventWait(emuSyncEvent, -1);
    }

    boardRewind();

    return -60;
}

static int WaitForSync(int maxSpeed, int breakpointHit) {
    UInt32 li1;
    UInt32 li2;
    static UInt32 tmp = 0;
    static UInt32 cnt = 0;
    UInt32 sysTime;
    UInt32 diffTime;
    UInt32 syncPeriod;
    static int overflowCount = 0;
    static UInt32 kbdPollCnt = 0;

    if (emuPlayReverse && properties->emulation.reverseEnable) {
        return WaitReverse();
    }

    boardEnableSnapshots(1);

    emuMaxEmuSpeed = maxSpeed;

    syncPeriod = emulatorGetSyncPeriod();
    li1 = archGetHiresTimer();

    emuSuspendFlag = 1;

    if (emuSingleStep) {
        debuggerNotifyEmulatorPause();
        emuSingleStep = 0;
        emuState = EMU_PAUSED;
        archSoundSuspend();
        archMidiEnable(0);
    }

    if (breakpointHit) {
        debuggerNotifyEmulatorPause();
        emuState = EMU_PAUSED;
        archSoundSuspend();
        archMidiEnable(0);
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
#ifdef NO_TIMERS
            while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
            overflowCount--;
        }
    }
    else {
        do {
#ifdef NO_TIMERS
            while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
            archEventWait(emuSyncEvent, -1);
            if (((emuMaxSpeed || emuMaxEmuSpeed) && !emuExitFlag) || overflowCount > 0) {
#ifdef NO_TIMERS
                while (timerCallback(NULL) == 0) emuExitFlag |= archPollEvent();
#endif
                archEventWait(emuSyncEvent, -1);
            }
            overflowCount = 0;
        } while (!emuExitFlag && emuState != EMU_RUNNING);
    }

    emuSuspendFlag = 0;
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
    if (emuMaxSpeed || emuMaxEmuSpeed) {
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

