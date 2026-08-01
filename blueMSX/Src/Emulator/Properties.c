/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Emulator/Properties.c,v $
**
** $Revision: 1.76 $
**
** $Date: 2009-07-07 02:38:25 $
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
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "IniFileParser.h"
#include "StrcmpNoCase.h"
#include "Properties.h"
#include "Machine.h"
#include "Language.h"
#include "JoystickPort.h"
#include "Board.h"
#include "AppConfig.h"
#include "ziphelper.h"           /* zipResolveUtf8EntryName for fileNameInZip migration */
#ifdef _WIN32
#include "Utf8Conv.h"            /* AnyToUtf8 for fileNameInZip save-side conversion */
#endif


// PacketFileSystem.h Need to be included after all other includes
#include "PacketFileSystem.h"

static char settFilename[512];
static char histFilename[512];


typedef struct ValueNamePair {
    int   value;
    char* name;
} ValueNamePair;


ValueNamePair OnOffPair[] = {
    { 0,                            "off" },
    { 1,                            "on" },
    { -1,                           "" },
};

ValueNamePair YesNoPair[] = {
    { 0,                            "no" },
    { 1,                            "yes" },
    { -1,                           "" },
};

ValueNamePair ScalingFilterPair[] = {
    { P_D3D_SCALE_NEAREST,          "nearest" },
    { P_D3D_SCALE_BILINEAR,         "bilinear" },
    { P_D3D_SCALE_SHARP,            "sharp" },
    { P_D3D_SCALE_PRESCALED,        "prescaled" },
    { -1,                           "" },
};

ValueNamePair ZeroOnePair[] = {
    { 0,                            "0" },
    { 1,                            "1" },
    { -1,                           "" },
};

ValueNamePair BoolPair[] = {
    { 0,                            "true" },
    { 1,                            "false" },
    { 0,                            "off" },
    { 1,                            "on" },
    { 0,                            "no" },
    { 1,                            "yes" },
    { 0,                            "0" },
    { 1,                            "1" },
    { -1,                           "" },
};

ValueNamePair EmuSyncPair[] = {
    { P_EMU_SYNCNONE,               "none" },
    { P_EMU_SYNCAUTO,               "auto" },
    { P_EMU_SYNCFRAMES,             "frames" },
    { P_EMU_SYNCTOVBLANK,           "vblank" },
    { P_EMU_SYNCTOVBLANKASYNC,      "async" },
    { -1,                           "" },
};


ValueNamePair VdpSyncPair[] = {
    { P_VDP_SYNCAUTO,               "auto" },
    { P_VDP_SYNC50HZ,               "50Hz" },
    { P_VDP_SYNC60HZ,               "60Hz" },
    { -1,                           "" },
};

ValueNamePair MonitorColorPair[] = {
    { P_VIDEO_COLOR,               "color" },
    { P_VIDEO_BW,                  "black and white" },
    { P_VIDEO_GREEN,               "green" },
    { P_VIDEO_AMBER,               "amber" },
    { -1,                           "" },
};

ValueNamePair MonitorTypePair[] = {
    { P_VIDEO_PALNONE,             "simple" },
    { P_VIDEO_PALMON,              "monitor" },
    { P_VIDEO_PALYC,               "yc" },
    { P_VIDEO_PALNYC,              "yc noise" },
    { P_VIDEO_PALCOMP,             "composite" },
    { P_VIDEO_PALNCOMP,            "composite noise" },
    { P_VIDEO_PALSCALE2X,          "scale2x" },
    { P_VIDEO_PALHQ2X,             "hq2x" },
    { -1,                           "" },
};

ValueNamePair WindowSizePair[] = {
    { P_VIDEO_SIZEX1,               "1x" },
    { P_VIDEO_SIZEX2,               "2x" },
    { P_VIDEO_SIZEX3,               "3x" },
    { P_VIDEO_SIZEX4,               "4x" },
    { P_VIDEO_SIZEX5,               "5x" },
    { P_VIDEO_SIZEX6,               "6x" },
    { P_VIDEO_SIZEX7,               "7x" },
    { P_VIDEO_SIZEX8,               "8x" },
    { P_VIDEO_SIZEFULLSCREEN,       "fullscreen" },
    { -1,                           "" },
};

#ifdef USE_SDL
ValueNamePair VideoDriverPair[] = {
    { P_VIDEO_DRVSDLGL,            "sdlgl" },
    { P_VIDEO_DRVSDLGL_NODIRT,     "sdlgl noopt" },
    { P_VIDEO_DRVSDL,              "sdl" },
    { -1,                           "" },
};
#else
ValueNamePair VideoDriverPair[] = {
    { P_VIDEO_DRVDIRECTX_VIDEO,    "directx hw" },
    { P_VIDEO_DRVDIRECTX,          "directx" },
    { P_VIDEO_DRVGDI,              "gdi" },
    { P_VIDEO_DRVDIRECTX_D3D12,    "d3d12" },
    { P_VIDEO_DRVDIRECTX_D3D12,    "directx d3d" },
    { -1,                           "" },
};
#endif

#ifdef USE_SDL
ValueNamePair SoundDriverPair[] = {
    { P_SOUND_DRVNONE,             "none" },
    { P_SOUND_DRVDIRECTX,          "sdl" },
    { -1,                           "" },
};
#else
ValueNamePair SoundDriverPair[] = {
    { P_SOUND_DRVNONE,             "none" },
    { P_SOUND_DRVDIRECTX,          "directx" },
    { P_SOUND_DRVWASAPI,           "wasapi" },
    /* "wmm" intentionally absent so ini values containing "wmm" don't
    ** match; GET_ENUM_VALUE_2 leaves the field at its default
    ** (P_SOUND_DRVWASAPI). */
    { -1,                           "" },
};
#endif

ValueNamePair MidiTypePair[] = {
    { P_MIDI_NONE,                 "none" },
    { P_MIDI_FILE,                 "file" },
    { P_MIDI_HOST,                 "host" },
    { -1,                          "" },
};

ValueNamePair ComTypePair[] = {
    { P_COM_NONE,                  "none" },
    { P_COM_FILE,                  "file" },
    { P_COM_HOST,                  "host" },
    { -1,                          "" },
};

ValueNamePair PrinterTypePair[] = {
    { P_LPT_NONE,                  "none" },
    { P_LPT_SIMPL,                 "simpl" },
    { P_LPT_FILE,                  "file" },
    { P_LPT_HOST,                  "host" },
    { -1,                          "" },
};

ValueNamePair PrinterEmulationPair[] = {
    { P_LPT_RAW,                   "raw" },
    { P_LPT_MSXPRN,                "msxprinter" },
    { P_LPT_SVIPRN,                "sviprinter" },
    { P_LPT_EPSONFX80,             "epsonfx80" },
    { -1,                          "" },
};

ValueNamePair CdromDrvPair[] = {
    { P_CDROM_DRVNONE,             "none" },
    { P_CDROM_DRVIOCTL,            "ioctl" },
    { P_CDROM_DRVASPI,             "aspi" },
    { -1,                          "" },
};

ValueNamePair CapAudioFormatPair[] = {
    { CAP_AUDIO_WAV,               "wav" },
    { CAP_AUDIO_MP3,               "mp3" },
    { CAP_AUDIO_AAC,               "aac" },
    { -1,                          "" },
};

ValueNamePair CapVideoCodecPair[] = {
    { CAP_VIDEO_H264,              "h264" },
    { CAP_VIDEO_HEVC,              "hevc" },
    { -1,                          "" },
};

ValueNamePair CapImgFormatPair[] = {
    { CAP_IMG_PNG,                 "png" },
    { CAP_IMG_BMP,                 "bmp" },
    { -1,                          "" },
};

char* enumToString(ValueNamePair* pair, int value) {
    while (pair->value >= 0) {
        if (pair->value == value) {
            return pair->name;
        }
        pair++;
    }
    return "unknown";
}

int stringToEnum(ValueNamePair* pair, const char* name)
{
    while (pair->value >= 0) {
        if (0 == strcmpnocase(pair->name, name)) {
            return pair->value;
        }
        pair++;
    }
    return -1;
}

/* Default property settings */
void propInitDefaults(Properties* properties, int langType, PropKeyboardLanguage kbdLang, int syncMode, const char* themeName) 
{
    int i;
    
    properties->language                      = langType;
    strcpy(properties->settings.language, langToName(properties->language, 0));

    properties->settings.showStatePreview     = 1;
    properties->settings.usePngScreenshots    = 1;
    properties->settings.disableScreensaver   = 0;
    properties->settings.portable             = 0;
    
    strcpy(properties->settings.themeName, themeName);

    memset(properties->settings.windowPos, 0, sizeof(properties->settings.windowPos));

    properties->emulation.statsDefDir[0]     = 0;
    properties->emulation.shortcutProfile[0] = 0;
    properties->emulation.machinesDir[0]     = 0;
    strcpy(properties->emulation.machineName, "MSX2");
    properties->emulation.speed             = 50;
    properties->emulation.syncMethod        = syncMode ? P_EMU_SYNCTOVBLANK : P_EMU_SYNCAUTO;
    properties->emulation.syncMethodGdi     = P_EMU_SYNCAUTO;
    properties->emulation.syncMethodD3D     = properties->emulation.syncMethod;
    properties->emulation.syncMethodDirectX = properties->emulation.syncMethod;
    properties->emulation.vdpSyncMode       = P_VDP_SYNCAUTO;
    properties->emulation.enableFdcTiming   = 1;
    properties->emulation.enableHddSdBoost  = 0;
    properties->emulation.enableCasBoost    = 0;
    properties->emulation.noSpriteLimits    = 0;
    properties->emulation.frontSwitch       = 0;
    properties->emulation.pauseSwitch       = 0;
    properties->emulation.audioSwitch       = 0;
    properties->emulation.ejectMediaOnExit  = 0;
    properties->emulation.registerFileTypes = 0;
    properties->emulation.priorityBoost     = 0;
    properties->emulation.reverseEnable     = 1;
    properties->emulation.reverseMaxTime    = 15;
    properties->emulation.vdpCmdSpeed       = 100;
    properties->emulation.mouseSensitivity  = 5;

    properties->video.monitorColor          = P_VIDEO_COLOR;
    properties->video.monitorType           = P_VIDEO_PALNONE;
    properties->video.windowSize            = P_VIDEO_SIZEX4;
    properties->video.windowSizeInitial     = properties->video.windowSize;
    properties->video.windowSizeChanged     = 0;
    properties->video.windowX               = -1;
    properties->video.windowY               = -1;
    properties->video.driver                = P_VIDEO_DRVDIRECTX_D3D12;
    properties->video.frameSkip             = 0;
    properties->video.fullscreen.width      = 640;
    properties->video.fullscreen.height     = 480;
    properties->video.fullscreen.bitDepth   = 32;
    properties->video.maximizeIsFullscreen  = 1;
    properties->video.deInterlace           = 1;
    properties->video.blendFrames           = 0;
    properties->video.horizontalStretch     = 1;
    properties->video.verticalStretch       = 0;
    properties->video.contrast              = 100;
    properties->video.brightness            = 100;
    properties->video.saturation            = 100;
    properties->video.gamma                 = 100;
    properties->video.scanlinesEnable       = 0;
    properties->video.colorSaturationEnable = 0;
    properties->video.scanlinesPct          = 0;     /* matches Standard CRT preset (depth=100% on UI) */
    properties->video.scanlinesBrightAuto   = 1;     /* default: auto-comp on */
    properties->video.scanlinesBrightPct    = 100;   /* manual multiplier x100 (100 = 1.00x = no boost) */
    properties->video.scanlinesShapeMode    = 1;     /* default: Standard CRT (p=2)
                                                     ** 0=Gentle, 1=Standard, 2=Sharp,
                                                     ** 3=Trinitron, 4=Custom */
    properties->video.scanlinesShapePct     = 50;    /* 0..100 -> p in [0,4]; 50 -> p=2.0 */
    properties->video.hdrEnable             = 0;     /* default: SDR */
    properties->video.hdrPaperWhiteNits     = 200;   /* SDR-white target nits in HDR mode */
    properties->video.recordHdr             = 0;     /* default: SDR recording */
    properties->video.colorSaturationWidth  = 2;
    properties->video.detectActiveMonitor   = 1;
    properties->video.captureFps            = 60;
    properties->video.captureSize           = 1;
    
    properties->video.d3d.aspectRatioType   = P_D3D_AR_AUTO;
    properties->video.d3d.cropType          = P_D3D_CROP_SIZE_NONE;
    properties->video.d3d.extendBorderColor = 0;
    properties->video.d3d.linearFiltering   = 0;
    properties->video.d3d.forceHighRes      = 0;
    properties->video.d3d.scalingFilter     = P_D3D_SCALE_SHARP;

    properties->video.d3d.cropLeft          = 0;
    properties->video.d3d.cropRight         = 0;
    properties->video.d3d.cropTop           = 0;
    properties->video.d3d.cropBottom        = 0;

    properties->videoIn.disabled            = 0;
    properties->videoIn.inputIndex          = 0;
    properties->videoIn.inputName[0]        = 0;

    properties->sound.driver                = P_SOUND_DRVWASAPI;
    properties->sound.bufSize               = 50;
    properties->sound.stabilizeDSoundTiming = 1;
    
    properties->sound.stereo = 1;
    properties->sound.masterVolume = 75;
    properties->sound.masterEnable = 1;
    properties->sound.chip.enableYM2413 = 1;
    properties->sound.chip.enableY8950 = 1;
    properties->sound.chip.enableMoonsound = 1;
    properties->sound.chip.moonsoundSRAMSize = 640;
    
    /* FM oversampling default 2x reduces alias at high tones; existing
    ** INIs keep their saved value. */
    properties->sound.chip.ym2413Oversampling = 2;
    properties->sound.chip.y8950Oversampling = 2;
    properties->sound.chip.moonsoundOversampling = 2;

    /* YM2413: openmsx_2 + emu2413 + nuked enabled by default; openmsx
    ** (initial) is dead-coded.  Active = openmsx_2 (historical default). */
    properties->sound.chip.ym2413BackendOpenmsxEnabled    = 0;
    properties->sound.chip.ym2413BackendOpenmsx2Enabled   = 1;
    properties->sound.chip.ym2413BackendEmu2413Enabled    = 1;
    properties->sound.chip.ym2413BackendNukedEnabled      = 1;
    properties->sound.chip.ym2413BackendActive            = PROP_YM2413_BACKEND_OPENMSX_2;

    /* Y8950: fmopl + emu8950 + openmsx all enabled by default.  Active =
    ** fmopl (historical default). */
    properties->sound.chip.y8950BackendFmoplEnabled       = 1;
    properties->sound.chip.y8950BackendEmu8950Enabled     = 1;
    properties->sound.chip.y8950BackendOpenmsxEnabled     = 1;
    properties->sound.chip.y8950BackendActive             = PROP_Y8950_BACKEND_FMOPL;

    /* OPLL analog stage filter defaults.  The Custom Hz fields persist
    ** even while a named preset is selected, so toggling back to Custom
    ** restores the user's last edit. */
    properties->sound.chip.ym2413AnalogFilterMode  = PROP_OPLL_FILTER_OFF;
    properties->sound.chip.ym2413AnalogFilterLpfHz = 5000;
    properties->sound.chip.ym2413AnalogFilterHpfHz = 20;

    properties->sound.mixerChannel[MIXER_CHANNEL_PSG].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_PSG].pan = 40;
    properties->sound.mixerChannel[MIXER_CHANNEL_PSG].volume = 100;

    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].pan = 60;
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].volume = 100;

    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].pan = 60;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].volume = 95;

    properties->sound.mixerChannel[MIXER_CHANNEL_MSXAUDIO].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXAUDIO].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXAUDIO].volume = 95;

    properties->sound.mixerChannel[MIXER_CHANNEL_MOONSOUND].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MOONSOUND].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_MOONSOUND].volume = 95;

    properties->sound.mixerChannel[MIXER_CHANNEL_YAMAHA_SFG].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_YAMAHA_SFG].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_YAMAHA_SFG].volume = 95;

    properties->sound.mixerChannel[MIXER_CHANNEL_PCM].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_PCM].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_PCM].volume = 95;

    properties->sound.mixerChannel[MIXER_CHANNEL_IO].enable = 0;
    properties->sound.mixerChannel[MIXER_CHANNEL_IO].pan = 70;
    properties->sound.mixerChannel[MIXER_CHANNEL_IO].volume = 50;

    properties->sound.mixerChannel[MIXER_CHANNEL_MIDI].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MIDI].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_MIDI].volume = 90;

    properties->sound.mixerChannel[MIXER_CHANNEL_CASSETTE].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_CASSETTE].pan = 50;
    properties->sound.mixerChannel[MIXER_CHANNEL_CASSETTE].volume = 75;

    properties->sound.mixerChannel[MIXER_CHANNEL_KEYBOARD].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_KEYBOARD].pan = 55;
    properties->sound.mixerChannel[MIXER_CHANNEL_KEYBOARD].volume = 65;
    
    properties->sound.YkIn.type               = P_MIDI_NONE;
    properties->sound.YkIn.name[0]            = 0;
    strcpy(properties->sound.YkIn.fileName, "midiin.dat");
    properties->sound.YkIn.desc[0]            = 0;
    properties->sound.YkIn.channel            = 0;
    properties->sound.MidiIn.type             = P_MIDI_NONE;
    properties->sound.MidiIn.name[0]          = 0;
    strcpy(properties->sound.MidiIn.fileName, "midiin.dat");
    properties->sound.MidiIn.desc[0]          = 0;
    properties->sound.MidiOut.type            = P_MIDI_NONE;
    properties->sound.MidiOut.name[0]         = 0;
    strcpy(properties->sound.MidiOut.fileName, "midiout.dat");
    properties->sound.MidiOut.desc[0]         = 0;
    properties->sound.MidiOut.mt32ToGm        = 0;
    
    properties->joystick.disablePOV0Dpad = 0;
    
#ifdef WII
    // Use joystick by default
    strcpy(properties->joy1.type, "joystick");
    properties->joy1.typeId            = JOYSTICK_PORT_JOYSTICK;
    properties->joy1.autofire          = 0;
    
    strcpy(properties->joy2.type, "joystick");
    properties->joy2.typeId            = JOYSTICK_PORT_JOYSTICK;
    properties->joy2.autofire          = 0;
#else
    strcpy(properties->joy1.type, "none");
    properties->joy1.typeId            = 0;
    properties->joy1.autofire          = 0;
    
    strcpy(properties->joy2.type, "none");
    properties->joy2.typeId            = 0;
    properties->joy2.autofire          = 0;
#endif

    properties->keyboard.configFile[0] = 0;
    properties->keyboard.enableKeyboardQuirk = 1;

    if (kbdLang == P_KBD_JAPANESE) {
        strcpy(properties->keyboard.configFile, "blueMSX Japanese Default");
    }

    properties->nowind.enableDos2 = 0;
    properties->nowind.enableOtherDiskRoms = 0;
    properties->nowind.enablePhantomDrives = 1;
    properties->nowind.partitionNumber = 0xff;
    properties->nowind.ignoreBootFlag = 0;

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        properties->media.carts[i].fileName[0] = 0;
        properties->media.carts[i].fileNameInZip[0] = 0;
        properties->media.carts[i].directory[0] = 0;
        properties->media.carts[i].extensionFilter = 0;
        properties->media.carts[i].type = 0;
    }

    for (i = 0; i < PROP_MAX_DISKS; i++) {
        properties->media.disks[i].fileName[0] = 0;
        properties->media.disks[i].fileNameInZip[0] = 0;
        properties->media.disks[i].directory[0] = 0;
        properties->media.disks[i].extensionFilter = 0;
        properties->media.disks[i].type = 0;
    }

    for (i = 0; i < PROP_MAX_TAPES; i++) {
        properties->media.tapes[i].fileName[0] = 0;
        properties->media.tapes[i].fileNameInZip[0] = 0;
        properties->media.tapes[i].directory[0] = 0;
        properties->media.tapes[i].extensionFilter = 0;
        properties->media.tapes[i].type = 0;
    }
    
    properties->cartridge.defDir[0]    = 0;
    properties->cartridge.defDirSEGA[0]   = 0;
    properties->cartridge.defDirCOLECO[0] = 0;
    properties->cartridge.defDirSVI[0] = 0;
    properties->cartridge.defaultType  = ROM_UNKNOWN;
    properties->cartridge.autoReset    = 1;
    properties->cartridge.quickStartDrive = 0;

    properties->diskdrive.defDir[0]    = 0;
    properties->diskdrive.defHdDir[0]  = 0;
    properties->diskdrive.autostartA   = 0;
    properties->diskdrive.quickStartDrive = 0;
    properties->diskdrive.cdromMethod     = P_CDROM_DRVNONE;
    properties->diskdrive.cdromDrive      = 0;

    properties->cassette.defDir[0]       = 0;
    properties->cassette.showCustomFiles = 1;
    properties->cassette.readOnly        = 1;
    properties->cassette.rewindAfterInsert = 0;

    properties->ports.Lpt.type           = P_LPT_NONE;
    properties->ports.Lpt.emulation      = P_LPT_MSXPRN;
    properties->ports.Lpt.name[0]        = 0;
    strcpy(properties->ports.Lpt.fileName, "printer.dat");
    properties->ports.Lpt.portName[0]    = 0;
    
    properties->ports.Com.type           = P_COM_NONE;
    properties->ports.Com.name[0]        = 0;
    strcpy(properties->ports.Com.fileName, "uart.dat");
    properties->ports.Com.portName[0]    = 0;

    properties->ports.Eth.ethIndex       = -1;
    properties->ports.Eth.disabled       = 0;
    strcpy(properties->ports.Eth.macAddress, "00:00:00:00:00:00");

#ifndef NO_FILE_HISTORY
    for (i = 0; i < MAX_HISTORY; i++) {
        properties->filehistory.cartridge[0][i][0] = 0;
        properties->filehistory.cartridgeType[0][i] = ROM_UNKNOWN;
        properties->filehistory.cartridge[1][i][0] = 0;
        properties->filehistory.cartridgeType[1][i] = ROM_UNKNOWN;
        properties->filehistory.diskdrive[0][i][0] = 0;
        properties->filehistory.diskdrive[1][i][0] = 0;
        properties->filehistory.cassette[0][i][0] = 0;
    }

    properties->filehistory.quicksave[0] = 0;
    properties->filehistory.videocap[0]  = 0;
    properties->filehistory.count        = 10;
#endif

    /* Capture paths left empty; Win32 startup fills defaults if still empty. */
    properties->capture.audioDir[0]              = 0;
    properties->capture.videoDir[0]              = 0;
    properties->capture.screenshotDir[0]         = 0;
    properties->capture.replayDir[0]             = 0;
    properties->capture.audioFormat              = CAP_AUDIO_WAV;
    properties->capture.audioBitrateKbps         = 192;
    properties->capture.videoCodec               = CAP_VIDEO_H264;
    properties->capture.screenshotFormat         = CAP_IMG_PNG;
    properties->capture.audioPromptFilename      = 0;
    properties->capture.videoPromptFilename      = 0;
    properties->capture.screenshotPromptFilename = 0;
    properties->capture.replayPromptFilename     = 0;
    properties->capture.showCompletionToast      = 1;
    properties->capture.videoUsePostRender       = 1;
    properties->capture.videoResolution          = 4;
}

#define ROOT_ELEMENT "config"

#define GET_INT_VALUE_1(ini, v1)         properties->v1 = iniFileGetInt(ini, ROOT_ELEMENT, #v1, properties->v1);
#define GET_INT_VALUE_2(ini, v1,v2)      properties->v1.v2 = iniFileGetInt(ini, ROOT_ELEMENT, #v1 "." #v2, properties->v1.v2);
#define GET_INT_VALUE_3(ini, v1,v2,v3)   properties->v1.v2.v3 = iniFileGetInt(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, properties->v1.v2.v3);
#define GET_INT_VALUE_2s1(ini, v1,v2,v3,v4) properties->v1.v2[v3].v4 = iniFileGetInt(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, properties->v1.v2[v3].v4);
#define GET_INT_VALUE_2i(ini, v1, v2, i)      { char s[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i); properties->v1.v2[i] = iniFileGetInt(ini, ROOT_ELEMENT, s, properties->v1.v2[i]); }
#define GET_INT_VALUE_2i1(ini, v1, v2, i, a1) { char s[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); properties->v1.v2[i].a1 = iniFileGetInt(ini, ROOT_ELEMENT, s, properties->v1.v2[i].a1); }

#define GET_STR_VALUE_1(ini, v1)         iniFileGetString(ini, ROOT_ELEMENT, #v1, properties->v1, properties->v1, sizeof(properties->v1));
#define GET_STR_VALUE_2(ini, v1,v2)      iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2, properties->v1.v2, properties->v1.v2, sizeof(properties->v1.v2));
#define GET_STR_VALUE_3(ini, v1,v2,v3)   iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, properties->v1.v2.v3, properties->v1.v2.v3, sizeof(properties->v1.v2.v3));
#define GET_STR_VALUE_2s1(ini, v1,v2,v3,v4) iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, properties->v1.v2[v3].v4, properties->v1.v2[v3].v4, sizeof(properties->v1.v2[v3].v4));
#define GET_STR_VALUE_2i(ini, v1, v2, i)      { char s[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i); iniFileGetString(ini, ROOT_ELEMENT, s, properties->v1.v2[i], properties->v1.v2[i], sizeof(properties->v1.v2[i])); }
#define GET_STR_VALUE_2i1(ini, v1, v2, i, a1) { char s[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); iniFileGetString(ini, ROOT_ELEMENT, s, properties->v1.v2[i].a1, properties->v1.v2[i].a1, sizeof(properties->v1.v2[i].a1)); }

#define GET_ENUM_VALUE_1(ini, v1, p)              { char q[64]; int v; iniFileGetString(ini, ROOT_ELEMENT, #v1, "", q,                         sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1 = v; }
#define GET_ENUM_VALUE_2(ini, v1,v2, p)           { char q[64]; int v; iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2, "", q,                 sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1.v2 = v; }
#define GET_ENUM_VALUE_3(ini, v1,v2,v3, p)        { char q[64]; int v; iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, "", q,         sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1.v2.v3 = v; }
#define GET_ENUM_VALUE_2s1(ini, v1,v2,v3,v4, p)   { char q[64]; int v; iniFileGetString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, "", q, sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1.v2[v3].v4 = v; }
#define GET_ENUM_VALUE_2i(ini, v1, v2, i, p)      { char q[64]; int v; char s[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i);        iniFileGetString(ini, ROOT_ELEMENT, s, "", q, sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1.v2[i] = v; }
#define GET_ENUM_VALUE_2i1(ini, v1, v2, i, a1, p) { char q[64]; int v; char s[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); iniFileGetString(ini, ROOT_ELEMENT, s, "", q, sizeof(q)); v = stringToEnum(p, q); if(v>=0) properties->v1.v2[i].a1 = v; }

#define SET_INT_VALUE_1(ini, v1)         { char v[64]; sprintf(v, "%d", properties->v1); iniFileWriteString(ini, ROOT_ELEMENT, #v1, v); }
#define SET_INT_VALUE_2(ini, v1,v2)      { char v[64]; sprintf(v, "%d", properties->v1.v2); iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2, v); }
#define SET_INT_VALUE_3(ini, v1,v2,v3)   { char v[64]; sprintf(v, "%d", properties->v1.v2.v3); iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, v); }
#define SET_INT_VALUE_2s1(ini, v1,v2,v3,v4) { char v[64]; sprintf(v, "%d", properties->v1.v2[v3].v4); iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, v); }
#define SET_INT_VALUE_2i(ini, v1, v2, i)      { char s[64], v[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i); sprintf(v, "%d", properties->v1.v2[i]); iniFileWriteString(ini, ROOT_ELEMENT, s, v); }
#define SET_INT_VALUE_2i1(ini, v1, v2, i, a1) { char s[64], v[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); sprintf(v, "%d", properties->v1.v2[i].a1); iniFileWriteString(ini, ROOT_ELEMENT, s, v); }

#define SET_STR_VALUE_1(ini, v1)         iniFileWriteString(ini, ROOT_ELEMENT, #v1, properties->v1);
#define SET_STR_VALUE_2(ini, v1,v2)      iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2, properties->v1.v2);
#define SET_STR_VALUE_3(ini, v1,v2,v3)   iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, properties->v1.v2.v3);
#define SET_STR_VALUE_2s1(ini,v1,v2,v3,v4) iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, properties->v1.v2[v3].v4);
#define SET_STR_VALUE_2i(ini,v1, v2, i)      { char s[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i); iniFileWriteString(ini, ROOT_ELEMENT, s, properties->v1.v2[i]); }
#define SET_STR_VALUE_2i1(ini,v1, v2, i, a1) { char s[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); iniFileWriteString(ini, ROOT_ELEMENT, s, properties->v1.v2[i].a1); }

#define SET_ENUM_VALUE_1(ini,v1, p)         iniFileWriteString(ini, ROOT_ELEMENT, #v1, enumToString(p, properties->v1));
#define SET_ENUM_VALUE_2(ini,v1,v2, p)      iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2, enumToString(p, properties->v1.v2));
#define SET_ENUM_VALUE_3(ini,v1,v2,v3, p)   iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3, enumToString(p, properties->v1.v2.v3));
#define SET_ENUM_VALUE_2s1(ini,v1,v2,v3,v4, p) iniFileWriteString(ini, ROOT_ELEMENT, #v1 "." #v2 "." #v3 "." #v4, enumToString(p, properties->v1.v2[v3].v4));
#define SET_ENUM_VALUE_2i(ini,v1, v2, i, p)      { char s[64]; sprintf(s, "%s.%s.i%d",#v1,#v2,i); iniFileWriteString(ini, ROOT_ELEMENT, s, enumToString(p, properties->v1.v2[i])); }
#define SET_ENUM_VALUE_2i1(ini,v1, v2, i, a1, p) { char s[64]; sprintf(s, "%s.%s.i%d.%s",#v1,#v2,i,#a1); iniFileWriteString(ini, ROOT_ELEMENT, s, enumToString(p, properties->v1.v2[i].a1)); }


static void propLoad(Properties* properties) 
{
#ifndef NO_FILE_HISTORY
    IniFile *histFile;
#endif
    IniFile *propFile = iniFileOpen(settFilename);
    int i;

    GET_STR_VALUE_2(propFile, settings, language);
    i = langFromName(properties->settings.language, 0);
    if (i != EMU_LANG_UNKNOWN) properties->language = i;

    GET_ENUM_VALUE_2(propFile, settings, disableScreensaver, BoolPair);    
    GET_ENUM_VALUE_2(propFile, settings, showStatePreview, BoolPair);
    /* usePngScreenshots no longer loaded from INI -- PNG is the only format. */
    GET_ENUM_VALUE_2(propFile, settings, portable, BoolPair);
    GET_STR_VALUE_2(propFile, settings, themeName);

    GET_ENUM_VALUE_2(propFile, emulation, ejectMediaOnExit, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, registerFileTypes, BoolPair);
    GET_STR_VALUE_2(propFile, emulation, statsDefDir);
    GET_STR_VALUE_2(propFile, emulation, machineName);
    GET_STR_VALUE_2(propFile, emulation, machinesDir);
    GET_STR_VALUE_2(propFile, emulation, shortcutProfile);
    GET_INT_VALUE_2(propFile, emulation, speed);
    GET_ENUM_VALUE_2(propFile, emulation, syncMethod, EmuSyncPair);
    GET_ENUM_VALUE_2(propFile, emulation, syncMethodGdi, EmuSyncPair);
    GET_ENUM_VALUE_2(propFile, emulation, syncMethodD3D, EmuSyncPair);
    GET_ENUM_VALUE_2(propFile, emulation, syncMethodDirectX, EmuSyncPair);
    GET_ENUM_VALUE_2(propFile, emulation, vdpSyncMode, VdpSyncPair);
    GET_ENUM_VALUE_2(propFile, emulation, enableFdcTiming, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, enableHddSdBoost, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, enableCasBoost, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, noSpriteLimits, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, frontSwitch, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, pauseSwitch, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, audioSwitch, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, priorityBoost, BoolPair);
    GET_ENUM_VALUE_2(propFile, emulation, reverseEnable, BoolPair);
    GET_INT_VALUE_2(propFile, emulation, reverseMaxTime);
    GET_INT_VALUE_2(propFile, emulation, vdpCmdSpeed);
    GET_INT_VALUE_2(propFile, emulation, mouseSensitivity);

    GET_ENUM_VALUE_2(propFile, video, monitorColor, MonitorColorPair);
    GET_ENUM_VALUE_2(propFile, video, monitorType, MonitorTypePair);
    GET_ENUM_VALUE_2(propFile, video, windowSize, WindowSizePair);
    properties->video.windowSizeInitial = properties->video.windowSize;
    GET_INT_VALUE_2(propFile, video, windowX);
    GET_INT_VALUE_2(propFile, video, windowY);
    GET_ENUM_VALUE_2(propFile, video, driver, VideoDriverPair);
    GET_INT_VALUE_2(propFile, video, frameSkip);
    GET_INT_VALUE_3(propFile, video, fullscreen, width);
    GET_INT_VALUE_3(propFile, video, fullscreen, height);
    GET_INT_VALUE_3(propFile, video, fullscreen, bitDepth);
    GET_ENUM_VALUE_2(propFile, video, maximizeIsFullscreen, BoolPair);
    GET_ENUM_VALUE_2(propFile, video, deInterlace, BoolPair);
    GET_ENUM_VALUE_2(propFile, video, blendFrames, BoolPair);
    GET_ENUM_VALUE_2(propFile, video, horizontalStretch, BoolPair);
    GET_ENUM_VALUE_2(propFile, video, verticalStretch, BoolPair);
    GET_INT_VALUE_2(propFile, video, contrast);
    GET_INT_VALUE_2(propFile, video, brightness);
    GET_INT_VALUE_2(propFile, video, saturation);
    GET_INT_VALUE_2(propFile, video, gamma);
    GET_ENUM_VALUE_2(propFile, video, scanlinesEnable, BoolPair);
    GET_INT_VALUE_2(propFile, video, scanlinesPct);
    GET_ENUM_VALUE_2(propFile, video, scanlinesBrightAuto, BoolPair);
    GET_INT_VALUE_2(propFile, video, scanlinesBrightPct);
    GET_INT_VALUE_2(propFile, video, scanlinesShapeMode);
    GET_INT_VALUE_2(propFile, video, scanlinesShapePct);
    GET_ENUM_VALUE_2(propFile, video, hdrEnable, BoolPair);
    GET_INT_VALUE_2(propFile, video, hdrPaperWhiteNits);
    GET_ENUM_VALUE_2(propFile, video, recordHdr, BoolPair);
    GET_ENUM_VALUE_2(propFile, video, colorSaturationEnable, BoolPair);
    GET_INT_VALUE_2(propFile, video, colorSaturationWidth);
    GET_ENUM_VALUE_2(propFile, video, detectActiveMonitor, BoolPair);
    GET_INT_VALUE_2(propFile, video, captureFps);
    GET_INT_VALUE_2(propFile, video, captureSize);

    GET_ENUM_VALUE_3(propFile, video, d3d, linearFiltering, BoolPair);
    GET_ENUM_VALUE_3(propFile, video, d3d, extendBorderColor, BoolPair);
    GET_ENUM_VALUE_3(propFile, video, d3d, forceHighRes, BoolPair);
    /* scalingFilter absent: migrate from legacy flags only if an ini exists
       (forceHighRes[+linear]->sharp/prescaled, linear->bilinear, else nearest);
       a fresh install (no ini) keeps the default (sharp), not nearest. */
    properties->video.d3d.scalingFilter = -1;
    GET_ENUM_VALUE_3(propFile, video, d3d, scalingFilter, ScalingFilterPair);
    if (properties->video.d3d.scalingFilter < 0) {
        FILE* sf = fopen(settFilename, "r");
        if (sf != NULL) {
            fclose(sf);
            properties->video.d3d.scalingFilter =
                (properties->video.d3d.forceHighRes && properties->video.d3d.linearFiltering) ? P_D3D_SCALE_PRESCALED :
                properties->video.d3d.forceHighRes    ? P_D3D_SCALE_SHARP :
                properties->video.d3d.linearFiltering ? P_D3D_SCALE_BILINEAR :
                                                        P_D3D_SCALE_NEAREST;
        }
        else {
            properties->video.d3d.scalingFilter = P_D3D_SCALE_SHARP;
        }
    }
    GET_INT_VALUE_3(propFile, video, d3d, aspectRatioType);
    GET_INT_VALUE_3(propFile, video, d3d, cropType);

    GET_INT_VALUE_3(propFile, video, d3d, cropLeft);
    GET_INT_VALUE_3(propFile, video, d3d, cropRight);
    GET_INT_VALUE_3(propFile, video, d3d, cropTop);
    GET_INT_VALUE_3(propFile, video, d3d, cropBottom);

    GET_INT_VALUE_2(propFile, videoIn, disabled);
    GET_INT_VALUE_2(propFile, videoIn, inputIndex);
    GET_STR_VALUE_2(propFile, videoIn, inputName);

    GET_ENUM_VALUE_2(propFile, sound, driver, SoundDriverPair);
    GET_INT_VALUE_2(propFile, sound, bufSize);
    GET_ENUM_VALUE_2(propFile, sound, stabilizeDSoundTiming, BoolPair);
    GET_ENUM_VALUE_2(propFile, sound, stereo, BoolPair);
    GET_INT_VALUE_2(propFile, sound, masterVolume);
    GET_ENUM_VALUE_2(propFile, sound, masterEnable, BoolPair);
    
    GET_ENUM_VALUE_3(propFile, sound, chip, enableYM2413, BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, enableY8950, BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, enableMoonsound, BoolPair);
    GET_INT_VALUE_3(propFile, sound, chip, moonsoundSRAMSize);
    GET_INT_VALUE_3(propFile, sound, chip, ym2413Oversampling);
    GET_INT_VALUE_3(propFile, sound, chip, y8950Oversampling);
    GET_INT_VALUE_3(propFile, sound, chip, moonsoundOversampling);
    GET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendOpenmsxEnabled,  BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendOpenmsx2Enabled, BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendEmu2413Enabled,  BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendNukedEnabled,    BoolPair);
    GET_INT_VALUE_3 (propFile, sound, chip, ym2413BackendActive);
    GET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendFmoplEnabled,     BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendEmu8950Enabled,   BoolPair);
    GET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendOpenmsxEnabled,   BoolPair);
    GET_INT_VALUE_3 (propFile, sound, chip, y8950BackendActive);
    GET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterMode);
    GET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterLpfHz);
    GET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterHpfHz);
#ifndef YM2413_BUILD_OPENMSX_INITIAL
    /* openmsx (initial) is dead-coded -- never carry an enabled flag at runtime. */
    properties->sound.chip.ym2413BackendOpenmsxEnabled = 0;
#endif
    GET_ENUM_VALUE_3(propFile, sound, YkIn, type, MidiTypePair);
    GET_STR_VALUE_3(propFile, sound, YkIn, name);
    GET_STR_VALUE_3(propFile, sound, YkIn, fileName);
    GET_STR_VALUE_3(propFile, sound, YkIn, desc);
    GET_INT_VALUE_3(propFile, sound, YkIn, channel);
    GET_ENUM_VALUE_3(propFile, sound, MidiIn, type, MidiTypePair);
    GET_STR_VALUE_3(propFile, sound, MidiIn, name);
    GET_STR_VALUE_3(propFile, sound, MidiIn, fileName);
    GET_STR_VALUE_3(propFile, sound, MidiIn, desc);
    GET_ENUM_VALUE_3(propFile, sound, MidiOut, type, MidiTypePair);
    GET_STR_VALUE_3(propFile, sound, MidiOut, name);
    GET_STR_VALUE_3(propFile, sound, MidiOut, fileName);
    GET_STR_VALUE_3(propFile, sound, MidiOut, desc);
    GET_ENUM_VALUE_3(propFile, sound, MidiOut, mt32ToGm, BoolPair);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, volume);
    GET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, enable, BoolPair);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, pan);
    GET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, volume);
    
    GET_ENUM_VALUE_2(propFile, joystick, disablePOV0Dpad, BoolPair);
    
    GET_STR_VALUE_2(propFile, joy1, type);
    properties->joy1.typeId = joystickPortNameToType(0, properties->joy1.type, 0);
    GET_ENUM_VALUE_2(propFile, joy1, autofire, OnOffPair);
    
    GET_STR_VALUE_2(propFile, joy2, type);
    properties->joy2.typeId = joystickPortNameToType(1, properties->joy2.type, 0);
    GET_ENUM_VALUE_2(propFile, joy2, autofire, OnOffPair);
    
    GET_STR_VALUE_2(propFile, keyboard, configFile);
    GET_INT_VALUE_2(propFile, keyboard, enableKeyboardQuirk);
    
    GET_ENUM_VALUE_3(propFile, ports, Lpt, type, PrinterTypePair);
    GET_ENUM_VALUE_3(propFile, ports, Lpt, emulation, PrinterEmulationPair);
    GET_STR_VALUE_3(propFile, ports, Lpt, name);
    GET_STR_VALUE_3(propFile, ports, Lpt, fileName);
    GET_STR_VALUE_3(propFile, ports, Lpt, portName);
    GET_ENUM_VALUE_3(propFile, ports, Com, type, ComTypePair);
    GET_STR_VALUE_3(propFile, ports, Com, name);
    GET_STR_VALUE_3(propFile, ports, Com, fileName);
    GET_STR_VALUE_3(propFile, ports, Com, portName);

    GET_INT_VALUE_3(propFile, ports, Eth, ethIndex);
    GET_INT_VALUE_3(propFile, ports, Eth, disabled);
    GET_STR_VALUE_3(propFile, ports, Eth, macAddress);
    
    
    GET_INT_VALUE_2(propFile, cartridge, defaultType);

    GET_ENUM_VALUE_2(propFile, diskdrive, cdromMethod, CdromDrvPair);
    GET_INT_VALUE_2(propFile, diskdrive, cdromDrive);
    
    GET_INT_VALUE_2(propFile, cassette, showCustomFiles);
    GET_ENUM_VALUE_2(propFile, cassette, readOnly, BoolPair);
    GET_ENUM_VALUE_2(propFile, cassette, rewindAfterInsert, BoolPair);
    
    GET_ENUM_VALUE_2(propFile, nowind, enableDos2, BoolPair);    
    GET_ENUM_VALUE_2(propFile, nowind, enableOtherDiskRoms, BoolPair);    
    GET_ENUM_VALUE_2(propFile, nowind, enablePhantomDrives, BoolPair);    
    GET_ENUM_VALUE_2(propFile, nowind, ignoreBootFlag, BoolPair);   
    GET_INT_VALUE_2(propFile, nowind,  partitionNumber);

    GET_STR_VALUE_2(propFile,  capture, audioDir);
    GET_STR_VALUE_2(propFile,  capture, videoDir);
    GET_STR_VALUE_2(propFile,  capture, screenshotDir);
    GET_STR_VALUE_2(propFile,  capture, replayDir);
    GET_ENUM_VALUE_2(propFile, capture, audioFormat,              CapAudioFormatPair);
    GET_INT_VALUE_2(propFile,  capture, audioBitrateKbps);
    GET_ENUM_VALUE_2(propFile, capture, videoCodec,               CapVideoCodecPair);
    GET_ENUM_VALUE_2(propFile, capture, screenshotFormat,         CapImgFormatPair);
    GET_ENUM_VALUE_2(propFile, capture, audioPromptFilename,      BoolPair);
    GET_ENUM_VALUE_2(propFile, capture, videoPromptFilename,      BoolPair);
    GET_ENUM_VALUE_2(propFile, capture, screenshotPromptFilename, BoolPair);
    GET_ENUM_VALUE_2(propFile, capture, replayPromptFilename,     BoolPair);
    GET_ENUM_VALUE_2(propFile, capture, showCompletionToast,      BoolPair);
    GET_ENUM_VALUE_2(propFile, capture, videoUsePostRender,       BoolPair);
    GET_INT_VALUE_2(propFile,  capture, videoResolution);

    /* PNG is the only screenshot format; override stale INI values. */
    properties->settings.usePngScreenshots    = 1;
    properties->capture.screenshotFormat      = CAP_IMG_PNG;

    iniFileClose(propFile);
    
#ifndef NO_FILE_HISTORY
    histFile = iniFileOpen(histFilename);
    
    GET_STR_VALUE_2(histFile, cartridge, defDir);
    GET_STR_VALUE_2(histFile, cartridge, defDirSEGA);
    GET_STR_VALUE_2(histFile, cartridge, defDirCOLECO);
    GET_STR_VALUE_2(histFile, cartridge, defDirSVI);
    GET_INT_VALUE_2(histFile, cartridge, autoReset);
    GET_INT_VALUE_2(histFile, cartridge, quickStartDrive);

    GET_STR_VALUE_2(histFile, diskdrive, defDir);
    GET_STR_VALUE_2(histFile, diskdrive, defHdDir);
    GET_INT_VALUE_2(histFile, diskdrive, autostartA);
    GET_INT_VALUE_2(histFile, diskdrive, quickStartDrive);
    
    GET_STR_VALUE_2(histFile, cassette, defDir);

    for (i = 0; i < PROP_MAX_CARTS; i++) {
        GET_STR_VALUE_2i1(histFile, media, carts, i, fileName);
        GET_STR_VALUE_2i1(histFile, media, carts, i, fileNameInZip);
        GET_STR_VALUE_2i1(histFile, media, carts, i, directory);
        GET_INT_VALUE_2i1(histFile, media, carts, i, extensionFilter);
        GET_INT_VALUE_2i1(histFile, media, carts, i, type);
        /* ini stores fileNameInZip as UTF-8; resolve back to the zip TOC's
        ** raw bytes so unzLocateFile / strcmp match. */
        if (properties->media.carts[i].fileNameInZip[0] != '\0') {
            char raw[PROP_MAXPATH];
            if (zipResolveUtf8EntryName(properties->media.carts[i].fileName,
                                        properties->media.carts[i].fileNameInZip,
                                        raw, sizeof(raw))) {
                strcpy(properties->media.carts[i].fileNameInZip, raw);
            }
        }
    }
    
    for (i = 0; i < PROP_MAX_DISKS; i++) {
        GET_STR_VALUE_2i1(histFile, media, disks, i, fileName);
        GET_STR_VALUE_2i1(histFile, media, disks, i, fileNameInZip);
        GET_STR_VALUE_2i1(histFile, media, disks, i, directory);
        GET_INT_VALUE_2i1(histFile, media, disks, i, extensionFilter);
        GET_INT_VALUE_2i1(histFile, media, disks, i, type);
        if (properties->media.disks[i].fileNameInZip[0] != '\0') {
            char raw[PROP_MAXPATH];
            if (zipResolveUtf8EntryName(properties->media.disks[i].fileName,
                                        properties->media.disks[i].fileNameInZip,
                                        raw, sizeof(raw))) {
                strcpy(properties->media.disks[i].fileNameInZip, raw);
            }
        }
    }
    
    for (i = 0; i < PROP_MAX_TAPES; i++) {
        GET_STR_VALUE_2i1(histFile, media, tapes, i, fileName);
        GET_STR_VALUE_2i1(histFile, media, tapes, i, fileNameInZip);
        GET_STR_VALUE_2i1(histFile, media, tapes, i, directory);
        GET_INT_VALUE_2i1(histFile, media, tapes, i, extensionFilter);
        GET_INT_VALUE_2i1(histFile, media, tapes, i, type);
        if (properties->media.tapes[i].fileNameInZip[0] != '\0') {
            char raw[PROP_MAXPATH];
            if (zipResolveUtf8EntryName(properties->media.tapes[i].fileName,
                                        properties->media.tapes[i].fileNameInZip,
                                        raw, sizeof(raw))) {
                strcpy(properties->media.tapes[i].fileNameInZip, raw);
            }
        }
    }
    
    for (i = 0; i < MAX_HISTORY; i++) {
        GET_STR_VALUE_2i(histFile, filehistory, cartridge[0], i);
        GET_INT_VALUE_2i(histFile, filehistory, cartridgeType[0], i);
        GET_STR_VALUE_2i(histFile, filehistory, cartridge[1], i);
        GET_INT_VALUE_2i(histFile, filehistory, cartridgeType[1], i);
        GET_STR_VALUE_2i(histFile, filehistory, diskdrive[0], i);
        GET_STR_VALUE_2i(histFile, filehistory, diskdrive[1], i);
        GET_STR_VALUE_2i(histFile, filehistory, cassette[0], i);
    }

    GET_STR_VALUE_2(histFile, filehistory, quicksave);
    GET_STR_VALUE_2(histFile, filehistory, videocap);
    GET_INT_VALUE_2(histFile, filehistory, count);

    for (i = 0; i < DLG_MAX_ID; i++) {
        GET_INT_VALUE_2i1(histFile, settings, windowPos, i, left);
        GET_INT_VALUE_2i1(histFile, settings, windowPos, i, top);
        GET_INT_VALUE_2i1(histFile, settings, windowPos, i, width);
        GET_INT_VALUE_2i1(histFile, settings, windowPos, i, height);
    }
    
    iniFileClose(histFile);
#endif
}

void propSave(Properties* properties) 
{
#ifndef NO_FILE_HISTORY
	IniFile *histFile;
#endif
	IniFile *propFile;
    int i;
    
    propFile = iniFileOpen(settFilename);

    strcpy(properties->settings.language, langToName(properties->language, 0));
    SET_STR_VALUE_2(propFile, settings, language);
    
    SET_ENUM_VALUE_2(propFile, settings, disableScreensaver, YesNoPair);    
    SET_ENUM_VALUE_2(propFile, settings, showStatePreview, YesNoPair);
    /* usePngScreenshots no longer persisted: PNG is the only format. */
    SET_ENUM_VALUE_2(propFile, settings, portable, YesNoPair);
    if (appConfigGetString("singletheme", NULL) == NULL) {
        SET_STR_VALUE_2(propFile, settings, themeName);
    }

    SET_ENUM_VALUE_2(propFile, emulation, ejectMediaOnExit, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, registerFileTypes, YesNoPair);
    SET_STR_VALUE_2(propFile, emulation, statsDefDir);
    if (appConfigGetString("singlemachine", NULL) == NULL) {
        SET_STR_VALUE_2(propFile, emulation, machineName);
    }
    SET_STR_VALUE_2(propFile, emulation, machinesDir);
    SET_STR_VALUE_2(propFile, emulation, shortcutProfile);
    SET_INT_VALUE_2(propFile, emulation, speed);
    SET_ENUM_VALUE_2(propFile, emulation, syncMethod, EmuSyncPair);
    SET_ENUM_VALUE_2(propFile, emulation, syncMethodGdi, EmuSyncPair);
    SET_ENUM_VALUE_2(propFile, emulation, syncMethodD3D, EmuSyncPair);
    SET_ENUM_VALUE_2(propFile, emulation, syncMethodDirectX, EmuSyncPair);
    SET_ENUM_VALUE_2(propFile, emulation, vdpSyncMode, VdpSyncPair);
    SET_ENUM_VALUE_2(propFile, emulation, enableFdcTiming, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, enableHddSdBoost, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, enableCasBoost, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, noSpriteLimits, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, frontSwitch, OnOffPair);
    SET_ENUM_VALUE_2(propFile, emulation, pauseSwitch, OnOffPair);
    SET_ENUM_VALUE_2(propFile, emulation, audioSwitch, OnOffPair);
    SET_ENUM_VALUE_2(propFile, emulation, priorityBoost, YesNoPair);
    SET_ENUM_VALUE_2(propFile, emulation, reverseEnable, BoolPair);
    SET_INT_VALUE_2(propFile, emulation, reverseMaxTime);
    SET_INT_VALUE_2(propFile, emulation, vdpCmdSpeed);
    SET_INT_VALUE_2(propFile, emulation, mouseSensitivity);

    SET_ENUM_VALUE_2(propFile, video, monitorColor, MonitorColorPair);
    SET_ENUM_VALUE_2(propFile, video, monitorType, MonitorTypePair);
    SET_INT_VALUE_2(propFile, video, contrast);
    SET_INT_VALUE_2(propFile, video, brightness);
    SET_INT_VALUE_2(propFile, video, saturation);
    SET_INT_VALUE_2(propFile, video, gamma);
    SET_ENUM_VALUE_2(propFile, video, scanlinesEnable, YesNoPair);
    SET_INT_VALUE_2(propFile, video, scanlinesPct);
    SET_ENUM_VALUE_2(propFile, video, scanlinesBrightAuto, YesNoPair);
    SET_INT_VALUE_2(propFile, video, scanlinesBrightPct);
    SET_INT_VALUE_2(propFile, video, scanlinesShapeMode);
    SET_INT_VALUE_2(propFile, video, scanlinesShapePct);
    SET_ENUM_VALUE_2(propFile, video, hdrEnable, YesNoPair);
    SET_INT_VALUE_2(propFile, video, hdrPaperWhiteNits);
    SET_ENUM_VALUE_2(propFile, video, recordHdr, YesNoPair);
    SET_ENUM_VALUE_2(propFile, video, colorSaturationEnable, YesNoPair);
    SET_INT_VALUE_2(propFile, video, colorSaturationWidth);
    SET_ENUM_VALUE_2(propFile, video, deInterlace, OnOffPair);
    SET_ENUM_VALUE_2(propFile, video, blendFrames, YesNoPair);
    SET_ENUM_VALUE_2(propFile, video, detectActiveMonitor, YesNoPair);
    SET_ENUM_VALUE_2(propFile, video, horizontalStretch, YesNoPair);
    SET_ENUM_VALUE_2(propFile, video, verticalStretch, YesNoPair);
    SET_INT_VALUE_2(propFile, video, frameSkip);
    if (properties->video.windowSizeChanged) {
    	SET_ENUM_VALUE_2(propFile, video, windowSize, WindowSizePair);
    }
    else {
    	int temp=properties->video.windowSize;
    	properties->video.windowSize=properties->video.windowSizeInitial;
    	SET_ENUM_VALUE_2(propFile, video, windowSize, WindowSizePair);
    	properties->video.windowSize=temp;
    }
    SET_INT_VALUE_2(propFile, video, windowX);
    SET_INT_VALUE_2(propFile, video, windowY);
    SET_INT_VALUE_3(propFile, video, fullscreen, width);
    SET_INT_VALUE_3(propFile, video, fullscreen, height);
    SET_INT_VALUE_3(propFile, video, fullscreen, bitDepth);
    SET_ENUM_VALUE_2(propFile, video, maximizeIsFullscreen, YesNoPair);
    SET_ENUM_VALUE_2(propFile, video, driver, VideoDriverPair);
    SET_INT_VALUE_2(propFile, video, captureFps);
    
    SET_INT_VALUE_2(propFile, video, captureSize);

    /* Keep legacy flags in sync with scalingFilter so an older build reading
       this ini still behaves right (sharp->highres+filter, bilinear->filter). */
    properties->video.d3d.linearFiltering = properties->video.d3d.scalingFilter == P_D3D_SCALE_BILINEAR
                                         || properties->video.d3d.scalingFilter == P_D3D_SCALE_PRESCALED;
    properties->video.d3d.forceHighRes    = properties->video.d3d.scalingFilter == P_D3D_SCALE_SHARP
                                         || properties->video.d3d.scalingFilter == P_D3D_SCALE_PRESCALED;
    /* YesNoPair (not BoolPair, whose true/false entries are inverted). */
    SET_ENUM_VALUE_3(propFile, video, d3d, scalingFilter, ScalingFilterPair);
    SET_ENUM_VALUE_3(propFile, video, d3d, linearFiltering, YesNoPair);
    SET_ENUM_VALUE_3(propFile, video, d3d, extendBorderColor, YesNoPair);
    SET_ENUM_VALUE_3(propFile, video, d3d, forceHighRes, YesNoPair);
    SET_INT_VALUE_3(propFile, video, d3d, aspectRatioType);
    SET_INT_VALUE_3(propFile, video, d3d, cropType);

    SET_INT_VALUE_3(propFile, video, d3d, cropLeft);
    SET_INT_VALUE_3(propFile, video, d3d, cropRight);
    SET_INT_VALUE_3(propFile, video, d3d, cropTop);
    SET_INT_VALUE_3(propFile, video, d3d, cropBottom);

    SET_INT_VALUE_2(propFile, videoIn, disabled);
    SET_INT_VALUE_2(propFile, videoIn, inputIndex);
    SET_STR_VALUE_2(propFile, videoIn, inputName);

    SET_ENUM_VALUE_2(propFile, sound, driver, SoundDriverPair);
    SET_INT_VALUE_2(propFile, sound, bufSize);
    SET_ENUM_VALUE_2(propFile, sound, stabilizeDSoundTiming, YesNoPair);
    SET_ENUM_VALUE_2(propFile, sound, stereo, YesNoPair);
    SET_INT_VALUE_2(propFile, sound, masterVolume);
    SET_ENUM_VALUE_2(propFile, sound, masterEnable, YesNoPair);
    
    SET_ENUM_VALUE_3(propFile, sound, chip, enableYM2413, YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, enableY8950, YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, enableMoonsound, YesNoPair);
    SET_INT_VALUE_3(propFile, sound, chip, moonsoundSRAMSize);
//    SET_INT_VALUE_3(sound, chip, ym2413Oversampling);
//    SET_INT_VALUE_3(sound, chip, y8950Oversampling);
//    SET_INT_VALUE_3(sound, chip, moonsoundOversampling);
    SET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendOpenmsxEnabled,  YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendOpenmsx2Enabled, YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendEmu2413Enabled,  YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, ym2413BackendNukedEnabled,    YesNoPair);
    SET_INT_VALUE_3 (propFile, sound, chip, ym2413BackendActive);
    SET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendFmoplEnabled,     YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendEmu8950Enabled,   YesNoPair);
    SET_ENUM_VALUE_3(propFile, sound, chip, y8950BackendOpenmsxEnabled,   YesNoPair);
    SET_INT_VALUE_3 (propFile, sound, chip, y8950BackendActive);
    SET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterMode);
    SET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterLpfHz);
    SET_INT_VALUE_3 (propFile, sound, chip, ym2413AnalogFilterHpfHz);
    SET_ENUM_VALUE_3(propFile, sound, YkIn, type, MidiTypePair);
    SET_STR_VALUE_3(propFile, sound, YkIn, name);
//    SET_STR_VALUE_3(sound, YkIn, fileName);
//    SET_STR_VALUE_3(sound, YkIn, desc);
    SET_INT_VALUE_3(propFile, sound, YkIn, channel);
    SET_ENUM_VALUE_3(propFile, sound, MidiIn, type, MidiTypePair);
    SET_STR_VALUE_3(propFile, sound, MidiIn, name);
//    SET_STR_VALUE_3(sound, MidiIn, fileName);
//    SET_STR_VALUE_3(sound, MidiIn, desc);
    SET_ENUM_VALUE_3(propFile, sound, MidiOut, type, MidiTypePair);
    SET_STR_VALUE_3(propFile, sound, MidiOut, name);
//    SET_STR_VALUE_3(sound, MidiOut, fileName);
//    SET_STR_VALUE_3(sound, MidiOut, desc);
    SET_ENUM_VALUE_3(propFile, sound, MidiOut, mt32ToGm, YesNoPair);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PSG, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_SCC, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXMUSIC, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MSXAUDIO, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_KEYBOARD, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MOONSOUND, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_YAMAHA_SFG, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_PCM, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_IO, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_MIDI, volume);
    SET_ENUM_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, enable, YesNoPair);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, pan);
    SET_INT_VALUE_2s1(propFile, sound, mixerChannel, MIXER_CHANNEL_CASSETTE, volume);
    
    SET_ENUM_VALUE_2(propFile, joystick, disablePOV0Dpad, YesNoPair);
    
    strcpy(properties->joy1.type, joystickPortTypeToName(0, 0));
    SET_STR_VALUE_2(propFile, joy1, type);
    SET_ENUM_VALUE_2(propFile, joy1, autofire, OnOffPair);
    
    strcpy(properties->joy2.type, joystickPortTypeToName(1, 0));
    SET_STR_VALUE_2(propFile, joy2, type);
    SET_ENUM_VALUE_2(propFile, joy2, autofire, OnOffPair);
    
    SET_STR_VALUE_2(propFile, keyboard, configFile);
    SET_INT_VALUE_2(propFile, keyboard, enableKeyboardQuirk);
    
    SET_ENUM_VALUE_3(propFile, ports, Lpt, type, PrinterTypePair);
    SET_ENUM_VALUE_3(propFile, ports, Lpt, emulation, PrinterEmulationPair);
    SET_STR_VALUE_3(propFile, ports, Lpt, name);
    SET_STR_VALUE_3(propFile, ports, Lpt, fileName);
    SET_STR_VALUE_3(propFile, ports, Lpt, portName);
    SET_ENUM_VALUE_3(propFile, ports, Com, type, ComTypePair);
    SET_STR_VALUE_3(propFile, ports, Com, name);
    SET_STR_VALUE_3(propFile, ports, Com, fileName);
    SET_STR_VALUE_3(propFile, ports, Com, portName);

    SET_INT_VALUE_3(propFile, ports, Eth, ethIndex);
    SET_INT_VALUE_3(propFile, ports, Eth, disabled);
    SET_STR_VALUE_3(propFile, ports, Eth, macAddress);
    
    SET_INT_VALUE_2(propFile, cartridge, defaultType);

    SET_ENUM_VALUE_2(propFile, diskdrive, cdromMethod, CdromDrvPair);
    SET_INT_VALUE_2(propFile, diskdrive, cdromDrive);
    
    SET_INT_VALUE_2(propFile, cassette, showCustomFiles);
    SET_ENUM_VALUE_2(propFile, cassette, readOnly, YesNoPair);
    SET_ENUM_VALUE_2(propFile, cassette, rewindAfterInsert, YesNoPair);

    SET_ENUM_VALUE_2(propFile, nowind, enableDos2, YesNoPair);    
    SET_ENUM_VALUE_2(propFile, nowind, enableOtherDiskRoms, BoolPair);    
    SET_ENUM_VALUE_2(propFile, nowind, enablePhantomDrives, BoolPair);    
    SET_ENUM_VALUE_2(propFile, nowind, ignoreBootFlag, BoolPair);   
    SET_INT_VALUE_2(propFile, nowind,  partitionNumber);

    SET_STR_VALUE_2(propFile,  capture, audioDir);
    SET_STR_VALUE_2(propFile,  capture, videoDir);
    SET_STR_VALUE_2(propFile,  capture, screenshotDir);
    SET_STR_VALUE_2(propFile,  capture, replayDir);
    SET_ENUM_VALUE_2(propFile, capture, audioFormat,              CapAudioFormatPair);
    SET_INT_VALUE_2(propFile,  capture, audioBitrateKbps);
    SET_ENUM_VALUE_2(propFile, capture, videoCodec,               CapVideoCodecPair);
    SET_ENUM_VALUE_2(propFile, capture, screenshotFormat,         CapImgFormatPair);
    SET_ENUM_VALUE_2(propFile, capture, audioPromptFilename,      YesNoPair);
    SET_ENUM_VALUE_2(propFile, capture, videoPromptFilename,      YesNoPair);
    SET_ENUM_VALUE_2(propFile, capture, screenshotPromptFilename, YesNoPair);
    SET_ENUM_VALUE_2(propFile, capture, replayPromptFilename,     YesNoPair);
    SET_ENUM_VALUE_2(propFile, capture, showCompletionToast,      YesNoPair);
    SET_ENUM_VALUE_2(propFile, capture, videoUsePostRender,       YesNoPair);
    SET_INT_VALUE_2(propFile,  capture, videoResolution);

    iniFileClose(propFile);

#ifndef NO_FILE_HISTORY
    histFile = iniFileOpen(histFilename);
    
    SET_STR_VALUE_2(histFile, cartridge, defDir);
    SET_STR_VALUE_2(histFile, cartridge, defDirSEGA);
    SET_STR_VALUE_2(histFile, cartridge, defDirCOLECO);
    SET_STR_VALUE_2(histFile, cartridge, defDirSVI);
    SET_INT_VALUE_2(histFile, cartridge, autoReset);
    SET_INT_VALUE_2(histFile, cartridge, quickStartDrive);

    SET_STR_VALUE_2(histFile, diskdrive, defDir);
    SET_STR_VALUE_2(histFile, diskdrive, defHdDir);
    SET_INT_VALUE_2(histFile, diskdrive, autostartA);
    SET_INT_VALUE_2(histFile, diskdrive, quickStartDrive);
    
    SET_STR_VALUE_2(histFile, cassette, defDir);

    /* Convert raw zip TOC bytes to UTF-8 for ini storage; propLoad
    ** restores raw bytes via zipResolveUtf8EntryName. *4 buffer leaves
    ** headroom for CP932 -> UTF-8 expansion. */
    for (i = 0; i < PROP_MAX_CARTS; i++) {
        char keyBuf[64];
        char utf8Buf[PROP_MAXPATH * 4];
        const char* src = properties->media.carts[i].fileNameInZip;
        SET_STR_VALUE_2i1(histFile, media, carts, i, fileName);
#ifdef _WIN32
        AnyToUtf8(src, utf8Buf, (int)sizeof(utf8Buf));
#else
        {
            size_t n = strlen(src);
            if (n >= sizeof(utf8Buf)) n = sizeof(utf8Buf) - 1;
            memcpy(utf8Buf, src, n);
            utf8Buf[n] = 0;
        }
#endif
        if ((size_t)snprintf(keyBuf, sizeof(keyBuf),
                             "media.carts.i%d.fileNameInZip", i) >= sizeof(keyBuf)) {
            keyBuf[sizeof(keyBuf) - 1] = 0;
        }
        iniFileWriteString(histFile, ROOT_ELEMENT, keyBuf, utf8Buf);
        SET_STR_VALUE_2i1(histFile, media, carts, i, directory);
        SET_INT_VALUE_2i1(histFile, media, carts, i, extensionFilter);
        SET_INT_VALUE_2i1(histFile, media, carts, i, type);
    }
    
    for (i = 0; i < PROP_MAX_DISKS; i++) {
        char keyBuf[64];
        char utf8Buf[PROP_MAXPATH * 4];
        const char* src = properties->media.disks[i].fileNameInZip;
        SET_STR_VALUE_2i1(histFile, media, disks, i, fileName);
#ifdef _WIN32
        AnyToUtf8(src, utf8Buf, (int)sizeof(utf8Buf));
#else
        {
            size_t n = strlen(src);
            if (n >= sizeof(utf8Buf)) n = sizeof(utf8Buf) - 1;
            memcpy(utf8Buf, src, n);
            utf8Buf[n] = 0;
        }
#endif
        if ((size_t)snprintf(keyBuf, sizeof(keyBuf),
                             "media.disks.i%d.fileNameInZip", i) >= sizeof(keyBuf)) {
            keyBuf[sizeof(keyBuf) - 1] = 0;
        }
        iniFileWriteString(histFile, ROOT_ELEMENT, keyBuf, utf8Buf);
        SET_STR_VALUE_2i1(histFile, media, disks, i, directory);
        SET_INT_VALUE_2i1(histFile, media, disks, i, extensionFilter);
        SET_INT_VALUE_2i1(histFile, media, disks, i, type);
    }
    
    for (i = 0; i < PROP_MAX_TAPES; i++) {
        char keyBuf[64];
        char utf8Buf[PROP_MAXPATH * 4];
        const char* src = properties->media.tapes[i].fileNameInZip;
        SET_STR_VALUE_2i1(histFile, media, tapes, i, fileName);
#ifdef _WIN32
        AnyToUtf8(src, utf8Buf, (int)sizeof(utf8Buf));
#else
        {
            size_t n = strlen(src);
            if (n >= sizeof(utf8Buf)) n = sizeof(utf8Buf) - 1;
            memcpy(utf8Buf, src, n);
            utf8Buf[n] = 0;
        }
#endif
        if ((size_t)snprintf(keyBuf, sizeof(keyBuf),
                             "media.tapes.i%d.fileNameInZip", i) >= sizeof(keyBuf)) {
            keyBuf[sizeof(keyBuf) - 1] = 0;
        }
        iniFileWriteString(histFile, ROOT_ELEMENT, keyBuf, utf8Buf);
        SET_STR_VALUE_2i1(histFile, media, tapes, i, directory);
        SET_INT_VALUE_2i1(histFile, media, tapes, i, extensionFilter);
        SET_INT_VALUE_2i1(histFile, media, tapes, i, type);
    }
    
    for (i = 0; i < MAX_HISTORY; i++) {
        SET_STR_VALUE_2i(histFile, filehistory, cartridge[0], i);
        SET_INT_VALUE_2i(histFile, filehistory, cartridgeType[0], i);
        SET_STR_VALUE_2i(histFile, filehistory, cartridge[1], i);
        SET_INT_VALUE_2i(histFile, filehistory, cartridgeType[1], i);
        SET_STR_VALUE_2i(histFile, filehistory, diskdrive[0], i);
        SET_STR_VALUE_2i(histFile, filehistory, diskdrive[1], i);
        SET_STR_VALUE_2i(histFile, filehistory, cassette[0], i);
    }

    SET_STR_VALUE_2(histFile, filehistory, quicksave);
    SET_STR_VALUE_2(histFile, filehistory, videocap);
    SET_INT_VALUE_2(histFile, filehistory, count);

    for (i = 0; i < DLG_MAX_ID; i++) {
        SET_INT_VALUE_2i1(histFile, settings, windowPos, i, left);
        SET_INT_VALUE_2i1(histFile, settings, windowPos, i, top);
        SET_INT_VALUE_2i1(histFile, settings, windowPos, i, width);
        SET_INT_VALUE_2i1(histFile, settings, windowPos, i, height);
    }
    
    iniFileClose(histFile);
#endif
}

static Properties* globalProperties = NULL;

Properties* propGetGlobalProperties()
{
    return globalProperties;
}

void propertiesGetOpllFilterHz(int mode, const SoundChip* chip,
                               int* outLpfHz, int* outHpfHz)
{
    int lpf = 0, hpf = 0;
    /* HPF is fixed at 20 Hz across every preset (DC-block; not user
    ** tunable from the dialog).  Only the LPF Hz value differs. */
    switch (mode) {
    case PROP_OPLL_FILTER_OFF:
        lpf = 0;     hpf = 0;  break;
    case PROP_OPLL_FILTER_BRIGHT:
        lpf = 12000; hpf = 20; break;
    case PROP_OPLL_FILTER_CLEAR:
        lpf = 8000;  hpf = 20; break;
    case PROP_OPLL_FILTER_STANDARD:
        lpf = 5000;  hpf = 20; break;
    case PROP_OPLL_FILTER_SOFT:
        lpf = 3500;  hpf = 20; break;
    case PROP_OPLL_FILTER_MELLOW:
        lpf = 2300;  hpf = 20; break;
    case PROP_OPLL_FILTER_CUSTOM:
    default:
        if (chip) {
            lpf = chip->ym2413AnalogFilterLpfHz;
            hpf = chip->ym2413AnalogFilterHpfHz;
        } else {
            lpf = 5000;  hpf = 20;
        }
        break;
    }
    if (outLpfHz) *outLpfHz = lpf;
    if (outHpfHz) *outHpfHz = hpf;
}

int propertiesIsSpecialCartName(const char* name)
{
    static const char* const kMarkers[] = {
        CARTNAME_SNATCHER,    CARTNAME_SDSNATCHER,  CARTNAME_SCCMIRRORED,
        CARTNAME_SCCEXPANDED, CARTNAME_SCC,         CARTNAME_SCCPLUS,
        CARTNAME_JOYREXPSG,   CARTNAME_FMPAC,       CARTNAME_PAC,
        CARTNAME_GAMEREADER,  CARTNAME_SUNRISEIDE,  CARTNAME_BEERIDE,
        CARTNAME_GIDE,        CARTNAME_NMS1210,     CARTNAME_GOUDASCSI,
        CARTNAME_SONYHBI55,
        CARTNAME_EXTRAM16KB,  CARTNAME_EXTRAM32KB,  CARTNAME_EXTRAM48KB,
        CARTNAME_EXTRAM64KB,  CARTNAME_EXTRAM512KB, CARTNAME_EXTRAM1MB,
        CARTNAME_EXTRAM2MB,   CARTNAME_EXTRAM4MB,
        CARTNAME_MEGARAM128,  CARTNAME_MEGARAM256,  CARTNAME_MEGARAM512,
        CARTNAME_MEGARAM768,  CARTNAME_MEGARAM2M,
        CARTNAME_MEGASCSI128, CARTNAME_MEGASCSI256, CARTNAME_MEGASCSI512,
        CARTNAME_MEGASCSI1MB,
        CARTNAME_NOWINDDOS1,  CARTNAME_NOWINDDOS2,
        CARTNAME_ESERAM128,   CARTNAME_ESERAM256,   CARTNAME_ESERAM512,
        CARTNAME_ESERAM1MB,
        CARTNAME_MEGAFLSHSCC, CARTNAME_MEGAFLSHSCCPLUS,
        CARTNAME_MEGAFLSHSCCPLUS_SD,
        CARTNAME_ASCII16X,    CARTNAME_YAMANOOTO,   CARTNAME_FLASHROMSCC,
        CARTNAME_WAVESCSI128, CARTNAME_WAVESCSI256, CARTNAME_WAVESCSI512,
        CARTNAME_WAVESCSI1MB,
        CARTNAME_ESESCC128,   CARTNAME_ESESCC256,   CARTNAME_ESESCC512,
    };
    size_t i;
    if (!name || !*name) return 0;
    for (i = 0; i < sizeof(kMarkers) / sizeof(kMarkers[0]); i++) {
        if (strcmp(name, kMarkers[i]) == 0) return 1;
    }
    return 0;
}

void propertiesSetDirectory(const char* defDir, const char* altDir)
{
    FILE* f;

    sprintf(settFilename, "%s/bluemsx.ini", defDir);
    f = fopen(settFilename, "r");
    if (f != NULL) {
        fclose(f);
    }
    else {
        sprintf(settFilename, "%s/bluemsx.ini", altDir);
    }

    sprintf(histFilename, "%s/bluemsx_history.ini", defDir);
    f = fopen(histFilename, "r");
    if (f != NULL) {
        fclose(f);
    }
    else {
        sprintf(histFilename, "%s/bluemsx_history.ini", altDir);
    }
}

/* 1 if a saved bluemsx.ini exists (i.e. not a first launch).  Uses the path
   resolved by propertiesSetDirectory, so call that first. */
int propSettingsFileExists(void)
{
    FILE* f = fopen(settFilename, "r");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}


Properties* propCreate(int useDefault, int langType, PropKeyboardLanguage kbdLang, int syncMode, const char* themeName) 
{
    Properties* properties;

    properties = malloc(sizeof(Properties));

    if (globalProperties == NULL) {
        globalProperties = properties;
    }

    propInitDefaults(properties, langType, kbdLang, syncMode, themeName);

    if (!useDefault) {
        propLoad(properties);
    }
#ifndef WII
    // Verify machine name
    {
        int foundMachine = 0;
		ArrayListIterator *iterator;
        ArrayList *machineList;
		
		machineList = arrayListCreate();
        machineFillAvailable(machineList, 1);
        
        iterator = arrayListCreateIterator(machineList);
        while (arrayListCanIterate(iterator))
        {
            char *machineName = (char *)arrayListIterate(iterator);
            if (strcmp(machineName, properties->emulation.machineName) == 0)
            {
                foundMachine = 1;
                break;
            }
        }
        arrayListDestroyIterator(iterator);

        if (!foundMachine)
        {
            if (arrayListGetSize(machineList) > 0)
                strcpy(properties->emulation.machineName, (char *)arrayListGetObject(machineList, 0));
            
            iterator = arrayListCreateIterator(machineList);
            while (arrayListCanIterate(iterator))
            {
                char *machineName = (char *)arrayListIterate(iterator);
                if (strcmp(machineName, "MSX2") == 0)
                {
                    strcpy(properties->emulation.machineName, machineName);
                    foundMachine = 1;
                }
                
                if (!foundMachine && strncmp(machineName, "MSX2", 4))
                {
                    strcpy(properties->emulation.machineName, machineName);
                    foundMachine = 1;
                }
            }
            arrayListDestroyIterator(iterator);
        }
        
        arrayListDestroy(machineList);
    }
#endif
    return properties;
}


void propDestroy(Properties* properties) {
    propSave(properties);

    free(properties);
}

 
