/*****************************************************************************
**
** Reset and snapshot op handlers.
** Copyright (C) 2026 Hesoten
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
#include "ControlOp.h"
#include "ControlMachine.h"
#include "ControlStopLog.h"
#include "ControlClient.h"
#include "ControlCpu.h"
#include "ControlMemory.h"
#include "ControlSymbol.h"
#include "ControlSlots.h"
#include "ControlStatus.h"
#include "ControlWindowThread.h"
#include "ControlProtocol.h"
#include "ControlPeer.h"
#include "ArchTimer.h"
#include "Board.h"
#include "Debugger.h"
#include "Disk.h"
#include "Emulator.h"
#include "Properties.h"
#include "R800Debug.h"
#include <stdio.h>
#include <string.h>

/* Long enough for the switch off, saving, and the 3 s the start may wait for its thread. */
#define RESET_TIMEOUT_MS 15000

/* After a reset or a restore: the hunt and the undo journal were about the machine before. */
static void tellMachineLeftBehind(CtrlText* text, const char* event, int hadHunt, int hadUndo)
{
    if (hadHunt) {
        ctrlTextAdd(text, "the hunt belongs to the machine before the %s, so it no longer "
                    "applies.\n", event);
    }
    if (hadUndo) {
        ctrlTextAdd(text, "the writes bmsx_undo could take back were made to the machine "
                    "before the %s, so they can no longer be taken back.\n", event);
    }
}

/* Why the window's work was cut short. */
#define CUT_OFF_TEXT "agent control was turned off at the emulator"
#define CLOSED_TEXT  "the connection was closed while this waited"

/* These mappers save their battery-backed memory with their state; the others do not. */
#define SNAPSHOT_SRAM_TEXT \
    "The battery-backed SRAM of FM-PAC, PAC, MEGA-SCSI, WAVE-SCSI, ESE-RAM, ESE-SCC and the " \
    "ColecoVision Opcode SaveRAM, and the firmware SRAM of the FS-A1WX/WSX and turbo R, go " \
    "back with it (their 2 kB Matsushita SRAM is not put back). Other cartridges' " \
    "SRAM and flash (ASCII 8/16 SRAM, flash ROMs and the like) and disk contents do NOT: " \
    "they keep what was written after the snapshot."

/* When the snapshot was taken and its PC, for the restore reply. */
static UInt32 snapshotAtMs;
static Int32  snapshotPc;

/* The drives whose contents a snapshot cannot take back. */
static void snapshotNameHostMedia(CtrlText* text)
{
    char drive[64];
    int i;

    for (i = 0; i < MAXDRIVES; i++) {
        if (diskIsDir(i) && diskDirPending(i)) {
            ctrlMachineDriveName(i, drive, sizeof(drive));
            ctrlTextAdd(text, " %s holds the folder %s, which the MSX has written to.",
                        drive, diskDirPath(i));
        }
        else if (diskIsHostMediumOffered(i)) {
            ctrlMachineDriveName(i, drive, sizeof(drive));
            ctrlTextAdd(text, " %s is a physical drive of this PC.", drive);
        }
    }
}

/* how=hard or soft; soft only on a running machine. Returns 0 with hard set, else the refusal. */
static int refuseHow(CtrlCall* call, char* how, int howLen, int* hard)
{
    const char* given = ctrlCallText(call, "how");
    int refused;

    if (given == NULL) {
        return ctrlReplyError(call, "missing_how",
                              "\"how\" is needed: hard (switched off and on: cartridge and "
                              "disk image files are read again) or soft (the reset "
                              "button: same ROM, RAM kept)");
    }
    if (strcmp(given, "hard") != 0 && strcmp(given, "soft") != 0) {
        return ctrlReplyError(call, "bad_how", "\"how\" must be hard or soft");
    }
    /* The args column holds it to the size of how. */
    snprintf(how, (size_t)howLen, "%s", given);
    *hard = strcmp(how, "hard") == 0;
    if (!*hard && (refused = ctrlRefuseWithoutMachine(call, "; how=hard starts it")) != 0) {
        return refused;
    }
    if (!*hard && emulatorGetState() == EMU_PAUSED) {
        return ctrlReplyError(call, "not_running",
                              "a soft reset needs a running machine, and this one is paused: "
                              "the emulator's own soft reset of a paused machine starts a "
                              "second one over it. Resume it first (bmsx_run how=resume), "
                              "or use how=hard. Nothing was reset.");
    }
    return 0;
}

/* The window did not do the reset, or found nothing to reset. Returns 0 when it did one. */
static int refuseResetNotDone(CtrlCall* call, int result)
{
    if (result == CTRL_WINDOW_THREAD_DIALOG) {
        return ctrlReplyError(call, "dialog_open", CTRL_WINDOW_THREAD_DIALOG_TEXT);
    }
    if (result != CTRL_WINDOW_THREAD_DONE) {
        const char* why = ctrlClientAllBlocked()
                        ? CUT_OFF_TEXT " while this waited"
                        : ctrlPeerGone()
                        ? CLOSED_TEXT
                        : NULL;

        if (result == CTRL_WINDOW_THREAD_NOT_TAKEN) {
            char late[32];

            snprintf(late, sizeof(late), "not within %d s", RESET_TIMEOUT_MS / 1000);
            return ctrlReplyErrorf(call, "not_taken",
                                   "the emulator window did not take the reset (%s), so nothing "
                                   "was reset. Call again.", why != NULL ? why : late);
        }
        /* Also for CTRL_WINDOW_THREAD_BUSY: an earlier reset is unfinished, and this one was not started. */
        return ctrlReplyErrorf(call, "reset_unfinished",
                               "a reset was started and has not finished%s%s%s: the emulator "
                               "window is most likely showing a message, such as a file it could "
                               "not find. Ask the person at the keyboard to look at it, then call "
                               "bmsx_status.", why != NULL ? " (" : "", why != NULL ? why : "",
                               why != NULL ? ")" : "");
    }
    if (ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_NOT_RUNNING) {
        return ctrlReplyError(call, "not_running",
                              "the machine was not running when the reset came to it, so "
                              "nothing was reset. Resume it first, or use how=hard.");
    }
    if (ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_FILE_MISSING) {
        return ctrlReplyErrorf(call, "rom_missing",
                               "the file for %s is not there (a build in progress?), so nothing "
                               "was reset: the machine goes on as it was. Call again when the file "
                               "is back; if it is not coming back, ask the person to eject it in "
                               "the emulator.", ctrlMachineResetReason());
    }
    return 0;
}

/* The plain breakpoints that stay over a hard reset; returns how many there are. */
static int reportKeptBreakpoints(CtrlCall* call, int hard)
{
    UInt16 kept[CTRL_PLAIN_LIST_MAX + 1];
    int    keptCount;
    int    i;

    keptCount = dbgListKeptBreakpoints(kept, CTRL_PLAIN_LIST_MAX + 1);
    if (hard && keptCount > 0) {
        if (keptCount > CTRL_PLAIN_LIST_MAX) {
            ctrlTextAdd(&call->text, "more than %d plain breakpoints", CTRL_PLAIN_LIST_MAX);
        }
        else {
            ctrlTextAdd(&call->text, "%d plain breakpoint%s", keptCount, keptCount == 1 ? "" : "s");
        }
        ctrlTextAdd(&call->text, " set with bmsx_break stay%s, armed before the CPU ran its first "
                    "instruction:", keptCount == 1 ? "s" : "");
        for (i = 0; i < keptCount && i < CTRL_PLAIN_LIST_MAX; i++) {
            ctrlTextAdd(&call->text, " 0x%04X", kept[i]);
        }
        ctrlTextAdd(&call->text, "%s\n", keptCount > CTRL_PLAIN_LIST_MAX ? " and more" : "");
    }
    return keptCount;
}

/* Logs a reset only where one may have happened, as failed when unfinished or the machine did
** not start again; a refusal is left to the failed line the dispatcher logs for its error. */
static void noteReset(int hard, int result)
{
    if (result == CTRL_WINDOW_THREAD_DIALOG || result == CTRL_WINDOW_THREAD_NOT_TAKEN ||
        result == CTRL_WINDOW_THREAD_BUSY ||
        (result == CTRL_WINDOW_THREAD_DONE &&
         (ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_NOT_RUNNING ||
          ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_FILE_MISSING)))
    {
        return;
    }
    ctrlActivityNoteOn(hard ? CTRL_ACT_RESET : CTRL_ACT_RESET_SOFT, NULL, NULL,
                       result == CTRL_WINDOW_THREAD_UNFINISHED ||
                       ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_NOT_STARTED
                       ? CTRL_ACT_FAILED : CTRL_ACT_DONE);
}

int ctrlOpReset(CtrlCall* call)
{
    int refused;
    char   how[16];
    int    keptCount;
    int    hard;
    int    segmentCount = dbgSegmentBreakpointCount();
    int    hadHunt = call->session->hunt != NULL;
    int    hadUndo = ctrlMemUndoDepth() > 0;
    UInt32 generation = dbgMachineGeneration();
    UInt32 stopsBefore = ctrlStopSeq();
    CtrlStop stop;
    int    stopReason = CTRL_STOP_UNKNOWN;
    int    result;

    if ((refused = refuseHow(call, how, sizeof(how), &hard)) != 0) {
        return refused;
    }

    result = ctrlWindowThreadRun(ctrlMachineReset, hard, RESET_TIMEOUT_MS);
    noteReset(hard, result);
    if ((refused = refuseResetNotDone(call, result)) != 0) {
        return refused;
    }
    /* The stop the reset logged is not one to wait for; a breakpoint hit at once is. */
    call->session->stopCursor = stopsBefore;
    while (ctrlStopGet(call->session->stopCursor, &stop) && stop.reason == CTRL_STOP_MACHINE) {
        call->session->stopCursor = stop.seq;
    }
    if (ctrlMachineResetOutcome() == CTRL_MACHINE_RESET_NOT_STARTED) {
        return ctrlReplyErrorf(call, "not_started",
                               "the machine was switched off and did not start again: %s. No "
                               "machine runs now. Fix it and call bmsx_reset how=hard again.",
                               ctrlMachineResetReason());
    }

    if (hard) {
        ctrlTextAdd(&call->text, "hard reset: the machine was switched off and on again and read "
                    "its cartridge and disk image files again.\n");
    }
    else {
        ctrlTextAdd(&call->text, "soft reset: the CPU started again from 0x0000 with the same "
                    "cartridges, and RAM as it was. A rebuilt ROM is NOT read: use "
                    "how=hard for that. Breakpoints stay as they were.\n");
    }

    keptCount = reportKeptBreakpoints(call, hard);

    if (hard) {
        ctrlSymDescribeStale(&call->text);
        if (segmentCount > 0) {
            ctrlTextAdd(&call->text, "%d segment breakpoint%s stay%s at the same segment and "
                        "offset (bmsx_break op=list shows them). If the rebuild moved "
                        "code, they now sit on whatever is at that offset, possibly in "
                        "the middle of an instruction: check them against the new "
                        "symbols.\n", segmentCount, segmentCount == 1 ? "" : "s",
                        segmentCount == 1 ? "s" : "");
        }
        ctrlTextAdd(&call->text, "%s", ctrlMachineResetNotes());
    }
    if (generation != dbgMachineGeneration()) {
        tellMachineLeftBehind(&call->text, "reset", hadHunt, hadUndo);
    }
    jsonOutInt(&call->out, "plain_kept",
               keptCount > CTRL_PLAIN_LIST_MAX ? CTRL_PLAIN_LIST_MAX : keptCount);
    jsonOutUInt(&call->out, "machine_generation", dbgMachineGeneration());
    /* Left unread, so bmsx_run_until reports it rather than running on. */
    if (emulatorGetState() == EMU_PAUSED && ctrlStopGet(call->session->stopCursor, &stop)) {
        stopReason = stop.reason;
    }
    ctrlStatusDescribe(&call->text, stopReason, (int)emulatorGetState());
    return ctrlReplyDone(call);
}

/* A machine stopped inside an instruction cannot be copied or put back there. */
static int refuseInsideInstruction(CtrlCall* call, int pastTest, const char* what)
{
    if (boardIsParkedInBreakpointStop() && !pastTest) {
        return ctrlReplyErrorf(call, "inside_instruction",
                               "the machine stopped in the middle of an instruction (at a "
                               "watchpoint, or at a VRAM access check of the debugger), and a "
                               "snapshot can only be %s between two instructions. Step once "
                               "(bmsx_run how=step) to finish the instruction, then call again. "
                               "Nothing was %s.", what, what);
    }
    return 0;
}

/* The snapshot could not be taken or put back; returns 0 when it was. */
static int refuseSnapshotNotDone(CtrlCall* call, int outcome, const char* what)
{
    if (outcome == BOARD_SNAPSHOT_NONE) {
        return ctrlReplyError(call, "no_snapshot",
                              "no snapshot to restore: none was taken, or a hard reset, a "
                              "machine change, a state load or a cartridge change has dropped "
                              "it since. Take one with op=take.");
    }
    if (outcome == BOARD_SNAPSHOT_HOST_MEDIUM) {
        ctrlTextAdd(&call->text, "a disk drive holds a medium that cannot be put back with the "
                    "machine, so nothing was %s:", what);
        snapshotNameHostMedia(&call->text);
        ctrlTextAdd(&call->text, " Eject it in the emulator (for a folder it asks whether to write "
                    "the MSX's changes back), or use a disk image.");
        return ctrlReplyTextAsError(call, "host_medium");
    }
    if (outcome != BOARD_SNAPSHOT_OK) {
        return ctrlReplyError(call, "snapshot_failed",
                              "the snapshot could not be stored whole (a state holds at most 64 "
                              "device parts, and this machine with its cartridges may need "
                              "more). There is no snapshot now: an earlier one was "
                              "dropped as well.");
    }
    return 0;
}

static void describeTake(CtrlCall* call, Int32 pc)
{
    int i;

    snapshotAtMs = archGetSystemUpTime(1000);
    snapshotPc   = pc;
    ctrlTextAdd(&call->text, "took a snapshot here, replacing any earlier one. It is kept in "
                "the emulator's memory, uncompressed (about 1 to 2 MB), "
                "until a hard reset, a machine change, a state load or a cartridge "
                "change drops it. Restoring puts back the CPU, RAM, VRAM and the other "
                "chips. " SNAPSHOT_SRAM_TEXT " A disk or tape changed after now is "
                "not noticed: a restore leaves the new one in, with the disk "
                "controller as it was, and does not wind the tape back. Breakpoints "
                "and watchpoints are not part of it "
                "and stay as they are.\n");
    for (i = 0; i < MAXDRIVES; i++) {
        char drive[64];

        if (diskIsDir(i)) {
            ctrlMachineDriveName(i, drive, sizeof(drive));
            ctrlTextAdd(&call->text, "%s holds the folder %s: once the MSX writes to it, a "
                        "restore is refused until it is ejected.\n", drive, diskDirPath(i));
        }
    }
    ctrlSlotsDescribeSwitched(&call->text, CTRL_SLOTS_BANKS_SNAPSHOT);
}

/* The CPU tests a breakpoint after the timer call a pause parks in and before the
** instruction a breakpoint stop parks at; a restore leaves either as it was. */
static void tellBreakpointAfterRestore(CtrlCall* call, Int32 pc, int pastTest)
{
    if (r800DebugWaitsInHalt()) {
        ctrlTextAdd(&call->text, "a breakpoint is set at 0x%04X, where the CPU waits in a "
                    "HALT: it stops there only when it arrives at it again.\n",
                    (unsigned)pc);
    }
    else if (pastTest) {
        ctrlTextAdd(&call->text, "a breakpoint is set at 0x%04X, but this restore was made "
                    "while the machine was stopped at a breakpoint (or at the end of a "
                    "step over or step out), which the CPU had "
                    "already tested: the next bmsx_run_until runs the instruction at "
                    "0x%04X without stopping there.\n", (unsigned)pc, (unsigned)pc);
    }
    else {
        ctrlTextAdd(&call->text, "a breakpoint is set at 0x%04X, so the next bmsx_run_until "
                    "stops there again at once, before anything runs. (A restore made while "
                    "stopped at a breakpoint runs past it instead.)\n", (unsigned)pc);
    }
}

static void describeRestore(CtrlCall* call, Int32 pc, int pastTest, int hadHunt, int hadUndo)
{
    int i;
    int atBreakpoint = ctrlCpuBreakpointAt(pc);
    int writableDisk = 0;

    ctrlTextAdd(&call->text, "put the machine back to the snapshot taken %u s ago at "
                "PC=0x%04X; it is paused, as it was before this call. " SNAPSHOT_SRAM_TEXT "\n",
                (unsigned)((archGetSystemUpTime(1000) - snapshotAtMs) / 1000),
                (unsigned)(snapshotPc & 0xffff));
    for (i = 0; i < MAXDRIVES; i++) {
        writableDisk |= diskPresent(i) && !diskReadOnly(i) && !diskIsDir(i);
    }
    if (writableDisk) {
        ctrlTextAdd(&call->text, "disk images are not put back: if the MSX wrote to one after "
                    "the snapshot, the image keeps those writes while DOS's copy of its "
                    "FAT in RAM went back to before them, so the next write can damage "
                    "the disk. Reset the machine before it writes to that disk again, "
                    "or put back a copy of the image from before the snapshot.\n");
    }
    if (propGetGlobalProperties()->media.tapes[0].fileName[0] != 0) {
        ctrlTextAdd(&call->text, "the tape stays where it is now (it is not wound back, and a "
                    "tape changed after the snapshot stays in), so a tape load that was "
                    "under way when the snapshot was taken reads the wrong place after "
                    "this restore.\n");
    }
    if (atBreakpoint) {
        tellBreakpointAfterRestore(call, pc, pastTest);
    }
    tellMachineLeftBehind(&call->text, "restore", hadHunt, hadUndo);
    ctrlTextAdd(&call->text, "the emulator's own step back (when rewind is on) can go back "
                "only to moments after this restore.\n");
    ctrlTextAdd(&call->text, "the emulator window and bmsx_screen as=png show the picture "
                "from before the restore until the machine runs a frame; "
                "bmsx_screen as=text reads video memory and is current.\n");
    /* A stop logged before the restore is not one to wait for. */
    call->session->stopCursor = ctrlStopSeq();
}

static int snapshotOp(CtrlCall* call, int restore)
{
    int refused;
    const char* what = restore ? "restored" : "taken";
    int hadHunt = call->session->hunt != NULL;
    int hadUndo = ctrlMemUndoDepth() > 0;
    CtrlStop stop;
    int stopReason = CTRL_STOP_UNKNOWN;
    int pastTest;
    int outcome;
    Int32 pc = -1;

    /* Copied on this thread from the parked machine, as the window's thread would. */
    if ((refused = ctrlRefuseUnlessViewed(call)) != 0) {
        return refused;
    }
    if (ctrlStopLatest(&stop)) {
        stopReason = stop.reason;
    }
    /* Decided where it stopped, not by the stop's label: a step over ends at a breakpoint. */
    pastTest = r800DebugStoppedBeforeInstruction();
    if ((refused = refuseInsideInstruction(call, pastTest, what)) != 0) {
        return refused;
    }

    outcome = restore ? boardSnapshotRestore() : boardSnapshotTake();
    if ((refused = refuseSnapshotNotDone(call, outcome, what)) != 0) {
        return refused;
    }

    ctrlActivityNoteOn(restore ? CTRL_ACT_SNAPSHOT_RESTORE : CTRL_ACT_SNAPSHOT, NULL, NULL,
                       CTRL_ACT_DONE);
    pc = ctrlCpuPc();
    if (!restore) {
        describeTake(call, pc);
    }
    else {
        describeRestore(call, pc, pastTest, hadHunt, hadUndo);
        stopReason = CTRL_STOP_UNKNOWN;
    }
    jsonOutUInt(&call->out, "machine_generation", dbgMachineGeneration());
    /* The registers are those of the snapshot. */
    ctrlStatusDescribe(&call->text, stopReason, EMU_PAUSED);
    return ctrlReplyDone(call);
}

int ctrlOpSnapshotTake(CtrlCall* call)
{
    return snapshotOp(call, 0);
}

int ctrlOpSnapshotRestore(CtrlCall* call)
{
    return snapshotOp(call, 1);
}
