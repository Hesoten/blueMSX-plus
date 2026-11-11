/*****************************************************************************
**
** This command engine may be used in any MSX emulator. It does not matter 
** under which conditions that the emulator gets distributed. Be it open-
** source or closed-source. Be it commercially or free-of-charge.
** 
** This command engine may not be used in any other kind of softwares.
** Using this command engine is at own risk. The author is not responsible 
** nor liable for any damages that may occur intentionally or unintentionally 
** from it's usage.
**
******************************************************************************
*/
#ifndef VDPCMD_H
#define VDPCMD_H

#include "MsxTypes.h"

struct DebugTraceClassDesc;

/* VDPCMD kinds. CMD at its end, time its start: a = CM | LO<<4 | fill<<8 | aborted<<9 | ARG<<16 | CL<<24,
** b = 1/8 ticks run; CONT a = NX | NY<<16, b = DX | DY<<16; BEGIN at start, a the same. PSET and
** POINT finish at once and give none. WAIT, the CPU polling CE: END a = polls, b = CM. */
#define VDPCMD_TRACE_CMD  1
#define VDPCMD_TRACE_WAIT 2

typedef struct VdpCmdState VdpCmdState;

/*************************************************************
** vdpCmdCreate
**
** Description:
**      Creates a V99x8 command engine. A timer running at
**      21477270 Hz drives the command engine. systemTime
**      is the initial time of this timer.
**************************************************************
*/
VdpCmdState* vdpCmdCreate(int vramSize, UInt8* vramPtr, UInt32 systemTime);

void vdpCmdDestroy(VdpCmdState* state);
void vdpCmdSetSecondary(VdpCmdState* state, const char* stateName);

/*************************************************************
** vdpCmdWrite
**
** Description:
**      Writes a new command to the VDP
**************************************************************
*/
void  vdpCmdWrite(VdpCmdState* state, UInt8 reg, UInt8 value, UInt32 systemTime);

/*************************************************************
** vdpCmdPeek
**
** Description:
**      Returns the current value of a VDP command register
**************************************************************
*/
UInt8 vdpCmdPeek(VdpCmdState* vdpCmd, UInt8 reg, UInt32 systemTime);

void vdpCmdSetVram256(VdpCmdState* state, int enable);
void vdpCmdSetInterleave(VdpCmdState* state, int enable);
void vdpCmdSetExpansionWindow(VdpCmdState* state, int enable);
void vdpCmdSetV9968(VdpCmdState* state, int enable);
void vdpCmdSetExtCommands(VdpCmdState* state, int enable);
void vdpCmdResetExtRegs(VdpCmdState* state);
void vdpCmdSetTextBackColor(VdpCmdState* state, int color);

int  vdpCmdGetEndFlag(VdpCmdState* state);
void vdpCmdClearEndFlag(VdpCmdState* state);
UInt32 vdpCmdGetEndTime(VdpCmdState* state);
void vdpCmdAddCredit(VdpCmdState* state, int units);
void vdpCmdTagFillSpan(VdpCmdState* state, int tag);
/* The trace link the command started next takes; 0 for the CPU's. */
void vdpCmdTagIssue(VdpCmdState* state, UInt32 link);
void vdpCmdSetTraceSource(VdpCmdState* state, UInt16 source);
const struct DebugTraceClassDesc* vdpCmdTraceClass(void);
void vdpCmdSetHighSpeed(VdpCmdState* state, int enable);
/* 0 display off, 1 sprites off, 2 sprite mode 1 or 2, 3 sprite mode 3. */
void vdpCmdSetSlotLoad(VdpCmdState* state, int load);

void vdpSetScreenMode(VdpCmdState* state, int screenMode, int commandEnable);
void vdpSetTimingMode(VdpCmdState* state, UInt8 timingMode);

/* Call for every CPU access to the VRAM port, which takes a slot the engine
** would otherwise have used. */
void vdpCmdStealAccessSlot(VdpCmdState* state);

/* VDP command-engine wait scaling: 100 = accurate, 0 = turbo. */
void vdpCmdSetWaitPct(int percent);
int  vdpCmdGetWaitPct(void);


UInt8 vdpGetStatus(VdpCmdState* state);
UInt16 vdpGetBorderX(VdpCmdState* state);
UInt8 vdpGetColor(VdpCmdState* state);

/*************************************************************
** vdpCmdExecute
**
** Description:
**      Executes a number of cycles of the VDP Command engine
**      (in 3579545 Hz)
**************************************************************
*/
void  vdpCmdExecute(VdpCmdState* state, UInt32 systemTime);


/*************************************************************
** vdpCmdFlush
**
** Description:
**      Flushes current VDP command
**************************************************************
*/
void  vdpCmdFlush(VdpCmdState* state);


/*************************************************************
** vdpCmdLoadState
**
** Description:
**      Loads the state of the command engine. 
**************************************************************
*/
void vdpCmdLoadState(VdpCmdState* state);


/*************************************************************
** vdpCmdSaveState
**
** Description:
**      Saves the state of the command engine.
**************************************************************
*/
void vdpCmdSaveState(VdpCmdState* state);

#endif /* VDPCMD_H */
