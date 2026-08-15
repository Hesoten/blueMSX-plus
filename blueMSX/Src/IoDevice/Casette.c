/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/IoDevice/Casette.c,v $
**
** $Revision: 1.18 $
**
** $Date: 2008-11-23 20:26:12 $
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
#include "Casette.h"
#include "TapeSignal.h"
#include "CasToWave.h"
#include "TsxParser.h"
#include "WavParser.h"
#include "Led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "Properties.h"
#include "SaveState.h"
#include "ziphelper.h"


// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"

static UInt8 hdrSVICAS[17] = { 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x7F};
static UInt8 hdrFMSX98[17] = { 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x48, 0x65, 0x61, 0x64, 0x65, 0x72, 0x5f };
static UInt8 hdrFMSXDOS[8] = { 0x1f,0xa6,0xde,0xba,0xcc,0x13,0x7d,0x74 };
static UInt8 hdrASCII[10]  = { 0xea,0xea,0xea,0xea,0xea,0xea,0xea,0xea,0xea,0xea };
static UInt8 hdrBINARY[10] = { 0xd0,0xd0,0xd0,0xd0,0xd0,0xd0,0xd0,0xd0,0xd0,0xd0 };
static UInt8 hdrBASIC[10]  = { 0xd3,0xd3,0xd3,0xd3,0xd3,0xd3,0xd3,0xd3,0xd3,0xd3 };
static char   tapeBaseDir[512];
static char   tapePosName[512];
static char   tapeName[512];
static int    tapeRdWr;
static TapeFormat tapeFormat;
static UInt8* tapeHeader;
static int    tapeHeaderSize;
static char*  ramImageBuffer = NULL;
static int    ramImageSize = 0;
static int    ramImagePos = 0;
static int    rewindNextInsert = 0;
static int    signalDirty = 0;

static void refreshSignal(void);

static char* stripPath(char* filename) {
    char* ptr = filename + strlen(filename) - 1;

    while (--ptr >= filename) {
        if (*ptr == '/' || *ptr == '\\') {
            return ptr + 1;
        }
    }

    return filename;
}

static int ramread(void* buf, int size, int* ramPos) {
    if (*ramPos > ramImageSize) {
        return 0;
    }
    if (*ramPos + size > ramImageSize) {
        size = ramImageSize - *ramPos;
    }

    memcpy(buf, ramImageBuffer + *ramPos, size);
    *ramPos += size;

    return size;
}

void tapeLoadState() {
    SaveState* state = saveStateOpenForRead("tape");

    ramImagePos = saveStateGet(state, "ramImagePos",  0);

    if (ramImagePos >= ramImageSize) {
        ramImagePos = 0;
    }
    saveStateClose(state);

    tapeSignalLoadState();
}

void tapeSaveState() {
    SaveState* state = saveStateOpenForWrite("tape");

    saveStateSet(state, "ramImagePos",  tapeGetCurrentPos());

    saveStateClose(state);

    tapeSignalSaveState();
}

int tapeIsSignalOnly(void)
{
    return tapeFormat == TAPE_TSX || tapeFormat == TAPE_WAV;
}

UInt8 tapeRead(UInt8* value)
{
    if (ramImageBuffer != NULL && !tapeIsSignalOnly()) {
        if (ramImagePos < ramImageSize) {
            *value = ramImageBuffer[ramImagePos++];
            ledSetCas(1);
            return 1;
        }
        return 0;
    }

    return 0;
}

UInt8 tapeWrite(UInt8 value)
{
    /* Once a recording holds what the byte image cannot, appending here would
    ** write into a buffer the eject no longer saves. Fail the save instead. */
    if (ramImageBuffer != NULL && !tapeIsSignalOnly() && !tapeSignalRecordDirty()) {
        if (ramImagePos >= ramImageSize) {
            char* newBuf = realloc(ramImageBuffer, ramImageSize + 128);
            if (newBuf) {
                ramImageBuffer = newBuf;
                memset(ramImageBuffer + ramImageSize, 0, 128);
                ramImageSize += 128;
            }
        }

        if (ramImagePos < ramImageSize) {
            ramImageBuffer[ramImagePos++] = value;
            signalDirty = 1;
            ledSetCas(1);
            return 1;
        }
        return 0;
    }

    return 0;
}

/* Build the waveform on the first motor start and after the byte image
** changed. Registered with TapeSignal, so it never runs mid playback and
** costs nothing on machines where the BIOS trap serves the load. */
static void refreshSignal(void)
{
    TapeSignalBuilder* builder;
    TapeSignalSource   source;

    /* A recording makes the mounted waveform newer than the bytes read here */
    if (!signalDirty || ramImageBuffer == NULL || tapeSignalRecordDirty()) {
        return;
    }

    if (tapeFormat == TAPE_TSX) {
        char err[128];
        builder = tsxToWave((UInt8*)ramImageBuffer, ramImageSize, err, sizeof(err));
        source  = TAPE_SIG_TSX;
    }
    else if (tapeFormat == TAPE_WAV) {
        char err[128];
        builder = wavToWave((UInt8*)ramImageBuffer, ramImageSize, err, sizeof(err));
        source  = TAPE_SIG_WAV;
    }
    else {
        builder = casToWave((UInt8*)ramImageBuffer, ramImageSize, tapeHeader, tapeHeaderSize);
        source  = TAPE_SIG_CAS;
    }

    if (builder == NULL || !tapeSignalInstall(builder, source)) {
        /* Nothing will ever match the stashed position now, so drop it instead
        ** of letting it apply to whatever tape is mounted next. */
        tapeSignalDropLoadedState();
        return;     /* keep signalDirty set so the next motor start retries */
    }

    signalDirty = 0;
    tapeSignalSetPosByByte(ramImagePos);
    tapeSignalApplyLoadedState();
}

UInt8 tapeReadHeader() 
{    
    if (ramImageBuffer != NULL) {
        UInt8 buf[32];
        int i;
        for (i = 0; i < tapeHeaderSize; i++) {
            if (!tapeRead(buf + i)) {
                return 0;
            }
        }
        
        while (memcmp(buf, tapeHeader, tapeHeaderSize)) {
            memmove(buf, buf + 1, tapeHeaderSize - 1);
            if (!tapeRead(buf + tapeHeaderSize - 1)) {
                return 0;
            }
        }
        return 1;
    }

    return 0;
}

UInt8 tapeWriteHeader() 
{
    if (ramImageBuffer != NULL) {
        int i;
        
        for (i = 0; i < tapeHeaderSize; i++) {
            if (!tapeWrite(tapeHeader[i])) {
                return 0;
            }
        }
        return 1;
    }

    return 0;
}

void tapeSetDirectory(char* baseDir, char* prefix) {
    strcpy(tapeBaseDir, baseDir);
}

/* Not TSX: it exists to preserve the exact timing of a real load, which a
** plain BIOS save would throw away. An empty deck stays silent too, or the
** tape output pin would conjure a waveform with no file behind it. */
static void updateRecordable(void)
{
    /* A blank image has no waveform to name the format for the deck */
    tapeSignalSetBlankSource(tapeFormat == TAPE_WAV ? TAPE_SIG_WAV : TAPE_SIG_CAS);
    tapeSignalSetRecordable(ramImageBuffer != NULL && tapeFormat != TAPE_TSX && tapeRdWr);
}

void tapeSetReadOnly(int readOnly)
{
    tapeRdWr = !readOnly;
    updateRecordable();
}

int tapeInsert(char *name, const char *fileInZipFile) 
{
    FILE* file;
    Properties* pProperties = propGetGlobalProperties();
    
    if (ramImageBuffer != NULL) {
        file = fopen(tapePosName, "w");
        if (file != NULL) {
            char buffer[32];
            sprintf(buffer, "POS:%d", tapeGetCurrentPos());
            fwrite(buffer, 1, 32, file);
            fclose(file);
        }

        /* Rewriting a WAV re-renders megabytes, so only once it has changed */
        if (*tapeName && tapeRdWr &&
            (tapeFormat != TAPE_WAV || tapeSignalRecordDirty())) {
            tapeSave(tapeName, tapeFormat);
        }

        free(ramImageBuffer);
        ramImageBuffer = NULL;
        ramImageSize   = 0;
    }

    *tapeName = 0;

    /* Cleared here rather than only on a successful load: the format decides
    ** whether the BIOS trap is installed, so an eject or an unreadable file
    ** must not leave the previous image's format behind. */
    tapeFormat     = TAPE_FMSXDOS;
    tapeHeader     = hdrFMSXDOS;
    tapeHeaderSize = sizeof(hdrFMSXDOS);

    tapeSignalEject();
    signalDirty = 0;
    updateRecordable();

    if(!name) {
        return 1;
    }

    // Create filename for tape position file
    sprintf(tapePosName, "%s" DIR_SEPARATOR "%s", tapeBaseDir, stripPath(name));
    if (fileInZipFile == NULL) {
        strcpy(tapeName, name);
    }
    else {
        strcat(tapePosName, stripPath((char*)fileInZipFile));
    }
    strcat(tapePosName, ".pos");

    ramImagePos = 0;

    // Load and verify tape position
    file = fopen(tapePosName, "rb");
    if (file != NULL) {
        char buffer[32] = { 0 };
        fread(buffer, 1, 31, file);
        sscanf(buffer, "POS:%d", &ramImagePos);
        fclose(file);
    }

    if (fileInZipFile != NULL) {
        /* Clamped further down instead, once the format is known: a signal only
        ** image counts the position in time units, not in file bytes. */
        ramImageBuffer = zipLoadFile(name, fileInZipFile, &ramImageSize);
    }
    else {
        file = fopen(name,"rb");
        if (file != NULL) {
            // Load file into RAM buffer
            fseek(file, 0, SEEK_END);
            ramImageSize = ftell(file);
            fseek(file, 0, SEEK_SET);
            ramImageBuffer = malloc(ramImageSize);
            if (ramImageBuffer != NULL) {
                if (ramImageSize != fread(ramImageBuffer, 1, ramImageSize, file)) {
                    free(ramImageBuffer);
                    ramImageBuffer = NULL;
                }
            }
            fclose(file);
        }
    }
    
    if (rewindNextInsert&&pProperties->cassette.rewindAfterInsert) ramImagePos=0;
    rewindNextInsert=0;

    if (ramImageBuffer != NULL &&
        (tsxIsTsxImage((UInt8*)ramImageBuffer, ramImageSize) ||
         wavIsWavImage((UInt8*)ramImageBuffer, ramImageSize))) {
        /* Checked before the scan below, which is O(size) and would also
        ** misread a signal only image as a CAS. */
        tapeFormat     = tsxIsTsxImage((UInt8*)ramImageBuffer, ramImageSize) ? TAPE_TSX : TAPE_WAV;
        tapeHeader     = NULL;
        tapeHeaderSize = 0;
    }
    else if (ramImageBuffer != NULL) {
        int cntFMSXDOS = 0;
        int cntFMSX98  = 0;
        int cntSVICAS  = 0;

        if (ramImageSize >= 17) {
            UInt8* ptr = ramImageBuffer + ramImageSize - 17;
            while (ptr >= ramImageBuffer) {
                if (!memcmp(ptr, hdrFMSXDOS, sizeof(hdrFMSXDOS))) {
                    cntFMSXDOS++;
                }
                if (!memcmp(ptr, hdrFMSX98, sizeof(hdrFMSX98))) {
                    cntFMSX98++;
                }
                if (!memcmp(ptr, hdrSVICAS, sizeof(hdrSVICAS))) {
                    cntSVICAS++;
                }
                ptr--;
            }
        }

        if (cntSVICAS > cntFMSXDOS && cntSVICAS > cntFMSX98) {
            tapeFormat     = TAPE_SVICAS;
            tapeHeader     = hdrSVICAS;
            tapeHeaderSize = sizeof(hdrSVICAS);
        }
        else if (cntFMSXDOS >= cntFMSX98) {
            tapeFormat     = TAPE_FMSXDOS;
            tapeHeader     = hdrFMSXDOS;
            tapeHeaderSize = sizeof(hdrFMSXDOS);
        }
        else {
            tapeFormat     = TAPE_FMSX98AT;
            tapeHeader     = hdrFMSX98;
            tapeHeaderSize = sizeof(hdrFMSX98);
        }
    }

    /* A signal only image counts in 1/128 s units, not in file bytes */
    if (!tapeIsSignalOnly() && ramImagePos > ramImageSize) {
        ramImagePos = ramImageSize;
    }

    if (ramImageBuffer != NULL) {
        tapeSignalSetRefreshCallback(refreshSignal);
        signalDirty = 1;
    }
    updateRecordable();

    return ramImageBuffer != NULL;
}

int tapeIsInserted()
{
    return ramImageBuffer != NULL || tapeSignalIsActive();
}

int tapeSave(char *name, TapeFormat format)
{
    FILE* file;
    int offset   = 0;
    int writePos = 0;
    UInt8* hdrData;
    int    hdrSize;

    if (ramImageBuffer == NULL) {
        return 0;
    }

    /* A recording lives as a waveform, not as the bytes still held here */
    if (format == TAPE_WAV) {
        return tapeSignalSaveWav(name);
    }

    switch (format) {
        case TAPE_FMSXDOS:
            hdrData = hdrFMSXDOS;
            hdrSize = sizeof(hdrFMSXDOS);
            break;
        case TAPE_FMSX98AT:
            hdrData = hdrFMSX98;
            hdrSize = sizeof(hdrFMSX98);
            break;
        case TAPE_SVICAS:
            hdrData = hdrSVICAS;
            hdrSize = sizeof(hdrSVICAS);
            break;
        default:
            return 0;
    }

    /* The marker of the format asked for, not of the image it came from */
    if (tapeSignalRecordDirty()) {
        return tapeSignalSaveCas(name, hdrData, hdrSize, format == TAPE_FMSXDOS);
    }

    /* A signal only image has no block marker, so the scan below would match at
    ** every offset and never advance. There is nothing to convert either: the
    ** buffer holds the source file, not a byte stream. */
    if (tapeIsSignalOnly()) {
        return 0;
    }

    file = fopen(name, "wb");
    if (file == NULL) {
        return 0;
    }

    while (offset < ramImageSize) {
        if (ramImageSize - offset >= tapeHeaderSize && !memcmp(ramImageBuffer + offset, tapeHeader, tapeHeaderSize)) {
            if (format == TAPE_FMSXDOS) {
                while (writePos & 7) {
                    UInt8 zero = 0;
                    fwrite(&zero, 1, 1, file);
                    writePos ++;
                }
            }

            fwrite(hdrData, 1, hdrSize, file);
            writePos += hdrSize;
            offset += tapeHeaderSize;
        }
        else {
            fwrite(ramImageBuffer + offset, 1, 1, file);
            writePos++;
            offset++;
        }
    }

    fclose(file);

    return 1;
}

/* A blank tape: CAS is an empty byte stream, WAV a header of zero samples */
int tapeImageCreate(const char* name, TapeFormat format)
{
    UInt8 hdr[TAPE_WAV_HEADER_SIZE];
    FILE* file;
    int   ok;

    if (name == NULL || (format != TAPE_WAV && format != TAPE_FMSXDOS)) {
        return 0;
    }

    file = fopen(name, "wb");
    if (file == NULL) {
        return 0;
    }
    if (format == TAPE_FMSXDOS) {
        fclose(file);
        return 1;
    }

    tapeSignalWavHeader(hdr, 0);
    ok = fwrite(hdr, 1, sizeof(hdr), file) == sizeof(hdr);
    fclose(file);
    return ok;
}

TapeFormat tapeGetFormat()
{
    return tapeFormat;
}

/* The waveform is built on the first tape read, so a freshly inserted signal
** only image has no length and no index yet. The position dialog asks for both
** with the emulation stopped, which is exactly when building it is safe. */
static void ensureSignal(void)
{
    if (signalDirty && tapeIsSignalOnly() && !tapeSignalIsDriving()) {
        refreshSignal();
    }
}

UInt32 tapeGetLength()
{
    if (tapeIsSignalOnly()) {
        ensureSignal();
        return tapeSignalGetLengthAsByte();
    }
    return ramImageSize;
}

TapeContent* tapeGetContent(int* count)
{
    static TapeContent tapeContent[1024];
    int  index = 0;
    char buffer[32];
    int  ramPos = 0;
    int  position = 0;
    int  skipNext = 0;

    memset(tapeContent, 0, sizeof(tapeContent));

    *count = 0;

    /* A signal only image has no byte stream to scan; the parser indexed it.
    ** Copied out because seeking can remount the tape and free that array. */
    if (tapeIsSignalOnly()) {
        int n;
        TapeContent* src;

        ensureSignal();
        src = tapeSignalGetContent(&n);
        if (n > 1024) {
            n = 1024;
        }
        memcpy(tapeContent, src, n * sizeof(TapeContent));
        *count = n;
        return tapeContent;
    }

    if (ramImageBuffer == NULL) {
        return tapeContent;
    }

    while (index < 1024 && ramread(buffer, tapeHeaderSize, &ramPos) == tapeHeaderSize) {
        if (!memcmp(buffer, tapeHeader, tapeHeaderSize)) {
            if (skipNext) {
                skipNext = 0;
            }
            else if (ramread(buffer, 10, &ramPos) == 10) {
                if (!memcmp(buffer, hdrASCII, 10)) {
                    ramread(tapeContent[index].fileName, 6, &ramPos);
                    tapeContent[index].type = TAPE_ASCII;
                    tapeContent[index++].pos = ramPos - 16 - tapeHeaderSize;

                    while (ramPos < ramImageSize && ramImageBuffer[ramPos] != 0x1a) {
                        ramPos++;
                    }

                    position = ramPos - 1;
                } 
                else if (!memcmp(buffer, hdrBINARY, 10)) {
                    ramread(tapeContent[index].fileName, 6, &ramPos); 
                    tapeContent[index].type = TAPE_BINARY;
                    tapeContent[index++].pos = ramPos - 16 - tapeHeaderSize;
                    skipNext = 1;
                }
                else if (!memcmp(buffer, hdrBASIC, 10)) {
                    ramread(tapeContent[index].fileName, 6, &ramPos); 
                    tapeContent[index].type = TAPE_BASIC;
                    tapeContent[index++].pos = ramPos - 16 - tapeHeaderSize;
                    skipNext = 1;
                }
                else {
                    strcpy(tapeContent[index].fileName, "");
                    tapeContent[index].type = TAPE_CUSTOM;
                    tapeContent[index++].pos = ramPos - 10 - tapeHeaderSize;
                }
            }
        }
        ramPos = ++position;
    }

    *count = index;

    return tapeContent;
}

/* Once the waveform drives the load the BIOS trap no longer advances
** ramImagePos, so the signal cursor becomes the authority. */
UInt32 tapeGetCurrentPos()
{
    if (tapeSignalIsDriving()) {
        ramImagePos = (int)tapeSignalGetPosAsByte();
    }
    return ramImagePos;
}

void tapeSetCurrentPos(int pos)
{
    if (pos >= 0 && pos <= (int)tapeGetLength()) {
        ramImagePos = pos;
        tapeSignalSetPosByByte(pos);
    }
}

void tapeRewindNextInsert(void)
{
	rewindNextInsert=1;
}
