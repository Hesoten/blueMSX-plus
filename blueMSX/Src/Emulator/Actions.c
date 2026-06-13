/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Emulator/Actions.c,v $
**
** $Revision: 1.80 $
**
** $Date: 2008-05-14 12:55:31 $
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
#include "Actions.h"
#include "MsxTypes.h"
#include "Switches.h"
#include "AudioMixer.h"
#include "Board.h"
#include "Casette.h"
#include "Debugger.h"
#include "Disk.h"
#include "FileHistory.h"
#include "LaunchFile.h"
#include "Emulator.h"
#include "InputEvent.h"
#include "VideoManager.h"
#include "VDP.h"
#include "../SoundChips/YM2413.h"
#include "../SoundChips/Y8950.h"

#include "ArchMenu.h"
#include "ArchDialog.h"
#include "ArchFile.h"
#include "ArchNotifications.h"
#include "ArchPrinter.h"
#include "ArchMidi.h"
#include "ArchInput.h"
#include "ArchVideoIn.h"

/* Required for langDlgSaveCapture* accessors used in prompt-mode capture flow;
** without it C returns implicit int and truncates the pointer on x64. */
#include "Language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Route fopen() through pkg_fopen so UTF-8 paths reach _wfopen (handles
** ROM names with non-ACP characters). Must come after <stdio.h>. */
#include "PacketFileSystem.h"

static struct {
    Properties* properties;
    Video* video;
    Mixer* mixer;
    int mouseLock;
    int windowedSize;
} state;

static char audioDir[PROP_MAXPATH]  = "";
static char audioPrefix[64]         = "";
static char videoDir[PROP_MAXPATH]  = "";
static char videoPrefix[64]         = "";
char stateDir[PROP_MAXPATH]  = "";
char statePrefix[64]         = "";


void actionCartInsert(int cartNo)
{
    RomType romType;
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenRom(state.properties, cartNo, &romType);
    if (filename != NULL) {        
        insertCartridge(state.properties, cartNo, filename, NULL, romType, 0);
    }
    else {
        emulatorResume();
    }
    archUpdateMenu(0);
}

void actionDiskInsert(int diskNo)
{
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenDisk(state.properties, diskNo, 0);
    if (filename != NULL) {        
        insertDiskette(state.properties, diskNo, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionDiskInsertNew(int diskNo)
{
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenDisk(state.properties, diskNo, 1);
    if (filename != NULL) {        
        insertDiskette(state.properties, diskNo, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionDiskInsertDir(int diskNo)
{
    char* filename;

    emulatorSuspend();
    filename = archDirnameGetOpenDisk(state.properties, diskNo);
    if (filename != NULL) {        
        strcpy(state.properties->media.disks[diskNo].directory, filename);
        diskPreviewDirOverflow(diskNo, filename);
        insertDiskette(state.properties, diskNo, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionDiskRemove(int i) {
    state.properties->media.disks[i].fileName[0] = 0;
    state.properties->media.disks[i].fileNameInZip[0] = 0;
    updateExtendedDiskName(i, state.properties->media.disks[i].fileName, state.properties->media.disks[i].fileNameInZip);
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
        boardChangeDiskette(i, NULL, NULL);
        emulatorResume();
    }
    archUpdateMenu(0);
}

void actionHarddiskInsert(int diskNo)
{
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenHarddisk(state.properties, diskNo, 0);
    if (filename != NULL) {        
        insertDiskette(state.properties, diskNo, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionHarddiskInsertCdrom(int diskNo)
{
    emulatorSuspend();
    insertDiskette(state.properties, diskNo, DISK_CDROM, NULL, 0);
    emulatorResume();
    archUpdateMenu(0);
}

void actionHarddiskInsertNew(int diskNo)
{
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenHarddisk(state.properties, diskNo, 1);
    if (filename != NULL) {        
        insertDiskette(state.properties, diskNo, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionHarddiskInsertDir(int diskNo)
{
}

void actionHarddiskRemove(int diskNo)
{
    state.properties->media.disks[diskNo].fileName[0] = 0;
    state.properties->media.disks[diskNo].fileNameInZip[0] = 0;
    updateExtendedDiskName(diskNo, state.properties->media.disks[diskNo].fileName, state.properties->media.disks[diskNo].fileNameInZip);
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
        boardChangeDiskette(diskNo, NULL, NULL);
        emulatorResume();
    }
    archUpdateMenu(0);
}

void actionHarddiskRemoveAll()
{
    int i, j;
    int diskNo;
    int flag;

    flag = (emulatorGetState() != EMU_STOPPED);
    if (flag) emulatorSuspend();

    for (i = 0; i < MAX_HD_COUNT; i++) {
        //if (boardGetHdType(i) != HD_NONE) {
            for (j = 0; j < MAX_DRIVES_PER_HD; j++) {
                diskNo = diskGetHdDriveId(i, j);
                if (state.properties->media.disks[diskNo].fileName) {
                    state.properties->media.disks[diskNo].fileName[0] = 0;
                    state.properties->media.disks[diskNo].fileNameInZip[0] = 0;
                    updateExtendedDiskName(diskNo, state.properties->media.disks[diskNo].fileName, state.properties->media.disks[diskNo].fileNameInZip);
                    if (flag) boardChangeDiskette(diskNo, NULL, NULL);
                }
            }
        //}
    }
    if (flag) emulatorResume();
    archUpdateMenu(0);
}

void actionSetAudioCaptureSetDirectory(char* dir, char* prefix)
{
    strcpy(audioDir, dir);
    strcpy(audioPrefix, prefix);
}

void actionSetVideoCaptureSetDirectory(char* dir, char* prefix)
{
    strcpy(videoDir, dir);
    strcpy(videoPrefix, prefix);
}

const char* actionGetAudioCaptureDir(void)
{
    return audioDir;
}

const char* actionGetVideoCaptureDir(void)
{
    return videoDir;
}

void actionSetQuickSaveSetDirectory(char* dir, char* prefix)
{
    strcpy(stateDir, dir);
    strcpy(statePrefix, prefix);
}

void actionInit(Video* video, Properties* properties, Mixer* mixer)
{
    memset(&state, 0, sizeof(state));

    state.properties = properties;
    state.video      = video;
    state.mixer      = mixer;

    state.windowedSize = properties->video.windowSize != P_VIDEO_SIZEFULLSCREEN ?
                         properties->video.windowSize : P_VIDEO_SIZEX2;
}

void actionToggleSpriteEnable() {
    vdpSetSpritesEnable(!vdpGetSpritesEnable());
}

void actionToggleNoSpriteLimits() {
    vdpSetNoSpriteLimits(!vdpGetNoSpritesLimit());
}

void actionToggleMsxKeyboardQuirk() {
    state.properties->keyboard.enableKeyboardQuirk = !state.properties->keyboard.enableKeyboardQuirk;
}

void actionToggleMsxAudioSwitch() {
    state.properties->emulation.audioSwitch = !state.properties->emulation.audioSwitch;
    switchSetAudio(state.properties->emulation.audioSwitch);
}

void actionToggleFrontSwitch() {
    state.properties->emulation.frontSwitch = !state.properties->emulation.frontSwitch;
    switchSetFront(state.properties->emulation.frontSwitch);
}

void actionTogglePauseSwitch() {
    state.properties->emulation.pauseSwitch = !state.properties->emulation.pauseSwitch;
    switchSetPause(state.properties->emulation.pauseSwitch);
}

void actionToggleFdcTiming() {
    state.properties->emulation.enableFdcTiming = !state.properties->emulation.enableFdcTiming;
    boardSetFdcTimingEnable(state.properties->emulation.enableFdcTiming);
}

void actionToggleHddSdBoost() {
    state.properties->emulation.enableHddSdBoost = !state.properties->emulation.enableHddSdBoost;
    boardSetHddSdBoostEnable(state.properties->emulation.enableHddSdBoost);
}


void actionToggleHorizontalStretch() {
    state.properties->video.horizontalStretch = !state.properties->video.horizontalStretch;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionToggleVerticalStretch() {
    state.properties->video.verticalStretch = !state.properties->video.verticalStretch;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionToggleScanlinesEnable() {
    state.properties->video.scanlinesEnable = !state.properties->video.scanlinesEnable;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionToggleDeinterlaceEnable() {
    state.properties->video.deInterlace = !state.properties->video.deInterlace;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionToggleBlendFrameEnable() {
    state.properties->video.blendFrames = !state.properties->video.blendFrames;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionToggleRfModulatorEnable() {
    state.properties->video.colorSaturationEnable = !state.properties->video.colorSaturationEnable;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}


void actionQuit() {
    archQuit();
}

/* Extract the basename from a full path produced by generateSaveFilename
** so it can be pre-filled into a Save As dialog. Returns a pointer into the
** input string; do not free. */
static const char* captureBasename(const char* fullPath) {
    const char* sep = strrchr(fullPath, '\\');
    const char* fwd = strrchr(fullPath, '/');
    if (fwd > sep) sep = fwd;
    return sep ? sep + 1 : fullPath;
}

/* Resolve capture filename: pop Save-As with auto-name pre-filled when prompt
** or alwaysPrompt is on, else return auto-name. NULL on dialog cancel. */
static char* resolveCaptureFilename(int prompt, int alwaysPrompt,
                                    const char* dialogTitle,
                                    const char* dir, const char* prefix,
                                    const char* extension,
                                    const char* fileTypeLabel) {
    char* autoName = generateSaveFilename(state.properties, (char*)dir,
                                           (char*)prefix, (char*)extension, 2);
    if (!(prompt || alwaysPrompt)) return autoName;

    return archFilenameGetSaveCapture(state.properties,
                                       dialogTitle,
                                       dir,
                                       captureBasename(autoName),
                                       extension,
                                       fileTypeLabel);
}

/* Stash for the most recent .wav path so the stop-side can toast the user
** with the saved location. Mixer's stop API doesn't return a filename. */
static char lastWavCapturePath[PROP_MAXPATH] = "";

static void waveCaptureStart(int alwaysPrompt) {
    char* fname = resolveCaptureFilename(state.properties->capture.audioPromptFilename,
                                          alwaysPrompt,
                                          langDlgSaveCaptureAudio(),
                                          audioDir, audioPrefix, ".wav", "WAV Audio");
    if (!fname) return;
    strncpy(lastWavCapturePath, fname, sizeof(lastWavCapturePath) - 1);
    lastWavCapturePath[sizeof(lastWavCapturePath) - 1] = 0;
    mixerStartLog(state.mixer, fname);
}

void actionToggleWaveCapture() {
    if (mixerIsLogging(state.mixer)) {
        mixerStopLog(state.mixer);
        if (state.properties->capture.showCompletionToast && lastWavCapturePath[0]) {
            archCaptureToastSaved(lastWavCapturePath);
        }
        lastWavCapturePath[0] = 0;
    }
    else {
        waveCaptureStart(0);
    }
    archUpdateMenu(0);
}

void actionWaveCaptureStartAs() {
    if (mixerIsLogging(state.mixer)) {
        /* Warning toast: always shown; showCompletionToast gates only save-completion. */
        archCaptureToastInfo(langInfoToastAlreadyRecording());
        return;
    }
    waveCaptureStart(1);
    archUpdateMenu(0);
}

void actionVideoCaptureLoad() {
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenCapture(state.properties);
    if (filename != NULL) {
        strcpy(state.properties->filehistory.videocap, filename);
        emulatorStop();
        emulatorStart(filename);
    }
    else {
        emulatorResume();
    }
    archUpdateMenu(0);
}

void actionVideoCapturePlay() {
    char* fname = state.properties->filehistory.videocap;

    /* Check the .cap exists *before* stopping the running emulator. The old
    ** behaviour silently stopped the emu when the file was missing, which
    ** looked like the menu just froze the session for no reason. */
    if (fname[0] == 0 || !fileExist(fname, NULL)) {
        archReplayMissing(fname);
        return;
    }

    if (emulatorGetState() != EMU_STOPPED) {
        emulatorStop();
    }
    emulatorStart(fname);
    archUpdateMenu(0);
}

void actionVideoCaptureSave() {
    /* The renderer now picks the source replay (.cap) from disk inside its
    ** own dialog, so no in-memory capture is required to enter the flow. */
    archVideoCaptureSave();
}

static void videoRecordStart(int alwaysPrompt) {
    char* fname = NULL;
    int prompt = state.properties->capture.videoPromptFilename || alwaysPrompt;

    if (prompt) {
        /* Empty prefix so the suggested name matches the recorder's auto
        ** path (machine_NN.mp4), consistent with audio / replay naming. */
        char* autoName = generateSaveFilename(state.properties, videoDir,
                                               (char*)"", (char*)".mp4", 2);
        fname = archFilenameGetSaveCapture(state.properties,
                                            langDlgSaveCaptureVideo(),
                                            videoDir,
                                            captureBasename(autoName),
                                            ".mp4", "MP4 Video");
        if (!fname) return;     /* user cancelled */
    }
    archRecordVideoStart(fname);    /* NULL = let the recorder auto-name */
    archUpdateMenu(0);
}

/* Warning toast: always shown regardless of showCompletionToast (which
** gates only the save-completion info toasts). */
static void notifyAlreadyRecording(void) {
    archCaptureToastInfo(langInfoToastAlreadyRecording());
}

void actionRecordVideoStart(void) {
    if (archRecordVideoIsActive()) { notifyAlreadyRecording(); return; }
    videoRecordStart(0);
}

void actionRecordVideoStartAs(void) {
    if (archRecordVideoIsActive()) { notifyAlreadyRecording(); return; }
    videoRecordStart(1);
}

void actionRecordVideoStop(void) {
    if (!archRecordVideoIsActive()) return;
    archRecordVideoStop();
    archUpdateMenu(0);
}

void actionRecordVideoToggle(void) {
    if (archRecordVideoIsActive()) actionRecordVideoStop();
    else                            actionRecordVideoStart();
}

void actionYm2413BackendCycle(void) {
    /* Walk to the next enabled backend and persist so the chip starts on
    ** the same backend after restart. */
    Properties* p = propGetGlobalProperties();
    int next = ym2413BackendCycle();
    if (p) p->sound.chip.ym2413BackendActive = next;
}

void actionY8950BackendCycle(void) {
    Properties* p = propGetGlobalProperties();
    int next = y8950BackendCycle();
    if (p) p->sound.chip.y8950BackendActive = next;
}

/* Success/failure toast for a just-finalized replay .cap. boardCaptureStop
** swallows fopen("wb") errors on missing / unwritable target dir, so
** existence-check the path before claiming success. Gated by
** capture.showCompletionToast (mirrors other capture-completion toasts). */
static void replayEmitCompletionToast(const char* fname) {
    if (!fname || !fname[0]) return;
    if (!fileExist((char*)fname, NULL)) {
        archReplaySaveFailure(fname);
    }
    else if (state.properties && state.properties->capture.showCompletionToast) {
        archCaptureToastSaved(fname);
    }
}

void actionVideoCaptureStop() {
    if (emulatorGetState() == EMU_STOPPED) {
        return;
    }
    
    emulatorSuspend();
    
    boardCaptureStop();

    emulatorResume();
    archUpdateMenu(0);

    /* boardCaptureStop queued the completion for the drain path; the
    ** consume-side handles the "was really recording" gate implicitly
    ** (Play-mode stop leaves the pending file clear). */
    actionReplayFlushCompletionToast();
}

void actionReplayFlushCompletionToast(void) {
    char fname[PROP_MAXPATH];
    if (boardCaptureConsumePendingToast(fname, sizeof(fname))) {
        replayEmitCompletionToast(fname);
    }
}

/* Preflight fopen("wb") on the .cap path before boardCaptureStart buffers
** anything; the empty file gets overwritten in boardCaptureStop. */
static int actionReplayPreflightWrite(const char* fname) {
    FILE* f = fopen(fname, "wb");
    if (!f) {
        archReplaySaveFailure(fname);
        return 0;
    }
    fclose(f);
    return 1;
}

/* Pick the .cap path for a replay-record start; stores into
** filehistory.videocap. Returns 0 on Save-As dialog cancel. */
static int replayRecResolveFilename(int alwaysPrompt) {
    char* slot = state.properties->filehistory.videocap;
    int prompt = state.properties->capture.replayPromptFilename || alwaysPrompt;
    char* picked;
    const char* dir = state.properties->capture.replayDir[0]
                      ? state.properties->capture.replayDir : videoDir;

    if (!prompt) {
        strcpy(slot, generateSaveFilename(state.properties, videoDir, videoPrefix, ".cap", 2));
        return 1;
    }

    {
        char* autoName = generateSaveFilename(state.properties, videoDir, videoPrefix, ".cap", 2);
        picked = archFilenameGetSaveCapture(state.properties,
                                             langDlgSaveCaptureReplay(),
                                             dir,
                                             captureBasename(autoName),
                                             ".cap", "Replay");
    }
    if (!picked) return 0;
    strncpy(slot, picked, PROP_MAXPATH - 1);
    slot[PROP_MAXPATH - 1] = 0;
    return 1;
}

static void videoCaptureRecStart(int alwaysPrompt) {
    char* fname = state.properties->filehistory.videocap;

    /* Already recording: bail before any Save-As dialog or preflight so
    ** the user doesn't get a misleading prompt that ends up no-op'd by
    ** boardCaptureStart's CAPTURE_REC guard. Toast (warning) for clarity. */
    if (boardCaptureIsRecording()) {
        archCaptureToastInfo(langInfoToastAlreadyRecording());
        return;
    }

    if (emulatorGetState() == EMU_STOPPED) {
        if (!replayRecResolveFilename(alwaysPrompt)) return;
        if (!actionReplayPreflightWrite(fname)) return;
        boardCaptureStart(fname);
        actionEmuTogglePause();
        archUpdateMenu(0);
        return;
    }

    emulatorSuspend();
    if (replayRecResolveFilename(alwaysPrompt) && actionReplayPreflightWrite(fname)) {
        boardCaptureStart(fname);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionVideoCaptureRec() {
    videoCaptureRecStart(0);
}

void actionVideoCaptureRecAs() {
    videoCaptureRecStart(1);
}

void actionLoadState() {
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenState(state.properties);
    if (filename != NULL) {
        emulatorStop();
        emulatorStart(filename);
    }
    else {
        emulatorResume();
    }
    archUpdateMenu(0);
}

void actionSaveState() {
    char* filename;

    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
        filename = archFilenameGetSaveState(state.properties);
        if (filename != NULL && strlen(filename) != 0) {
            char *ptr = filename + strlen(filename) - 1;
            while(*ptr != '.' && ptr > filename) {
                ptr--;
            }
            if (ptr == filename) {
                ptr = filename + strlen(filename);
            }

            strcpy(ptr, ".sta");
            boardSaveState(filename, 1);
        }
        emulatorResume();
    }
}

void actionQuickLoadState() {
    if (fileExist(state.properties->filehistory.quicksave, NULL)) {
        emulatorStop();
        emulatorStart(state.properties->filehistory.quicksave);
    }
    archUpdateMenu(0);
}

void actionQuickSaveState() {
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
        strcpy(state.properties->filehistory.quicksave, generateSaveFilename(state.properties, stateDir, statePrefix, ".sta", 2));
        boardSaveState(state.properties->filehistory.quicksave, 1);
        emulatorResume();
    }
}

void actionQuickSaveStateUndo() {
    if (emulatorGetState() != EMU_STOPPED) {
        // what this does:
        // convert "c:\blah\states\blah_19.sta" to "c:\blah\states\blah_18.sta"
        // if its at "blah_00.sta" and "blah_99.sta" exists, then wrap around
        // (as quicksavestate goes from 99 -> 00)
        if (state.properties->filehistory.quicksave && strlen(state.properties->filehistory.quicksave) > 10) {
            char numstr[5], *oldstatefilename;
            int numstrtonum;
            int qslen=strlen(state.properties->filehistory.quicksave)-6; // focus on the 2 numbers before the ext
            oldstatefilename = strdup(state.properties->filehistory.quicksave);
            memset(&numstr, 0, sizeof(numstr));
            strncpy(numstr, state.properties->filehistory.quicksave+qslen, 2);
            numstrtonum = atoi(numstr);
            if (numstrtonum>0) {
                numstrtonum--;
            } else { //wrap-around to 99 if it exists!
                state.properties->filehistory.quicksave[qslen]='9';
                state.properties->filehistory.quicksave[qslen+1]='9';
                if (archFileExists(state.properties->filehistory.quicksave)) {
                    archFileDelete(oldstatefilename);
                    free(oldstatefilename);
                    return;
                }
            }
            state.properties->filehistory.quicksave[qslen]='0'+(numstrtonum/10);
            state.properties->filehistory.quicksave[qslen+1]='0'+(numstrtonum%10);
            if (archFileExists(state.properties->filehistory.quicksave) &&
                        strcmp(oldstatefilename, state.properties->filehistory.quicksave)) {
                archFileDelete(oldstatefilename);
            } else { // no state to go back to, keep filehistory.quicksave the same
                state.properties->filehistory.quicksave[qslen]=oldstatefilename[qslen];
                state.properties->filehistory.quicksave[qslen+1]=oldstatefilename[qslen+1];
            }
            free(oldstatefilename);
        }
    }
}

void actionCartInsert1() {
    actionCartInsert(0);
}

void actionCartInsert2() {
    actionCartInsert(1);
}

void actionToggleMouseCapture() {
    state.mouseLock ^= 1;
    archMouseSetForceLock(state.mouseLock);
}

void actionEmuStep() {
    if (emulatorGetState() == EMU_PAUSED) {
        emulatorSetState(EMU_STEP);
    }
}

void actionEmuStepBack() {
    if (emulatorGetState() == EMU_PAUSED) {
        emulatorSetState(EMU_STEP_BACK);
    }
}

void actionEmuTogglePause() {
    if (emulatorGetState() == EMU_STOPPED) {
        emulatorStart(NULL);
    }
    else if (emulatorGetState() == EMU_PAUSED) {
        emulatorSetState(EMU_RUNNING);
        debuggerNotifyEmulatorResume();
    }
    else {  
        emulatorSetState(EMU_PAUSED);
        debuggerNotifyEmulatorPause();
    }
    archUpdateMenu(0);
}

void actionEmuStop() {
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorStop();
    }
    archUpdateMenu(0);
}

void actionDiskDirInsertA() {
    actionDiskInsertDir(0);
}

void actionDiskDirInsertB() {
    actionDiskInsertDir(1);
}

void actionDiskInsertA() {
    actionDiskInsert(0);
}

void actionDiskInsertB() {
    actionDiskInsert(1);
}

void actionMaxSpeedSet() {
    emulatorSetMaxSpeed(1);
}

void actionMaxSpeedRelease() {
    emulatorSetMaxSpeed(0);
}

void actionStartPlayReverse()
{
    emulatorPlayReverse(1);
}

void actionStopPlayReverse()
{
    emulatorPlayReverse(0);
}

void actionDiskQuickChange() {
    if (*state.properties->media.disks[0].fileName) {
        if (*state.properties->media.disks[0].fileNameInZip) {
            strcpy(state.properties->media.disks[0].fileNameInZip, fileGetNext(state.properties->media.disks[0].fileNameInZip, state.properties->media.disks[0].fileName));
#ifdef WII
            archDiskQuickChangeNotify(0, state.properties->media.disks[0].fileName, state.properties->media.disks[0].fileNameInZip);
#endif
            boardChangeDiskette(0, state.properties->media.disks[0].fileName, state.properties->media.disks[0].fileNameInZip);
            updateExtendedDiskName(0, state.properties->media.disks[0].fileName, state.properties->media.disks[0].fileNameInZip);
        }
        else {
            strcpy(state.properties->media.disks[0].fileName, fileGetNext(state.properties->media.disks[0].fileName, NULL));
#ifdef WII
            archDiskQuickChangeNotify(0, state.properties->media.disks[0].fileName, state.properties->media.disks[0].fileNameInZip);
#endif
            boardChangeDiskette(0, state.properties->media.disks[0].fileName, NULL);
            updateExtendedDiskName(0, state.properties->media.disks[0].fileName, state.properties->media.disks[0].fileNameInZip);
        }
#ifndef WII
        archDiskQuickChangeNotify();
#endif
    }
    archUpdateMenu(0);
}

void actionChangeWindowSize(int zoom) {
    if (zoom != P_VIDEO_SIZEFULLSCREEN) {
        state.windowedSize = zoom;
    }
    if (state.properties->video.windowSize != zoom) {
        state.properties->video.windowSize = zoom;
        state.properties->video.windowSizeChanged = 1;
        archUpdateWindow();
    }
}

void actionWindowSize1x()     { actionChangeWindowSize(P_VIDEO_SIZEX1); }
void actionWindowSize2x()     { actionChangeWindowSize(P_VIDEO_SIZEX2); }
void actionWindowSize3x()     { actionChangeWindowSize(P_VIDEO_SIZEX3); }
void actionWindowSize4x()     { actionChangeWindowSize(P_VIDEO_SIZEX4); }
void actionWindowSize5x()     { actionChangeWindowSize(P_VIDEO_SIZEX5); }
void actionWindowSize6x()     { actionChangeWindowSize(P_VIDEO_SIZEX6); }
void actionWindowSize7x()     { actionChangeWindowSize(P_VIDEO_SIZEX7); }
void actionWindowSize8x()     { actionChangeWindowSize(P_VIDEO_SIZEX8); }

void actionWindowSizeFullscreen() {
    actionChangeWindowSize(P_VIDEO_SIZEFULLSCREEN);
}

void actionWindowSizeMinimized() {
    archMinimizeMainWindow();
}

void actionMaxSpeedToggle() {
    emulatorSetMaxSpeed(emulatorGetMaxSpeed() ? 0 : 1);
}

void actionFullscreenToggle() {
    if (state.properties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        actionChangeWindowSize(state.windowedSize);
    }
    else {
        actionWindowSizeFullscreen();
    }
    archUpdateMenu(0);
}

void actionEmuSpeedNormal() {
    state.properties->emulation.speed = 50;
    emulatorSetFrequency(state.properties->emulation.speed, NULL);
}

void actionEmuSpeedDecrease() {
    if (state.properties->emulation.speed > 0) {
        state.properties->emulation.speed--;
        emulatorSetFrequency(state.properties->emulation.speed, NULL);
    }
}

void actionEmuSpeedIncrease() {
    if (state.properties->emulation.speed < 100) {
        state.properties->emulation.speed++;
        emulatorSetFrequency(state.properties->emulation.speed, NULL);
    }
}

void actionCasInsert() {
    char* filename;

    emulatorSuspend();
    filename = archFilenameGetOpenCas(state.properties);
    if (filename != NULL) {
        if (state.properties->cassette.rewindAfterInsert) tapeRewindNextInsert();
        insertCassette(state.properties, 0, filename, NULL, 0);
    }
    emulatorResume();
    archUpdateMenu(0);
}

void actionCasRewind() {
    if (emulatorGetState() != EMU_STOPPED) {
            emulatorSuspend();
        }
        else {
            tapeSetReadOnly(1);
            boardChangeCassette(0, strlen(state.properties->media.tapes[0].fileName) ? state.properties->media.tapes[0].fileName : NULL, 
                                strlen(state.properties->media.tapes[0].fileNameInZip) ? state.properties->media.tapes[0].fileNameInZip : NULL);
        }
        tapeSetCurrentPos(0);
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorResume();
    }
    else {
        boardChangeCassette(0, NULL, NULL);
        tapeSetReadOnly(state.properties->cassette.readOnly);
    }
    archUpdateMenu(0);
}

void actionCasSetPosition() {
    archShowCassettePosDialog();
}

void actionEmuResetSoft() {
    archUpdateMenu(0);
    if (emulatorGetState() == EMU_RUNNING) {
        emulatorSuspend();
        boardReset();
        debuggerNotifyEmulatorReset();
        emulatorResume();
    }
    else {
        emulatorStart(NULL);
    }
    archUpdateMenu(0);
}

void actionEmuResetHard() {
    archUpdateMenu(0);
    emulatorStop();
    emulatorStart(NULL);
    archUpdateMenu(0);
}

void actionEmuResetClean() {
    int i;

    emulatorStop();

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        state.properties->media.carts[i].fileName[0] = 0;
        state.properties->media.carts[i].fileNameInZip[0] = 0;
        state.properties->media.carts[i].type = ROM_UNKNOWN;
        updateExtendedRomName(i, state.properties->media.carts[i].fileName, state.properties->media.carts[i].fileNameInZip);
    }
    
    for (i = 0; i < PROP_MAX_DISKS; i++) {
        state.properties->media.disks[i].fileName[0] = 0;
        state.properties->media.disks[i].fileNameInZip[0] = 0;
        updateExtendedDiskName(i, state.properties->media.disks[i].fileName, state.properties->media.disks[i].fileNameInZip);
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        state.properties->media.tapes[i].fileName[0] = 0;
        state.properties->media.tapes[i].fileNameInZip[0] = 0;
        updateExtendedCasName(i, state.properties->media.tapes[i].fileName, state.properties->media.tapes[i].fileNameInZip);
    }

    emulatorStart(NULL);
    archUpdateMenu(0);
}

void actionScreenCapture() {
    archScreenCapture(SC_NORMAL, NULL, 0);
}

void actionScreenCaptureAs() {
    archScreenCaptureAs();
}

void actionScreenCaptureUnfilteredSmall() {
    archScreenCapture(SC_SMALL, NULL, 0);
}

void actionScreenCaptureUnfilteredLarge() {
    archScreenCapture(SC_LARGE, NULL, 0);
}

void actionTapeRemove(int i) {
    state.properties->media.tapes[i].fileName[0] = 0;
    state.properties->media.tapes[i].fileNameInZip[0] = 0;
    if (emulatorGetState() != EMU_STOPPED) {
        emulatorSuspend();
        boardChangeCassette(i, NULL, NULL);
        emulatorResume();
    }
    updateExtendedCasName(0, state.properties->media.tapes[0].fileName, state.properties->media.tapes[0].fileNameInZip);
    archUpdateMenu(0);
}

void actionCartRemove(int i) {
    state.properties->media.carts[i].fileName[0] = 0;
    state.properties->media.carts[i].fileNameInZip[0] = 0;
    state.properties->media.carts[i].type = ROM_UNKNOWN;
    updateExtendedRomName(i, state.properties->media.carts[i].fileName, state.properties->media.carts[i].fileNameInZip);
    if (emulatorGetState() != EMU_STOPPED) {
        if (state.properties->cartridge.autoReset) {
            emulatorStop();
            emulatorStart(NULL);
        }
        else {
            emulatorSuspend();
            boardChangeCartridge(i, ROM_UNKNOWN, NULL, NULL);
            emulatorResume();
        }
    }
    else {
        boardChangeCartridge(i, ROM_UNKNOWN, NULL, NULL);
    }
    archUpdateMenu(0);
}

void actionCasRemove() {
    actionTapeRemove(0);
}

void actionDiskRemoveA() {
    actionDiskRemove(0);
}

void actionDiskRemoveB() {
    actionDiskRemove(1);
}

void actionCartRemove1() {
    actionCartRemove(0);
}

void actionCartRemove2() {
    actionCartRemove(1);
}

void actionToggleCartAutoReset() {
    state.properties->cartridge.autoReset ^= 1;
    archUpdateMenu(0);
}

void actionToggleDiskAutoReset() {
    state.properties->diskdrive.autostartA ^= 1;
    archUpdateMenu(0);
}

void actionCasToggleReadonly() {
    state.properties->cassette.readOnly ^= 1;
    archUpdateMenu(0);
}

void actionToggleCasAutoRewind() {
    state.properties->cassette.rewindAfterInsert ^= 1;
    archUpdateMenu(0);
}

void actionCasSave() {
    char* filename;

    if (*state.properties->media.tapes[0].fileName) {
        int type;

        if (emulatorGetState() == EMU_STOPPED) {
            tapeSetReadOnly(1);
            boardChangeCassette(0, strlen(state.properties->media.tapes[0].fileName) ? state.properties->media.tapes[0].fileName : NULL, 
                                strlen(state.properties->media.tapes[0].fileNameInZip) ? state.properties->media.tapes[0].fileNameInZip : NULL);
        }
        else {
            emulatorSuspend();
        }
        
        type = tapeGetFormat();

        filename = archFilenameGetSaveCas(state.properties, &type);

        if (filename != NULL && strlen(filename) != 0) {
            if (type == 1 || type == 2 || type == 3) {
                tapeSave(filename, type);
            }
        }

        if (emulatorGetState() == EMU_STOPPED) {
            boardChangeCassette(0, NULL, NULL);
            tapeSetReadOnly(state.properties->cassette.readOnly);
        }
        else {
            emulatorResume();
        }
    }
    archUpdateMenu(0);
}

void actionPropShowEmulation() {
    archShowPropertiesDialog(PROP_EMULATION);
}

void actionPropShowAudio() {
    archShowPropertiesDialog(PROP_SOUND);
}

void actionPropShowMidi() {
    archShowPropertiesDialog(PROP_MIDI);
}

void actionPropShowVideo() {
    archShowPropertiesDialog(PROP_PERFORMANCE);
}

void actionPropShowSettings() {
    archShowPropertiesDialog(PROP_SETTINGS);
}

void actionPropShowDisk() {
    archShowPropertiesDialog(PROP_DISK);
}

void actionPropShowPorts() {
    archShowPropertiesDialog(PROP_PORTS);
}

void actionPropShowEffects() {
    archShowPropertiesDialog(PROP_VIDEO);
}

void actionPropShowApearance() {
    archShowPropertiesDialog(PROP_APEARANCE);
}

void actionPropShowCapture() {
    archShowPropertiesDialog(PROP_CAPTURE);
}

void actionOptionsShowLanguage() {
    archShowLanguageDialog();
}

void actionToolsShowMachineEditor() {
    archShowMachineEditor();
}

void actionToolsShowShorcutEditor() {
    archShowShortcutsEditor();
}

void actionToolsShowKeyboardEditor() {
    archShowKeyboardEditor();
}

void actionToolsShowMixer() {
    archShowMixer();
}

void actionToolsShowDebugger() {
    archShowDebugger();
}

void actionToolsShowTrainer() {
    archShowTrainer();
}

void actionHelpShowHelp() {
    archShowHelpDialog();
}

void actionHelpShowAbout() {
    archShowAboutDialog();
}

void actionMaximizeWindow() {
    archMaximizeWindow();
}

void actionMinimizeWindow() {
    archMinimizeWindow();
}

void actionCloseWindow() {
    archCloseWindow();
}

void actionVolumeIncrease() {
    state.properties->sound.masterVolume += 5;
    if (state.properties->sound.masterVolume > 100) {
        state.properties->sound.masterVolume = 100;
    }
    mixerSetMasterVolume(state.mixer, state.properties->sound.masterVolume);
}

void actionVolumeDecrease() {
    state.properties->sound.masterVolume -= 5;
    if (state.properties->sound.masterVolume < 0) {
        state.properties->sound.masterVolume = 0;
    }
    mixerSetMasterVolume(state.mixer, state.properties->sound.masterVolume);
}
 
void actionMuteToggleMaster() {
    state.properties->sound.masterEnable = !state.properties->sound.masterEnable;
    mixerEnableMaster(state.mixer, state.properties->sound.masterEnable);
}

void actionMuteTogglePsg() {
    int channel = MIXER_CHANNEL_PSG;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteTogglePcm() {
    int channel = MIXER_CHANNEL_PCM;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleIo() {
    int channel = MIXER_CHANNEL_IO;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleScc() {
    int channel = MIXER_CHANNEL_SCC;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleKeyboard() {
    int channel = MIXER_CHANNEL_KEYBOARD;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleMsxMusic() {
    int channel = MIXER_CHANNEL_MSXMUSIC;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleMsxAudio() {
    int channel = MIXER_CHANNEL_MSXAUDIO;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleMoonsound() {
    int channel = MIXER_CHANNEL_MOONSOUND;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleYamahaSfg() {
    int channel = MIXER_CHANNEL_YAMAHA_SFG;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionMuteToggleMidi() {
    int channel = MIXER_CHANNEL_MIDI;
    int newEnable = !state.properties->sound.mixerChannel[channel].enable;
    state.properties->sound.mixerChannel[channel].enable = newEnable;
    mixerEnableChannelType(state.mixer, channel, newEnable);
}

void actionPrinterForceFormFeed()
{
    emulatorSuspend();
    archForceFormFeed();
    emulatorResume();
}

void actionVolumeToggleStereo() {
    state.properties->sound.stereo = !state.properties->sound.stereo;

    /* Driver runs at 2ch unconditionally; the mixer dual-monos in mono
    ** mode via recalculateChannelVolume.  No driver tear-down needed. */
    mixerSetStereo(mixerGetGlobalMixer(), state.properties->sound.stereo);
}

void actionNextTheme() {
    archThemeSetNext();
}

void actionMenuSpecialCart1(int x, int y) {
    archShowMenuSpecialCart1(x, y);
}

void actionMenuSpecialCart2(int x, int y) {
    archShowMenuSpecialCart2(x, y);
}

void actionMenuReset(int x, int y) {
    archShowMenuReset(x, y);
}

void actionMenuHelp(int x, int y) {
    archShowMenuHelp(x, y);
}

void actionMenuRun(int x, int y) {
    archShowMenuRun(x, y);
}

void actionMenuFile(int x, int y) {
    archShowMenuFile(x, y);
}

void actionMenuCart1(int x, int y) {
    archShowMenuCart1(x, y);
}

void actionMenuCart2(int x, int y) {
    archShowMenuCart2(x, y);
}

void actionMenuHarddisk(int x, int y) {
    archShowMenuHarddisk(x, y);
}

void actionMenuDiskA(int x, int y) {
    archShowMenuDiskA(x, y);
}

void actionMenuDiskB(int x, int y) {
    archShowMenuDiskB(x, y);
}

void actionMenuCassette(int x, int y) {
    archShowMenuCassette(x, y);
}

void actionMenuPrinter(int x, int y) {
    archShowMenuPrinter(x, y);
}

void actionMenuJoyPort1(int x, int y) {
    archShowMenuJoyPort1(x, y);
}

void actionMenuJoyPort2(int x, int y) {
    archShowMenuJoyPort2(x, y);
}

void actionMenuZoom(int x, int y) {
    archShowMenuZoom(x, y);
}

void actionMenuOptions(int x, int y) {
    archShowMenuOptions(x, y);
}

void actionMenuTools(int x, int y) {
    archShowMenuTools(x, y);
}

void actionVideoEnableMon1(int value) {
    videoManagerSetActive(videoManagerGetActive() != 0 ? 0 : -1);
    archUpdateMenu(0);
}

void actionVideoEnableMon2(int value) {
    videoManagerSetActive(videoManagerGetActive() != 1 ? 1 : -1);
    archUpdateMenu(0);
}

void actionVideoEnableMon3(int value) {
    videoManagerSetActive(videoManagerGetActive() != 2 ? 2 : -1);
    archUpdateMenu(0);
}

// Actions controlled by value 0 - 100

void actionVideoSetGamma(int value) {
    state.properties->video.gamma = 50 + value;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetBrightness(int value) {
    state.properties->video.brightness = 50 + value;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetContrast(int value) {
    state.properties->video.contrast = 50 + value;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetSaturation(int value) {
    state.properties->video.saturation = 50 + value;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetScanlines(int value) {
    state.properties->video.scanlinesPct = 100 - value;
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetRfModulation(int value) {
    state.properties->video.colorSaturationWidth = (int)ceil((5 - 1) * value / 100.0);
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetColorMode(int value) {
    state.properties->video.monitorColor = (int)ceil((P_VIDEO_MONCOUNT - 1) * value / 100.0);
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionVideoSetFilter(int value) {
    state.properties->video.monitorType = (int)ceil((P_VIDEO_PALCOUNT - 1) * value / 100.0);
    videoUpdateAll(state.video, state.properties);
    archUpdateEmuDisplayConfig();
}

void actionEmuSpeedSet(int value) {
    state.properties->emulation.speed = value;
    emulatorSetFrequency(state.properties->emulation.speed, NULL);
}

void actionVolumeSetMaster(int value) {
    state.properties->sound.masterVolume = value;
    mixerSetMasterVolume(state.mixer, value);
}

void actionVolumeSetPsg(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_PSG].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_PSG, value);
}

void actionVolumeSetPcm(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_PCM].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_PCM, value);
}

void actionVolumeSetIo(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_IO].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_IO, value);
}

void actionVolumeSetScc(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_SCC].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_SCC, value);
}

void actionVolumeSetMsxMusic(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_MSXMUSIC, value);
}

void actionVolumeSetMsxAudio(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MSXAUDIO].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_MSXAUDIO, value);
}

void actionVolumeSetMoonsound(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MOONSOUND].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_MOONSOUND, value);
}

void actionVolumeSetYamahaSfg(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_YAMAHA_SFG].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_YAMAHA_SFG, value);
}

void actionVolumeSetKeyboard(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_KEYBOARD].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_KEYBOARD, value);
}

void actionVolumeSetMidi(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MIDI].volume = value;
    mixerSetChannelTypeVolume(state.mixer, MIXER_CHANNEL_MIDI, value);
}

void actionPanSetPsg(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_PSG].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_PSG, value);
}

void actionPanSetPcm(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_PCM].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_PCM, value);
}

void actionPanSetIo(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_IO].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_IO, value);
}

void actionPanSetScc(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_SCC].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_SCC, value);
}

void actionPanSetMsxMusic(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_MSXMUSIC, value);
}

void actionPanSetMsxAudio(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MSXAUDIO].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_MSXAUDIO, value);
}

void actionPanSetMoonsound(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MOONSOUND].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_MOONSOUND, value);
}

void actionPanSetYamahaSfg(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_YAMAHA_SFG].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_YAMAHA_SFG, value);
}

void actionPanSetKeyboard(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_KEYBOARD].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_KEYBOARD, value);
}

void actionPanSetMidi(int value) {
    state.properties->sound.mixerChannel[MIXER_CHANNEL_MIDI].pan = value;
    mixerSetChannelTypePan(state.mixer, MIXER_CHANNEL_MIDI, value);
}

void actionRenshaSetLevel(int value) {
    state.properties->joy1.autofire = (int)ceil((11 - 1) * value / 100.0);
    switchSetRensha(state.properties->joy1.autofire);
}

void actionSetSpriteEnable(int value) {
    vdpSetSpritesEnable(value);
}

void actionSetNoSpriteLimits(int value) {
	vdpSetNoSpriteLimits(value);
}

void actionSetMsxAudioSwitch(int value) {
    state.properties->emulation.audioSwitch = value ? 1 : 0;
    switchSetAudio(state.properties->emulation.audioSwitch);
}

void actionSetFrontSwitch(int value) {
    state.properties->emulation.frontSwitch = value ? 1 : 0;
    switchSetFront(state.properties->emulation.frontSwitch);
}

void actionSetPauseSwitch(int value) {
    state.properties->emulation.pauseSwitch = value ? 1 : 0;
    switchSetPause(state.properties->emulation.pauseSwitch);
}

void actionSetFdcTiming(int value) {
    state.properties->emulation.enableFdcTiming = value ? 1 : 0;
    boardSetFdcTimingEnable(state.properties->emulation.enableFdcTiming);
}

void actionSetHddSdBoost(int value) {
    state.properties->emulation.enableHddSdBoost = value ? 1 : 0;
    boardSetHddSdBoostEnable(state.properties->emulation.enableHddSdBoost);
}

void actionSetWaveCapture(int value) {
    if (value == 0) {
        mixerStopLog(state.mixer);
    }
    else {
        mixerStartLog(state.mixer, generateSaveFilename(state.properties, 
                                                        audioDir, 
                                                        audioPrefix, ".wav", 2));
    }
    archUpdateMenu(0);
}

void actionSetMouseCapture(int value) {
    state.mouseLock = value ? 1 : 0;
    archMouseSetForceLock(state.mouseLock);
}

void actionSetFullscreen(int value) {
    if (value == 0 && state.properties->video.windowSize == P_VIDEO_SIZEFULLSCREEN) {
        if (state.windowedSize == P_VIDEO_SIZEX2) {
            actionWindowSize2x();
        }
        else {
            actionWindowSize1x();
        }
    }
    else if (state.properties->video.windowSize != P_VIDEO_SIZEFULLSCREEN) {
        actionWindowSizeFullscreen();
    }
}

void actionSetCartAutoReset(int value) {
    state.properties->cartridge.autoReset = value ? 1 : 0;
    archUpdateMenu(0);
}

void actionSetDiskAutoResetA(int value) {
    state.properties->diskdrive.autostartA = value ? 1 : 0;
    archUpdateMenu(0);
}

void actionSetCasReadonly(int value) {
    state.properties->cassette.readOnly = value ? 1 : 0;
    archUpdateMenu(0);
}

void actionSetCasAutoRewind(int value) {
    state.properties->cassette.rewindAfterInsert = value ? 1 : 0;
    archUpdateMenu(0);
}

void actionSetVolumeMute(int value) {
    int oldEnable = state.properties->sound.masterEnable;
    state.properties->sound.masterEnable = value ? 1 : 0;
    if (oldEnable != state.properties->sound.masterEnable) {
        mixerEnableMaster(state.mixer, state.properties->sound.masterEnable);
    }
}

void actionSetVolumeStereo(int value) {
    int oldStereo = state.properties->sound.stereo;
    state.properties->sound.stereo = value ? 1 : 0;
    if (oldStereo != state.properties->sound.stereo) {
        mixerSetStereo(mixerGetGlobalMixer(), state.properties->sound.stereo);
    }
}

void actionKeyPress(int keyCode, int pressed)
{
    if (pressed) {
        inputEventSet(keyCode);
        archKeyboardSetSelectedKey(keyCode);
    }
    else {
        inputEventUnset(keyCode);
    }
}

