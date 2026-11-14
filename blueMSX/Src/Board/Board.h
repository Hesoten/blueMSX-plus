/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Board/Board.h,v $
**
** $Revision: 1.40 $
**
** $Date: 2007-03-20 02:30:31 $
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
#ifndef BOARD_H
#define BOARD_H
 
#include "MsxTypes.h"
#include "MediaDb.h"
#include "Machine.h"
#include "VDP.h"
#include "AudioMixer.h"
#include <stdio.h>

typedef struct {
    struct {
        int     inserted;
        RomType type;
        char    name[512];
        char    inZipName[512];
    } carts[2];
    struct {
        int  inserted;
        char name[512];
        char inZipName[512];
    } disks[64];
    struct {
        int  inserted;
        char name[512];
        char inZipName[512];
    } tapes[1];
    struct {
        VdpSyncMode vdpSyncMode;
    } video;
} BoardDeviceInfo;

typedef struct {
    int  cartridgeCount;
    int  diskdriveCount;
    int  casetteCount;

    void* cpuRef;

    void   (*destroy)();
    void   (*softReset)();
    void   (*loadState)();
    void   (*saveState)();
    int    (*getRefreshRate)();
    UInt8* (*getRamPage)(int);
    void   (*setDataBus)(void*, UInt8, UInt8, int);

    void   (*run)(void*);
    void   (*stop)(void*);
    void   (*setInt)(void*);
    void   (*clearInt)(void*);
    void   (*setCpuTimeout)(void*, UInt32);
    void   (*setBreakpoint)(void*, UInt16);
    void   (*clearBreakpoint)(void*, UInt16);

    void   (*changeCartridge)(void*, int, int);

    UInt32   (*getTimeTrace)(int);
} BoardInfo;

void boardInit(UInt32* systemTime);

int boardRun(Machine* machine, 
             BoardDeviceInfo* deviceInfo,
             Mixer* mixer,
             char* stateFile,
             int frequency,
             int reversePeriod,
             int reverseBufferCnt,
             int (*syncCallback)(int, int));

int boardRewind();
int boardRewindOne();
/* Drops the snapshots taken after dropAfter and loads the newest one left taken at or before
** loadAt (else the oldest), keeping those after it; 0 when none is at or before dropAfter. */
int boardRewindTo(UInt32 loadAt, UInt32 dropAfter);
/* Non-zero when the board time given is not ahead of now and the oldest snapshot was taken
** at or before it. */
int boardRewindReaches(UInt32 at);
/* Drops the snapshots taken after the board time given, keeping at least one. */
void boardRewindForget(UInt32 after);
void boardEnableSnapshots(int enable);

BoardType boardGetType();

/* Non-zero while loading an old (2.8.2 era) format state; per-device LoadState
** routines use this to gate the old-format compatibility fallbacks. */
int boardStateLoadIsOldFormat(void);

void boardSetMachine(Machine* machine);
void boardReset();

void boardSetDataBus(UInt8 value, UInt8 defaultValue, int setDefault);

UInt64 boardSystemTime64();

void boardCaptureStart(const char* filename);
void boardCaptureStop();
int boardCaptureHasData();
int boardCaptureIsRecording();
int boardCaptureIsPlaying();
int boardCaptureCompleteAmount();
int boardCaptureCheckFinish(void);

/* UI-thread drain for the "recording just finalized" event. Returns 1 and
** copies the .cap path into out (null-terminated) if a completion is pending,
** self-clearing so a second call returns 0. Callable from any path that can
** cause a record-end (emulatorStop, menu Stop, per-frame poll for RLE overflow). */
int boardCaptureConsumePendingToast(char* out, int outSize);

/* Missing-file report populated by boardRun's state pre-validation and by
** machineInitialize when a slot ROM fails to load on plain (non-state)
** launch. Reporter dedupes and clips at MISSING_FILES_MAX. */
void        boardClearMissingFiles(void);
int         boardGetMissingFileCount(void);
const char* boardGetMissingFile(int idx);
void        boardReportMissingFile(const char* file, const char* inZip);

UInt8 boardCaptureUInt8(UInt8 logId, UInt8 value);

/* Zero when the state could not be stored whole, as when a part did not fit. */
int boardSaveState(const char* stateFile, int screenshot);

#define BOARD_SNAPSHOT_OK          0
#define BOARD_SNAPSHOT_NONE        1  /* none since the machine was built or a cartridge changed */
#define BOARD_SNAPSHOT_HOST_MEDIUM 2  /* a host folder or drive in a disk drive cannot go back  */
#define BOARD_SNAPSHOT_FAILED      3  /* not stored whole; an earlier one is gone too            */

/* One machine state kept in memory. Only while the emulation thread is parked, on
** the window's thread, which is where step back loads its states too. */
int boardSnapshotTake(void);
int boardSnapshotRestore(void);

/* Non-zero while parked by a breakpoint or a watchpoint stop. A breakpoint stop has
** tested its address already; a watchpoint stop is in the middle of an instruction. */
int boardIsParkedInBreakpointStop(void);

void boardSetFrequency(int frequency);
int  boardGetRefreshRate();

void boardSetBreakpoint(UInt16 address);
void boardClearBreakpoint(UInt16 address);

/* One bit per kind of /INT source; two sources on one bit drop each other's
** request. A new bit goes here and into the overlap check in Board.c. */
#define BOARD_INT_VDP_IE0           0x00000001
#define BOARD_INT_VDP_IE1           0x00000002
#define BOARD_INT_VDP_IE2           0x00000004
#define BOARD_INT_YMF262            0x00000008
#define BOARD_INT_Y8950             0x00000010
#define BOARD_INT_MAKOTO            0x00000020
#define BOARD_INT_YM2151            0x00000040
#define BOARD_INT_MSX_RS232         0x00000080
#define BOARD_INT_MSX_MIDI_TMR      0x00000100
#define BOARD_INT_MSX_MIDI_RXRDY    0x00000200
#define BOARD_INT_MSX_AUDIO_MIDI    0x00000400
#define BOARD_INT_YM2148            0x00000800
#define BOARD_INT_V9990             0x00001000
#define BOARD_INT_V9968_CART_IE0    0x00002000
#define BOARD_INT_V9968_CART_IE1    (BOARD_INT_V9968_CART_IE0 << 1)
#define BOARD_INT_V9968_CART_IE2    (BOARD_INT_V9968_CART_IE0 << 2)

void   boardSetInt(UInt32 irq);
void   boardClearInt(UInt32 irq);
UInt32 boardGetInt(UInt32 irq);

/* Bits of device kinds that can exist more than once: each unit holds its own
** request and the bit stays set while any unit holds it. Plain set/clear never take these. */
#define BOARD_INT_UNIT_BITS (BOARD_INT_YM2151 | BOARD_INT_YM2148)
void   boardSetIntUnit(UInt32 irq, const void* unit);
void   boardClearIntUnit(UInt32 irq, const void* unit);

UInt8* boardGetRamPage(int page);
UInt32 boardGetRamSize();
UInt32 boardGetVramSize();

int boardUseRom();
int boardUseMegaRom();
int boardUseMegaRam();
int boardUseFmPac();

void boardSetNoSpriteLimits(int enable);
int boardGetNoSpriteLimits();

void boardSetVdpCmdSpeed(int percent);
int  boardGetVdpCmdSpeed();

/* Drop the FDC core boost on melodic sound-chip writes (FM key-on,
** PSG R8-R10 audible volume, etc.); address latches are tracked
** off-boost so only audible writes trigger. VBLANK keyboard scans
** and VDP VRAM streaming stay excluded. */
void boardCheckFdcBoostKill(UInt16 port, UInt8 value);

/* Drop the FDC/HDD boost on SCC volume / channel-enable writes;
** called from sccWrite() because SCC is memory-mapped (the I/O hook
** doesn't see it).  address is the SCC register offset. */
void boardCheckSccBoostKill(UInt8 address, UInt8 value);

/* Drop the boost once the picture starts moving; called once per frame with
** the scroll registers packed into origin. */
void boardCheckVdpBoostKill(UInt32 origin);

RomType boardGetRomType(int cartNo);

typedef enum { HD_NONE, HD_SUNRISEIDE, HD_BEERIDE, HD_GIDE, HD_RSIDE,
               HD_MEGASCSI, HD_WAVESCSI, HD_GOUDASCSI, HD_NOWIND,
               HD_MFRSD, HD_ROOKIEDRIVE } HdType;
HdType boardGetHdType(int hdIndex);

const char* boardGetBaseDirectory();

Mixer* boardGetMixer();

void boardChangeCartridge(int cartNo, RomType romType, char* cart, char* cartZip);
void boardChangeDiskette(int driveId, char* fileName, const char* fileInZipFile);
void boardChangeCassette(int tapeId, char* name, const char* fileInZipFile);

int  boardGetCassetteInserted();

#define boardFrequency() (6 * 3579545)

static UInt32 boardSystemTime() {
    extern UInt32* boardSysTime;
    return *boardSysTime;
}

UInt64 boardSystemTime64();

typedef void (*BoardTimerCb)(void* ref, UInt32 time);

typedef struct BoardTimer BoardTimer;

BoardTimer* boardTimerCreate(BoardTimerCb callback, void* ref);
void boardTimerDestroy(BoardTimer* timer);
void boardTimerAdd(BoardTimer* timer, UInt32 timeout);
void boardTimerRemove(BoardTimer* timer);
void boardTimerCheckTimeout(void* dummy);
UInt32 boardCalcRelativeTimeout(UInt32 timerFrequency, UInt32 nextTimeout);

/* Stops the machine where it is, for cause (a DBG_STOP_ value), which the pause tells on. */
void   boardOnBreakpoint(int cause);
/* Brings the next sync with the real clock forward to the next tick. Emulation thread only. */
void   boardSyncSoon(void);

int boardInsertExternalDevices();
int boardRemoveExternalDevices();

// The following methods are more generic config than board specific
// They should be moved from board.
void boardSetDirectory(const char* dir);

void boardSetFdcTimingEnable(int enable);
int  boardGetFdcTimingEnable();
int  boardGetFdcActive(void);
void boardSetFdcActive();

/* HDD/SD I/O boost: same skip-real-clock-sync mechanism as the FDC
** boost, gated by an independent property so users can keep FDC
** accurate while fast-forwarding SCSI / SD / IDE bulk transfers. */
void boardSetHddSdBoostEnable(int enable);
int  boardGetHddSdBoostEnable(void);
void boardSetHddSdActive(void);

/* Cassette load boost. Kept off the FDC's timer because the two want opposite
** things: the disk guesses at a load and covers the work that follows it,
** while a tape read says exactly "the machine is waiting" and nothing else
** happens until it stops. */
void boardSetCasBoostEnable(int enable);
void boardSetCasActive(void);
int  boardGetCasActive(void);

/* Boost level handed to the sync callback. Disk keeps the historic rate limit
** because its boost also has to carry the game's own post-load work; a tape
** load is pure waiting, so it runs at whatever the host manages. */
#define BOARD_BOOST_NONE  0
#define BOARD_BOOST_DISK  1
#define BOARD_BOOST_TAPE  2

void boardSetYm2413Oversampling(int value);
int  boardGetYm2413Oversampling();
void boardSetY8950Oversampling(int value);
int  boardGetY8950Oversampling();
void boardSetMoonsoundOversampling(int value);
int  boardGetMoonsoundOversampling();

void boardSetYm2413Enable(int value);
int  boardGetYm2413Enable();
void boardSetY8950Enable(int value);
int  boardGetY8950Enable();
void boardSetMoonsoundEnable(int value);
int  boardGetMoonsoundEnable();
void boardSetVideoAutodetect(int value);
int  boardGetVideoAutodetect();

void boardSetPeriodicCallback(BoardTimerCb cb, void* reference, UInt32 frequency);

#endif /* BOARD_H */

