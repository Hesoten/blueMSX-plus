/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Board/Board.c,v $
**
** $Revision: 1.79 $
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
#include "Board.h"
#include "MSX.h"
#include "SVI.h"
#include "SG1000.h"
#include "Coleco.h"
#include "Adam.h"
#include "AudioMixer.h"
#include "YM2413.h"
#include "Y8950.h"
#include "Moonsound.h"
#include "SaveState.h"
#include "ziphelper.h"
#include "ArchNotifications.h"
#include "VideoManager.h"
#include "DebugDeviceManager.h"
#include "V9938.h"
#include "MegaromCartridge.h"
#include "Disk.h"
#include "VideoManager.h"
#include "Casette.h"
#include "TapeSignal.h"
#include "romMapperCasette.h"
#include "MediaDb.h"
#include "RomLoader.h"
#include "JoystickPort.h"
#include "FileHistory.h"
#include "Utf8Conv.h"

#ifndef _WIN32
/* Non-Windows: paths are already UTF-8, so just copy. */
static void AnyToUtf8(const char* src, char* dst, int dstCap) {
    if (dstCap <= 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, dstCap - 1);
    dst[dstCap - 1] = 0;
}
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* Route fopen() through pkg_fopen so UTF-8 .cap paths (e.g. ROM names with
** Japanese characters) reach _wfopen instead of being mangled by the runtime
** ACP. Must come after <stdio.h>. */
#include "PacketFileSystem.h"

extern void PatchReset(BoardType boardType);

static int skipSync;
static int pendingInt;
static int boardType;
static Mixer* boardMixer = NULL;
static int (*syncToRealClock)(int, int) = NULL;
UInt32* boardSysTime;
static UInt64 boardSysTime64;
static UInt32 oldTime;
static UInt32 boardFreq = boardFrequency();
static int fdcTimingEnable = 1;
static int fdcActive       = 0;
static UInt32 fdcSectorCount = 0;   /* sectors accessed since the current boost session began */
static int hddSdBoostEnable = 0;
static int casBoostEnable   = 0;
static int casActive        = 0;
static BoardTimer* fdcTimer;
static BoardTimer* casTimer;
static BoardTimer* syncTimer;
static BoardTimer* mixerTimer;
static BoardTimer* stateTimer;
static BoardTimer* breakpointTimer;
static BoardDeviceInfo* boardDeviceInfo;
static Machine* boardMachine;
static BoardInfo boardInfo;
static UInt32 boardRamSize;
static UInt32 boardVramSize;
static int boardRunning = 0;

static HdType hdType[MAX_HD_COUNT];
  
static int     ramMaxStates;
static int     ramStateCur;
static int     ramStateCount;
static UInt32* ramStateTime;
static int     stateFrequency;
static int     enableSnapshots;
static int     useRom;
static int     useMegaRom;
static int     useMegaRam;
static int     useFmPac;
static RomType currentRomType[2];

static BoardType boardLoadState(void);
static void boardUpdateDisketteInfo();

/* Missing-file list populated by boardRun pre-validation; surfaced via
** boardGetMissingFile* in the failure dialog. */
#define MISSING_FILES_MAX 16
static char missingFiles[MISSING_FILES_MAX][512];
static int  missingFileCount = 0;

void boardClearMissingFiles(void) {
    missingFileCount = 0;
}

int boardGetMissingFileCount(void) {
    return missingFileCount;
}

const char* boardGetMissingFile(int idx) {
    if (idx < 0 || idx >= missingFileCount) return NULL;
    return missingFiles[idx];
}

void boardReportMissingFile(const char* file, const char* inZip) {
    int i;
    char entry[512];
    /* Convert the host ACP path to UTF-8 (older .cap/.sta stored paths
    ** in whatever ACP the saving host used). */
    char fileUtf8[260];
    char inZipUtf8[260];
    if (missingFileCount >= MISSING_FILES_MAX) return;
    AnyToUtf8(file ? file : "",   fileUtf8,  sizeof(fileUtf8));
    AnyToUtf8(inZip ? inZip : "", inZipUtf8, sizeof(inZipUtf8));
    if (inZipUtf8[0]) {
        sprintf_s(entry, sizeof(entry), "%s (in %s)", inZipUtf8, fileUtf8);
    } else {
        sprintf_s(entry, sizeof(entry), "%s", fileUtf8);
    }
    /* Dedupe: multi-slot machines can reference the same ROM more than
    ** once, and machineInitialize hits each slot separately. */
    for (i = 0; i < missingFileCount; i++) {
        if (0 == strcmp(missingFiles[i], entry)) {
            return;
        }
    }
    sprintf_s(missingFiles[missingFileCount], sizeof(missingFiles[0]), "%s", entry);
    missingFileCount++;
}

static void boardProbeMissingFile(const char* file, const char* inZip) {
    if (!file || *file == 0) return;
    /* fileExist(memberName, zipPath) for zip case, else plain path. */
    int exists;
    if (inZip && *inZip) {
        exists = fileExist((char*)inZip, (char*)file);
    } else {
        exists = fileExist((char*)file, NULL);
    }
    if (!exists) {
        boardReportMissingFile(file, inZip);
    }
}

/* Version stamped into states this build writes. Bumped from "v 8" to "v 10":
** the on-disk device serialization diverged from upstream 2.8.2 ("v 8") without
** a version bump, so "v 8" ambiguously covered two incompatible generations.
** Loading still accepts both "v 8" and "v 10" (see boardRun). */
static char saveStateVersion[32] = "blueMSX - state  v 10";

/* Set per load: non-zero when the state being loaded is the old (2.8.2 era)
** format, enabling the per-device old-format load fallbacks. */
static int boardLoadOldFormat = 0;

int boardStateLoadIsOldFormat(void)
{
    return boardLoadOldFormat;
}

static BoardTimerCb periodicCb;
static void*        periodicRef;
static UInt32       periodicInterval;
static BoardTimer*  periodicTimer;

void boardTimerCleanup();

#define HIRES_CYCLES_PER_LORES_CYCLE (UInt64)100000
#define boardFrequency64() (HIRES_CYCLES_PER_LORES_CYCLE * boardFrequency())


static void boardPeriodicCallback(void* ref, UInt32 time)
{
    if (periodicCb != NULL && periodicInterval > 0) {
        periodicCb(periodicRef, time);
        boardTimerAdd(periodicTimer, time + periodicInterval);
    }
    else {
        /* Re-arm so a mid-emulation boardSetPeriodicCallback install
        ** still gets dispatched; idle poll at 60 Hz. */
        UInt32 pollInterval = boardFrequency() / 60;
        if (pollInterval == 0) pollInterval = 1;
        boardTimerAdd(periodicTimer, time + pollInterval);
    }
}

//------------------------------------------------------
// Board supports one external periodic timer (can be
// used to sync external components with the emulation)
// The callback needs to be added before the emulation
// starts in order to be called.
//------------------------------------------------------
void boardSetPeriodicCallback(BoardTimerCb cb, void* ref, UInt32 freq)
{
    periodicCb       = cb;
    periodicRef      = ref;
    if (periodicCb != NULL && freq > 0) {
        periodicInterval = boardFrequency() / freq;
    }
}

//------------------------------------------------------
// Capture stuff
//------------------------------------------------------

#define CAPTURE_VERSION     3

typedef struct {
    UInt8  index;
    UInt8  value;
    UInt16 count;
} RleData;

static RleData* rleData;
static int      rleDataSize;
static int      rleIdx;
static int      rleRemaining;   /* reads left in the current entry (decode side) */
static UInt8    rleCache[256];

static void rleEncStartEncode(void* buffer, int length, int startOffset)
{
    rleIdx = startOffset - 1;
    rleDataSize = length / sizeof(RleData) - 1;
    rleData = (RleData*)buffer;

    if (startOffset == 0) {
        memset(rleCache, 0, sizeof(rleCache));
    }
}

static void rleEncAdd(UInt8 index, UInt8 value)
{
    /* count is UInt16: split the run before it wraps 0xffff->0, which would
    ** desync the whole stream on playback (count-- underflows to 0xffff). */
    if (rleIdx < 0 || rleCache[index] != value || rleData[rleIdx].count == 0
        || rleData[rleIdx].count == 0xffff) {
        rleIdx++;
        rleData[rleIdx].value = value;
        rleData[rleIdx].count = 1;
        rleData[rleIdx].index = index;
        rleCache[index] = value;
    }
    else {
        rleData[rleIdx].count++;
    }
}

static int rleEncGetLength()
{
    return rleIdx + 1;
}

static void rleEncStartDecode(void* encodedData, int encodedSize)
{
    rleIdx = 0;
    rleDataSize = encodedSize;
    rleData = (RleData*)encodedData;

    memset(rleCache, 0, sizeof(rleCache));

    if (encodedSize > 0) {
        rleRemaining = rleData[0].count;
        rleCache[rleData[0].index] = rleData[0].value;
    }
    else {
        rleRemaining = 0;
    }
}

/* Non-destructive: track remaining reads in rleRemaining instead of
** decrementing rleData[].count, so the input log survives playback intact
** (needed to append onto a replay without corrupting the played-back part). */
static UInt8 rleEncGet(UInt8 index)
{
    UInt8 value = rleCache[index];

    if (rleRemaining > 0) {
        rleRemaining--;
    }
    if (rleRemaining == 0) {
        rleIdx++;
        if (rleIdx < rleDataSize) {
            rleRemaining = rleData[rleIdx].count;
            rleCache[rleData[rleIdx].index] = rleData[rleIdx].value;
        }
    }

    return value;
}

static int rleEncEof()
{
    /* >= keeps the final slot as a guard: rleEncAdd writes rleData[rleIdx]
    ** *before* this is checked, so '>' let the last add run one entry past
    ** the buffer (OOB write on record, OOB read on the last playback step). */
    return rleIdx >= rleDataSize;
}


typedef enum 
{
    CAPTURE_IDLE = 0,
    CAPTURE_REC  = 1,
    CAPTURE_PLAY = 2,
} CaptureState;

typedef struct Capture {
    BoardTimer* timer;

    UInt8  initState[0x100000];
    int    initStateSize;
    UInt32 endTime;
    UInt64 endTime64;
    UInt64 startTime64;
    CaptureState state;
    UInt8  inputs[0x100000];
    int    inputCnt;
    char   filename[512];
    /* Set by boardCaptureStop when it finalizes a recording; drained by
    ** the UI thread (boardCaptureConsumePendingToast) so that record-end
    ** paths outside the menu Stop (emulatorStop on quit / Run>Stop / load
    ** state / RLE overflow) also surface a "Saved:" toast. */
    char   pendingToastFile[512];
} Capture;

static Capture cap;

/* Set while saving the replay's initial-state snapshot (cap.tmp). The snapshot
** must NOT embed a capture block: cap.state is REC during the deferred arm, so
** without this guard boardCaptureSaveState would write a CAPTURE_REC block into
** initState, and the .cap then loads as REC -> playback resumes recording. */
static int capSavingInitState = 0;

int boardCaptureHasData() {
    return cap.endTime != 0 || cap.endTime64 != 0 || boardCaptureIsRecording();
}

int boardCaptureIsRecording() {
    return cap.state == CAPTURE_REC;
}

int  boardCaptureIsPlaying() {
    return cap.state == CAPTURE_PLAY;
}

int boardCaptureCompleteAmount() {
    UInt64 length = (cap.endTime64 - cap.startTime64) / 1000;
    UInt64 current = (boardSysTime64 - cap.startTime64) / 1000;
    // Return complete if almost complete
    if (cap.endTime64 - boardSysTime64 < HIRES_CYCLES_PER_LORES_CYCLE * 100) {
        return 1000;
    }
    if (length == 0) {
        return 1000;
    }
    return (int)(1000 * current / length);
}

extern void actionEmuTogglePause();

/* Snapshot the initial state and begin RLE encoding; returns 1 if armed.
** Separate so the deferred (emu-was-stopped) start can arm without first
** dropping cap.state to IDLE, which raced the menu refresh (see boardTimerCb). */
static int boardCaptureArmRecording(void)
{
    FILE* f;

    cap.initStateSize = 0;
    capSavingInitState = 1;
    boardSaveState("cap.tmp", 1);
    capSavingInitState = 0;
    f = fopen("cap.tmp", "rb");
    if (f != NULL) {
        cap.initStateSize = (int)fread(cap.initState, 1, sizeof(cap.initState), f);
        fclose(f);
    }

    if (cap.initStateSize > 0) {
        rleEncStartEncode(cap.inputs, sizeof(cap.inputs), 0);
    }

    cap.startTime64 = boardSystemTime64();
    return cap.initStateSize > 0;
}

static void boardTimerCb(void* dummy, UInt32 time)
{
    if (cap.state == CAPTURE_PLAY) {
        /* Drive stop off the 64-bit clock so endTime32/endTime64 drift
        ** can't leave the timer spinning past the natural end; step from
        ** the actual remaining HIRES cycles for instruction-granular finish. */
        boardSystemTime64();   // sync 64-bit clock from r800

        if (boardSysTime64 >= cap.endTime64
            || cap.endTime64 - boardSysTime64 < HIRES_CYCLES_PER_LORES_CYCLE * 100) {
            actionEmuTogglePause();
            cap.state = CAPTURE_IDLE;
        }
        else {
            /* Fire at cap.endTime64 when it fits the 32-bit timer window;
            ** else step 0x40000000 (max safe under timeAnchor wrap math). */
            UInt64 remaining = (cap.endTime64 - boardSysTime64) / HIRES_CYCLES_PER_LORES_CYCLE;
            UInt32 step = (remaining < (UInt64)0x40000000) ? (UInt32)remaining : 0x40000000;
            if (step == 0) step = 1;   // forward progress safety net
            boardTimerAdd(cap.timer, time + step);
        }
    }
    
    if (cap.state == CAPTURE_REC) {
        /* Deferred arm: emu was stopped when the user hit Record, so the
        ** snapshot waited until emulation actually started. Keep cap.state
        ** at CAPTURE_REC throughout so a concurrent menu refresh sees it. */
        if (!boardCaptureArmRecording()) {
            cap.state = CAPTURE_IDLE;
        }
    }
}

/* Per-frame finish poll: cap.timer's ~50 emu sec resolution would let
** short replays overrun by tens of seconds. Returns 1 on transition to IDLE. */
int boardCaptureCheckFinish(void)
{
    if (cap.state != CAPTURE_PLAY) return 0;
    boardSystemTime64();   // sync 64-bit clock from r800
    if (boardCaptureCompleteAmount() < 1000) return 0;
    actionEmuTogglePause();
    cap.state = CAPTURE_IDLE;
    return 1;
}

void boardCaptureInit()
{
    cap.timer = boardTimerCreate(boardTimerCb, NULL);
    if (cap.state == CAPTURE_REC) {
        boardTimerAdd(cap.timer, boardSystemTime() + 1);
    }
}

void boardCaptureDestroy()
{
    boardCaptureStop();

    if (cap.timer != NULL) {
        boardTimerDestroy(cap.timer);
        cap.timer = NULL;
    }
    cap.state = CAPTURE_IDLE;
}

void boardCaptureStart(const char* filename) {
    if (cap.state == CAPTURE_REC) {
        return;
    }

    /* Append: truncate the in-flight RLE entry to the played count, then resume
    ** encoding past it. cap.initState was snapshotted at load (see PLAY branch
    ** of boardCaptureLoadState) so boardCaptureStop can save a merged .cap. */
    if (cap.state == CAPTURE_PLAY) {
        if (rleIdx < rleDataSize && rleRemaining > 0) {
            rleData[rleIdx].count -= (UInt16)rleRemaining;
            if (rleData[rleIdx].count == 0) {
                rleIdx--;
            }
            rleRemaining = 0;
        }
        rleEncStartEncode(cap.inputs, sizeof(cap.inputs), rleIdx + 1);
        boardTimerRemove(cap.timer);
        strcpy(cap.filename, filename);
        cap.state = CAPTURE_REC;
        return;
    }

    strcpy(cap.filename, filename);

    // If emulation is not running we want to start recording once
    // the emulation is started
    if (cap.timer == NULL) {
        cap.state = CAPTURE_REC;
        return;
    }

    if (boardCaptureArmRecording()) {
        cap.state = CAPTURE_REC;
    }
}

void boardCaptureStop() {
    boardTimerRemove(cap.timer);

    if (cap.state == CAPTURE_REC) {
        SaveState* state;
        FILE* f;

        cap.endTime = boardSystemTime();
        cap.endTime64 = boardSystemTime64();
        cap.state = CAPTURE_PLAY;
        cap.inputCnt = rleEncGetLength();

        f = fopen(cap.filename, "wb");
        if (f != NULL) {
            fwrite(cap.initState, 1, cap.initStateSize, f);
            fclose(f);
        }

        saveStateCreateForWrite(cap.filename);

        state = saveStateOpenForWrite("capture");

        saveStateSet(state, "version", CAPTURE_VERSION);

        saveStateSet(state, "state", cap.state);
        saveStateSet(state, "endTime", cap.endTime);
        saveStateSet(state, "endTime64Hi", (UInt32)(cap.endTime64 >> 32));
        saveStateSet(state, "endTime64Lo", (UInt32)cap.endTime64);
        saveStateSet(state, "startTime64Hi", (UInt32)(cap.startTime64 >> 32));
        saveStateSet(state, "startTime64Lo", (UInt32)cap.startTime64);
        saveStateSet(state, "inputCnt", cap.inputCnt);
        
        if (cap.inputCnt > 0) {
            saveStateSetBuffer(state, "inputs", cap.inputs, cap.inputCnt * sizeof(RleData));
        }

        saveStateClose(state);
        saveStateDestroy();

        /* Queue completion toast for the UI thread. Only for CAPTURE_REC
        ** (a Play-mode stop takes the outer no-op path and leaves this
        ** clear), so replay playback finish never triggers a toast. */
        if (cap.filename[0]) {
            strncpy(cap.pendingToastFile, cap.filename, sizeof(cap.pendingToastFile) - 1);
            cap.pendingToastFile[sizeof(cap.pendingToastFile) - 1] = 0;
        }
    }

    // go back to idle state
    cap.state = CAPTURE_IDLE;
}

int boardCaptureConsumePendingToast(char* out, int outSize) {
    if (out == NULL || outSize < 1) return 0;
    if (cap.pendingToastFile[0] == 0) return 0;
    strncpy(out, cap.pendingToastFile, outSize - 1);
    out[outSize - 1] = 0;
    cap.pendingToastFile[0] = 0;
    return 1;
}

UInt8 boardCaptureUInt8(UInt8 logId, UInt8 value) {
    if (cap.state == CAPTURE_REC) {
        rleEncAdd(logId, value);
        if (rleEncEof()) {
            boardCaptureStop();
        }
    }
    if (cap.state == CAPTURE_PLAY) {
        if (!rleEncEof()) {
            value= rleEncGet(logId);
        }
    }
    return value;
}

static void boardCaptureSaveState()
{
    if (cap.state == CAPTURE_REC && !capSavingInitState) {
        SaveState* state = saveStateOpenForWrite("capture");

        cap.inputCnt = rleEncGetLength();

        saveStateSet(state, "version", CAPTURE_VERSION);

        saveStateSet(state, "state", cap.state);
        saveStateSet(state, "endTime", cap.endTime);
        saveStateSet(state, "endTime64Hi", (UInt32)(cap.endTime64 >> 32));
        saveStateSet(state, "endTime64Lo", (UInt32)cap.endTime64);
        saveStateSet(state, "startTime64Hi", (UInt32)(cap.startTime64 >> 32));
        saveStateSet(state, "startTime64Lo", (UInt32)cap.startTime64);
        saveStateSet(state, "inputCnt", cap.inputCnt);
        if (cap.inputCnt > 0) {
            saveStateSetBuffer(state, "inputs", cap.inputs, cap.inputCnt * sizeof(RleData));
        }
        saveStateSet(state, "initStateSize", cap.initStateSize);
        if (cap.initStateSize > 0) {
            saveStateSetBuffer(state, "initState", cap.initState, cap.initStateSize);
        }
        
        saveStateSetBuffer(state, "rleCache", rleCache, sizeof(rleCache));

        saveStateClose(state);
    }
}

static void boardCaptureLoadState()
{
    int version;

    SaveState* state = saveStateOpenForRead("capture");

    version = saveStateGet(state, "version", 0);

    cap.state = saveStateGet(state, "state", CAPTURE_IDLE);
    cap.endTime = saveStateGet(state, "endTime", 0);
    cap.endTime64 = (UInt64)saveStateGet(state, "endTime64Hi", 0) << 32 |
                    (UInt64)saveStateGet(state, "endTime64Lo", 0);
    {
        /* New format persists startTime64; old format falls back to current
        ** boardSystemTime64 (bounded to this playback's own time scale). */
        UInt64 startT = (UInt64)saveStateGet(state, "startTime64Hi", 0) << 32 |
                        (UInt64)saveStateGet(state, "startTime64Lo", 0);
        cap.startTime64 = (startT != 0) ? startT : boardSystemTime64();
    }
    cap.inputCnt = saveStateGet(state, "inputCnt", 0);
    /* Reject a corrupt or oversized log rather than overflowing cap.inputs. */
    if (cap.inputCnt < 0 ||
        (size_t)cap.inputCnt > sizeof(cap.inputs) / sizeof(RleData)) {
        cap.state = CAPTURE_IDLE;
        saveStateClose(state);
        return;
    }
    if (cap.inputCnt > 0) {
        saveStateGetBuffer(state, "inputs", cap.inputs, cap.inputCnt * sizeof(RleData));
    }
    cap.initStateSize = saveStateGet(state, "initStateSize", 0);
    if (cap.initStateSize > 0) {
        saveStateGetBuffer(state, "initState", cap.initState, cap.initStateSize);
    }
        
    saveStateGetBuffer(state, "rleCache", rleCache, sizeof(rleCache));

    saveStateClose(state);

    if (version != CAPTURE_VERSION) {
        cap.state = CAPTURE_IDLE;
        return;
    }

    if (cap.state == CAPTURE_PLAY) {
        rleEncStartDecode(cap.inputs, cap.inputCnt);

        while (cap.endTime - boardSystemTime() > 0x40000000 || cap.endTime == boardSystemTime()) {
            cap.endTime -= 0x40000000;
        }
        boardTimerAdd(cap.timer, cap.endTime);

        /* Snapshot the just-restored machine state for a later PLAY->REC append.
        ** A normal .cap's capture_00 has no initState, so initStateSize==0 here;
        ** savestate-during-recording carries its own initState — skip in that case. */
        if (cap.initStateSize == 0) {
            FILE* f;
            capSavingInitState = 1;
            boardSaveState("cap.tmp", 1);
            capSavingInitState = 0;
            f = fopen("cap.tmp", "rb");
            if (f != NULL) {
                cap.initStateSize = (int)fread(cap.initState, 1, sizeof(cap.initState), f);
                fclose(f);
            }
        }
    }
    
    if (cap.state == CAPTURE_REC) {
        rleEncStartEncode(cap.inputs, sizeof(cap.inputs), cap.inputCnt);
    }
}

//------------------------------------------------------


int boardGetNoSpriteLimits() {
    return vdpGetNoSpritesLimit();
}

void boardSetNoSpriteLimits(int enable) {
    vdpSetNoSpriteLimits(enable);
}

int boardGetVdpCmdSpeed() {
    return vdpCmdGetWaitPct();
}

void boardSetVdpCmdSpeed(int percent) {
    vdpCmdSetWaitPct(percent);
}

RomType boardGetRomType(int cartNo)
{
    return currentRomType[cartNo];
}

int boardGetFdcTimingEnable() {
    return fdcTimingEnable;
}

/* True while the FDC/HDD access boost is engaged. Used by the emulator core
** to decouple the display during the boost (same as user max-speed) so the
** per-frame present is not on the fast-forward critical path. */
int boardGetFdcActive(void) {
    return fdcActive;
}

void boardSetFdcTimingEnable(int enable) {
    fdcTimingEnable = enable;
}

/* Boost-release idle tail (ms emu time): scales with the boost
** session's sector count, clamped to [MIN, MAX]. Short reads keep
** it tight so animations don't visibly accelerate; long loads need
** a wider tail to span between-sector CPU work. */
#define FDC_TAIL_MIN_MS         200
#define FDC_TAIL_MAX_MS        1000
#define FDC_TAIL_PER_SECTOR_MS   20

static void fdcScheduleTail(void) {
    UInt32 tail;
    if (!fdcActive) fdcSectorCount = 0;
    fdcSectorCount++;
    tail = (UInt32)FDC_TAIL_PER_SECTOR_MS * fdcSectorCount;
    if (tail < FDC_TAIL_MIN_MS) tail = FDC_TAIL_MIN_MS;
    if (tail > FDC_TAIL_MAX_MS) tail = FDC_TAIL_MAX_MS;
    boardTimerAdd(fdcTimer, boardSystemTime() + (UInt32)((UInt64)tail * boardFrequency() / 1000));
    fdcActive = 1;
}

/* End the current boost session: clear the flag and cancel the
** pending release timer to keep "active iff timer scheduled". Both boosts go
** down together: the kill list fires on the sound a game makes once its load
** has finished, and neither should outlive that. */
static void fdcKillBoost(void) {
    fdcActive = 0;
    boardTimerRemove(fdcTimer);
    casActive = 0;
    boardTimerRemove(casTimer);
}

void boardSetFdcActive() {
    if (!fdcTimingEnable) {
        fdcScheduleTail();
    }
}

/* HDD/SD boost: same mechanism as FDC, gated separately; shares the
** FDC audio-write kill-list (boardCheckFdcBoostKill). */
void boardSetHddSdActive() {
    if (hddSdBoostEnable) {
        fdcScheduleTail();
    }
}

void boardSetHddSdBoostEnable(int enable) {
    hddSdBoostEnable = enable;
}

int boardGetHddSdBoostEnable(void) {
    return hddSdBoostEnable;
}

/* Cassette boost: its own timer, because the tape wants the opposite of the
** disk's guesswork. A tape read is an exact signal that the machine is doing
** nothing but wait, so the tail only has to bridge the arming interval rather
** than cover unrelated work after the load. */
#define CAS_TAIL_MS 50

static void onCasDone(void* ref, UInt32 time)
{
    casActive = 0;
}

void boardSetCasActive(void) {
    if (casBoostEnable) {
        boardTimerAdd(casTimer, boardSystemTime() + (UInt32)((UInt64)CAS_TAIL_MS * boardFrequency() / 1000));
        casActive = 1;
    }
}

int boardGetCasActive(void) {
    return casActive;
}

void boardSetCasBoostEnable(int enable) {
    casBoostEnable = enable;
}

/* PSG channel ch (0=A,1=B,2=C) produces an audible AC signal only if
** its tone or noise is enabled in mixer R7 (bit set = disabled): with
** both off, a fixed level is just silent DC (see AY8910.c). */
static int psgChannelAudibleViaMixer(UInt8 r7, int ch) {
    return !(r7 & (1 << ch)) || !(r7 & (1 << (ch + 3)));
}

/* Drop the FDC boost on melodic sound-chip writes that cause the
** audible "BGM at double speed" tail. Excluded: VDP VRAM (sector
** streaming), PPI (VBLANK keyboard scan), PSG R15. Included: VDP
** palette R0x9A (fades), key-on / volume / TL / pitch writes on
** YM2413, Y8950, OPL3, OPL4 FM+wave, Turbo-R PCM, OPL4 mix. */
void boardCheckFdcBoostKill(UInt16 port, UInt8 value) {
    static UInt8  ym2413LatchedReg = 0;
    static UInt8  y8950LatchedReg  = 0;
    static UInt16 ymf262LatchedReg = 0;     /* low byte = reg, 0x100 = bank 1 */
    static UInt8  ymf278LatchedReg = 0;
    static UInt8  psgLatchedReg    = 0;
    static UInt8  psgReg7          = 0xff;  /* PSG mixer R7 (1 = ch disabled) */
    static UInt8  psgVol[3]        = { 0, 0, 0 }; /* PSG R8-R10 ch volumes */
    static UInt8  pcmStatus        = 0;     /* Turbo-R PCM status (port 0xA5), low 5 bits */
    static UInt32 ymf278KeyOn      = 0;     /* wave key-on bits, ch 0-23 */
    UInt8 p = (UInt8)(port & 0xff);

    /* Address latches: keep in sync regardless of boost state. */
    if (p == 0x7c) { ym2413LatchedReg = value;           return; }
    if (p == 0xc0) { y8950LatchedReg  = value;           return; }
    if (p == 0xc4) { ymf262LatchedReg = value;           return; }
    if (p == 0xc6) { ymf262LatchedReg = value | 0x100;   return; }
    if (p == 0x7e) { ymf278LatchedReg = value;           return; }
    if (p == 0xa0) { psgLatchedReg    = value & 0x0f;    return; }

    /* PSG data-register value latches: track the mixer (R7) and per-channel
    ** volumes (R8-R10) regardless of boost state so the audibility test below
    ** stays in sync with the chip across boost on/off transitions. */
    if (p == 0xa1) {
        if (psgLatchedReg == 7)                             psgReg7 = value;
        else if (psgLatchedReg >= 8 && psgLatchedReg <= 10) psgVol[psgLatchedReg - 8] = value;
    }
    /* Turbo-R PCM status (port 0xA5): low 5 bits matter (see romMapperTurboRPcm
    ** write handler). Tracked regardless of boost state so the 0xA4 sample
    ** audibility test stays correct across boost on/off transitions. */
    if (p == 0xa5) pcmStatus = value & 0x1f;
    /* YMF278 wave key-on bits (regs 0x68-0x7F, bit 7): tracked regardless of
    ** boost state so the wave audibility tests below stay in sync with the
    ** chip across boost on/off transitions. */
    if (p == 0x7f && ymf278LatchedReg >= 0x68 && ymf278LatchedReg <= 0x7f) {
        UInt32 bit = 1ul << (ymf278LatchedReg - 0x68);
        if (value & 0x80) ymf278KeyOn |= bit;
        else              ymf278KeyOn &= ~bit;
    }

    if (!fdcActive && !casActive) return;

    if (p == 0x9a) {                                    /* VDP palette data (V9938+) */
        /* A palette write during a load is the signature of a visible fade.
        ** Drop the boost so the fade animates at real speed; the next FDC
        ** access re-engages the boost if the load continues. */
        fdcKillBoost();
        return;
    }
    if (p == 0xa5) {                                    /* Turbo-R PCM status */
        /* Bit 1 = mixer enable. Kill on the un-mute itself, not on the
        ** first 0xA4 sample after it, so no PCM frame slips out at
        ** fast-forward. */
        if (value & 0x02) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0xa4) {                                    /* Turbo-R PCM sample */
        /* Sample reaches the DAC only when status bit 1 is set. Skip 0x80
        ** (DAC mid-level = silence) so an explicit-silence write is not a
        ** false positive. */
        if ((pcmStatus & 0x02) && value != 0x80) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0x7d) {                                    /* YM2413 data */
        if ((ym2413LatchedReg >= 0x20 && ym2413LatchedReg <= 0x28 && (value & 0x10)) ||
            (ym2413LatchedReg >= 0x30 && ym2413LatchedReg <= 0x38 && (value & 0x0f) != 0x0f) ||
            (ym2413LatchedReg == 0x0e && (value & 0x1f))) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0xc1) {                                    /* Y8950 data */
        if ((y8950LatchedReg >= 0xb0 && y8950LatchedReg <= 0xb8 && (value & 0x20)) ||
            (y8950LatchedReg >= 0x40 && y8950LatchedReg <= 0x55 && (value & 0x3f) != 0x3f) ||
            (y8950LatchedReg == 0xbd && (value & 0x1f)) ||
            (y8950LatchedReg == 0x07 && (value & 0x80) && !(value & 0x40))) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0xc5 || p == 0xc7) {                       /* YMF262 data, either bank */
        UInt8 reg   = (UInt8)(ymf262LatchedReg & 0xff);
        int   bank1 = (ymf262LatchedReg & 0x100) != 0;
        if ((reg >= 0xb0 && reg <= 0xb8 && (value & 0x20)) ||
            (reg >= 0x40 && reg <= 0x55 && (value & 0x3f) != 0x3f) ||
            (!bank1 && reg == 0xbd && (value & 0x1f))) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0x7f) {                                    /* YMF278 data */
        UInt8 r = ymf278LatchedReg;
        if (r >= 0x68 && r <= 0x7f && (value & 0xc0) == 0x80) {
            fdcKillBoost();                             /* key on, damp off */
            return;
        }
        /* Sounds on a keyed-on channel without a key-on write: wave number
        ** (0x08, retrigger), pitch (0x20/0x38, slides), TL in bits 7:1 of
        ** 0x50 (fades; mute 0x7F excluded, bit 0 is the LD flag). */
        if (r >= 0x08 && r <= 0x67 &&
            (ymf278KeyOn & (1ul << ((UInt8)(r - 0x08) % 24)))) {
            if (r < 0x50 || (value & 0xfe) != 0xfe) {
                fdcKillBoost();
            }
            return;
        }
        /* Wave mix control (F9h): a master fade on held wave notes. The FM
        ** mix (F8h) cannot be gated on the wave key-on mask, so it is left
        ** to the YMF262 key-on / TL tests above. */
        if (r == 0xf9 && ymf278KeyOn) {
            fdcKillBoost();
        }
        return;
    }
    if (p == 0xa1) {                                    /* PSG data */
        /* Volume registers 8-10. Fixed level (bit 4 clear) is audible only
        ** when the channel's tone or noise is enabled in mixer R7 -- a level
        ** written to a fully-disabled channel just sets a DC offset (common
        ** false positive: drivers poke levels on silenced channels).
        ** Envelope mode (bit 4) plays through even with tone+noise off, so
        ** it always counts. R11-15 (env period/shape, keyboard scan) and
        ** R0-6 (tone / noise period, audible only after volume) are skipped. */
        if (psgLatchedReg >= 8 && psgLatchedReg <= 10) {
            int ch = psgLatchedReg - 8;
            if ((value & 0x10) ||
                ((value & 0x0f) && psgChannelAudibleViaMixer(psgReg7, ch))) {
                fdcKillBoost();
            }
        }
        /* R7 (mixer) write: catch the "set level first, enable channel later"
        ** ordering. If un-muting a channel now makes a fixed-level channel
        ** audible, drop the boost; envelope-mode was caught at volume write. */
        else if (psgLatchedReg == 7) {
            int ch;
            for (ch = 0; ch < 3; ch++) {
                if (psgChannelAudibleViaMixer(value, ch) &&
                    (psgVol[ch] & 0x0f) && !(psgVol[ch] & 0x10)) {
                    fdcKillBoost();
                    break;
                }
            }
        }
        return;
    }
}

void boardCheckSccBoostKill(UInt8 address, UInt8 value)
{
    if (!fdcActive) return;
    /* address has been masked to low 4 bits by sccUpdateFreqAndVol's
    ** dispatch. 0x0a-0x0e = per-channel volume (lower 4 bits = level
    ** 0-15); 0x0f = channel enable bitmask (bit 0-4 = ch A-E). */
    if ((address >= 0x0a && address <= 0x0e && (value & 0x0f)) ||
        (address == 0x0f && (value & 0x1f))) {
        fdcKillBoost();
    }
}

void boardSetBreakpoint(UInt16 address) {
    if (boardRunning) {
        boardInfo.setBreakpoint(boardInfo.cpuRef, address);
    }
}

void boardClearBreakpoint(UInt16 address) {
    if (boardRunning) {
        boardInfo.clearBreakpoint(boardInfo.cpuRef, address);
    }
}

static void onFdcDone(void* ref, UInt32 time)
{
    fdcActive = 0;
}

static void doSync(UInt32 time, int breakpointHit)
{
    int execTime = 10;
    if (!skipSync) {
        execTime = syncToRealClock(casActive ? BOARD_BOOST_TAPE :
                                   fdcActive ? BOARD_BOOST_DISK : BOARD_BOOST_NONE,
                                   breakpointHit);
    }
    if (execTime == -99) {
        boardInfo.stop(boardInfo.cpuRef);
        return;
    }

    boardSystemTime64();

    if (execTime == 0) {
        boardTimerAdd(syncTimer, boardSystemTime() + 1);
    }
    else if (execTime < 0) {
        execTime = -execTime;
        boardTimerAdd(syncTimer, boardSystemTime() + (UInt32)((UInt64)execTime * boardFreq / 1000));
    }
    else {
        boardTimerAdd(syncTimer, time + (UInt32)((UInt64)execTime * boardFreq / 1000));
    }
}

static void onMixerSync(void* ref, UInt32 time)
{
    mixerSync(boardMixer);

    boardTimerAdd(mixerTimer, boardSystemTime() + boardFrequency() / 50);
}

static void onStateSync(void* ref, UInt32 time)
{    
    if (enableSnapshots) {
        char memFilename[16];
        ramStateCur = (ramStateCur + 1) % ramMaxStates;
        if (ramStateCount < ramMaxStates) {
            ramStateCount++;
        }

        sprintf(memFilename, "mem%d", ramStateCur);
        
        /* What the clock will read once this snapshot is restored; step back
        ** uses it to pick a snapshot instead of restoring them to find out. */
        ramStateTime[ramStateCur] = boardSystemTime();

        boardSaveState(memFilename, 0);
    }

    boardTimerAdd(stateTimer, boardSystemTime() + stateFrequency);
}

static void onSync(void* ref, UInt32 time)
{
    doSync(time, 0);
}

void boardOnBreakpoint(UInt16 pc)
{
    doSync(boardSystemTime(), 1);
}

int boardInsertExternalDevices()
{
    int i;
    for (i = 0; i < 2; i++) {
        if (boardDeviceInfo->carts[i].inserted) {
            boardChangeCartridge(i, boardDeviceInfo->carts[i].type, 
                                 boardDeviceInfo->carts[i].name,
                                 boardDeviceInfo->carts[i].inZipName);
        }
    }

    for (i = 0; i < MAXDRIVES; i++) {
        if (boardDeviceInfo->disks[i].inserted) {
            boardChangeDiskette(i, boardDeviceInfo->disks[i].name,
                                boardDeviceInfo->disks[i].inZipName);
        }
    }

    if (boardDeviceInfo->tapes[0].inserted) {
        boardChangeCassette(0, boardDeviceInfo->tapes[0].name,
                            boardDeviceInfo->tapes[0].inZipName);
    }
    return 1;
}

int boardRemoveExternalDevices()
{
     boardChangeDiskette(0, NULL, NULL);
     boardChangeDiskette(1, NULL, NULL);

     boardChangeCassette(0, NULL, NULL);

     return 1;
}

static void onBreakpointSync(void* ref, UInt32 time) {
    skipSync = 0;
    doSync(time, 1);
}

/* Discard the newest snapshot without restoring it. */
static int boardRewindDrop()
{
    if (ramStateCount < 2) {
        return 0;
    }
    ramStateCount--;
    ramStateCur = (ramStateCur + ramMaxStates - 1) % ramMaxStates;
    return 1;
}

int boardRewindOne() {
    UInt32 rewindTime;
    int skip;
    if (stateFrequency <= 0) {
        return 0;
    }
    rewindTime = boardInfo.getTimeTrace(1);
    if (rewindTime == 0 || ramStateCount < 2) {
        return 0;
    }
    /* The trace only advances when PC changes, so halt and block instructions
    ** can leave the target several snapshots old. Picking the one to land on
    ** from the recorded times costs one restore instead of one per snapshot. */
    for (skip = 0; skip <= ramStateCount - 2; skip++) {
        int slot = (ramStateCur + ramMaxStates - skip) % ramMaxStates;
        if ((Int32)(ramStateTime[slot] - rewindTime) < 0) {
            break;
        }
    }
    /* Nothing on the ring is old enough, so take the newest one rather than
    ** spend the whole history on a single step back. */
    if (skip > ramStateCount - 2) {
        skip = 0;
    }
    while (skip-- > 0) {
        boardRewindDrop();
    }
    if (!boardRewind()) {
        return 0;
    }
    /* Holds whenever the target predates the whole ring. boardTimerAdd drops a
    ** timer that has expired, which would leave skipSync set with nothing to
    ** clear it, so stop where we landed instead. */
    if ((Int32)(rewindTime - boardSystemTime()) <= 0) {
        rewindTime = boardSystemTime();
    }
    boardTimerAdd(breakpointTimer, rewindTime);
    skipSync = 1;
    return 1;
}

int boardRewind()
{
    char stateFile[16];

    sprintf(stateFile, "mem%d", ramStateCur);
    if (!boardRewindDrop()) {
        return 0;
    }

    boardTimerCleanup();
    /* The cleanup drops fdcTimer without ever running onFdcDone, so the boost
    ** would stay engaged with no timer to release it. */
    fdcKillBoost();

    saveStateCreateForRead(stateFile);

//    boardType = boardLoadState();
//    machineLoadState(boardMachine);

    /* boardInfo.loadState clobbers boardSysTime64 (rebuilt from r800's
    ** 32-bit systemTime).  This is the only "board" section open in the
    ** rewind path so getIndexedFilename returns "board_00"; stash and
    ** restore the saved 64-bit value around the call. */
    {
        SaveState* bs = saveStateOpenForRead("board");
        UInt64 stashedTime = (UInt64)saveStateGet(bs, "boardSysTime64Hi", 0) << 32
                           | (UInt64)saveStateGet(bs, "boardSysTime64Lo", 0);
        saveStateClose(bs);
        boardInfo.loadState();
        if (stashedTime != 0) boardSysTime64 = stashedTime;
    }
    /* The tape clock is anchored to boardSysTime64, which just went backwards.
    ** Without this the next update underflows and seeks to the end of the tape. */
    tapeSignalReset();
    /* boardSystemTime64 accumulates from oldTime, so it has to follow the clock
    ** back too. Left alone the next call adds a wrapped UInt32 delta. */
    oldTime = boardSystemTime();
    boardCaptureLoadState();

#if 1
    if (stateFrequency > 0) {
        boardTimerAdd(stateTimer, boardSystemTime() + stateFrequency);
    }
    //boardTimerAdd(syncTimer, boardSystemTime() + 1);
    boardTimerAdd(mixerTimer, boardSystemTime() + boardFrequency() / 50);
    
    if (boardPeriodicCallback != NULL) {
        boardTimerAdd(periodicTimer, boardSystemTime() + periodicInterval);
    }
#endif
    return 1;
}

void boardEnableSnapshots(int enable)
{
    enableSnapshots = enable;
}

int boardRun(Machine* machine, 
             BoardDeviceInfo* deviceInfo,
             Mixer* mixer,
             char* stateFile,
             int frequency,
             int reversePeriod,
             int reverseBufferCnt,
             int (*syncCallback)(int, int))
{
    int loadState = 0;
    int success = 0;
    boardLoadOldFormat = 0;
    /* Stash boardSysTime64 across msxCreate / boardInfo.loadState since
    ** boardInit clobbers it; reapply after all init runs. */
    UInt64 stashedSysTime64 = 0;

    syncToRealClock = syncCallback;

    videoManagerReset();
    debugDeviceManagerReset();

    boardMixer      = mixer;
    boardDeviceInfo = deviceInfo;
    boardMachine    = machine;

    boardUpdateDisketteInfo();

    /* Shared list: state pre-validation and machineInitialize both feed it. */
    boardClearMissingFiles();

    if (stateFile != NULL) {
        int   size;
        char *version;

        saveStateCreateForRead(stateFile);

        version = zipLoadFile(stateFile, "version", &size);
        if (version != NULL) {
            /* Accept both the current "v 10" and the legacy "v 8" generation. */
            if (0 == strncmp(version, "blueMSX - state  v 10", 21) ||
                0 == strncmp(version, "blueMSX - state  v 8",  20)) {
                loadState = 1;
                boardLoadOldFormat = saveStateFileFormatIsOld(stateFile);

                boardType = boardLoadState();
                stashedSysTime64 = boardSysTime64;

                machineLoadState(boardMachine);
            }
            free(version);
        }
    }

    /* Pre-validate state-referenced files so missing paths surface as a
    ** dialog instead of silently broken slots / failed BIOS reads. */
    if (loadState) {
        int i;
        if (deviceInfo != NULL) {
            for (i = 0; i < 2; i++) {
                /* Special Carts (MEGA-SCSI, MFR SCC+ SD, ExtraRAM, ...) use
                ** a fixed marker in .name and have no real ROM path; skip. */
                if (deviceInfo->carts[i].inserted &&
                    !propertiesIsSpecialCartName(deviceInfo->carts[i].name)) {
                    boardProbeMissingFile(deviceInfo->carts[i].name,
                                          deviceInfo->carts[i].inZipName);
                }
            }
            for (i = 0; i < MAXDRIVES; i++) {
                if (deviceInfo->disks[i].inserted) {
                    boardProbeMissingFile(deviceInfo->disks[i].name,
                                          deviceInfo->disks[i].inZipName);
                }
            }
            if (deviceInfo->tapes[0].inserted) {
                boardProbeMissingFile(deviceInfo->tapes[0].name,
                                      deviceInfo->tapes[0].inZipName);
            }
        }
        if (machine != NULL) {
            for (i = 0; i < machine->slotInfoCount; i++) {
                /* Skip slotInfo entries with no ROM file (RAM/CMOS/etc). */
                if (machine->slotInfo[i].name[0] != 0) {
                    boardProbeMissingFile(machine->slotInfo[i].name,
                                          machine->slotInfo[i].inZipName);
                }
            }
        }
        if (boardGetMissingFileCount() > 0) {
            saveStateDestroy();
            return 0;   // emulator.c surfaces the list via archEmulationStartFailure
        }
    }

    boardType = machine->board.type;
    PatchReset(boardType);

#if 0
    useRom     = 0;
    useMegaRom = 0;
    useMegaRam = 0;
    useFmPac   = 0;
    currentRomType[0] = ROM_UNKNOWN;
    currentRomType[1] = ROM_UNKNOWN;
#endif

    pendingInt = 0;

    boardSetFrequency(frequency);

    memset(&boardInfo, 0, sizeof(boardInfo));

    boardRunning = 1;
    switch (boardType) {
    case BOARD_MSX:
    case BOARD_MSX_S3527:
    case BOARD_MSX_S1985:
    case BOARD_MSX_T9769B:
    case BOARD_MSX_T9769C:
    case BOARD_MSX_FORTE_II:
        success = msxCreate(machine, deviceInfo->video.vdpSyncMode, &boardInfo);
        break;
    case BOARD_SVI:
        success = sviCreate(machine, deviceInfo->video.vdpSyncMode, &boardInfo);
        break;
    case BOARD_COLECO:
        success = colecoCreate(machine, deviceInfo->video.vdpSyncMode, &boardInfo);
        break;
    case BOARD_COLECOADAM:
        success = adamCreate(machine, deviceInfo->video.vdpSyncMode, &boardInfo);
        break;
    case BOARD_SG1000:
    case BOARD_SC3000:
    case BOARD_SF7000:
        success = sg1000Create(machine, deviceInfo->video.vdpSyncMode, &boardInfo);
        break;
    default:
        success = 0;
    }
    
    boardCaptureInit();

    if (success && loadState) {
        boardInfo.loadState();
        /* Re-apply the stashed boardSysTime64 (boardInit clobbered it). The
        ** tape clock is anchored to it, so it has to be re-anchored too. */
        if (stashedSysTime64 != 0) boardSysTime64 = stashedSysTime64;
        tapeSignalReset();
        boardCaptureLoadState();
    }

    if (stateFile != NULL) {
        saveStateDestroy();
    }

    if (success) {
        /* fdcActive is module-static and survives across boardRun cycles
        ** (hard reset = stop + start). If a previous run left it engaged
        ** the old fdcTimer was destroyed without firing onFdcDone, so force
        ** a fresh boost-off state before scheduling new timers. */
        fdcActive = 0;
        casActive = 0;
        syncTimer = boardTimerCreate(onSync, NULL);
        fdcTimer = boardTimerCreate(onFdcDone, NULL);
        casTimer = boardTimerCreate(onCasDone, NULL);
        mixerTimer = boardTimerCreate(onMixerSync, NULL);
        
        stateFrequency = boardFrequency() / 1000 * reversePeriod;

        /* Outside the test below on purpose: with reverse off there is no ram
        ** file system at all, and a count left over from the previous run would
        ** let boardRewind load a memN that no longer exists. */
        ramStateCur   = 0;
        ramStateCount = 0;

        if (stateFrequency > 0) {
            ramMaxStates = reverseBufferCnt;
            memZipFileSystemCreate(ramMaxStates);
            ramStateTime = calloc(ramMaxStates, sizeof(UInt32));
            stateTimer = boardTimerCreate(onStateSync, NULL);
            breakpointTimer = boardTimerCreate(onBreakpointSync, NULL); 
            boardTimerAdd(stateTimer, boardSystemTime() + stateFrequency);
        }
        else {
            stateTimer = NULL;
            breakpointTimer = NULL;
        }

        boardTimerAdd(syncTimer, boardSystemTime() + 1);
        boardTimerAdd(mixerTimer, boardSystemTime() + boardFrequency() / 50);
        
        if (boardPeriodicCallback != NULL) {
            periodicTimer = boardTimerCreate(boardPeriodicCallback, periodicRef);
            boardTimerAdd(periodicTimer, boardSystemTime() + periodicInterval);
        }

        if (!skipSync) {
            syncToRealClock(0, 0);
        }

        boardInfo.run(boardInfo.cpuRef);

        if (periodicTimer != NULL) {
            boardTimerDestroy(periodicTimer);
            periodicTimer = NULL;
        }

        boardCaptureDestroy();

        boardInfo.destroy();

        /* Null each pointer after destroy. breakpointTimer is created only
        ** when reverse is enabled (stateFrequency > 0); a later reverse-off
        ** run skips the re-create, so a stale (freed) pointer here would be
        ** double-freed at the next teardown -> heap corruption / crash. */
        boardTimerDestroy(fdcTimer);   fdcTimer = NULL;
        boardTimerDestroy(casTimer);   casTimer = NULL;
        boardTimerDestroy(syncTimer);  syncTimer = NULL;
        boardTimerDestroy(mixerTimer); mixerTimer = NULL;
        if (breakpointTimer != NULL) {
            boardTimerDestroy(breakpointTimer);
            breakpointTimer = NULL;
        }
        if (stateTimer != NULL) {
            boardTimerDestroy(stateTimer);
            stateTimer = NULL;
            memZipFileSystemDestroy();
            free(ramStateTime);
            ramStateTime = NULL;
        }
    }
    else {
        boardCaptureStop();
    }

    boardRunning = 0;

    return success;
}

BoardType boardGetType()
{
    return boardType & BOARD_MASK;
}

Mixer* boardGetMixer()
{
    return boardMixer;
}

void boardSetMachine(Machine* machine)
{
    int i;
    int hdIndex = FIRST_INTERNAL_HD_INDEX;

    // Update HD info
    for (i = FIRST_INTERNAL_HD_INDEX; i < MAX_HD_COUNT; i++) {
        hdType[i] = HD_NONE;
    }
    for (i = 0; i < machine->slotInfoCount; i++) {
        switch (machine->slotInfo[i].romType) {
        case ROM_SUNRISEIDE:  hdType[hdIndex++] = HD_SUNRISEIDE; break;
        case ROM_BEERIDE:     hdType[hdIndex++] = HD_BEERIDE;    break;
        case ROM_GIDE:        hdType[hdIndex++] = HD_GIDE;       break;
        case ROM_SVI328RSIDE: hdType[hdIndex++] = HD_RSIDE;      break;
        case ROM_NOWIND:      hdType[hdIndex++] = HD_NOWIND;     break;
        case SRAM_MEGASCSI:   hdType[hdIndex++] = HD_MEGASCSI;   break;
        case SRAM_WAVESCSI:   hdType[hdIndex++] = HD_WAVESCSI;   break;
        case ROM_GOUDASCSI:   hdType[hdIndex++] = HD_GOUDASCSI;  break;
        case ROM_MEGAFLSHSCCPLUS_SD: hdType[hdIndex++] = HD_MFRSD; break;
        }
    }

    // Update RAM info
    boardRamSize  = 0;
    boardVramSize = machine->video.vramSize;

    for (i = 0; i < machine->slotInfoCount; i++) {
        if (machine->slotInfo[i].romType == RAM_1KB_MIRRORED) {
            boardRamSize = 0x400;
        }
        if (machine->slotInfo[i].romType == RAM_2KB_MIRRORED) {
            boardRamSize = 0x800;
        }
    }

    if (boardRamSize == 0) {
        for (i = 0; i < machine->slotInfoCount; i++) {
            if (machine->slotInfo[i].romType == RAM_NORMAL || machine->slotInfo[i].romType == RAM_MAPPER) {
                boardRamSize = 0x2000 * machine->slotInfo[i].pageCount;
            }
        }
    }

    boardType = machine->board.type;
    PatchReset(boardType);

    joystickPortUpdateBoardInfo();
}

void boardReset()
{
    if (boardRunning) {
        boardInfo.softReset();
    }
}

void boardSetDataBus(UInt8 value, UInt8 defValue, int useDef) {
    if (boardRunning) {
        boardInfo.setDataBus(boardInfo.cpuRef, value, defValue, useDef);
    }
}

static BoardType boardLoadState(void)
{
    BoardDeviceInfo* di = boardDeviceInfo;
    SaveState* state;
    BoardType boardType;
    int   i;
    char  tag[16];
            
    state = saveStateOpenForRead("board");

    boardType      = saveStateGet(state, "boardType", BOARD_MSX);
    boardSysTime64 = (UInt64)saveStateGet(state, "boardSysTime64Hi", 0) << 32 |
                     (UInt64)saveStateGet(state, "boardSysTime64Lo", 0);
    oldTime        = saveStateGet(state, "oldTime", 0);
    pendingInt     = saveStateGet(state, "pendingInt", 0);
    
    di->carts[0].inserted = saveStateGet(state, "cartInserted00", 0);
    di->carts[0].type     = saveStateGet(state, "cartType00",     0);
    saveStateGetBuffer(state, "cartName00",  di->carts[0].name, sizeof(di->carts[0].name));
    saveStateGetBuffer(state, "cartInZip00", di->carts[0].inZipName, sizeof(di->carts[0].inZipName));

    di->carts[1].inserted = saveStateGet(state, "cartInserted01", 0);
    di->carts[1].type     = saveStateGet(state, "cartType01",     0);
    saveStateGetBuffer(state, "cartName01",  di->carts[1].name, sizeof(di->carts[1].name));
    saveStateGetBuffer(state, "cartInZip01", di->carts[1].inZipName, sizeof(di->carts[1].inZipName));
#if 0
    di->disks[0].inserted = saveStateGet(state, "diskInserted00", 0);
    saveStateGetBuffer(state, "diskName00",  di->disks[0].name, sizeof(di->disks[0].name));
    saveStateGetBuffer(state, "diskInZip00", di->disks[0].inZipName, sizeof(di->disks[0].inZipName));

    di->disks[1].inserted = saveStateGet(state, "diskInserted01", 0);
    saveStateGetBuffer(state, "diskName01",  di->disks[1].name, sizeof(di->disks[1].name));
    saveStateGetBuffer(state, "diskInZip01", di->disks[1].inZipName, sizeof(di->disks[1].inZipName));
#else
    for (i = 0; i < MAXDRIVES; i++) {
        sprintf(tag, "diskInserted%.2d", i);
        di->disks[i].inserted = saveStateGet(state, tag, 0);
        sprintf(tag, "diskName%.2d", i);
        saveStateGetBuffer(state, tag, di->disks[i].name, sizeof(di->disks[i].name));
        sprintf(tag, "diskInZip%.2d", i);
        saveStateGetBuffer(state, tag, di->disks[i].inZipName, sizeof(di->disks[i].inZipName));
    }
#endif

    di->tapes[0].inserted = saveStateGet(state, "casInserted", 0);
    saveStateGetBuffer(state, "casName",  di->tapes[0].name, sizeof(di->tapes[0].name));
    saveStateGetBuffer(state, "casInZip", di->tapes[0].inZipName, sizeof(di->tapes[0].inZipName));

    di->video.vdpSyncMode = saveStateGet(state, "vdpSyncMode", 0);

    /* Sound chip enable flags: must be applied before machineCreate so
    ** each cartridge mapper sees the loaded value.  Sentinel 0xFFFFFFFF
    ** preserves Properties when loading a pre-flag .sta. */
    {
        UInt32 v;
        v = saveStateGet(state, "enableYm2413",    0xFFFFFFFFu);
        if (v != 0xFFFFFFFFu) boardSetYm2413Enable((int)v);
        v = saveStateGet(state, "enableY8950",     0xFFFFFFFFu);
        if (v != 0xFFFFFFFFu) boardSetY8950Enable((int)v);
        v = saveStateGet(state, "enableMoonsound", 0xFFFFFFFFu);
        if (v != 0xFFFFFFFFu) boardSetMoonsoundEnable((int)v);
    }

    saveStateClose(state);

    videoManagerLoadState();
    tapeLoadState();

    return boardType;
}


void boardSaveState(const char* stateFile, int screenshot)
{
    BoardDeviceInfo* di = boardDeviceInfo;
    char buf[128];
    time_t ltime;
    SaveState* state;
    int size;
    void* bitmap;
    int rv;
    int i;

    if (!boardRunning) {
        return;
    }

    saveStateCreateForWrite(stateFile);
    
    rv = zipSaveFile(stateFile, "version", 0, saveStateVersion, (int)strlen(saveStateVersion) + 1);
    if (!rv) {
        return;
    }
    
    state = saveStateOpenForWrite("board");

    saveStateSet(state, "pendingInt", pendingInt);
    saveStateSet(state, "boardType", boardType);
    saveStateSet(state, "boardSysTime64Hi", (UInt32)(boardSysTime64 >> 32));
    saveStateSet(state, "boardSysTime64Lo", (UInt32)boardSysTime64);
    saveStateSet(state, "oldTime", oldTime);

    saveStateSet(state, "cartInserted00", di->carts[0].inserted);
    saveStateSet(state, "cartType00",     di->carts[0].type);
    saveStateSetBuffer(state, "cartName00",  di->carts[0].name, (int)strlen(di->carts[0].name) + 1);
    saveStateSetBuffer(state, "cartInZip00", di->carts[0].inZipName, (int)strlen(di->carts[0].inZipName) + 1);
    saveStateSet(state, "cartInserted01", di->carts[1].inserted);
    saveStateSet(state, "cartType01",     di->carts[1].type);
    saveStateSetBuffer(state, "cartName01",  di->carts[1].name, (int)strlen(di->carts[1].name) + 1);
    saveStateSetBuffer(state, "cartInZip01", di->carts[1].inZipName, (int)strlen(di->carts[1].inZipName) + 1);
#if 0
    saveStateSet(state, "diskInserted00", di->disks[0].inserted);
    saveStateSetBuffer(state, "diskName00",  di->disks[0].name, strlen(di->disks[0].name) + 1);
    saveStateSetBuffer(state, "diskInZip00", di->disks[0].inZipName, strlen(di->disks[0].inZipName) + 1);
    saveStateSet(state, "diskInserted01", di->disks[1].inserted);
    saveStateSetBuffer(state, "diskName01",  di->disks[1].name, strlen(di->disks[1].name) + 1);
    saveStateSetBuffer(state, "diskInZip01", di->disks[1].inZipName, strlen(di->disks[1].inZipName) + 1);
#else
    for (i = 0; i < MAXDRIVES; i++) {
    sprintf(buf, "diskInserted%.2d", i);
    saveStateSet(state, buf, di->disks[i].inserted);
    sprintf(buf, "diskName%.2d", i);
    saveStateSetBuffer(state, buf,  di->disks[i].name, (int)strlen(di->disks[i].name) + 1);
    sprintf(buf, "diskInZip%.2d", i);
    saveStateSetBuffer(state, buf, di->disks[i].inZipName, (int)strlen(di->disks[i].inZipName) + 1);
    }
#endif
    saveStateSet(state, "casInserted", di->tapes[0].inserted);
    saveStateSetBuffer(state, "casName",  di->tapes[0].name, (int)strlen(di->tapes[0].name) + 1);
    saveStateSetBuffer(state, "casInZip", di->tapes[0].inZipName, (int)strlen(di->tapes[0].inZipName) + 1);

    saveStateSet(state, "vdpSyncMode",   di->video.vdpSyncMode);

    /* Persist sound chip enable flags so a reload restores the same
    ** audio configuration regardless of current Properties values. */
    saveStateSet(state, "enableYm2413",    boardGetYm2413Enable());
    saveStateSet(state, "enableY8950",     boardGetY8950Enable());
    saveStateSet(state, "enableMoonsound", boardGetMoonsoundEnable());

    saveStateClose(state);

    boardCaptureSaveState();

    videoManagerSaveState();
    tapeSaveState();

    // Save machine state
    machineSaveState(boardMachine);

    // Call board dependent save state
    boardInfo.saveState(stateFile);

    if (screenshot) {
        bitmap = archScreenCapture(SC_SMALL, &size, 1);
        if( bitmap != NULL && size > 0 ) {
#ifdef WII
            zipSaveFile(stateFile, "screenshot.png", 1, bitmap, size);
#else
            zipSaveFile(stateFile, "screenshot.bmp", 1, bitmap, size);
#endif
        }
        if( bitmap != NULL ) {
            free(bitmap);
        }
    }

    memset(buf, 0, 128);
    time(&ltime);
    strftime(buf, 128, "%X   %A, %B %d, %Y", localtime(&ltime));
    zipSaveFile(stateFile, "date.txt", 1, buf, (int)strlen(buf) + 1);

    saveStateDestroy();
}


void boardSetFrequency(int frequency)
{
    boardFreq = frequency * (boardFrequency() / 3579545);
    
	mixerSetBoardFrequency(frequency);
}

int boardGetRefreshRate()
{
    if (boardRunning) {
        return boardInfo.getRefreshRate();
    }
    return 0;
}

void  boardSetInt(UInt32 irq)
{
    pendingInt |= irq;
    boardInfo.setInt(boardInfo.cpuRef);
}

void   boardClearInt(UInt32 irq)
{
    pendingInt &= ~irq;
    if (pendingInt == 0) {
        boardInfo.clearInt(boardInfo.cpuRef);
    }
}

UInt32 boardGetInt(UInt32 irq)
{
    return pendingInt & irq;
}

UInt8* boardGetRamPage(int page)
{
    if (boardInfo.getRamPage == NULL) {
        return NULL;
    }
    return boardInfo.getRamPage(page);
}

UInt32 boardGetRamSize()
{
    return boardRamSize;
}

UInt32 boardGetVramSize()
{
    return boardVramSize;
}

int boardUseRom()
{
    return useRom;
}

int boardUseMegaRom()
{
    return useMegaRom;
}

int boardUseMegaRam()
{
    return useMegaRam;
}

int boardUseFmPac()
{
    return useFmPac;
}

HdType boardGetHdType(int hdIndex)
{
    if (hdIndex < 0 || hdIndex >= MAX_HD_COUNT) {
        return HD_NONE;
    }
    return hdType[hdIndex];
}

void boardChangeCartridge(int cartNo, RomType romType, char* cart, char* cartZip)
{
    if (cart && strlen(cart) == 0) {
        cart = NULL;
    }

    if (cartZip && strlen(cartZip) == 0) {
        cartZip = NULL;
    }
    
    if (romType == ROM_UNKNOWN) {
        int size;
        UInt8* buf = romLoad(cart, cartZip, &size);
        if (buf != NULL) {
            MediaType* mediaType = mediaDbGuessRom(buf, size);
            romType = mediaDbGetRomType(mediaType);
            free(buf);
        }
    }

    if (boardDeviceInfo != NULL) {
        boardDeviceInfo->carts[cartNo].inserted = cart != NULL;
        boardDeviceInfo->carts[cartNo].type = romType;

        if (boardDeviceInfo->carts[cartNo].name != cart) {
            strcpy(boardDeviceInfo->carts[cartNo].name, cart ? cart : "");
        }
        if (boardDeviceInfo->carts[cartNo].inZipName != cartZip) {
            strcpy(boardDeviceInfo->carts[cartNo].inZipName, cartZip ? cartZip : "");
        }
    }

    useRom     -= romTypeIsRom(currentRomType[cartNo]);
    useMegaRom -= romTypeIsMegaRom(currentRomType[cartNo]);
    useMegaRam -= romTypeIsMegaRam(currentRomType[cartNo]);
    useFmPac   -= romTypeIsFmPac(currentRomType[cartNo]);
    hdType[cartNo] = HD_NONE;
    currentRomType[cartNo] = ROM_UNKNOWN;

    if (cart != NULL) {
        currentRomType[cartNo] = romType;
        useRom     += romTypeIsRom(romType);
        useMegaRom += romTypeIsMegaRom(romType);
        useMegaRam += romTypeIsMegaRam(romType);
        useFmPac   += romTypeIsFmPac(romType);
        if (currentRomType[cartNo] == ROM_SUNRISEIDE)   hdType[cartNo] = HD_SUNRISEIDE;
        if (currentRomType[cartNo] == ROM_BEERIDE)      hdType[cartNo] = HD_BEERIDE;
        if (currentRomType[cartNo] == ROM_GIDE)         hdType[cartNo] = HD_GIDE;
        if (currentRomType[cartNo] == ROM_SVI328RSIDE)  hdType[cartNo] = HD_RSIDE;
        if (currentRomType[cartNo] == ROM_NOWIND)       hdType[cartNo] = HD_NOWIND;
        if (currentRomType[cartNo] == SRAM_MEGASCSI)    hdType[cartNo] = HD_MEGASCSI;
        if (currentRomType[cartNo] == SRAM_MEGASCSI128) hdType[cartNo] = HD_MEGASCSI;
        if (currentRomType[cartNo] == SRAM_MEGASCSI256) hdType[cartNo] = HD_MEGASCSI;
        if (currentRomType[cartNo] == SRAM_MEGASCSI512) hdType[cartNo] = HD_MEGASCSI;
        if (currentRomType[cartNo] == SRAM_MEGASCSI1MB) hdType[cartNo] = HD_MEGASCSI;
        if (currentRomType[cartNo] == SRAM_WAVESCSI)    hdType[cartNo] = HD_WAVESCSI;
        if (currentRomType[cartNo] == SRAM_WAVESCSI128) hdType[cartNo] = HD_WAVESCSI;
        if (currentRomType[cartNo] == SRAM_WAVESCSI256) hdType[cartNo] = HD_WAVESCSI;
        if (currentRomType[cartNo] == SRAM_WAVESCSI512) hdType[cartNo] = HD_WAVESCSI;
        if (currentRomType[cartNo] == SRAM_WAVESCSI1MB) hdType[cartNo] = HD_WAVESCSI;
        if (currentRomType[cartNo] == ROM_GOUDASCSI)    hdType[cartNo] = HD_GOUDASCSI;
        if (currentRomType[cartNo] == ROM_MEGAFLSHSCCPLUS_SD) hdType[cartNo] = HD_MFRSD;
    }

    if (boardRunning && cartNo < boardInfo.cartridgeCount) {
        int inserted = cartridgeInsert(cartNo, romType, cart, cartZip);
        if (boardInfo.changeCartridge != NULL) {
            boardInfo.changeCartridge(boardInfo.cpuRef, cartNo, inserted);
        }
    }
}

static void boardUpdateDisketteInfo()
{
    int i;
    for (i = 0; i < MAXDRIVES; i++) {
        if (boardDeviceInfo->disks[i].inserted) {
            diskSetInfo(i, boardDeviceInfo->disks[i].name,
                        boardDeviceInfo->disks[i].inZipName);
        }
        else {
            diskSetInfo(i, NULL, NULL);
        }
    }
}

void boardChangeDiskette(int driveId, char* fileName, const char* fileInZipFile)
{
    if (fileName && strlen(fileName) == 0) {
        fileName = NULL;
    }

    if (fileInZipFile && strlen(fileInZipFile) == 0) {
        fileInZipFile = NULL;
    }

    if (boardDeviceInfo != NULL) {
        boardDeviceInfo->disks[driveId].inserted = fileName != NULL;
        
        if (boardDeviceInfo->disks[driveId].name != fileName) {
            strcpy(boardDeviceInfo->disks[driveId].name, fileName ? fileName : "");
        }
        if (boardDeviceInfo->disks[driveId].inZipName != fileInZipFile) {
            strcpy(boardDeviceInfo->disks[driveId].inZipName, fileInZipFile ? fileInZipFile : "");
        }
    }

    diskChange(driveId ,fileName, fileInZipFile);
}

void boardChangeCassette(int tapeId, char* name, const char* fileInZipFile)
{
    if (name && strlen(name) == 0) {
        name = NULL;
    }

    if (fileInZipFile && strlen(fileInZipFile) == 0) {
        fileInZipFile = NULL;
    }

    if (boardDeviceInfo != NULL) {
        boardDeviceInfo->tapes[tapeId].inserted = name != NULL;

        if (boardDeviceInfo->tapes[tapeId].name != name) {
            strcpy(boardDeviceInfo->tapes[tapeId].name, name ? name : "");
        }
        if (boardDeviceInfo->tapes[tapeId].inZipName != fileInZipFile) {
            strcpy(boardDeviceInfo->tapes[tapeId].inZipName, fileInZipFile ? fileInZipFile : "");
        }
    }

    tapeInsert(name, fileInZipFile);

    /* The trap is installed by the machine config (romType CasPatch). Signal
    ** only images carry no byte stream, so it has to stand down for those. */
    romMapperCasetteSetPatchEnable(!tapeIsSignalOnly());
}

int boardGetCassetteInserted()
{
    return tapeIsInserted();
}

UInt32 boardCalcRelativeTimeout(UInt32 timerFrequency, UInt32 nextTimeout)
{
    UInt64 currentTime = boardSystemTime64();
    UInt64 frequency   = boardFrequency64() / timerFrequency;

    currentTime = frequency * (currentTime / frequency);

    return (UInt32)((currentTime + nextTimeout * frequency) / HIRES_CYCLES_PER_LORES_CYCLE);
}

/////////////////////////////////////////////////////////////
// Board timer

struct BoardTimer {
    BoardTimer*  next;
    BoardTimer*  prev;
    BoardTimerCb callback;
    void*        ref;
    UInt32       timeout;
};

static BoardTimer* timerList = NULL;
static UInt32 timeAnchor;
static int    timeoutCheckBreak;

#define MAX_TIME  (2 * 1368 * 313)
#define TEST_TIME 0x7fffffff

BoardTimer* boardTimerCreate(BoardTimerCb callback, void* ref)
{
    BoardTimer* timer = malloc(sizeof(BoardTimer));

    timer->next     = timer;
    timer->prev     = timer;
    timer->callback = callback;
    timer->ref      = ref ? ref : timer;
    timer->timeout  = 0;

    return timer;
}

void boardTimerDestroy(BoardTimer* timer)
{
    boardTimerRemove(timer);

    free(timer);
}

void boardTimerAdd(BoardTimer* timer, UInt32 timeout)
{
    UInt32 currentTime = boardSystemTime();
    BoardTimer* refTimer;
    BoardTimer* next = timer->next;
    BoardTimer* prev = timer->prev;

    // Remove current timer
    next->prev = prev;
    prev->next = next;

    timerList->timeout = currentTime + TEST_TIME;

    refTimer = timerList->next;

    if (timeout - timeAnchor - TEST_TIME < currentTime - timeAnchor - TEST_TIME) {
        timer->next = timer;
        timer->prev = timer;
        
        // Time has already expired
        return;
    }

    while (timeout - timeAnchor > refTimer->timeout - timeAnchor) {
        refTimer = refTimer->next;
    }
#if 0
    {
        static int highWatermark = 0;
        int cnt = 0;
        BoardTimer* t = timerList->next;
        while (t != timerList) {
            cnt++;
            t = t->next;
        }
        if (cnt > highWatermark) {
            highWatermark = cnt;
            printf("HIGH: %d\n", highWatermark);
        }
    }
#endif

    timer->timeout       = timeout;
    timer->next          = refTimer;
    timer->prev          = refTimer->prev;
    refTimer->prev->next = timer;
    refTimer->prev       = timer;

    boardInfo.setCpuTimeout(boardInfo.cpuRef, timerList->next->timeout);
}

void boardTimerRemove(BoardTimer* timer)
{
    BoardTimer* next = timer->next;
    BoardTimer* prev = timer->prev;

    next->prev = prev;
    prev->next = next;

    timer->next = timer;
    timer->prev = timer;
}

void boardTimerCleanup()
{
    while (timerList->next != timerList) {
        boardTimerRemove(timerList->next);
    }

    timeoutCheckBreak = 1;
}

void boardTimerCheckTimeout(void* dummy)
{
    UInt32 currentTime = boardSystemTime();
    timerList->timeout = currentTime + MAX_TIME;

    timeoutCheckBreak = 0;
    while (!timeoutCheckBreak) {
        BoardTimer* timer = timerList->next;
        if (timer == timerList) {
            return;
        }
        if (timer->timeout - timeAnchor > currentTime - timeAnchor) {
            break;
        }

        boardTimerRemove(timer);
        timer->callback(timer->ref, timer->timeout);
    }

    timeAnchor = boardSystemTime();    

    boardInfo.setCpuTimeout(boardInfo.cpuRef, timerList->next->timeout);
}

UInt64 boardSystemTime64() {
    UInt32 currentTime = boardSystemTime();
    boardSysTime64 += HIRES_CYCLES_PER_LORES_CYCLE * (currentTime - oldTime);
    oldTime = currentTime;
    return boardSysTime64;
}

void boardInit(UInt32* systemTime)
{
    static BoardTimer dummy_timer;
    boardSysTime = systemTime;
    oldTime = *systemTime;
    boardSysTime64 = oldTime * HIRES_CYCLES_PER_LORES_CYCLE;

    timeAnchor = *systemTime;

    if (timerList == NULL) {
        dummy_timer.next     = &dummy_timer;
        dummy_timer.prev     = &dummy_timer;
        dummy_timer.callback = NULL;
        dummy_timer.ref      = &dummy_timer;
        dummy_timer.timeout  = 0;
        timerList = &dummy_timer;
    }
}


/////////////////////////////////////////////////////////////
// Not board specific stuff....

static char baseDirectory[512];
static int oversamplingYM2413    = 1;
static int oversamplingY8950     = 1;
static int oversamplingMoonsound = 1;
static int enableYM2413          = 1;
static int enableY8950           = 1;
static int enableMoonsound       = 1;
static int videoAutodetect       = 1;

const char* boardGetBaseDirectory() {
    return baseDirectory;
}

void boardSetDirectory(const char* dir) {
    strcpy(baseDirectory, dir);
}

void boardSetYm2413Oversampling(int value) {
    oversamplingYM2413 = value;
}

int boardGetYm2413Oversampling() {
    return oversamplingYM2413;
}

void boardSetY8950Oversampling(int value) {
    oversamplingY8950 = value;
}

int boardGetY8950Oversampling() {
    return oversamplingY8950;
}

void boardSetMoonsoundOversampling(int value) {
    oversamplingMoonsound = value;
}

int boardGetMoonsoundOversampling() {
    return oversamplingMoonsound;
}

void boardSetYm2413Enable(int value) {
    enableYM2413 = value;
}

int boardGetYm2413Enable() {
    return enableYM2413;
}

void boardSetY8950Enable(int value) {
    enableY8950 = value;
}

int boardGetY8950Enable() {
    return enableY8950;
}

void boardSetMoonsoundEnable(int value) {
    enableMoonsound = value;
}

int boardGetMoonsoundEnable() {
    return enableMoonsound;
}

void boardSetVideoAutodetect(int value) {
    videoAutodetect = value;
}

int  boardGetVideoAutodetect() {
    return videoAutodetect;
}
