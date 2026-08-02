/*****************************************************************************
**
** Signal level cassette tape playback.
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
#ifndef TAPE_SIGNAL_H
#define TAPE_SIGNAL_H

#include "MsxTypes.h"
#include "Casette.h"

/* Tape waveform held as a list of polarity flip intervals in Z80 T-states at
** the nominal 3579545 Hz. */
#define TAPE_TSTATE_FREQ  3579545

/* A gigabyte of edges, orders of magnitude past the longest real tape. Builds
** that ask for more are rejected rather than allowed to overflow the counts. */
#define TAPE_MAX_PULSES   0x20000000ul

typedef enum {
    TAPE_SIG_NONE = 0,
    TAPE_SIG_CAS,
    TAPE_SIG_TSX,
    TAPE_SIG_WAV
} TapeSignalSource;

typedef struct TapeSignalBuilder TapeSignalBuilder;

/* Builder, used by the format parsers */
TapeSignalBuilder* tapeSignalBuilderCreate(void);
void tapeSignalBuilderDestroy(TapeSignalBuilder* b);
void tapeSignalBuilderReserve(TapeSignalBuilder* b, UInt32 slots);
void tapeSignalBuilderAddPulse(TapeSignalBuilder* b, UInt32 tstates);
void tapeSignalBuilderAddPulses(TapeSignalBuilder* b, UInt32 tstates, UInt32 count);
void tapeSignalBuilderAddSilenceMs(TapeSignalBuilder* b, UInt32 ms);
void tapeSignalBuilderMarkBytePos(TapeSignalBuilder* b, UInt32 byteOffset);
int  tapeSignalBuilderFailed(const TapeSignalBuilder* b);

/* Index entry for the tape position dialog. pos is a byte offset, the same
** unit tapeGetContent and tapeSetCurrentPos use. */
void tapeSignalBuilderAddIndex(TapeSignalBuilder* b, TapeContentType type,
                               const char* name, UInt32 byteOffset);

/* Formats without a byte stream position themselves on a synthetic axis of
** 1/128 s units, which is what the tape dialog displays as a time. */
UInt32 tapeSignalBuilderTimeAsByte(const TapeSignalBuilder* b);
UInt32 tapeSignalGetLengthAsByte(void);

/* Mount and eject. Install takes ownership of the builder either way. */
int  tapeSignalInstall(TapeSignalBuilder* b, TapeSignalSource source);
void tapeSignalEject(void);
int  tapeSignalIsActive(void);

/* True once the waveform has actually been played, so the signal cursor and
** not the BIOS trap's byte cursor is the authority on the tape position. */
int  tapeSignalIsDriving(void);

/* Called when the motor starts, so the owner can rebuild a stale waveform */
typedef void (*TapeSignalRefreshCb)(void);
void tapeSignalSetRefreshCallback(TapeSignalRefreshCb cb);

void  tapeSignalSetMotor(int on);
UInt8 tapeSignalReadBit(void);

/* Recording. The pin level comes from PPI port C bit 5 */
void tapeSignalSetRecordable(int on);

/* The format a recording belongs to when there is no waveform to take it from,
** as with an image that is still blank */
void tapeSignalSetBlankSource(TapeSignalSource source);
void tapeSignalWriteBit(int level);
int  tapeSignalRecordDirty(void);
int  tapeSignalSaveWav(const char* name);

/* Decodes the mounted waveform back to a CAS byte stream. The marker is the
** block separator Casette.c detected for this image. */
int  tapeSignalSaveCas(const char* name, const UInt8* marker, int markerSize,
                       int align8);

/* Describes a blank image and a recorded one the same way */
#define TAPE_WAV_HEADER_SIZE 44
void tapeSignalWavHeader(UInt8* header, UInt32 sampleCount);

UInt64 tapeSignalGetPosT(void);
void   tapeSignalSetPosT(UInt64 t);
void   tapeSignalSetPosByByte(UInt32 byteOffset);
UInt32 tapeSignalGetPosAsByte(void);

TapeContent* tapeSignalGetContent(int* count);

Int32* tapeSignalRenderAudio(Int32* buffer, UInt32 count);

void tapeSignalReset(void);
void tapeSignalSaveState(void);
void tapeSignalLoadState(void);

/* The state is read before the tape is mounted, so the restored position has
** to be applied once a waveform is in place. */
void tapeSignalApplyLoadedState(void);
void tapeSignalDropLoadedState(void);

#endif
