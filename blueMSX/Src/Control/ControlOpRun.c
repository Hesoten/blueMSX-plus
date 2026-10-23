/*****************************************************************************
**
** Run state op handlers.
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
#include "ControlStep.h"
#include "ControlWatch.h"
#include "ControlClient.h"
#include "ControlCpu.h"
#include "ControlStopLog.h"
#include "ControlStatus.h"
#include "ControlProtocol.h"
#include "ControlPeer.h"
#include "Debugger.h"
#include "Emulator.h"
#include "Properties.h"
#include "R800Debug.h"
#include <stdio.h>
#include <string.h>

/* A machine stop posts before the state says stopped; a start since bumps the generation. */
static int machineStopState(const CtrlStop* stop)
{
    return stop->runGeneration == emulatorGetStateSeq() ? EMU_STOPPED
                                                       : (int)emulatorGetState();
}

/* A register of the paused machine through a view of its own, or -1. */
static Int32 registerNow(const char* name)
{
    CtrlView view;
    Int32 value = -1;

    if (ctrlViewTake(&view, DBG_ACCESS_VIEW, CTRL_VIEW_TIMEOUT_MS) == DBG_ACCESS_OK) {
        value = ctrlCpuRegister(name);
        ctrlViewRelease(&view);
    }
    return value;
}

/* A resume refused: no machine, or the window holds it for a question or a change. Returns
** 0 when it resumed, or the machine was not paused to begin with. */
static int refuseResume(CtrlCall* call, int result)
{
    if (result == DBG_RESUME_NO_MACHINE) {
        return ctrlReplyError(call, "no_machine", CTRL_TEXT_NO_MACHINE "; a resume never starts one");
    }
    if (result == DBG_RESUME_BUSY) {
        return ctrlReplyError(call, "busy", CTRL_TEXT_FOLDER_DISK_BUSY ", so nothing was "
                              "resumed; call again once it is closed");
    }
    return 0;
}

/* A pause that did not pause a machine not paused already: there is none, or the window holds
** it suspended. Returns 0 when it is paused. */
static int refusePause(CtrlCall* call, int paused)
{
    int refused;

    if (paused || emulatorGetState() == EMU_PAUSED) {
        return 0;
    }
    if ((refused = ctrlRefuseWithoutMachine(call, NULL)) != 0) {
        return refused;
    }
    return ctrlReplyError(call, "busy", "the emulator window is changing the machine, or has a "
                          "menu, a dialog or a question about a folder disk open, so nothing was "
                          "paused; call again once it is closed");
}

/* A step refused after done steps: no machine, or the window holds it for a question or a
** change. Returns the reply. */
static int refuseStep(CtrlCall* call, int result, Int32 done)
{
    char after[40] = "";

    if (done > 0) {
        snprintf(after, sizeof(after), " after %d steps", (int)done);
    }
    if (result == DBG_RESUME_NO_MACHINE) {
        return ctrlReplyErrorf(call, "no_machine", "%s, so it did not step%s",
                               CTRL_TEXT_NO_MACHINE, after);
    }
    return ctrlReplyErrorf(call, "busy", CTRL_TEXT_FOLDER_DISK_BUSY ", so it did not step%s; "
                           "call again once it is closed", after);
}

/* Steps once and waits for the posted stop: 1 when it came back to one, 0 when none came, so
** the step may not have happened, or -1 with the refusal in *refused. */
static int stepAndWait(CtrlCall* call, Int32 done, int* refused)
{
    CtrlStop stop;
    UInt32 from = ctrlStopSeq();
    int    result = dbgStep();

    if (result == DBG_RESUME_NO_MACHINE || result == DBG_RESUME_BUSY) {
        *refused = refuseStep(call, result, done);
        return -1;
    }
    if (result == DBG_RESUME_OK && ctrlStopWait(from, 1000, &stop)) {
        call->session->stopCursor = stop.seq;
        return 1;
    }
    return 0;
}

/* Stepping needs a paused machine; stepping a running one does not stop it. */
static int refuseUnlessPaused(CtrlCall* call)
{
    /* A run takes its own view, so it holds none: refused as an op without one is. */
    return emulatorGetState() == EMU_PAUSED ? 0 : ctrlRefuseUnlessViewed(call);
}

/* A step that did not come back to a stop: the machine is not assumed paused, as it was
** resumed or changed underneath. */
static int replyLostStep(CtrlCall* call)
{
    call->session->stopCursor = ctrlStopSeq();
    ctrlStatusDescribe(&call->text, CTRL_STOP_UNKNOWN, (int)emulatorGetState());
    return ctrlReplyDone(call);
}

/* The first step after a watchpoint stop, which stopped inside the writing instruction, may
** only finish it: then the counter did not go up (an LDIR repeat takes one off). Returns how
** many steps that counts as, 0 or 1. */
static Int32 stepPastWatch(CtrlCall* call, int* finishedWrite, int* lost, int* refused)
{
    CtrlStop latest;
    Int32 before;
    Int32 after;
    Int32 moved;
    int   stepped;

    if (!ctrlStopLatest(&latest) || latest.reason != CTRL_STOP_WATCH) {
        return 0;
    }
    before = registerNow("CNT");
    if ((stepped = stepAndWait(call, 0, refused)) != 1) {
        *lost = stepped == 0;
        return 0;
    }
    after = registerNow("CNT");
    moved = (after - before) & 0xFFFF;
    if (before >= 0 && after >= 0 && (moved == 0 || moved == 0xFFFF)) {
        *finishedWrite = 1;
        return 0;
    }
    return 1;
}

/* A step loop cut short by Off, Read Only or the client going: why, after how many. */
static int refuseCutSteps(CtrlCall* call, Int32 done, Int32 count)
{
    if (ctrlClientAllBlocked()) {
        return ctrlReplyErrorf(call, "revoked", "agent control was turned off at the "
                               "emulator after %d of the %d steps", (int)done, (int)count);
    }
    /* Read only refuses new steps, so it ends a long loop too. */
    if (ctrlGateLevel() != CTRL_GATE_FULL) {
        return ctrlReplyErrorf(call, "read_only", "the emulator was set to read only "
                               "after %d of the %d steps", (int)done, (int)count);
    }
    if (ctrlPeerGone()) {
        return ctrlReplyErrorf(call, "abandoned", "the connection was closed after %d "
                               "of the %d steps, so the loop ended there", (int)done,
                               (int)count);
    }
    return 0;
}

/* how=step: count instructions, one stop each. Returns 0 when the run state is to be
** reported, else the reply. */
static int runStep(CtrlCall* call, Int32 count)
{
    int   finishedWrite = 0;
    int   lost = 0;
    int   refused = 0;
    int   stepped;
    Int32 i;

    if (count < CTRL_RUN_COUNT_MIN || count > CTRL_RUN_COUNT_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "count must be between %d and %d",
                               CTRL_RUN_COUNT_MIN, CTRL_RUN_COUNT_MAX);
    }
    if ((refused = refuseUnlessPaused(call)) != 0) {
        return refused;
    }
    i = stepPastWatch(call, &finishedWrite, &lost, &refused);
    for (; i < count && !lost && !refused && !ctrlClientAllBlocked() &&
           ctrlGateLevel() == CTRL_GATE_FULL && !ctrlPeerGone(); i++) {
        if ((stepped = stepAndWait(call, i, &refused)) != 1) {
            lost = stepped == 0;
            break;
        }
    }
    if (refused != 0 || (refused = refuseCutSteps(call, i, count)) != 0) {
        return refused;
    }
    if (finishedWrite) {
        ctrlTextAdd(&call->text, "first finished the instruction the watch stopped inside; "
                    "that is not counted as a step\n");
    }
    if (lost) {
        ctrlTextAdd(&call->text, "stepped %d of %d; the machine did not come back to a stop "
                    "after the next one, so it may have been resumed or changed "
                    "underneath\n", (int)i, (int)count);
        return replyLostStep(call);
    }
    return 0;
}

/* Where a step out stops: the return address of the innermost call on the stack, and the
** SP the RET leaves. Returns 0, or the refusal when there is none. */
static int stepOutTarget(CtrlCall* call, UInt16* stopAt, Int32* expectSp)
{
    UInt16 frames[1];
    UInt16 frameStackAt[1];
    int notFound;
    int innerNotFound;

    if (ctrlCpuCallstack(frames, frameStackAt, 1, &notFound, &innerNotFound) != 1) {
        return ctrlReplyError(call, "no_frame",
                              "nothing on the stack is a return address the CPU "
                              "recorded; at the top of a main loop there is "
                              "nothing to step out of. Step instead, or let it "
                              "run until it stops");
    }
    if (innerNotFound > 0) {
        ctrlTextAdd(&call->text, "the innermost %d call-log entr%s not on the stack; "
                    "stepping out to the first one that is\n", innerNotFound,
                    innerNotFound == 1 ? "y is" : "ies are");
    }
    stopAt[0] = frames[0];
    /* The RET pops the stack word the return address was found in. */
    *expectSp = (Int32)frameStackAt[0] + 2;
    return 0;
}

/* No stop within the wait: says where the step was headed, and that it runs on. */
static int reportStepTimeout(CtrlCall* call, const UInt16* stopAt, int stops, int temporary)
{
    if (ctrlClientAllBlocked()) {
        return ctrlReplyError(call, "revoked",
                              "agent control was turned off at the emulator "
                              "while this waited, so the step ended early; the "
                              "machine is left running");
    }
    jsonOutString(&call->out, "reason", "timeout");
    jsonOutUInt(&call->out, "target", stopAt[0]);
    if (stops == 2) {
        jsonOutUInt(&call->out, "target2", stopAt[1]);
        ctrlTextAdd(&call->text, "still running after 2000 ms, at neither 0x%04X nor "
                    "0x%04X; ", stopAt[0], stopAt[1]);
    }
    else {
        ctrlTextAdd(&call->text, "still running after 2000 ms; ");
    }
    ctrlTextAdd(&call->text, temporary == 0
                ? "the machine is left running\n"
                : temporary == 1
                ? "the temporary breakpoint is removed and the machine is left "
                  "running\n"
                : "the temporary breakpoints are removed and the machine is left "
                  "running\n");
    return ctrlReplyDone(call);
}

/* The stop a step over or out came to: one of its stops, at the SP a return leaves, or
** something else on the way. */
static int reportStepStop(CtrlCall* call, CtrlStop* stop, const UInt16* stopAt,
                          int stops, const int* hadBreakpoint, Int32 expectSp, int over)
{
    int   reached = -1;
    int   pc = -1;
    Int32 sp = -1;

    call->session->stopCursor = stop->seq;
    jsonOutUInt(&call->out, "stop_seq", stop->seq);
    if (stop->reason == CTRL_STOP_BREAKPOINT) {
        CtrlView view;

        if (ctrlViewTake(&view, DBG_ACCESS_VIEW, CTRL_VIEW_TIMEOUT_MS) == DBG_ACCESS_OK) {
            pc = ctrlCpuPc();
            sp = ctrlCpuRegister("SP");
            ctrlViewRelease(&view);
        }
    }
    if (pc == stopAt[0] || (stops == 2 && pc == stopAt[1])) {
        reached = pc == stopAt[0] ? 0 : 1;
    }
    if (reached < 0 && stops == 2) {
        ctrlTextAdd(&call->text, "stopped before reaching 0x%04X or 0x%04X\n",
                    stopAt[0], stopAt[1]);
    }
    else if (reached < 0) {
        ctrlTextAdd(&call->text, "stopped before reaching 0x%04X\n", stopAt[0]);
    }
    else if (expectSp >= 0 && sp != expectSp) {
        /* Recursion, or a jump to the same address, gets there with another SP. */
        ctrlTextAdd(&call->text, "reached 0x%04X but SP is 0x%04X, not 0x%04X: another "
                    "path got there first, not the return\n",
                    stopAt[reached], (unsigned)sp, (unsigned)expectSp);
    }
    else {
        ctrlTextAdd(&call->text, "%s to 0x%04X\n", over ? "stepped over" : "stepped out",
                    stopAt[reached]);
        jsonOutUInt(&call->out, "stopped_at", stopAt[reached]);
        /* It was our temporary breakpoint, so this is the step finishing. */
        if (!hadBreakpoint[reached]) {
            ctrlStopRelabel(stop->seq, CTRL_STOP_STEP);
            stop->reason = CTRL_STOP_STEP;
        }
    }
    jsonOutString(&call->out, "reason", ctrlStopReasonText(stop->reason));
    ctrlStatusDescribe(&call->text, stop->reason,
                       stop->reason == CTRL_STOP_MACHINE ? machineStopState(stop)
                                                         : EMU_PAUSED);
    ctrlWatchDescribeStop(&call->text, stop);
    return ctrlReplyDone(call);
}

/* Runs to the step's stops, each with a temporary breakpoint where none is set yet. */
static int waitAtStepStops(CtrlCall* call, const UInt16* stopAt, int stops, Int32 expectSp,
                           int over)
{
    CtrlStop stop;
    int hadBreakpoint[2] = { 0, 0 };
    int temporary;
    int refused;

    call->session->stopCursor = ctrlStopSeq();
    /* Kept, as the agent's own: the debugger window setting it meanwhile keeps its own. */
    ctrlStepSetStops(stopAt, stops, hadBreakpoint);
    temporary = stops - hadBreakpoint[0] - hadBreakpoint[1];
    if ((refused = refuseResume(call, dbgResume())) != 0) {
        ctrlStepClearStops(stopAt, stops, hadBreakpoint);
        return refused;
    }
    /* Bounded so a call that never returns does not hang the client. */
    if (!ctrlStopWait(call->session->stopCursor, 2000, &stop)) {
        ctrlStepClearStops(stopAt, stops, hadBreakpoint);
        return reportStepTimeout(call, stopAt, stops, temporary);
    }
    ctrlStepClearStops(stopAt, stops, hadBreakpoint);
    return reportStepStop(call, &stop, stopAt, stops, hadBreakpoint, expectSp, over);
}

/* how=step_over or step_out. Returns 0 when it stepped once and the run state is to be
** reported, else the reply. */
static int runStepOverOrOut(CtrlCall* call, const char* how)
{
    CtrlView view;
    UInt16 stopAt[2] = { 0, 0 };
    int    stops = 1;
    Int32  expectSp = -1;
    int    plainStep = 0;
    int    over = strcmp(how, "step_over") == 0;
    int    refused;
    int    stepped;

    if ((refused = refuseUnlessPaused(call)) != 0) {
        return refused;
    }
    if ((refused = ctrlCallTakeView(call, &view)) != 0) {
        return refused;
    }

    if (over) {
        /* A call returns with SP where it is now. */
        expectSp = ctrlCpuRegister("SP");
        plainStep = ctrlStepOverStops(&view, stopAt, &stops);
    }
    else if ((refused = stepOutTarget(call, stopAt, &expectSp)) != 0) {
        ctrlViewRelease(&view);
        return refused;
    }
    /* Decided; the step runs the machine, which no view may outlast. */
    ctrlViewRelease(&view);

    if (!plainStep) {
        return waitAtStepStops(call, stopAt, stops, expectSp, over);
    }
    if ((stepped = stepAndWait(call, 0, &refused)) == -1) {
        return refused;
    }
    if (stepped == 0) {
        ctrlTextAdd(&call->text, "the step did not come back to a stop, so it may have been "
                    "resumed or changed underneath\n");
        return replyLostStep(call);
    }
    return 0;
}

/* After a pause, a resume or a step of one instruction: what the next run meets at PC. */
static int reportRunState(CtrlCall* call, const char* how)
{
    CtrlView view = { 0 };
    int stepped = strcmp(how, "step") == 0 || strcmp(how, "step_over") == 0;

    /* A step ends on a parked machine; its PC and registers are read in one view, and the status
    ** waits for the park after a pause itself. A resume says running, so it reads no
    ** registers even from a stop met at once. */
    if (stepped) {
        ctrlViewTake(&view, DBG_ACCESS_VIEW, CTRL_VIEW_TIMEOUT_MS);
    }
    else if (strcmp(how, "resume") == 0) {
        ctrlViewTake(&view, DBG_ACCESS_HOLD, 0);
    }
    /* A step tests no breakpoint where it ends, so the next run does, before anything runs. */
    if (stepped) {
        Int32 pc = ctrlCpuPc();

        if (ctrlCpuStopsAtOnce(pc)) {
            ctrlTextAdd(&call->text, "PC 0x%04X has a breakpoint, checked before the instruction "
                        "there runs: resume, run_until, step_out and step_over on a call "
                        "stop here again at once; how=step runs it first\n", (unsigned)pc);
        }
    }
    /* A resume set its own, from before it: a stop met as it went is one to report. */
    if (strcmp(how, "resume") != 0) {
        call->session->stopCursor = ctrlStopSeq();
    }
    /* From the request, not emulatorGetState(), which lags the stop. */
    ctrlStatusDescribe(&call->text, CTRL_STOP_UNKNOWN,
                       strcmp(how, "resume") == 0 ? EMU_RUNNING : EMU_PAUSED);
    ctrlViewRelease(&view);
    return ctrlReplyDone(call);
}

int ctrlOpRun(CtrlCall* call)
{
    const char* how = ctrlCallText(call, "how");
    Int32  count = ctrlCallInt(call, "count", CTRL_RUN_COUNT_DEFAULT);
    int    replied = 0;
    UInt32 before = ctrlStopSeq();

    if (how == NULL) {
        return ctrlReplyError(call, "missing_how",
                              "\"how\" is needed: pause, resume, step, step_over or "
                              "step_out");
    }

    if (strcmp(how, "pause") == 0) {
        replied = refusePause(call, dbgPause());
    }
    else if (strcmp(how, "resume") == 0) {
        call->session->stopCursor = before;
        replied = refuseResume(call, dbgResume());
    }
    else if (strcmp(how, "step") == 0) {
        replied = runStep(call, count);
    }
    else if (strcmp(how, "step_over") == 0 || strcmp(how, "step_out") == 0) {
        replied = runStepOverOrOut(call, how);
    }
    else {
        return ctrlReplyError(call, "bad_how",
                              "\"how\" must be pause, resume, step, step_over or "
                              "step_out");
    }
    return replied != 0 ? replied : reportRunState(call, how);
}

/* Still in a stop nobody has read: reports it instead of running. Returns 0 when there
** is none, else the reply. */
static int reportUnreadStop(CtrlCall* call)
{
    CtrlStop stop;

    if (emulatorGetState() != EMU_PAUSED || !ctrlStopLatest(&stop) ||
        stop.seq <= call->session->stopCursor)
    {
        return 0;
    }
    call->session->stopCursor = stop.seq;
    jsonOutString(&call->out, "reason", ctrlStopReasonText(stop.reason));
    jsonOutUInt(&call->out, "stop_seq", stop.seq);
    ctrlTextAdd(&call->text, "this stop happened before the call and nothing ran; "
                "call again to run on\n");
    ctrlStatusDescribe(&call->text, stop.reason, EMU_PAUSED);
    ctrlWatchDescribeStop(&call->text, &stop);
    return ctrlReplyDone(call);
}

/* The wait ended with no stop: why, and what the machine is doing. Returns 0 when it is a
** plain timeout said in the text, else the refusal. */
static int reportNoStop(CtrlCall* call, Int32 timeout)
{
    int state = (int)emulatorGetState();

    if (ctrlClientAllBlocked()) {
        return ctrlReplyError(call, "revoked",
                              "agent control was turned off at the emulator while "
                              "this waited, so the wait ended before any stop");
    }
    if (ctrlPeerGone()) {
        return ctrlReplyError(call, "abandoned",
                              "the connection was closed while this waited, so the "
                              "wait ended before any stop");
    }
    jsonOutString(&call->out, "reason", "timeout");
    jsonOutUInt(&call->out, "stop_seq", ctrlStopSeq());
    if (state == EMU_STOPPED) {
        ctrlTextAdd(&call->text, "no machine has run for %d ms: the emulator is stopped, "
                    "or is being reset\n", (int)timeout);
    }
    else if (state == EMU_PAUSED) {
        ctrlTextAdd(&call->text, "the machine is paused, but no new stop was logged in "
                    "%d ms; the status report shows where it is\n", (int)timeout);
    }
    else {
        ctrlTextAdd(&call->text, "still running after %d ms\n", (int)timeout);
    }
    return 0;
}

int ctrlOpRunUntil(CtrlCall* call)
{
    CtrlStop stop;
    Int32 timeout = ctrlCallInt(call, "timeout_ms", CTRL_RUN_UNTIL_TIMEOUT_MS_DEFAULT);
    int   pauseAtTimeout = ctrlCallBool(call, "pause_at_timeout", 0);
    int   replied;

    if (timeout < CTRL_RUN_UNTIL_TIMEOUT_MS_MIN || timeout > CTRL_RUN_UNTIL_TIMEOUT_MS_MAX) {
        return ctrlReplyErrorf(call, "out_of_range", "timeout_ms must be between %d and %d",
                               CTRL_RUN_UNTIL_TIMEOUT_MS_MIN, CTRL_RUN_UNTIL_TIMEOUT_MS_MAX);
    }
    if ((replied = reportUnreadStop(call)) != 0) {
        return replied;
    }

    if (emulatorGetState() == EMU_PAUSED) {
        /* Only a stop from this run answers it. */
        call->session->stopCursor = ctrlStopSeq();
        if ((replied = refuseResume(call, dbgResume())) != 0) {
            return replied;
        }
    }
    else {
        /* Unread stops from earlier runs were resumed from, so they are not this one. */
        while (ctrlStopGet(call->session->stopCursor, &stop) &&
               stop.runGeneration != emulatorGetStateSeq())
        {
            call->session->stopCursor = stop.seq;
        }
    }

    /* Hold no view while waiting: the machine must run to reach a stop. */
    if (ctrlStopWait(call->session->stopCursor, timeout, &stop)) {
        call->session->stopCursor = stop.seq;
        jsonOutString(&call->out, "reason", ctrlStopReasonText(stop.reason));
        jsonOutUInt(&call->out, "stop_seq", stop.seq);
        /* Not emulatorGetState(), which lags the stop. */
        ctrlStatusDescribe(&call->text, stop.reason,
                           stop.reason == CTRL_STOP_MACHINE ? machineStopState(&stop)
                                                            : EMU_PAUSED);
        ctrlWatchDescribeStop(&call->text, &stop);
    }
    else if ((replied = reportNoStop(call, timeout)) != 0) {
        return replied;
    }
    else if (pauseAtTimeout && dbgPause()) {
        /* Its own pause is no stop to report later. */
        call->session->stopCursor = ctrlStopSeq();
        ctrlTextAdd(&call->text, "paused it, as pause_at_timeout asked: read what you need, "
                    "then run on with bmsx_run_until\n");
        ctrlStatusDescribe(&call->text, CTRL_STOP_UNKNOWN, EMU_PAUSED);
    }
    if (ctrlStopDropped() > 0) {
        jsonOutUInt(&call->out, "stops_dropped", ctrlStopDropped());
    }
    return ctrlReplyDone(call);
}

int ctrlOpStatus(CtrlCall* call)
{
    CtrlStop stop;
    int reason = CTRL_STOP_UNKNOWN;
    int stateAsFound = (int)emulatorGetState();

    /* The stop the machine is in now, not the oldest unread one; seen here, it is read. */
    if (ctrlStopLatest(&stop)) {
        reason = stop.reason;
        if (stop.seq > call->session->stopCursor) {
            call->session->stopCursor = stop.seq;
        }
    }
    ctrlStatusDescribe(&call->text, reason, stateAsFound);
    /* Registers, the CPU and the VDP only from a paused machine. */
    if (call->view.held == DBG_ACCESS_VIEW) {
        /* A turbo R switches CPU, and CLKH/CLKL count the clock of the one running. */
        ctrlTextAdd(&call->text, "cpu:     %s\n", r800DebugIsR800() ? "R800" : "Z80");
        ctrlStatusDescribeVdp(&call->text);
    }
    else if (stateAsFound != EMU_STOPPED) {
        /* What a running machine may say: which one it is, and how far it has run. */
        ctrlTextAdd(&call->text, "machine: %s\n", propGetGlobalProperties()->emulation.machineName);
        ctrlTextAdd(&call->text, "frames:  %u\n", (unsigned)emulatorGetFramesRun());
        ctrlTextAdd(&call->text, "(registers, CPU and VDP omitted: the machine is running; pause "
                    "to read registers)\n");
    }
    if (reason == CTRL_STOP_WATCH) {
        ctrlWatchDescribeStop(&call->text, &stop);
    }
    jsonOutUInt(&call->out, "stop_seq", ctrlStopSeq());
    jsonOutUInt(&call->out, "machine_generation", dbgMachineGeneration());
    return ctrlReplyDone(call);
}
