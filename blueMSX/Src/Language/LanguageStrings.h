/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Language/LanguageStrings.h,v $
**
** $Revision: 1.91 $
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
#ifndef LANGUAGE_STRINGS_H
#define LANGUAGE_STRINGS_H

typedef struct {
    //----------------------
    // Language lines
    //----------------------

    char* langCatalan;
    char* langChineseSimplified;
    char* langChineseTraditional;
    char* langDutch;
    char* langEnglish;
    char* langFinnish;
    char* langFrench;
    char* langGerman;
    char* langItalian;
    char* langJapanese;
    char* langKorean;
    char* langPolish;
    char* langPortuguese;
    char* langRussian;
    char* langSpanish;
    char* langSwedish;


    //----------------------
    // Generic lines
    //----------------------

    char* textDevice;
    char* textFilename;
    char* textFile;
    char* textNone;
    char* textUnknown;


    //----------------------
    // Warning and Error lines
    //----------------------

    char* warningTitle;
    char* warningDiscardChanges;
    char* warningOverwriteFile;
    char* warningStateOldFormat;
    char* errorTitle;
    char* errorEnterFullscreen;
    char* errorDirectXFailed;
    char* errorNoRomInZip;
    char* errorNoDskInZip;
    char* errorNoCasInZip;
    char* errorDirAsDskOverflow;
    char* errorNoHelp;
    char* errorStartEmu;
    /* Machine-load failure details shown as the body of errorStartEmu. */
    char* errorStartEmuNoMachine;         /* no machine selected */
    char* errorStartEmuMachinesDirMissing;/* Machines/ absent */
    char* errorStartEmuMachineNotFound;   /* %s = machine name */
    char* errorStartEmuConfigInvalid;     /* %s = machine name */
    char* errorMissingFiles;
    char* errorPortableReadonly;
    char* infoColorDepth;
    char* infoTitle;
    char* infoGameReaderRedirect;
    char* errorKeyboardThemeMissing;
    char* errorMixerThemeMissing;
    char* errorRecorderTitle;
    char* errorRecorderSaveReplay;
    char* errorRecorderReplayMissing;
    char* errorRecorderRequiresDX12;
    char* errorRecorderRequiresDX12Title;
    char* infoRecorderComplete;
    char* infoToastSaved;
    char* infoToastAlreadyRecording;
    char* dlgRecorderPickTitle;
    char* dlgRecorderPickSourceCap;
    char* dlgRecorderPickOutputMp4;
    char* menuFileRecordVideo;
    char* menuFileStopRecordVideo;
    char* shortcutRecordVideoStart;
    char* shortcutRecordVideoStartAs;
    char* shortcutRecordVideoStop;
    char* shortcutRecordVideoToggle;
    char* shortcutAudioCaptureAs;
    char* shortcutVideoRecordAs;
    char* shortcutScreenshotAs;

    /* Capture page (IDD_CAPTURE). */
    char* propCapture;
    char* propCaptureAudioGB;
    char* propCaptureVideoGB;
    char* propCaptureScreenshotGB;
    char* propCaptureReplayGB;
    char* propCaptureSaveDir;
    char* propCaptureFormat;
    char* propCaptureCodec;
    char* propCaptureAutoName;
    char* propCapturePromptName;
    char* propCaptureShowToast;


    //----------------------
    // File related lines
    //----------------------

    char* fileRom;
    char* fileAll;
    char* fileCpuState;
    char* fileVideoCapture;
    char* fileDisk;
    char* fileCas;
    char* fileAvi;


    //----------------------
    // Menu related lines
    //----------------------

    char* menuNoRecentFiles;
    char* menuInsert;
    char* menuEject;

    char* menuCartGameReader;
    char* menuCartIde;
    char* menuCartBeerIde;
    char* menuCartGIde;
    char* menuCartSunriseIde;
    char* menuCartScsi;
    char* menuCartMegaSCSI;
    char* menuCartWaveSCSI;
    char* menuCartGoudaSCSI;
    char* menuJoyrexPsg;
    char* menuCartSCC;
    char* menuCartSCCPlus;
    char* menuCartFMPac;
    char* menuCartPac;
    char* menuCartHBI55;
    char* menuCartInsertSpecial;
    char* menuCartMegaRam;
    char* menuCartExternalRam;
    char* menuCartEseRam;
    char* menuCartEseSCC;
    char* menuCartMegaFlashRom;
    char* menuCartFlashCart;

    char* menuDiskInsertNew;
    char* menuDiskInsertCdrom;
    char* menuDiskDirInsert;
    char* menuDiskAutoStart;
    char* menuCartAutoReset;

    char* menuCasRewindAfterInsert;
    char* menuCasUseReadOnly;
    char* lmenuCasSaveAs;
    char* menuCasSetPosition;
    char* menuCasRewind;

    char* menuVideoLoad;
    char* menuVideoPlay;
    char* menuVideoRecord;
    char* menuVideoRecording;
    char* menuVideoRecAppend;
    char* menuVideoStop;
    char* menuVideoRender;

    char* menuPrnFormfeed;

    char* menuZoom1x;
    char* menuZoom2x;
    char* menuZoom3x;
    char* menuZoom4x;
    char* menuZoom5x;
    char* menuZoom6x;
    char* menuZoom7x;
    char* menuZoom8x;
    char* menuZoomFullscreen;

    char* menuPropsEmulation;
    char* menuPropsVideo;
    char* menuPropsSound;
    char* menuPropsMidi;
    char* menuPropsControls;
    char* menuPropsEffects;
    char* menuPropsSettings;
    char* menuPropsFile;
    char* menuPropsDisk;
    char* menuPropsLanguage;
    char* menuPropsPorts;
    char* menuPropsCapture;

    char* menuVideoSource;
    char* menuVideoSourceDefault;
    char* menuVideoChipAutodetect;
    char* menuVideoInSource;
    char* menuVideoInBitmap;
    
    char* menuEthInterface;

    char* menuHelpHelp;
    char* menuHelpAbout;

    char* menuFileCart;
    char* menuFileDisk;
    char* menuFileCas;
    char* menuFilePrn;
    char* menuFileLoadState;
    char* menuFileSaveState;
    char* menuFileQLoadState;
    char* menuFileQSaveState;
    char* menuFileCaptureAudio;     /* "Record Audio"  -- shown when idle */
    char* menuFileStopAudio;        /* "Stop Audio"    -- shown while recording */
    char* menuFileCaptureVideo;     /* Replay submenu title */
    char* menuFileScreenShot;
    char* menuFileExit;
    char* menuFileHarddisk;
    char* menuFileHarddiskNoPesent;
    char* menuFileHarddiskRemoveAll;

    char* menuRunRun;
    char* menuRunPause;
    char* menuRunStop;
    char* menuRunSoftReset;
    char* menuRunHardReset;
    char* menuRunCleanReset;

    char* menuToolsMachine;
    char* menuToolsShortcuts;
    char* menuToolsKeyboard;
    char* menuToolsCtrlEditor;
    char* menuToolsMixer;
    char* menuToolsLoadMemory;
    char* menuToolsDebugger;
    char* menuToolsTrainer;
    char* menuToolsTraceLogger;

    char* menuFile;
    char* menuRun;
    char* menuWindow;
    char* menuOptions;
    char* menuTools;
    char* menuHelp;


    //----------------------
    // Dialog related lines
    //----------------------

    char* dlgOK;
    char* dlgOpen;
    char* dlgCancel;
    char* dlgYes;
    char* dlgNo;
    char* dlgSave;
    char* dlgSaveAs;
    char* dlgRun;
    char* dlgClose;

    char* dlgLoadRom;
    char* dlgLoadDsk;
    char* dlgLoadCas;
    char* dlgLoadRomDskCas;
    char* dlgLoadRomDesc;
    char* dlgLoadDskDesc;
    char* dlgLoadCasDesc;
    char* dlgLoadRomDskCasDesc;
    char* dlgLoadState;
    char* dlgLoadVideoCapture;
    char* dlgSaveState;
    char* dlgSaveCassette;
    char* dlgSaveVideoClipAs;
    char* dlgSaveCaptureAudio;
    char* dlgSaveCaptureVideo;
    char* dlgSaveCaptureReplay;
    char* dlgSaveCaptureScreenshot;
    char* dlgAmountCompleted;
    char* dlgInsertRom1;
    char* dlgInsertRom2;
    char* dlgInsertDiskA;
    char* dlgInsertDiskB;
    char* dlgInsertHarddisk;
    char* dlgInsertCas;
    char* dlgRomType;
    char* dlgDiskSize;

    char* dlgTapeTitle;
    char* dlgTapeFrameText;
    char* dlgTapeCurrentPos;
    char* dlgTapeTotalTime;
    char* dlgTapeSetPosText;
    char* dlgTapeCustom;
    char* dlgTabPosition;
    char* dlgTabType;
    char* dlgTabFilename;
    char* dlgZipReset;

    char* dlgAboutTitle;

    char* dlgLangLangText;
    char* dlgLangLangTitle;

    char* dlgAboutAbout;
    char* dlgAboutVersion;
    char* dlgAboutBuildNumber;
    char* dlgAboutBuildDate;
    char* dlgAboutForkNote;
    char* dlgAboutOrigDevel;
    char* dlgAboutCreat;
    char* dlgAboutDevel;
    char* dlgAboutThanks;
    char* dlgAboutLisence;

    char* dlgSavePreview;
    char* dlgSaveDate;
    
    char* dlgRenderVideoCapture;


    //----------------------
    // Properties related lines
    //----------------------

    char* propTitle;
    char* propEmulation;
    char* propD3D;
    char* propVideo;
    char* propSound;
    char* propControls;
    char* propPerformance;
    char* propEffects;
    char* propSettings;
    char* propFile;
    char* propDisk;
    char* propPorts;
    char* propMidi;

    char* propEmuGeneralGB;
    char* propEmuFamilyText;
    char* propEmuMemoryGB;
    char* propEmuRamSizeText;
    char* propEmuVramSizeText;
    char* propEmuSpeedGB;
    char* propEmuSpeedText;
    char* propEmuVdpCmdSpeedText;
    char* propEmuFrontSwitchGB;
    char* propEmuFrontSwitch;
    char* propEmuFdcTiming;
    char* propEmuHddSdBoost;
    char* propEmuNoSpriteLimits;
    char* propEnableMsxKeyboardQuirk;
    char* propEmuReversePlay;
    char* propEmuPauseSwitch;
    char* propEmuAudioSwitch;
    char* propVideoFreqText;
    char* propVideoFreqAuto;
    char* propSndOversampleText;
    char* propSndOpllAnalogText;        /* "Analog filter:" label */
    char* propSndOpllAnalogLpfText;     /* "LPF cutoff:" label */
    char* enumOpllFilterOff;
    char* enumOpllFilterBright;
    char* enumOpllFilterClear;
    char* enumOpllFilterStandard;
    char* enumOpllFilterSoft;
    char* enumOpllFilterMellow;
    char* enumOpllFilterCustom;
    char* propSndMidiInGB;
    char* propSndYkInGB;
    char* propSndMidiOutGB;
    char* propSndMidiChannel;
    char* propSndMidiAll;

    char* propMonMonGB;
    char* propMonTypeText;
    char* propMonEmuText;
    char* propVideoTypeText;
    char* propMonHorizStretch;
    char* propMonVertStretch;
    char* propMonDeInterlace;
    char* propBlendFrames;
    char* propMonBrightness;
    char* propMonContrast;
    char* propMonSaturation;
    char* propMonGamma;
    char* propMonScanlines;
    char* propMonScanlinesBright;       /* slider label */
    char* propMonScanlinesBrightAuto;   /* checkbox */
    char* propMonScanlinesShape;        /* "Shape:" label (for the preset combobox) */
    char* propMonScanlinesDepth;        /* "Depth:" label (for the depth slider) */
    char* propMonScanlinesSharpness;    /* "Sharpness:" label (for the shape-sharpness slider) */
    char* enumScanShapeGentle;          /* dropdown items */
    char* enumScanShapeStandard;
    char* enumScanShapeSharp;
    char* enumScanShapeTrinitron;
    char* enumScanShapeCustom;
    char* propMonHdrEnable;             /* HDR enable checkbox */
    char* propMonHdrPaperWhite;         /* HDR brightness slider label */
    char* propMonHdrSystemMode;         /* "System HDR mode:" prefix */
    char* propMonHdrRestartHint;        /* "HDR setting changed -- restart" message body */
    char* propMonHdrRecord;             /* "Record in HDR" checkbox label */
    char* propMonColorGhosting;
    char* propMonEffectsGB;

    char* propPerfVideoDrvGB;
    char* propPerfVideoDispDrvText;
    char* propPerfFrameSkipText;
    char* propPerfAudioDrvGB;
    char* propPerfAudioDrvText;
    char* propPerfAudioBufSzText;
    char* propPerfAudioBufSzActualFmt;
    char* propPerfEmuGB;
    char* propPerfSyncModeText;
    char* propFullscreenResText;

    char* propSndChipEmuGB;
    char* propSndMsxMusic;
    char* propSndMsxAudio;
    char* propSndMoonsound;
    char* propSndMt32ToGm;
    char* propSoundChipsActive;
    char* propSoundChipsHint;
    char* propSoundChipsYm2413GB;
    char* propSoundChipsY8950GB;

    char* propPortsLptGB;
    char* propPortsComGB;
    char* propPortsLptText;
    char* propPortsCom1Text;
    char* propPortsNone;
    char* propPortsSimplCovox;
    char* propPortsFile;
    char* propPortsComFile;
    char* propPortsOpenLogFile;
    char* propPortsEmulateMsxPrn;

    char* propSetFileHistoryGB;
    char* propSetFileHistorySize;
    char* propSetFileHistoryClear;
    char* propFileTypes;
    char* propOpenDefaultApps;
    char* propWindowsEnvGB;
    char* propSetScreenSaver;
    char* propPriorityBoost;
    char* propScreenshotPng;
    char* propEjectMediaOnExit;
    char* propClearHistory;
    char* propOpenRomGB;
    char* propDefaultRomType;
    char* propGuessRomType;

    char* propSettDefSlotGB;
    char* propSettDefSlots;
    char* propSettDefSlot;
    char* propSettDefDrives;
    char* propSettDefDrive;

    char* propThemeGB;
    char* propTheme;

    char* propCdromGB;
    char* propCdromMethod;
    char* propCdromMethodNone;
    char* propCdromMethodIoctl;
    char* propCdromMethodAspi;
    char* propCdromDrive;

	char* propD3DParametersGB;
    char* propD3DAspectRatioText;
    char* propD3DLinearFilteringText;
    char* propD3DForceHighResText;
    char* propD3DExtendBorderColorText;

    char* propD3DCroppingGB;
	char* propD3DCroppingTypeText;
	char* propD3DCroppingLeftText;
    char* propD3DCroppingRightText;
    char* propD3DCroppingTopText;
    char* propD3DCroppingBottomText;


    //----------------------
    // Dropdown related lines
    //----------------------

    char* enumVideoMonColor;
    char* enumVideoMonGrey;
    char* enumVideoMonGreen;
    char* enumVideoMonAmber;

    char* enumVideoTypePAL;
    char* enumVideoTypeNTSC;

    char* enumVideoEmuNone;
    char* enumVideoEmuYc;
    char* enumVideoEmuMonitor;
    char* enumVideoEmuYcBlur;
    char* enumVideoEmuComp;
    char* enumVideoEmuCompBlur;
    char* enumVideoEmuScale2x;
    char* enumVideoEmuHq2x;


    char* enumVideoDrvDirectDrawHW;
    char* enumVideoDrvDirectDraw;
    char* enumVideoDrvGDI;
    char* enumVideoDrvD3D;

    char* enumVideoFrameskip0;
    char* enumVideoFrameskip1;
    char* enumVideoFrameskip2;
    char* enumVideoFrameskip3;
    char* enumVideoFrameskip4;
    char* enumVideoFrameskip5;

	char* enumD3DARAuto;
	char* enumD3DARStretch;
	char* enumD3DARPAL;
	char* enumD3DARNTSC;
	char* enumD3DAR11;

	char* enumD3DCropNone;
	char* enumD3DCropMSX1;
	char* enumD3DCropMSX1Plus8;
	char* enumD3DCropMSX2;
	char* enumD3DCropMSX2Plus8;
	char* enumD3DCropCustom;

    char* enumSoundDrvNone;
    char* enumSoundDrvWMM;
    char* enumSoundDrvDirectX;
    char* enumSoundDrvWasapi;

    char* enumEmuSync1ms;
    char* enumEmuSyncAuto;
    char* enumEmuSyncNone;
    char* enumEmuSyncVblank;
    char* enumEmuAsyncVblank;

    char* enumControlsJoyNone;
    char* enumControlsJoyMouse;
    char* enumControlsJoyTetris2Dongle;
    char* enumControlsJoyTMagicKeyDongle;
    char* enumControlsJoy2Button;
    char* enumControlsJoyGunstick;
    char* enumControlsJoyAsciiLaser;
    char* enumControlsArkanoidPad;
    char* enumControlsJoyColeco;
    
    char* enumDiskMsx35Dbl9Sect;
    char* enumDiskMsx35Dbl8Sect;
    char* enumDiskMsx35Sgl9Sect;
    char* enumDiskMsx35Sgl8Sect;
    char* enumDiskSvi525Dbl;
    char* enumDiskSvi525Sgl;
    char* enumDiskSf3Sgl;
    char* enumDiskSize;     /* "Disk size" group label in new-image dialogs */
    char* enumDiskFormat;   /* "Format:" group label in new-image dialogs */
    char* enumDiskFormatUnformatted; /* format-combobox entry, only word needing translation */


    //----------------------
    // Configuration related lines
    //----------------------

    char* confTitle;
    char* confConfigText;
    char* confSlotLayout;
    char* confMemory;
    char* confChipEmulation;
    char* confChipExtras;

    char* confOpenRom;
    char* confSaveTitle;
    char* confSaveText;
    char* confSaveAsTitle;
    char* confSaveAsMachineName;
    char* confDiscardTitle;
    char* confExitSaveTitle;
    char* confExitSaveText;

    char* confSlotLayoutGB;
    char* confSlotExtSlotGB;
    char* confBoardGB;
    char* confBoardText;
    char* confSlotPrimary;
    char* confSlotExpanded;

    char* confSlotCart;
    char* confSlot;
    char* confSubslot;

    char* confMemAdd;
    char* confMemEdit;
    char* confMemRemove;
    char* confMemSlot;
    char* confMemAddresss;
    char* confMemType;
    char* confMemRomImage;

    char* confChipVideoGB;
    char* confChipVideoChip;
    char* confChipVideoRam;
    char* confChipSoundGB;
    char* confChipPsgStereoText;

    char* confCmosGB;
    char* confCmosEnable;
    char* confCmosBattery;

    char* confCpuFreqGB;
    char* confZ80FreqText;
    char* confR800FreqText;
    char* confFdcGB;
    char* confCFdcNumDrivesText;

    char* confEditMemTitle;
    char* confEditMemGB;
    char* confEditMemType;
    char* confEditMemFile;
    char* confEditMemAddress;
    char* confEditMemSize;
    char* confEditMemSlot;


    //----------------------
    // Shortcut lines
    //----------------------

    char* shortcutKey;
    char* shortcutDescription;

    char* shortcutSaveConfig;
    char* shortcutOverwriteConfig;
    char* shortcutCreateConfig;
    char* shortcutExitConfig;
    char* shortcutDiscardConfig;
    char* shortcutSaveConfigAs;
    char* shortcutConfigName;
    char* shortcutNewProfile;
    char* shortcutConfigTitle;
    char* shortcutAssign;
    char* shortcutPressText;
    char* shortcutScheme;
    char* shortcutCartInsert1;
    char* shortcutCartRemove1;
    char* shortcutCartInsert2;
    char* shortcutCartRemove2;
    char* shortcutSpecialMenu1;
    char* shortcutSpecialMenu2;
    char* shortcutCartAutoReset;
    char* shortcutDiskInsertA;
    char* shortcutDiskDirInsertA;
    char* shortcutDiskRemoveA;
    char* shortcutDiskChangeA;
    char* shortcutDiskAutoResetA;
    char* shortcutDiskInsertB;
    char* shortcutDiskDirInsertB;
    char* shortcutDiskRemoveB;
    char* shortcutCasInsert;
    char* shortcutCasEject;
    char* shortcutCasAutorewind;
    char* shortcutCasReadOnly;
    char* shortcutCasSetPosition;
    char* shortcutCasRewind;
    char* shortcutCasSave;
    char* shortcutPrnFormFeed;
    char* shortcutCpuStateLoad;
    char* shortcutCpuStateSave;
    char* shortcutCpuStateQload;
    char* shortcutCpuStateQsave;
    char* shortcutAudioCapture;
    char* shortcutScreenshotOrig;
    char* shortcutScreenshotSmall;
    char* shortcutScreenshotLarge;
    char* shortcutQuit;
    char* shortcutRunPause;
    char* shortcutStop;
    char* shortcutResetHard;
    char* shortcutResetSoft;
    char* shortcutResetClean;
    char* shortcutSize1x;
    char* shortcutSize2x;
    char* shortcutSize3x;
    char* shortcutSize4x;
    char* shortcutSize5x;
    char* shortcutSize6x;
    char* shortcutSize7x;
    char* shortcutSize8x;
    char* shortcutSizeFullscreen;
    char* shortcutSizeMinimized;
    char* shortcutToggleFullscren;
    char* shortcutVolumeIncrease;
    char* shortcutVolumeDecrease;
    char* shortcutVolumeMute;
    char* shortcutVolumeStereo;
    char* shortcutYm2413BackendCycle;
    char* shortcutY8950BackendCycle;
    char* shortcutSwitchMsxAudio;
    char* shortcutSwitchFront;
    char* shortcutSwitchPause;
    char* shortcutToggleMouseLock;
    char* shortcutEmuSpeedMax;
    char* shortcutEmuPlayReverse;
    char* shortcutEmuSpeedToggle;
    char* shortcutEmuSpeedNormal;
    char* shortcutEmuSpeedInc;
    char* shortcutEmuSpeedDec;
    char* shortcutThemeSwitch;
    char* shortcutShowEmuProp;
    char* shortcutShowVideoProp;
    char* shortcutShowAudioProp;
    char* shortcutShowCtrlProp;
    char* shortcutShowEffectsProp;
    char* shortcutShowSettProp;
    char* shortcutShowPorts;
    char* shortcutShowLanguage;
    char* shortcutShowMachines;
    char* shortcutShowShortcuts;
    char* shortcutShowKeyboard;
    char* shortcutShowMixer;
    char* shortcutShowDebugger;
    char* shortcutShowTrainer;
    char* shortcutShowHelp;
    char* shortcutShowAbout;
    char* shortcutShowFiles;
    char* shortcutToggleSpriteEnable;
    char* shortcutToggleFdcTiming;
    char* shortcutToggleHddSdBoost;
    char* shortcutToggleNoSpriteLimits;
    char* shortcutEnableMsxKeyboardQuirk;
    char* shortcutToggleCpuTrace;
    char* shortcutVideoLoad;
    char* shortcutVideoPlay;
    char* shortcutVideoRecord;
    char* shortcutVideoStop;
    char* shortcutVideoRender;


    //----------------------
    // Keyboard config lines
    //----------------------

    char* keyconfigSelectedKey;
    char* keyconfigMappedTo;
    char* keyconfigMappingScheme;

    
    //----------------------
    // Rom type lines
    //----------------------

    char* romTypeStandard;

    char* romTypeZenima80;
    char* romTypeZenima90;
    char* romTypeZenima126;

    char* romTypeSccMirrored;
    char* romTypeSccExtended;

    char* romTypeKonamiGeneric;

    char* romTypeMirrored;
    char* romTypeNormal;
    char* romTypeDiskPatch;
    char* romTypeCasPatch;
    char* romTypeTc8566afFdc;
    char* romTypeTc8566afTrFdc;
    char* romTypeMicrosolFdc;
    char* romTypeNationalFdc;
    char* romTypePhilipsFdc;
    char* romTypeSvi707Fdc;
    char* romTypeSvi738Fdc;
    char* romTypeMappedRam;
    char* romTypeMirroredRam1k;
    char* romTypeMirroredRam2k;
    char* romTypeNormalRam;

    char* romTypeTurborPause;
    char* romTypeF4deviceNormal;
    char* romTypeF4deviceInvert;

    char* romTypeTurborTimer;

    char* romTypeNormal4000;
    char* romTypeNormalC000;

    char* romTypeExtRam;
    char* romTypeExtRam16;
    char* romTypeExtRam32;
    char* romTypeExtRam48;
    char* romTypeExtRam64;
    char* romTypeExtRam512;
    char* romTypeExtRam1mb;
    char* romTypeExtRam2mb;
    char* romTypeExtRam4mb;

    char* romTypeSvi328Cart;
    char* romTypeSvi328Fdc;
    char* romTypeSvi328Prn;
    char* romTypeSvi328Uart;
    char* romTypeSvi328col80;
    char* romTypeSvi328RsIde;
    char* romTypeSvi727col80;
    char* romTypeColecoCart;
    char* romTypeSg1000Cart;
    char* romTypeSc3000Cart;

    char* romTypeMsxPrinter;
    char* romTypeTurborPcm;

    char* romTypeNms8280Digitiz;
    char* romTypeHbiV1Digitiz;

    //----------------------
    // Debug type lines
    // Note: Only needs translation if debugger is translated
    //----------------------
    
    char* dbgMemVisible;
    char* dbgMemRamNormal;
    char* dbgMemRamMapped;
    char* dbgMemYmf278;
    char* dbgMemAy8950;
    char* dbgMemScc;

    char* dbgCallstack;

    char* dbgRegs;
    char* dbgRegsCpu;
    char* dbgRegsYmf262;
    char* dbgRegsYmf278;
    char* dbgRegsAy8950;
    char* dbgRegsYm2413;

    char* dbgDevRamMapper;
    char* dbgDevRam;
    char* dbgDevF4Device;
    char* dbgDevKorean80;
    char* dbgDevKorean90;
    char* dbgDevKorean128;
    char* dbgDevFdcMicrosol;

    char* dbgDevPrinter;

    char* dbgDevSviFdc;
    char* dbgDevSviPrn;
    char* dbgDevSvi80Col;

    char* dbgDevRtc;
    char* dbgDevTrPause;


    //----------------------
    // Debug type lines
    // Note: Can only be translated to european languages
    //----------------------
    char* aboutScrollThanksTo;
    char* aboutScrollAndYou;

} LanguageStrings;

#endif

