/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Language/LanguageEnglish.h,v $
**
** $Revision: 1.108 $
**
** $Date: 2009-04-04 20:57:19 $
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
#ifndef LANGUAGE_ENGLISH_H
#define LANGUAGE_ENGLISH_H

#include "LanguageStrings.h"
 
void langInitEnglish(LanguageStrings* ls) 
{
    //----------------------
    // Language lines
    //----------------------

    ls->langCatalan             = "Catalan";
    ls->langChineseSimplified   = "Chinese Simplified";
    ls->langChineseTraditional  = "Chinese Traditional";
    ls->langDutch               = "Dutch";
    ls->langEnglish             = "English";
    ls->langFinnish             = "Finnish";
    ls->langFrench              = "French";
    ls->langGerman              = "German";
    ls->langItalian             = "Italian";
    ls->langJapanese            = "Japanese";
    ls->langKorean              = "Korean";
    ls->langPolish              = "Polish";
    ls->langPortuguese          = "Portuguese";
    ls->langRussian             = "Russian";            // v2.8
    ls->langSpanish             = "Spanish";
    ls->langSwedish             = "Swedish";


    //----------------------
    // Generic lines
    //----------------------

    ls->textDevice              = "Device:";
    ls->textFilename            = "Filename:";
    ls->textFile                = "File";
    ls->textNone                = "None";
    ls->textUnknown             = "Unknown";


    //----------------------
    // Warning and Error lines
    //----------------------

    ls->warningTitle            = "blueMSX+ - Warning";
    ls->warningDiscardChanges   = "Do you want to discard changes?";
    ls->warningOverwriteFile    = "Do you want to overwrite the file:";
    ls->warningStateOldFormat   = "This is an old-format state save. It may not resume correctly. Load anyway?";
    ls->errorTitle              = "blueMSX+ - Error";
    ls->errorEnterFullscreen    = "Failed to enter fullscreen mode.           \n";
    ls->errorDirectXFailed      = "Failed to create DirectX objects.           \nUsing GDI instead.\nCheck Video properties.";
    ls->errorNoRomInZip         = "Could not locate a .ROM file in the ZIP archive.";
    ls->errorNoDskInZip         = "Could not locate a .DSK file in the ZIP archive.";
    ls->errorNoCasInZip         = "Could not locate a .CAS file in the ZIP archive.";
    ls->errorDirAsDskOverflow   = "%d file(s) (%d KB total) did not fit in the 720 KB disk image and were skipped.";
    ls->errorNoHelp             = "Could not locate the blueMSX+ help file.";
    ls->errorStartEmu           = "Failed to Start MSX emulator.";
    ls->errorMissingFiles       = "The following files referenced by the saved state could not be loaded:";
    ls->errorPortableReadonly   = "Portable device is readonly";
    ls->infoTitle                  = "blueMSX+ Info";
    ls->infoGameReaderRedirect     = "blueMSX+ does not directly support the MSX Game Reader (the original ASCII XP-era driver no longer works on modern Windows).\n\nOpen the MSX Game Reader - Web Dumper (by Kunihiko Ohnaka) in your browser instead?";
    ls->infoColorDepth             = "blueMSX+ works best in 16 or 32 bit color depth.";
    ls->errorKeyboardThemeMissing  = "Could not find the Keyboard Editor theme.";
    ls->errorMixerThemeMissing     = "Could not find the Mixer theme.";
    ls->errorRecorderTitle      = "blueMSX+ - Recorder";
    ls->errorRecorderSaveReplay = "Failed to save replay file:\n  %s\n\nCheck that the destination directory exists and is writable.";
    ls->errorRecorderReplayMissing = "Replay file not found:\n  %s\n\nRecord a replay first, or use Load to pick an existing .cap file.";
    ls->errorRecorderRequiresDX12      = "Switch the video driver to Direct3D 12 and start recording?";
    ls->errorRecorderRequiresDX12Title = "blueMSX+ - Video driver change";
    ls->infoRecorderComplete    = "Video file saved:\n  %s";
    ls->infoToastSaved          = "Saved: %s";
    ls->infoToastAlreadyRecording  = "Already recording";
    ls->dlgRecorderPickTitle       = "blueMSX+ - Render Replay to Video";
    ls->dlgRecorderPickSourceCap   = "Replay file to render (.cap):";
    ls->dlgRecorderPickOutputMp4   = "Output video file (.mp4):";
    ls->menuFileRecordVideo        = "Record Video";
    ls->menuFileStopRecordVideo    = "Stop Video";
    ls->shortcutRecordVideoStart   = "Record video: Start";
    ls->shortcutRecordVideoStartAs = "Record video: Start As...";
    ls->shortcutRecordVideoStop    = "Record video: Stop";
    ls->shortcutRecordVideoToggle  = "Record video: Toggle";
    ls->shortcutAudioCaptureAs     = "Record audio: Start As...";
    ls->shortcutVideoRecordAs      = "Replay: Record As...";
    ls->shortcutScreenshotAs       = "Screenshot: Save As...";

    ls->propCapture                = "Capture";
    ls->propCaptureAudioGB         = " Audio Recording ";
    ls->propCaptureVideoGB         = " Video Recording ";
    ls->propCaptureScreenshotGB    = " Screenshot ";
    ls->propCaptureReplayGB        = " Replay Recording ";
    ls->propCaptureSaveDir         = "Save dir:";
    ls->propCaptureFormat          = "Format:";
    ls->propCaptureCodec           = "Codec:";
    ls->propCaptureAutoName        = "Auto-name file";
    ls->propCapturePromptName      = "Prompt for filename";
    ls->propCaptureShowToast       = "Show toast on capture completion";


    //----------------------
    // File related lines
    //----------------------

    ls->fileRom                 = "ROM image";
    ls->fileAll                 = "All Files";
    ls->fileCpuState            = "CPU state";
    ls->fileVideoCapture        = "Video Capture"; 
    ls->fileDisk                = "Disk Image";
    ls->fileCas                 = "Tape Image";
    ls->fileAvi                 = "Video Clip";    


    //----------------------
    // Menu related lines
    //----------------------

    ls->menuNoRecentFiles       = "- no recent files -";
    ls->menuInsert              = "Insert";
    ls->menuEject               = "Eject";

    ls->menuCartGameReader      = "Game Reader";
    ls->menuCartIde             = "IDE";
    ls->menuCartBeerIde         = "Beer";
    ls->menuCartGIde            = "GIDE";
    ls->menuCartSunriseIde      = "Sunrise";  
    ls->menuCartScsi            = "SCSI";                // New in 2.7
    ls->menuCartMegaSCSI        = "MEGA-SCSI";           // New in 2.7
    ls->menuCartWaveSCSI        = "WAVE-SCSI";           // New in 2.7
    ls->menuCartGoudaSCSI       = "Gouda SCSI";          // New in 2.7
    ls->menuJoyrexPsg           = "Joyrex PSG Cartridge"; // New in 2.9
    ls->menuCartSCC             = "SCC Cartridge";
    ls->menuCartSCCPlus         = "SCC-I Cartridge";
    ls->menuCartFMPac           = "FM-PAC Cartridge";
    ls->menuCartPac             = "PAC Cartridge";
    ls->menuCartHBI55           = "Sony HBI-55 Cartridge";
    ls->menuCartInsertSpecial   = "Insert Special";
    ls->menuCartMegaRam         = "MegaRAM";
    ls->menuCartExternalRam     = "External RAM";
    ls->menuCartEseRam          = "Ese-RAM";             // New in 2.7
    ls->menuCartEseSCC          = "Ese-SCC";             // New in 2.7
    ls->menuCartMegaFlashRom    = "Mega Flash ROM";      // New in 2.7

    ls->menuDiskInsertNew       = "Insert New Disk Image";
    ls->menuDiskInsertCdrom     = "Insert CD-ROM";       // New in 2.7
    ls->menuDiskDirInsert       = "Insert Directory";
    ls->menuDiskAutoStart       = "Reset After Insert";
    ls->menuCartAutoReset       = "Reset After Insert/Remove";

    ls->menuCasRewindAfterInsert= "Rewind After Insert";
    ls->menuCasUseReadOnly      = "Use Cassette Image Read Only";
    ls->lmenuCasSaveAs          = "Save Cassette Image As...";
    ls->menuCasSetPosition      = "Set Position";
    ls->menuCasRewind           = "Rewind";

    ls->menuVideoLoad           = "Replay from File...";
    ls->menuVideoPlay           = "Replay Last Recording";
    ls->menuVideoRecord         = "Record";
    ls->menuVideoRecording      = "Recording...";
    ls->menuVideoRecAppend      = "Append";
    ls->menuVideoStop           = "Stop";
    ls->menuVideoRender         = "Export Replay to Video...";

    ls->menuPrnFormfeed         = "Form Feed";

    ls->menuZoom1x              = "1x Window";
    ls->menuZoom2x              = "2x Window";
    ls->menuZoom3x              = "3x Window";
    ls->menuZoom4x              = "4x Window";
    ls->menuZoom5x              = "5x Window";
    ls->menuZoom6x              = "6x Window";
    ls->menuZoom7x              = "7x Window";
    ls->menuZoom8x              = "8x Window";
    ls->menuZoomFullscreen      = "Fullscreen";
    
    ls->menuPropsEmulation      = "Emulation";
    ls->menuPropsVideo          = "Video";
    ls->menuPropsSound          = "Sound";
    ls->menuPropsMidi           = "MIDI";
    ls->menuPropsControls       = "Controls";
    ls->menuPropsEffects        = "Effects";               // New in 2.9
    ls->menuPropsSettings       = "Settings";
    ls->menuPropsFile           = "Files";
    ls->menuPropsDisk           = "Disks";               // New in 2.7
    ls->menuPropsLanguage       = "Language";
    ls->menuPropsPorts          = "Ports";
    ls->menuPropsCapture        = "Capture";
    
    ls->menuVideoSource         = "Video Out Source";
    ls->menuVideoSourceDefault  = "No Video Out Source Connected";
    ls->menuVideoChipAutodetect = "Autodetect Video Chip";
    ls->menuVideoInSource       = "Video In Source";
    ls->menuVideoInBitmap       = "Bitmap File";
    
    ls->menuEthInterface        = "Ethernet Interface"; 

    ls->menuHelpHelp            = "Help Topics";
    ls->menuHelpAbout           = "About blueMSX+";

    ls->menuFileCart            = "Cartridge Slot";
    ls->menuFileDisk            = "Disk Drive";
    ls->menuFileCas             = "Cassette";
    ls->menuFilePrn             = "Printer";
    ls->menuFileLoadState       = "Load CPU State";
    ls->menuFileSaveState       = "Save CPU State";
    ls->menuFileQLoadState      = "Quick Load State";
    ls->menuFileQSaveState      = "Quick Save State";
    ls->menuFileCaptureAudio    = "Record Audio";
    ls->menuFileStopAudio       = "Stop Audio";
    ls->menuFileCaptureVideo    = "Replay";
    ls->menuFileScreenShot      = "Take Screenshot";
    ls->menuFileExit            = "Exit";

    ls->menuFileHarddisk        = "Hard Disk / SD Card";
    ls->menuFileHarddiskNoPesent= "No Controllers Present";
    ls->menuFileHarddiskRemoveAll= "Eject All Harddisks";    // New in 2.7

    ls->menuRunRun              = "Run";
    ls->menuRunPause            = "Pause";
    ls->menuRunStop             = "Stop";
    ls->menuRunSoftReset        = "Soft Reset";
    ls->menuRunHardReset        = "Hard Reset";
    ls->menuRunCleanReset       = "Complete Reset";

    ls->menuToolsMachine        = "Machine Editor";
    ls->menuToolsShortcuts      = "Shortcuts Editor";
    ls->menuToolsCtrlEditor     = "Input Editor"; 
    ls->menuToolsMixer          = "Sound Mixer";
    ls->menuToolsLoadMemory     = "Load Memory";
    ls->menuToolsDebugger       = "Debugger";               
    ls->menuToolsTrainer        = "Trainer";                
    ls->menuToolsTraceLogger    = "Trace Logger";           

    ls->menuFile                = "File";
    ls->menuRun                 = "Emulation";
    ls->menuWindow              = "Window";
    ls->menuOptions             = "Options";
    ls->menuTools               = "Tools";
    ls->menuHelp                = "Help";
    

    //----------------------
    // Dialog related lines
    //----------------------

    ls->dlgOK                   = "OK";
    ls->dlgOpen                 = "Open";
    ls->dlgCancel               = "Cancel";
    ls->dlgYes                  = "Yes";
    ls->dlgNo                   = "No";
    ls->dlgSave                 = "Save";
    ls->dlgSaveAs               = "Save As...";
    ls->dlgRun                  = "Run";
    ls->dlgClose                = "Close";

    ls->dlgLoadRom              = "blueMSX+ - Select a ROM image to load";
    ls->dlgLoadDsk              = "blueMSX+ - Select a DSK image to load";
    ls->dlgLoadCas              = "blueMSX+ - Select a CAS image to load";
    ls->dlgLoadRomDskCas        = "blueMSX+ - Select a ROM, DSK, or CAS file to load";
    ls->dlgLoadRomDesc          = "Choose a ROM image to load:";
    ls->dlgLoadDskDesc          = "Choose a disk image to load:";
    ls->dlgLoadCasDesc          = "Choose a tape image to load:";
    ls->dlgLoadRomDskCasDesc    = "Choose a ROM, disk, or tape image to load:";
    ls->dlgLoadState            = "Load state";
    ls->dlgLoadVideoCapture     = "Load video capture";      
    ls->dlgSaveState            = "Save state as...";
    ls->dlgSaveCassette         = "blueMSX+ - Save Tape Image";
    ls->dlgSaveVideoClipAs      = "Save video clip as...";      
    ls->dlgSaveCaptureAudio     = "Save Audio Recording As";
    ls->dlgSaveCaptureVideo     = "Save Video Recording As";
    ls->dlgSaveCaptureReplay    = "Save Replay As";
    ls->dlgSaveCaptureScreenshot = "Save Screenshot As";      
    ls->dlgAmountCompleted      = "Amount completed:";          
    ls->dlgInsertRom1           = "Insert ROM cartridge into slot 1";
    ls->dlgInsertRom2           = "Insert ROM cartridge into slot 2";
    ls->dlgInsertDiskA          = "Insert disk image into drive A";
    ls->dlgInsertDiskB          = "Insert disk image into drive B";
    ls->dlgInsertHarddisk       = "Insert Hard Disk";
    ls->dlgInsertCas            = "Insert cassette tape";
    ls->dlgRomType              = "ROM Type:";
    ls->dlgDiskSize             = "Disk Size:";             

    ls->dlgTapeTitle            = "blueMSX+ - Tape Position";
    ls->dlgTapeFrameText        = "Tape Position";
    ls->dlgTapeCurrentPos       = "Current Position";
    ls->dlgTapeTotalTime        = "Total Time";
    ls->dlgTapeSetPosText       = "Tape Position:";
    ls->dlgTapeCustom           = "Show Custom Files";
    ls->dlgTabPosition          = "Position";
    ls->dlgTabType              = "Type";
    ls->dlgTabFilename          = "Filename";
    ls->dlgZipReset             = "Reset after insert";

    ls->dlgAboutTitle           = "blueMSX+ - About";

    ls->dlgLangLangText         = "Choose the language that blueMSX+ will use";
    ls->dlgLangLangTitle        = "blueMSX+ - Language";

    ls->dlgAboutAbout           = "ABOUT\r\n====";
    ls->dlgAboutVersion         = "Version:";
    ls->dlgAboutBuildNumber     = "Build:";
    ls->dlgAboutBuildDate       = "Date:";
    ls->dlgAboutForkNote        = "This software is an enhanced fork of blueMSX 2.8.3 for Windows 11.\r\n\r\n"
                                  "Author:\tHesoten\r\n\r\n"
                                  "Visit https://github.com/Hesoten/bluemsx-plus for more details.\r\n\r\n"
                                  "For the original blueMSX, visit https://msxblue.com/bluemsx";
    ls->dlgAboutOrigDevel       = "ABOUT THE ORIGINAL blueMSX\r\n"
                                  "=====================\r\n\r\n"
                                  "The original blueMSX 2.8.3 was developed by the following developers:";
    ls->dlgAboutCreat           = "Created by Daniel Vik";
    ls->dlgAboutDevel           = "DEVELOPERS\r\n========";
    ls->dlgAboutThanks          = "CONTRIBUTORS\r\n==========";       // New in 2.7 (retranslate, see english)
    ls->dlgAboutLisence         = "LICENSE\r\n"
                                  "======\r\n\r\n"
                                  "This software is provided 'as-is', without any express or implied "
                                  "warranty. In no event will the author(s) be held liable for any damages "
                                  "arising from the use of this software.";

    ls->dlgSavePreview          = "Show Preview";
    ls->dlgSaveDate             = "Time Saved:";

    ls->dlgRenderVideoCapture   = "blueMSX+ - Exporting Replay to Video...";


    //----------------------
    // Properties related lines
    //----------------------

    ls->propTitle               = "blueMSX+ - Properties";
    ls->propEmulation           = "Emulation";
    ls->propD3D                 = "Direct3D";
    ls->propVideo               = "Video";
    ls->propSound               = "Sound";
    ls->propControls            = "Controls";
    ls->propPerformance         = "Performance";
    ls->propEffects             = "Effects";             // New in 2.9
    ls->propSettings            = "Settings";
    ls->propFile                = "Files";
    ls->propDisk                = "Disks";              // New in 2.7
    ls->propPorts               = "Ports";
    ls->propMidi                = "MIDI";
    
    ls->propEmuGeneralGB        = "General ";
    ls->propEmuFamilyText       = "MSX machine:";
    ls->propEmuMemoryGB         = "Memory ";
    ls->propEmuRamSizeText      = "RAM size:";
    ls->propEmuVramSizeText     = "VRAM size:";
    ls->propEmuSpeedGB          = "Emulation Speed ";
    ls->propEmuSpeedText        = "Emulator core:";
    ls->propEmuVdpCmdSpeedText  = "VDP command wait time:";
    ls->propEmuFrontSwitchGB    = "Panasonic Switches ";
    ls->propEmuFrontSwitch      = " Front Switch";
    ls->propEmuNoSpriteLimits   = " Disable Sprites Limitation";  // New in 2.9
    ls->propEnableMsxKeyboardQuirk = " Emulate MSX keyboard quirk";  // New in 2.9
    ls->propEmuFdcTiming        = " Accelerate during FDD access";
    ls->propEmuHddSdBoost       = " Accelerate during HDD/SD card access";
    ls->propEmuReversePlay      = " Enable reverse playback"; // New in 2.8.3
    ls->propEmuPauseSwitch      = " Pause Switch";
    ls->propEmuAudioSwitch      = " MSX-AUDIO cartridge switch";
    ls->propVideoFreqText       = "Video Frequency:";
    ls->propVideoFreqAuto       = "Auto";
    ls->propSndOversampleText   = "Oversample:";
    ls->propSndOpllAnalogText   = "Analog filter:";
    ls->propSndOpllAnalogLpfText = "LPF cutoff:";
    ls->enumOpllFilterOff       = "Off";
    ls->enumOpllFilterBright    = "Bright (LPF 12 kHz)";
    ls->enumOpllFilterClear     = "Clear (LPF 8 kHz)";
    ls->enumOpllFilterStandard  = "Standard (LPF 5 kHz)";
    ls->enumOpllFilterSoft      = "Soft (LPF 3.5 kHz)";
    ls->enumOpllFilterMellow    = "Mellow (LPF 2.3 kHz)";
    ls->enumOpllFilterCustom    = "Custom";
    ls->propSndYkInGB           = "YK-01/YK-10/YK-20 In ";
    ls->propSndMidiInGB         = "MIDI In ";
    ls->propSndMidiOutGB        = "MIDI Out ";
    ls->propSndMidiChannel      = "MIDI Channel:";
    ls->propSndMidiAll          = "All";

    ls->propMonMonGB            = "Monitor ";
    ls->propMonTypeText         = "Monitor type:";
    ls->propMonEmuText          = "Monitor emulation:";
    ls->propVideoTypeText       = "Video type:";
    ls->propMonHorizStretch     = " Horizontal Stretch";
    ls->propMonVertStretch      = " Vertical Stretch";
    ls->propMonDeInterlace      = " De-interlace";
    ls->propBlendFrames         = " Blend consecutive frames";
    ls->propMonBrightness       = "Brightness:";
    ls->propMonContrast         = "Contrast:";
    ls->propMonSaturation       = "Saturation:";
    ls->propMonGamma            = "Gamma:";
    ls->propMonScanlines        = " Scanlines:";
    ls->propMonScanlinesBright  = "Bright comp:";
    ls->propMonScanlinesBrightAuto = " Auto";
    ls->propMonScanlinesShape   = "Preset:";
    ls->propMonScanlinesDepth   = "Depth:";
    ls->propMonScanlinesSharpness = "Sharpness:";
    ls->enumScanShapeGentle     = "Gentle";
    ls->enumScanShapeStandard   = "Standard";
    ls->enumScanShapeSharp      = "Sharp";
    ls->enumScanShapeTrinitron  = "Trinitron";
    ls->enumScanShapeCustom     = "Custom";
    ls->propMonHdrEnable        = " HDR";
    ls->propMonHdrPaperWhite    = "White brightness:";
    ls->propMonHdrSystemMode    = "Current HDR mode:";
    ls->propMonHdrRestartHint   = "Restart blueMSX+ to apply the HDR mode change.";
    ls->propMonHdrRecord        = " Record in HDR";
    ls->propMonColorGhosting    = " RF-Modulator:";
    ls->propMonEffectsGB        = "Effects ";

    ls->propPerfVideoDrvGB      = "Video Driver ";
    ls->propPerfVideoDispDrvText= "Display driver:";
    ls->propPerfFrameSkipText   = "Frame skipping:";
    ls->propPerfAudioDrvGB      = "Audio Driver ";
    ls->propPerfAudioDrvText    = "Sound driver:";
    ls->propPerfAudioBufSzText  = "Sound buffer size:";
    ls->propPerfAudioBufSzActualFmt = "(actual buffer: %u ms)";
    ls->propPerfEmuGB           = "Emulation ";
    ls->propPerfSyncModeText    = "SYNC Mode:";
    ls->propFullscreenResText   = "Fullscreen Resolution:";

    ls->propSndChipEmuGB        = "Sound Chip Emulation ";
    ls->propSndMsxMusic         = " MSX-MUSIC";
    ls->propSndMsxAudio         = " MSX-AUDIO";
    ls->propSndMoonsound        = " Moonsound";
    ls->propSndMt32ToGm         = " Map MT-32 instruments to General MIDI";
    ls->propSoundChipsActive    = "Active backend:";
    ls->propSoundChipsHint      = "When several backends are enabled, you can A/B compare them at runtime.";
    ls->propSoundChipsYm2413GB  = " MSX-MUSIC backend ";
    ls->propSoundChipsY8950GB   = " MSX-AUDIO backend ";

    ls->propPortsLptGB          = "Parallel port ";
    ls->propPortsComGB          = "Serial ports ";
    ls->propPortsLptText        = "Port:";
    ls->propPortsCom1Text       = "Port 1:";
    ls->propPortsNone           = "None";
    ls->propPortsSimplCovox     = "SiMPL / Covox DAC";
    ls->propPortsFile           = "Print to File";
    ls->propPortsComFile        = "Send to File";
    ls->propPortsOpenLogFile    = "Open Log File";
    ls->propPortsEmulateMsxPrn  = "Emulation:";

    ls->propSetFileHistoryGB    = "File History ";
    ls->propSetFileHistorySize  = "Number of items in File History:";
    ls->propSetFileHistoryClear = "Clear History";
    ls->propFileTypes           = " Register .ROM/.DSK/.CAS/.STA in the \"Open with\" menu";
    ls->propOpenDefaultApps     = "Open Windows default-apps settings";
    ls->propWindowsEnvGB        = "Windows Environment "; 
    ls->propSetScreenSaver      = " Keep display on while blueMSX+ is running (no screen-off / sleep / saver)";
    ls->propPriorityBoost       = " Use Windows game scheduler (MMCSS) for emulation";
    ls->propScreenshotPng       = " Save screenshots in PNG format instead of BMP";
    ls->propEjectMediaOnExit    = " Eject all media when blueMSX+ exits";        // New in 2.8
    ls->propClearHistory        = "Are you sure you want to clear the file history?";
    ls->propOpenRomGB           = "Open ROM Dialog ";
    ls->propDefaultRomType      = "Default ROM Type:";
    ls->propGuessRomType        = "Guess ROM Type";

    ls->propSettDefSlotGB       = "Drag and Drop ";
    ls->propSettDefSlots        = "Insert ROM Into:";
    ls->propSettDefSlot         = " Slot";
    ls->propSettDefDrives       = "Insert Diskette Into:";
    ls->propSettDefDrive        = " Drive";

    ls->propThemeGB             = "Theme ";
    ls->propTheme               = "Theme:";

    ls->propCdromGB             = "CD-ROM ";         // New in 2.7
    ls->propCdromMethod         = "Access Method:";  // New in 2.7
    ls->propCdromMethodNone     = "None";            // New in 2.7
    ls->propCdromMethodIoctl    = "IOCTL";           // New in 2.7
    ls->propCdromMethodAspi     = "ASPI";            // New in 2.7
    ls->propCdromDrive          = "Drive:";          // New in 2.7

    ls->propD3DParametersGB         = "Parameters ";                // New in 2.9
    ls->propD3DAspectRatioText      = "Aspect ratio";               // New in 2.9
    ls->propD3DLinearFilteringText  = " Linear filtering";          // New in 2.9
    ls->propD3DForceHighResText     = " Force high resolution";     // New in 2.9
    ls->propD3DExtendBorderColorText    = " Extend border color";   // New in 2.9

    ls->propD3DCroppingGB               = "Cropping ";              // New in 2.9
    ls->propD3DCroppingTypeText         = "Cropping type:";         // New in 2.9
    ls->propD3DCroppingLeftText         = "Left:";                  // New in 2.9
    ls->propD3DCroppingRightText        = "Right:";                 // New in 2.9
    ls->propD3DCroppingTopText          = "Top:";                   // New in 2.9
    ls->propD3DCroppingBottomText       = "Bottom:";                // New in 2.9

    //----------------------
    // Dropdown related lines
    //----------------------

    ls->enumVideoMonColor       = "Color";
    ls->enumVideoMonGrey        = "Black and white";
    ls->enumVideoMonGreen       = "Green";
    ls->enumVideoMonAmber       = "Amber";

    ls->enumVideoTypePAL        = "PAL";
    ls->enumVideoTypeNTSC       = "NTSC";

    ls->enumVideoEmuNone        = "None";
    ls->enumVideoEmuYc          = "Y/C cable (sharp)";
    ls->enumVideoEmuMonitor     = "Monitor";
    ls->enumVideoEmuYcBlur      = "Noisy Y/C cable (sharp)";
    ls->enumVideoEmuComp        = "Composite (blurry)";
    ls->enumVideoEmuCompBlur    = "Noisy Composite (blurry)";
    ls->enumVideoEmuScale2x     = "Scale 2x";
    ls->enumVideoEmuHq2x        = "Hq2x";


    ls->enumVideoDrvDirectDrawHW= "DirectDraw HW accel."; 
    ls->enumVideoDrvDirectDraw  = "DirectDraw";
    ls->enumVideoDrvGDI         = "GDI";
    ls->enumVideoDrvD3D         = "Direct3D";

    ls->enumVideoFrameskip0     = "None";
    ls->enumVideoFrameskip1     = "1 frame";
    ls->enumVideoFrameskip2     = "2 frames";
    ls->enumVideoFrameskip3     = "3 frames";
    ls->enumVideoFrameskip4     = "4 frames";
    ls->enumVideoFrameskip5     = "5 frames";

    ls->enumD3DARAuto           = "Auto";           // New in 2.9
    ls->enumD3DARStretch        = "Stretch";        // New in 2.9
    ls->enumD3DARPAL            = "PAL";            // New in 2.9
    ls->enumD3DARNTSC           = "NTSC";           // New in 2.9
    ls->enumD3DAR11             = "1:1";            // New in 2.9

    ls->enumD3DCropNone         = "None";           // New in 2.9
    ls->enumD3DCropMSX1         = "MSX1";           // New in 2.9
    ls->enumD3DCropMSX1Plus8    = "MSX1+8";         // New in 2.9
    ls->enumD3DCropMSX2         = "MSX2";           // New in 2.9
    ls->enumD3DCropMSX2Plus8    = "MSX2+8";         // New in 2.9
    ls->enumD3DCropCustom       = "Custom";         // New in 2.9

    ls->enumSoundDrvNone        = "No Sound";
    ls->enumSoundDrvWMM         = "WMM driver";
    ls->enumSoundDrvDirectX     = "DirectX driver";
    ls->enumSoundDrvWasapi      = "WASAPI driver";

    ls->enumEmuSync1ms          = "Sync on MSX refresh";
    ls->enumEmuSyncAuto         = "Auto (fast)";
    ls->enumEmuSyncNone         = "None";
    ls->enumEmuSyncVblank       = "Sync to PC Vertical Blank";
    ls->enumEmuAsyncVblank      = "Asynchronous PC VBlank";             

    ls->enumControlsJoyNone     = "None";
    ls->enumControlsJoyMouse    = "Mouse";
    ls->enumControlsJoyTetris2Dongle = "Tetris 2 Dongle";
    ls->enumControlsJoyTMagicKeyDongle = "MagicKey Dongle";
    ls->enumControlsJoy2Button = "2-button Joystick";                   
    ls->enumControlsJoyGunstick  = "Gun Stick";                         
    ls->enumControlsJoyAsciiLaser="ASCII Plus-X Terminator Laser";      
    ls->enumControlsArkanoidPad  ="Arkanoid Pad";                   // New in 2.7.1
    ls->enumControlsJoyColeco = "ColecoVision Joystick";                

    ls->enumDiskMsx35Dbl9Sect    = "MSX 3.5\" Double Sided, 9 Sectors";     
    ls->enumDiskMsx35Dbl8Sect    = "MSX 3.5\" Double Sided, 8 Sectors";     
    ls->enumDiskMsx35Sgl9Sect    = "MSX 3.5\" Single Sided, 9 Sectors";     
    ls->enumDiskMsx35Sgl8Sect    = "MSX 3.5\" Single Sided, 8 Sectors";     
    ls->enumDiskSvi525Dbl        = "SVI-328 5.25\" Double Sided";           
    ls->enumDiskSvi525Sgl        = "SVI-328 5.25\" Single Sided"; 
    ls->enumDiskSf3Sgl           = "Sega SF-7000 3\" Single Sided";            
    ls->enumDiskSize             = "Disk size";
    ls->enumDiskFormat           = "Format:";
    ls->enumDiskFormatUnformatted= "Unformatted";


    //----------------------
    // Configuration related lines
    //----------------------

    ls->confTitle               = "blueMSX+ - Machine Configuration Editor";
    ls->confConfigText          = "Profile Name";
    ls->confSlotLayout          = "Slot Layout";
    ls->confMemory              = "Memory";
    ls->confChipEmulation       = "Chip Emulation";
    ls->confChipExtras          = "Extras";

    ls->confOpenRom             = "Open ROM image";
    ls->confSaveTitle           = "blueMSX+ - Save Configuration";
    ls->confSaveText            = "Do you want to overwrite the machine configuration:";
    ls->confSaveAsTitle         = "Save Configuration As...";
    ls->confSaveAsMachineName   = "Machine Name:";
    ls->confDiscardTitle        = "blueMSX+ - Configuration";
    ls->confExitSaveTitle       = "blueMSX+ - Exit Configuration Editor";
    ls->confExitSaveText        = "Do you want to discard changes to the current configuration?";

    ls->confSlotLayoutGB        = "Slot Layout ";
    ls->confSlotExtSlotGB       = "External Slots ";
    ls->confBoardGB             = "Board ";
    ls->confBoardText           = "Board Type:";
    ls->confSlotPrimary         = "Primary";
    ls->confSlotExpanded        = "Expanded (four subslots)";

    ls->confSlotCart            = "Cartridge";
    ls->confSlot                = "Slot";
    ls->confSubslot             = "Subslot";

    ls->confMemAdd              = "Add...";
    ls->confMemEdit             = "Edit...";
    ls->confMemRemove           = "Remove";
    ls->confMemSlot             = "Slot";
    ls->confMemAddresss         = "Address";
    ls->confMemType             = "Type";
    ls->confMemRomImage         = "ROM Image";

    ls->confChipVideoGB          = "Video ";
    ls->confChipVideoChip        = "Video Chip:";
    ls->confChipVideoRam         = "Video RAM:";
    ls->confChipSoundGB          = "Sound ";
    ls->confChipPsgStereoText    = " PSG Stereo";

    ls->confCmosGB               = "CMOS ";
    ls->confCmosEnable           = " Enable CMOS";
    ls->confCmosBattery          = " Use Charged Battery";

    ls->confCpuFreqGB            = "CPU Frequency ";
    ls->confZ80FreqText          = "Z80 Frequency:";
    ls->confR800FreqText         = "R800 Frequency:";
    ls->confFdcGB                = "Floppy Disk Controller ";
    ls->confCFdcNumDrivesText    = "Number of Drives:";

    ls->confEditMemTitle         = "blueMSX+ - Edit Mapper";
    ls->confEditMemGB            = "Mapper Details ";
    ls->confEditMemType          = "Type:";
    ls->confEditMemFile          = "File:";
    ls->confEditMemAddress       = "Address";
    ls->confEditMemSize          = "Size";
    ls->confEditMemSlot          = "Slot";


    //----------------------
    // Shortcut lines
    //----------------------

    ls->shortcutKey             = "Action";
    ls->shortcutDescription     = "Shortcut";

    ls->shortcutSaveConfig      = "blueMSX+ - Save Configuration";
    ls->shortcutOverwriteConfig = "Do you want to overwrite the shortcut configuration:";
    ls->shortcutCreateConfig    = "Do you want to save the new shortcut configuration:";
    ls->shortcutExitConfig      = "blueMSX+ - Exit Shortcut Editor";
    ls->shortcutDiscardConfig   = "Do you want to discard changes to the current configuration?";
    ls->shortcutSaveConfigAs    = "blueMSX+ - Save Shortcut Configuration As...";
    ls->shortcutConfigName      = "Config Name:";
    ls->shortcutNewProfile      = "< New Profile >";
    ls->shortcutConfigTitle     = "blueMSX+ - Shortcut Mapping Editor";
    ls->shortcutAssign          = "Assign";
    ls->shortcutPressText       = "Press shortcut key(s):";
    ls->shortcutScheme          = "Mapping Scheme:";
    ls->shortcutCartInsert1     = "Insert Cartridge 1";
    ls->shortcutCartRemove1     = "Remove Cartridge 1";
    ls->shortcutCartInsert2     = "Insert Cartridge 2";
    ls->shortcutCartRemove2     = "Remove Cartridge 2";
    ls->shortcutSpecialMenu1    = "Show Special ROM Menu for Cartridge 1";
    ls->shortcutSpecialMenu2    = "Show Special ROM Menu for Cartridge 2";
    ls->shortcutCartAutoReset   = "Reset Emulator when Cartridge is Inserted";
    ls->shortcutDiskInsertA     = "Insert Diskette A";
    ls->shortcutDiskDirInsertA  = "Insert Directory as Diskette A";
    ls->shortcutDiskRemoveA     = "Eject Diskette A";
    ls->shortcutDiskChangeA     = "Quick change Diskette A";
    ls->shortcutDiskAutoResetA  = "Reset Emulator when Diskette A is Inserted";
    ls->shortcutDiskInsertB     = "Insert Diskette B";
    ls->shortcutDiskDirInsertB  = "Insert Directory as Diskette B";
    ls->shortcutDiskRemoveB     = "Eject Diskette B";
    ls->shortcutCasInsert       = "Insert Cassette";
    ls->shortcutCasEject        = "Eject Cassette";
    ls->shortcutCasAutorewind   = "Toggle Auto-rewind on Cassette";
    ls->shortcutCasReadOnly     = "Toggle Read-only Cassette";
    ls->shortcutCasSetPosition  = "Set Tape position";
    ls->shortcutCasRewind       = "Rewind Cassette";
    ls->shortcutCasSave         = "Save Cassette Image";
    ls->shortcutPrnFormFeed     = "Printer Form Feed";
    ls->shortcutCpuStateLoad    = "Load CPU state";
    ls->shortcutCpuStateSave    = "Save CPU state";
    ls->shortcutCpuStateQload   = "Quick load CPU state";
    ls->shortcutCpuStateQsave   = "Quick save CPU state";
    ls->shortcutAudioCapture    = "Start/stop audio capture";
    ls->shortcutScreenshotOrig  = "Screenshot capture";
    ls->shortcutScreenshotSmall = "Small unfiltered screenshot capture";
    ls->shortcutScreenshotLarge = "Large unfiltered screenshot capture";
    ls->shortcutQuit            = "Quit blueMSX+";
    ls->shortcutRunPause        = "Run/Pause emulation";
    ls->shortcutStop            = "Stop emulation";
    ls->shortcutResetHard       = "Hard Reset";
    ls->shortcutResetSoft       = "Soft Reset";
    ls->shortcutResetClean      = "Complete Reset";
    ls->shortcutSize1x          = "Set 1x window size";
    ls->shortcutSize2x          = "Set 2x window size";
    ls->shortcutSize3x          = "Set 3x window size";
    ls->shortcutSize4x          = "Set 4x window size";
    ls->shortcutSize5x          = "Set 5x window size";
    ls->shortcutSize6x          = "Set 6x window size";
    ls->shortcutSize7x          = "Set 7x window size";
    ls->shortcutSize8x          = "Set 8x window size";
    ls->shortcutSizeFullscreen  = "Set fullscreen";
    ls->shortcutSizeMinimized   = "Minimize window";
    ls->shortcutToggleFullscren = "Toggle fullscreen";
    ls->shortcutVolumeIncrease  = "Increase Volume";
    ls->shortcutVolumeDecrease  = "Decrease Volume";
    ls->shortcutVolumeMute      = "Mute Volume";
    ls->shortcutVolumeStereo    = "Toggle mono/stereo";
    ls->shortcutYm2413BackendCycle = "Cycle MSX-MUSIC audio backend";
    ls->shortcutY8950BackendCycle  = "Cycle MSX-AUDIO audio backend";
    ls->shortcutSwitchMsxAudio  = "Toggle MSX-AUDIO switch";
    ls->shortcutSwitchFront     = "Toggle Panasonic front switch";
    ls->shortcutSwitchPause     = "Toggle pause switch";
    ls->shortcutToggleMouseLock = "Toggle mouse lock";
    ls->shortcutEmuSpeedMax     = "Max emulation speed";
    ls->shortcutEmuPlayReverse  = "Rewind emulation";                     // New in 2.8.3
    ls->shortcutEmuSpeedToggle  = "Toggle max emulation speed";
    ls->shortcutEmuSpeedNormal  = "Normal emulation speed";
    ls->shortcutEmuSpeedInc     = "Increase emulation speed";
    ls->shortcutEmuSpeedDec     = "Decrease emulation speed";
    ls->shortcutThemeSwitch     = "Switch theme";
    ls->shortcutShowEmuProp     = "Show Emulation Properties";
    ls->shortcutShowVideoProp   = "Show Video Properties";
    ls->shortcutShowAudioProp   = "Show Audio Properties";
    ls->shortcutShowCtrlProp    = "Show Controls Properties";
    ls->shortcutShowEffectsProp = "Show Effects Properties";
    ls->shortcutShowSettProp    = "Show Settings Properties";
    ls->shortcutShowPorts       = "Show Ports Properties";
    ls->shortcutShowLanguage    = "Show Language Dialog";
    ls->shortcutShowMachines    = "Show Machine Editor";
    ls->shortcutShowShortcuts   = "Show Shortcuts Editor";
    ls->shortcutShowKeyboard    = "Show Input Editor";
    ls->shortcutShowMixer       = "Show Sound Mixer";
    ls->shortcutShowDebugger    = "Show Debugger";
    ls->shortcutShowTrainer     = "Show Trainer";
    ls->shortcutShowHelp        = "Show Help Dialog";
    ls->shortcutShowAbout       = "Show About Dialog";    
    ls->shortcutShowFiles       = "Show Files Properties";
    ls->shortcutToggleSpriteEnable = "Show/Hide Sprites";
    ls->shortcutToggleFdcTiming = "Toggle FDD access acceleration";
    ls->shortcutToggleHddSdBoost = "Toggle HDD/SD access acceleration";
    ls->shortcutToggleNoSpriteLimits = "Toggle Sprites Limitation";                 // New in 2.9
    ls->shortcutEnableMsxKeyboardQuirk = "Emulate MSX Keyboard Quirk";              // New in 2.9
    ls->shortcutToggleCpuTrace  = "Enable/Disable CPU Trace";
    ls->shortcutVideoLoad       = "Replay: Load from File";             
    ls->shortcutVideoPlay       = "Replay: Play Last Recording";   
    ls->shortcutVideoRecord     = "Replay: Record";              
    ls->shortcutVideoStop       = "Replay: Stop";                
    ls->shortcutVideoRender     = "Replay: Export to Video";   


    //----------------------
    // Keyboard config lines
    //----------------------    
 
    ls->keyconfigSelectedKey    = "Selected Key:";
    ls->keyconfigMappedTo       = "Mapped To:";
    ls->keyconfigMappingScheme  = "Mapping Scheme:";

    
    //----------------------
    // ROM type lines
    //----------------------

    ls->romTypeStandard         = "Standard";
    ls->romTypeZenima80         = "Zemina 80 in 1";
    ls->romTypeZenima90         = "Zemina 90 in 1";
    ls->romTypeZenima126        = "Zemina 126 in 1";
    ls->romTypeSccMirrored      = "SCC mirrored";
    ls->romTypeSccExtended      = "SCC extended";
    ls->romTypeKonamiGeneric    = "Konami Generic";
    ls->romTypeMirrored         = "Mirrored ROM";
    ls->romTypeNormal           = "Normal ROM";
    ls->romTypeDiskPatch        = "Normal + Disk Patch";
    ls->romTypeCasPatch         = "Normal + Cassette Patch";
    ls->romTypeTc8566afFdc      = "TC8566AF Disk Controller";
    ls->romTypeTc8566afTrFdc    = "TC8566AF Turbo-R Disk Controller";
    ls->romTypeMicrosolFdc      = "Microsol Disk Controller";
    ls->romTypeNationalFdc      = "National Disk Controller";
    ls->romTypePhilipsFdc       = "Philips Disk Controller";
    ls->romTypeSvi707Fdc        = "SVI-707 Disk Controller";
    ls->romTypeSvi738Fdc        = "SVI-738 Disk Controller";
    ls->romTypeMappedRam        = "Mapped RAM";
    ls->romTypeMirroredRam1k    = "1kB Mirrored RAM";
    ls->romTypeMirroredRam2k    = "2kB Mirrored RAM";
    ls->romTypeNormalRam        = "Normal RAM";
    ls->romTypeTurborPause      = "Turbo-R Pause";
    ls->romTypeF4deviceNormal   = "F4 Device Normal";
    ls->romTypeF4deviceInvert   = "F4 Device Inverted";
    ls->romTypeTurborTimer      = "Turbo-R Timer";
    ls->romTypeNormal4000       = "Normal 4000h";
    ls->romTypeNormalC000       = "Normal C000h";
    ls->romTypeExtRam           = "External RAM";
    ls->romTypeExtRam16         = "16kB External RAM";
    ls->romTypeExtRam32         = "32kB External RAM";
    ls->romTypeExtRam48         = "48kB External RAM";
    ls->romTypeExtRam64         = "64kB External RAM";
    ls->romTypeExtRam512        = "512kB External RAM";
    ls->romTypeExtRam1mb        = "1MB External RAM";
    ls->romTypeExtRam2mb        = "2MB External RAM";
    ls->romTypeExtRam4mb        = "4MB External RAM";
    ls->romTypeSvi328Cart       = "SVI-328 Cartridge";
    ls->romTypeSvi328Fdc        = "SVI-328 Disk Controller";
    ls->romTypeSvi328Prn        = "SVI-328 Printer";
    ls->romTypeSvi328Uart       = "SVI-328 Serial Port";
    ls->romTypeSvi328col80      = "SVI-328 80 Column Card";
    ls->romTypeSvi328RsIde      = "SVI-328 RS IDE";
    ls->romTypeSvi727col80      = "SVI-727 80 Column Card";
    ls->romTypeColecoCart       = "Coleco Cartridge";
    ls->romTypeSg1000Cart       = "SG-1000 Cartridge";
    ls->romTypeSc3000Cart       = "SC-3000 Cartridge";
    ls->romTypeMsxPrinter       = "MSX Printer";
    ls->romTypeTurborPcm        = "Turbo-R PCM Chip";
    ls->romTypeNms8280Digitiz   = "Philips NMS-8280 Digitizer";
    ls->romTypeHbiV1Digitiz     = "Sony HBI-V1 Digitizer";
    
    
    //----------------------
    // Debug type lines
    // Note: Only needs translation if debugger is translated
    //----------------------

    ls->dbgMemVisible           = "Visible Memory";
    ls->dbgMemRamNormal         = "Normal";
    ls->dbgMemRamMapped         = "Mapped";
    ls->dbgMemYmf278            = "YMF278 Sample RAM";
    ls->dbgMemAy8950            = "AY8950 Sample RAM";
    ls->dbgMemScc               = "Memory";

    ls->dbgCallstack            = "Callstack";

    ls->dbgRegs                 = "Registers";
    ls->dbgRegsCpu              = "CPU Registers";
    ls->dbgRegsYmf262           = "YMF262 Registers";
    ls->dbgRegsYmf278           = "YMF278 Registers";
    ls->dbgRegsAy8950           = "AY8950 Registers";
    ls->dbgRegsYm2413           = "YM2413 Registers";

    ls->dbgDevRamMapper         = "RAM Mapper";
    ls->dbgDevRam               = "RAM";
    ls->dbgDevF4Device          = "F4 Device";
    ls->dbgDevKorean80          = "Korean 80";
    ls->dbgDevKorean90          = "Korean 90";
    ls->dbgDevKorean128         = "Korean 128";
    ls->dbgDevFdcMicrosol       = "Microsol FDC";
    ls->dbgDevPrinter           = "Printer";
    ls->dbgDevSviFdc            = "SVI FDC";
    ls->dbgDevSviPrn            = "SVI Printer";
    ls->dbgDevSvi80Col          = "SVI 80 Column";
    ls->dbgDevRtc               = "RTC";
    ls->dbgDevTrPause           = "TR Pause";


    //----------------------
    // Debug type lines
    // Note: Can only be translated to european languages
    //----------------------

    ls->aboutScrollThanksTo     = "Special thanks to: ";
    ls->aboutScrollAndYou       = "and YOU !!!!";
};

#endif
