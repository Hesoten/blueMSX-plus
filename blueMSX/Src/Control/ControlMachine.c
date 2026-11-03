/*****************************************************************************
**
** Machine reset for agents.
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
#include "ControlMachine.h"
#include "ControlText.h"
#include "Actions.h"
#include "Board.h"
#include "Disk.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Properties.h"
#include "sramLoader.h"
#include "Utf8Conv.h"
#include <stdio.h>
#include <string.h>


/* Written by ctrlMachineReset on the window's thread; read once it is done. */
static int  resetOutcome;
static char resetReason[600];
static char resetNotes[2048];

/* A disk drive as the emulator's menus place it: drive A or B, or a hard disk unit. */
void ctrlMachineDriveName(int drive, char* out, int outLen)
{
    int hd   = (drive - MAX_FDC_COUNT) / MAX_DRIVES_PER_HD;
    int unit = (drive - MAX_FDC_COUNT) % MAX_DRIVES_PER_HD;
    char unitText[16] = "";

    if (unit != 0) {
        snprintf(unitText, sizeof(unitText), " unit %d", unit);
    }
    if (drive < MAX_FDC_COUNT) {
        snprintf(out, (size_t)outLen, "drive %c", 'A' + drive);
    }
    else if (hd < FIRST_INTERNAL_HD_INDEX) {
        snprintf(out, (size_t)outLen, "the hard disk%s of cartridge slot %d", unitText, hd + 1);
    }
    else {
        snprintf(out, (size_t)outLen, "internal hard disk %d%s", hd - FIRST_INTERNAL_HD_INDEX + 1,
                 unitText);
    }
}

/* The cartridge, disk or tape file the machine is built from that is not there,
** with where it is inserted. */
static int resetFindMissingFile(char* out, int outLen)
{
    Properties* properties = propGetGlobalProperties();
    FileProperties* media[PROP_MAX_CARTS + PROP_MAX_DISKS + PROP_MAX_TAPES];
    char  where[PROP_MAX_CARTS + PROP_MAX_DISKS + PROP_MAX_TAPES][64];
    char  member[512];
    int count = 0;
    int i;

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        if (!propertiesIsSpecialCartName(properties->media.carts[i].fileName)) {
            snprintf(where[count], sizeof(where[0]), "cartridge slot %d", i + 1);
            media[count++] = &properties->media.carts[i];
        }
    }
    for (i = 0; i < PROP_MAX_DISKS; i++) {
        if (!diskIsHostMediumName(properties->media.disks[i].fileName)) {
            ctrlMachineDriveName(i, where[count], sizeof(where[0]));
            media[count++] = &properties->media.disks[i];
        }
    }
    for (i = 0; i < PROP_MAX_TAPES; i++) {
        snprintf(where[count], sizeof(where[0]), "the tape");
        media[count++] = &properties->media.tapes[i];
    }
    for (i = 0; i < count; i++) {
        FileProperties* file = media[i];

        if (file->fileName[0] == 0) {
            continue;
        }
        if (file->fileNameInZip[0] != 0 && !fileExist(file->fileNameInZip, file->fileName)) {
            /* The name comes from the zip itself and may not be UTF-8. */
            AnyToUtf8(file->fileNameInZip, member, sizeof(member));
            snprintf(out, (size_t)outLen, "%s (%s in %s)", where[i], member, file->fileName);
            return 1;
        }
        if (file->fileNameInZip[0] == 0 && !fileExist(file->fileName, NULL)) {
            snprintf(out, (size_t)outLen, "%s (%s)", where[i], file->fileName);
            return 1;
        }
    }
    return 0;
}

/* What a hard reset did not start afresh: flash saved by earlier runs, and folder disks
** the drive kept, as keptFolder tells from before the reset. Here because the SRAM file
** name comes in a static buffer. */
static void tellResetLeftovers(const UInt8* keptFolder)
{
    Properties* properties = propGetGlobalProperties();
    CtrlText notes;
    int size;
    int i;

    ctrlTextInit(&notes, resetNotes, sizeof(resetNotes));
    for (i = 0; i < PROP_MAX_CARTS; i++) {
        const char* rom = properties->media.carts[i].fileNameInZip[0] != 0
                        ? properties->media.carts[i].fileNameInZip
                        : properties->media.carts[i].fileName;
        RomType type = boardGetRomType(i);
        /* The flash sizes their mappers give AmdFlash; these keep what the MSX flashed in a .flash file. */
        int overlaySize = type == ROM_FLASHROMSCC ? 0x1000000
                        : type == ROM_ASCII16X || type == ROM_YAMANOOTO ? 0x800000 : 0;
        const char* saved;

        if (rom[0] == 0 || propertiesIsSpecialCartName(rom)) {
            continue;
        }
        saved = sramCreateFilenameWithSuffix(rom, "", ".flash");
        if (overlaySize != 0 && sramFileStatus(saved, &size) == SRAM_FILE_PRESENT) {
            ctrlTextAdd(&notes, "flash: the sectors the MSX has flashed, kept in %s, are laid "
                        "over the ROM, so those sectors are not from the ROM file. ", saved);
        }
        else if (overlaySize != 0 || type == ROM_MEGAFLSHSCC || type == ROM_MEGAFLSHSCCPLUS ||
                 type == ROM_MANBOW2 || type == ROM_MANBOW2_V2 || type == ROM_HAMARAJANIGHT)
        {
            /* A whole image: the .flash kind takes only one of exactly the flash size. */
            saved = sramCreateFilenameWithSuffix(rom, "", ".sram");
            if (sramFileStatus(saved, &size) != SRAM_FILE_PRESENT || size == 0 ||
                (overlaySize != 0 && size != overlaySize))
            {
                continue;
            }
            ctrlTextAdd(&notes, "flash: this flash cartridge started from the flash image in "
                        "%s, saved when it was last switched off, not from the ROM file, so a "
                        "rebuilt ROM is not what runs. ", saved);
        }
        else {
            continue;
        }
        ctrlTextAdd(&notes, "The file is written again whenever the machine is switched off: to "
                    "start from the ROM alone, delete it while no machine runs (Emulation, Stop "
                    "in the emulator, or close it), then start the machine again.\n");
    }
    for (i = 0; i < MAXDRIVES; i++) {
        char drive[64];

        if (!diskIsDir(i) || !keptFolder[i]) {
            continue;
        }
        ctrlMachineDriveName(i, drive, sizeof(drive));
        if (diskDirPending(i)) {
            ctrlTextAdd(&notes, "disk: the folder %s in %s holds changes the MSX wrote, so the "
                        "drive kept the MSX's version: files changed in the folder since then "
                        "(a rebuilt .COM) are not on it, and nothing was written to the folder. "
                        "Ejecting it in the emulator writes the changes back or drops them (it "
                        "asks); after that the folder is read afresh.\n", diskDirPath(i), drive);
        }
        else {
            ctrlTextAdd(&notes, "disk: the folder %s in %s was kept as it was, as a reset does "
                        "not read a hard disk folder again: files changed in the folder since "
                        "the drive read it (a rebuilt .COM) are not on it. Eject it and insert "
                        "it again in the emulator to read it afresh.\n",
                        diskDirPath(i), drive);
        }
    }
}

void ctrlMachineReset(int hard)
{
    UInt8 keptFolder[MAXDRIVES];
    int   i;

    resetOutcome   = CTRL_MACHINE_RESET_DONE;
    resetReason[0] = 0;
    resetNotes[0]  = 0;

    if (!hard) {
        /* The emulator's soft reset starts a second machine over one that is not running. */
        if (emulatorGetState() != EMU_RUNNING) {
            resetOutcome = CTRL_MACHINE_RESET_NOT_RUNNING;
            return;
        }
        actionEmuResetSoft();
        return;
    }
    /* A file the build is rewriting would start the machine without it, or not at all. */
    if (resetFindMissingFile(resetReason, sizeof(resetReason))) {
        resetOutcome = CTRL_MACHINE_RESET_FILE_MISSING;
        return;
    }
    for (i = 0; i < MAXDRIVES; i++) {
        keptFolder[i] = (UInt8)diskDirKeptOverStart(i);
    }
    actionEmuResetHard();
    if (emulatorGetState() == EMU_STOPPED) {
        const char* machineName = propGetGlobalProperties()->emulation.machineName;

        resetOutcome = CTRL_MACHINE_RESET_NOT_STARTED;
        if (boardGetMissingFileCount() > 0) {
            snprintf(resetReason, sizeof(resetReason), "%s is missing", boardGetMissingFile(0));
        }
        else if (machineDiagnose(machineName) != MACHINE_LOAD_OK) {
            snprintf(resetReason, sizeof(resetReason),
                     "the machine \"%s\" could not be read from the Machines folder", machineName);
        }
        else {
            snprintf(resetReason, sizeof(resetReason), "the emulator gave no reason here");
        }
        return;
    }
    tellResetLeftovers(keptFolder);
}

int ctrlMachineResetOutcome(void)
{
    return resetOutcome;
}

const char* ctrlMachineResetReason(void)
{
    return resetReason;
}

const char* ctrlMachineResetNotes(void)
{
    return resetNotes;
}
