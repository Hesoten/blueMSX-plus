/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Language/LanguagePolish.h,v $
**
** $Revision: 1.49 $
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
#ifndef LANGUAGE_POLISH_H
#define LANGUAGE_POLISH_H

#include "LanguageStrings.h"
 
void langInitPolish(LanguageStrings* ls) 
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

    ls->textDevice              = "Urządzenie:";
    ls->textFilename            = "Nazwa pliku:";
    ls->textFile                = "Plik";
    ls->textNone                = "Brak";
    ls->textUnknown             = "Nieznany";                            


    //----------------------
    // Warning and Error lines
    //----------------------

    ls->warningTitle             = "blueMSX+ - Uwaga";
    ls->warningDiscardChanges   = "Czy chcesz odrzucić zmiany?";
    ls->warningOverwriteFile    = "Czy chcesz nadpisać plik:"; 
    ls->warningStateOldFormat   = "To zapis stanu w starym formacie. Wznowienie może być niepoprawne. Wczytać mimo to?";
    ls->errorTitle              = "blueMSX+ - błąd";
    ls->errorEnterFullscreen    = "Nie mogę przełączyć na pełny ekran.           \n";
    ls->errorDirectXFailed      = "Nie mogę stworzyć obiektów DirectX.           \nPrzełączam w tryb GDI.\nSprawdź właściwości wideo.";
    ls->errorNoRomInZip         = "Nie znaleziono pliku .rom w archiwum zip.";
    ls->errorNoDskInZip         = "Nie znaleziono pliku .dsk w archiwum zip.";
    ls->errorCreateDiskImage    = "Nie można utworzyć pliku obrazu dysku.";
    ls->errorCreateTapeImage    = "Nie można utworzyć pliku obrazu taśmy.";
    ls->errorNoCasInZip         = "Nie znaleziono pliku .cas w archiwum zip.";
    ls->errorDirAsDskOverflow   = "%d plik(ów) (%d KB łącznie) nie zmieściło się w obrazie dysku 720 KB i zostały pominięte.";
    ls->errorNoHelp             = "Nie znaleziono pliku pomocy blueMSX+.";
    ls->errorStartEmu           = "Nie udało się uruchomić emulatora MSX.";
    ls->errorStartEmuNoMachine           = "Nie ustawiono maszyny MSX do emulacji. Wybierz jedną w Opcje -> Emulacja -> Typ MSX.";
    ls->errorStartEmuMachinesDirMissing  = "Nie znaleziono folderu Machines.";
    ls->errorStartEmuMachineNotFound     = "Nie znaleziono wybranej konfiguracji maszyny '%s'.";
    ls->errorStartEmuConfigInvalid       = "Nie udało się odczytać pliku config.ini konfiguracji maszyny '%s'. Plik może być uszkodzony lub pochodzić z niekompatybilnej wersji.";
    ls->errorMissingFiles       = "Nie można wczytać następujących plików:";
    ls->errorPortableReadonly   = "Urządzenie przenośne - tylko do odczytu";        
    ls->errorMidiOpenFailed     = "Nie udało się otworzyć urządzenia MIDI '%s'. Może używać go inna aplikacja.";
    ls->infoTitle               = "Informacje blueMSX+";
    ls->infoGameReaderRedirect  = "blueMSX+ nie obsługuje bezpośrednio MSX Game Reader (oryginalny sterownik ASCII z ery XP nie działa już w nowoczesnym Windowsie).\n\nOtworzyć MSX Game Reader - Web Dumper (autorstwa Kunihiko Ohnaka) w przeglądarce?";
    ls->infoColorDepth          = "blueMSX+ najlepiej działa przy głębi kolorów 16 lub 32 bit.";
    ls->errorKeyboardThemeMissing = "Nie można znaleźć motywu edytora klawiatury.";
    ls->errorMixerThemeMissing    = "Nie można znaleźć motywu miksera.";
    ls->errorRecorderTitle      = "blueMSX+ - Nagrywanie";
    ls->errorRecorderSaveReplay = "Nie można zapisać pliku powtórki:\n  %s\n\nUpewnij się, że folder docelowy istnieje i ma uprawnienia do zapisu.";
    ls->errorRecorderReplayMissing = "Plik powtórki nie znaleziony:\n  %s\n\nNajpierw nagraj powtórkę lub użyj Wczytaj, by wybrać istniejący plik .cap.";
    ls->errorRecorderRequiresDX12   = "Przełączyć sterownik wideo na Direct3D 12 i rozpocząć nagrywanie?";
    ls->errorRecorderRequiresDX12Title = "blueMSX+ - Zmiana sterownika wideo";
    ls->infoRecorderComplete    = "Plik wideo zapisany:\n  %s";
    ls->infoToastSaved          = "Zapisano: %s";
    ls->infoToastAlreadyRecording   = "Już nagrywa";
    ls->infoToastMouseConnected    = "Mysz PC podłączona do MSX";
    ls->infoToastMouseDisconnected = "Mysz PC odłączona od MSX";
    ls->propControlsMouseSens      = "Czułość:";
    ls->dlgRecorderPickTitle        = "blueMSX+ - Renderuj powtórkę do wideo";
    ls->dlgRecorderPickSourceCap    = "Plik powtórki do renderowania (.cap):";
    ls->dlgRecorderPickOutputMp4    = "Wyjściowy plik wideo (.mp4):";
    ls->menuFileRecordVideo         = "Nagraj wideo";
    ls->menuFileStopRecordVideo     = "Zatrzymaj nagrywanie wideo";
    ls->shortcutRecordVideoStart    = "Nagrywanie wideo: Start";
    ls->shortcutRecordVideoStartAs  = "Nagrywanie wideo: Rozpocznij jako";
    ls->shortcutRecordVideoStop     = "Nagrywanie wideo: Stop";
    ls->shortcutRecordVideoToggle   = "Nagrywanie wideo: Przełącz";
    ls->shortcutAudioCaptureAs      = "Nagrywanie audio: Rozpocznij jako";
    ls->shortcutVideoRecordAs       = "Powtórka: Nagraj jako";
    ls->shortcutScreenshotAs        = "Zrzut ekranu: Zapisz jako";

    ls->propCapture                 = "Przechwytywanie";
    ls->propCaptureAudioGB          = " Nagrywanie audio ";
    ls->propCaptureVideoGB          = " Nagrywanie wideo ";
    ls->propCaptureScreenshotGB     = " Zrzut ekranu ";
    ls->propCaptureReplayGB         = " Nagrywanie powtórki ";
    ls->propCaptureSaveDir          = "Folder:";
    ls->propCaptureFormat           = "Format:";
    ls->propCaptureCodec            = "Kodek:";
    ls->propCaptureAutoName         = "Automatyczna nazwa pliku";
    ls->propCapturePromptName       = "Pytaj o nazwę pliku";
    ls->propCaptureShowToast        = "Pokaż powiadomienie po zakończeniu";


    //----------------------
    // File related lines
    //----------------------

    ls->fileRom                 = "ROM image";
    ls->fileAll                 = "Wszystkie pliki";
    ls->fileCpuState            = "Stan CPU";
    ls->fileVideoCapture        = "Video Capture"; // New in 2.6
    ls->fileDisk                = "Obraz dysku";
    ls->fileCas                 = "Obraz taśmy";
    ls->fileAvi                 = "Video Clip";    // New in 2.6


    //----------------------
    // Menu related lines
    //----------------------

    ls->menuNoRecentFiles       = "- brak ostatnich plików -";
    ls->menuInsert              = "Wybierz";
    ls->menuEject               = "Wysuń";

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
    ls->menuCartSCCPlus         = "Kartridż SCC-I";
    ls->menuCartFMPac           = "Kartridż FM-PAC";
    ls->menuCartPac             = "Kartridż PAC";
    ls->menuCartHBI55           = "Sony HBI-55 Cartridge";
    ls->menuCartInsertSpecial   = "Włóż inny";                     
    ls->menuCartMegaRam         = "MegaRAM";                            
    ls->menuCartExternalRam     = "Zewnętrzny RAM";
    ls->menuCartEseRam          = "Ese-RAM";             // New in 2.7
    ls->menuCartEseSCC          = "Ese-SCC";             // New in 2.7
    ls->menuCartMegaFlashRom    = "Mega Flash ROM";      // New in 2.7
    ls->menuCartFlashCart       = "Kartridże Flash";

    ls->menuDiskInsertNew       = "Włóż nowy obraz dysku";              
    ls->menuDiskInsertCdrom     = "Insert CD-Rom";       // New in 2.7
    ls->menuDiskDirInsert       = "Podepnij folder";
    ls->menuDiskAutoStart       = "Resetuj po zmianie dyskietki";
    ls->menuCartAutoReset       = "Resetuj po zmianie kartridża";
    
    ls->menuCasInsertNew         = "Włóż nowy obraz taśmy";
    ls->menuCasRewindAfterInsert = "Najpierw przewiń do początku";
    ls->menuCasSaveMonitor       = "Odsłuch podczas zapisu";
    ls->menuCasUseReadOnly       = "Używaj kaset 'tylko do odczytu'";
    ls->lmenuCasSaveAs           = "Zapisz kasetę jako...";
    ls->menuCasSetPosition      = "Ustaw pozycję";
    ls->menuCasRewind           = "Przewiń do początku";

    ls->menuVideoLoad           = "Load...";             // New in 2.6
    ls->menuVideoPlay           = "Play Last Capture";   // New in 2.6
    ls->menuVideoRecord         = "Record";              // New in 2.6
    ls->menuVideoRecording      = "Recording";           // New in 2.6
    ls->menuVideoRecAppend      = "Record (append)";     // New in 2.6
    ls->menuVideoStop           = "Stop";                // New in 2.6
    ls->menuVideoRender         = "Render Video File";   // New in 2.6

    ls->menuPrnFormfeed         = "Wysuń papier";

    ls->menuZoom1x              = "Okno 1x";
    ls->menuZoom2x              = "Okno 2x";
    ls->menuZoom3x              = "Okno 3x";
    ls->menuZoom4x              = "Okno 4x";
    ls->menuZoom5x              = "Okno 5x";
    ls->menuZoom6x              = "Okno 6x";
    ls->menuZoom7x              = "Okno 7x";
    ls->menuZoom8x              = "Okno 8x";
    ls->menuZoomFullscreen      = "Pełny ekran";
    
    ls->menuPropsEmulation      = "Emulacja";
    ls->menuPropsVideo          = "Obraz";
    ls->menuPropsSound          = "Dźwięk";
    ls->menuPropsMidi           = "MIDI";
    ls->menuPropsControls       = "Sterowanie";
    ls->menuPropsEffects        = "Efekty";               // New in 2.9
    ls->menuPropsSettings        = "Ustawienia";
    ls->menuPropsFile           = "Pliki";
    ls->menuPropsDisk           = "Disks";               // New in 2.7
    ls->menuPropsLanguage       = "Język";
    ls->menuPropsPorts          = "Porty";
    ls->menuPropsCapture        = "Przechwytywanie";
    
    ls->menuVideoSource         = "Źródło wyjścia 'Video Out'";
    ls->menuVideoSourceDefault  = "Brak źródła dla 'Video Out'";
    ls->menuVideoChipAutodetect = "Autodetekcja karty obrazu";
    ls->menuVideoInSource       = "Źródło 'Video In'";
    ls->menuVideoInBitmap       = "Plik bitmapy";                        
    
    ls->menuEthInterface        = "Ethernet Interface"; // New in 2.6

    ls->menuHelpHelp            = "Tematy pomocy";
    ls->menuHelpAbout           = "O blueMSX+...";

    ls->menuFileCart            = "Kartridż";
    ls->menuFileDisk            = "Stacja dyskietek";
    ls->menuFileCas             = "Kaseta";
    ls->menuFilePrn             = "Drukarka";
    ls->menuFileLoadState       = "Wczytaj stan CPU";
    ls->menuFileSaveState       = "Zapisz stan CPU";
    ls->menuFileQLoadState      = "Szybki odczyt stanu";
    ls->menuFileQSaveState      = "Szybki zapis stanu";
    ls->menuFileCaptureAudio    = "Przechwyć dźwięk";
    ls->menuFileStopAudio       = "Zatrzymaj audio";
    ls->menuFileCaptureVideo    = "Video Capture"; // New in 2.6
    ls->menuFileScreenShot      = "Zapisz ekran";
    ls->menuFileExit            = "Wyjście";

    ls->menuFileHarddisk        = "Dysk Twardy / Karta SD";                          
    ls->menuFileHarddiskNoPesent= "Brak sterownika";             
    ls->menuFileHarddiskRemoveAll= "Eject All Harddisk";    // New in 2.7

    ls->menuRunRun              = "Uruchom";
    ls->menuRunPause            = "Pauza";
    ls->menuRunStop             = "Zatrzymaj";
    ls->menuRunSoftReset        = "Miękki reset";
    ls->menuRunHardReset        = "Twardy reset";
    ls->menuRunCleanReset       = "Pełny reset";

    ls->menuToolsMachine         = "Edytor komputerów";
    ls->menuToolsCtrlEditor     = "Controllers / Keyboard Editor"; // New in 2.6
    ls->menuToolsKeyboard       = "Edytor klawiatury";
    ls->menuToolsMixer          = "Mikser";
    ls->menuToolsLoadMemory     = "Wczytaj do pamięci";
    ls->menuToolsDebugger       = "Debugger";               
    ls->menuToolsTrainer        = "Trainer";                
    ls->menuToolsTraceLogger    = "Trace Logger";           

    ls->menuFile                = "Plik";
    ls->menuRun                 = "Uruchamianie";
    ls->menuWindow              = "Okno";
    ls->menuOptions             = "Opcje";
    ls->menuTools                = "Narzędzia";
    ls->menuHelp                = "Pomoc";
    

    //----------------------
    // Dialog related lines
    //----------------------

    ls->dlgOK                   = "OK";
    ls->dlgOpen                 = "Otwórz";
    ls->dlgCancel               = "Anuluj";
    ls->dlgYes                  = "Tak";
    ls->dlgNo                   = "Nie";
    ls->dlgSave                 = "Zapisz";
    ls->dlgSaveAs               = "Zapisz jako...";
    ls->dlgRun                  = "Uruchom";
    ls->dlgClose                = "Zamknij";

    ls->dlgLoadRom              = "blueMSX+ - Wybierz plik rom do wczytania";
    ls->dlgLoadDsk              = "blueMSX+ - Wybierz plik dsk do wczytania";
    ls->dlgLoadCas              = "blueMSX+ - Wybierz plik cas do wczytania";
    ls->dlgLoadRomDskCas        = "blueMSX+ - Wybierz plik rom, dsk lub cas do wczytania";
    ls->dlgLoadRomDesc          = "Wybierz rom do wczytania:";
    ls->dlgLoadDskDesc          = "Wybierz dyskietkę do wczytania:";
    ls->dlgLoadCasDesc          = "Wybierz taśmę do wczytania:";
    ls->dlgLoadRomDskCasDesc    = "Wybierz rom, dyskietkę lub taśmę do wczytania:";
    ls->dlgLoadState            = "Wczytaj stan CPU";
    ls->dlgLoadVideoCapture     = "Load video capture";      // New in 2.6
    ls->dlgSaveState            = "Zapisz stan CPU";
    ls->dlgSaveCassette          = "blueMSX+ - Zapisz obraz kasety";
    ls->dlgSaveVideoClipAs      = "Save video clip as...";      // New in 2.6
    ls->dlgSaveCaptureAudio     = "Zapisz nagranie audio jako";
    ls->dlgSaveCaptureVideo     = "Zapisz nagranie wideo jako";
    ls->dlgSaveCaptureReplay    = "Zapisz powtórkę jako";
    ls->dlgSaveCaptureScreenshot = "Zapisz zrzut ekranu jako";
    ls->dlgAmountCompleted      = "Amount completed:";          // New in 2.6
    ls->dlgInsertRom1           = "Wybierz kartridż ROM dla slotu 1";
    ls->dlgInsertRom2           = "Wybierz kartridż ROM dla slotu 2";
    ls->dlgInsertDiskA          = "Wybierz dyskietkę dla stacji A";
    ls->dlgInsertDiskB          = "Wybierz dyskietkę dla stacji B";
    ls->dlgInsertHarddisk       = "Podłącz Twardy Dysk";                   
    ls->dlgInsertCas            = "Wybierz kasetę";
    ls->dlgCreateCas            = "Utwórz nowy obraz taśmy";
    ls->dlgRomType              = "Typ romu:";
    ls->dlgDiskSize             = "Disk Size:";             // New in 2.6

    ls->dlgTapeTitle            = "blueMSX+ - Pozycja taśmy";
    ls->dlgTapeFrameText        = "Pozycja taśmy";
    ls->dlgTapeCurrentPos       = "Obecna pozycja";
    ls->dlgTapeTotalTime        = "Czas całkowity";
    ls->dlgTapeSetPosText        = "Pozycja taśmy:";
    ls->dlgTapeCustom            = "Pokaż dowolne pliki";
    ls->dlgTabPosition           = "Pozycja";
    ls->dlgTabType               = "Typ";
    ls->dlgTabFilename           = "Nazwa pliku";
    ls->dlgZipReset             = "Resetuj po zmianie";

    ls->dlgAboutTitle           = "blueMSX+ - O programie";

    ls->dlgLangLangText         = "Wybierz język dla blueMSX+";
    ls->dlgLangLangTitle        = "blueMSX+ - Język";

    ls->dlgAboutAbout           = "O programie\r\n====";
    ls->dlgAboutVersion         = "Wersja:";
    ls->dlgAboutBuildNumber     = "Kompilacja:";
    ls->dlgAboutBuildDate       = "Data:";
    ls->dlgAboutCreat           = "Program Daniela Vika";
    ls->dlgAboutDevel           = "PROGRAMIŚCI\r\n========";
    ls->dlgAboutThanks          = "WSPÓŁTWÓRCY\r\n==========";       // New in 2.7 (retranslate, see english)
    ls->dlgAboutLisence         = "LICENSE\r\n"
                                  "======\r\n\r\n"
                                  "This software is provided 'as-is', without any express or implied "
                                  "warranty. In no event will the author(s) be held liable for any damages "
                                  "arising from the use of this software.\r\n\r\n"
                                  "Visit www.bluemsx.com for more details.";

    ls->dlgSavePreview          = "Pokaż podgląd";
    ls->dlgSaveDate             = "Czas zapisu:";

    ls->dlgRenderVideoCapture   = "blueMSX+ - Rendering Video Capture...";  // New in 2.6


    //----------------------
    // Properties related lines
    //----------------------

    ls->propTitle               = "blueMSX+ - Właściwości";
    ls->propEmulation           = "Emulacja";
    ls->propD3D                 = "Direct3D";
    ls->propVideo               = "Obraz";
    ls->propSound               = "Dźwięk";
    ls->propMidi                = "MIDI";
    ls->propControls            = "Sterowanie";
    ls->propPerformance         = "Wydajność";
    ls->propEffects             = "Efekty";             // New in 2.9
    ls->propSettings             = "Ustawienia";
    ls->propFile                = "Pliki";
    ls->propDisk                = "Disks";              // New in 2.7
    ls->propPorts               = "Porty";
    
    ls->propEmuGeneralGB        = "Ogólne ";
    ls->propEmuFamilyText       = "Typ MSX:";
    ls->propEmuMemoryGB         = "Pamięć ";
    ls->propEmuRamSizeText      = "Rozmiar RAMu:";
    ls->propEmuVramSizeText     = "Rozmiar VRAMu:";
    ls->propEmuSpeedGB          = "Szybkość emulacji ";
    ls->propEmuSpeedText        = "Rdzeń emulatora:";
    ls->propEmuVdpCmdSpeedText  = "Czas oczekiwania VDP:";
    ls->propEmuFrontSwitchGB     = "Przełączniki Panasonic ";
    ls->propEmuFrontSwitch       = " Przełącznik główny";
    ls->propEmuNoSpriteLimits   = " Wyłącz limit duszków";  // New in 2.9
    ls->propEnableMsxKeyboardQuirk = " Emuluj specyfikę klawiatury MSX";  // New in 2.9
    ls->propEmuBoostText        = "Przyspiesz podczas dostępu do urządzeń:";
    ls->propEmuFdcTiming        = " FDD";
    ls->propEmuCasBoost         = " Kaseta";
    ls->propEmuHddSdBoost       = " Karta HDD/SD";
    ls->propEmuReversePlay      = " Włącz odtwarzanie wstecz"; // New in 2.8.3
    ls->propEmuPauseSwitch      = " Przełącznik pauzy";
    ls->propEmuAudioSwitch       = " Przełącznik kartridża MSX-AUDIO";
    ls->propVideoFreqText       = "Częstotliwość obrazu:";
    ls->propVideoFreqAuto       = "Auto";
    ls->propSndOversampleText   = "Oversampling:";
    ls->propSndYkInGB           = "Wejście YK-01/YK-10/YK-20 ";                
    ls->propSndMidiInGB         = "MIDI In ";
    ls->propSndMidiOutGB        = "MIDI Out ";
    ls->propSndMidiChannel      = "Kanał MIDI:";                      
    ls->propSndMidiAll          = "Wszystkie";                                

    ls->propMonMonGB            = "Monitor ";
    ls->propMonTypeText         = "Typ monitora:";
    ls->propMonEmuText          = "Emulacja monitora:";
    ls->propVideoTypeText       = "Typ obrazu:";
    ls->propMonHorizStretch      = " Rozciągaj w poziomie";
    ls->propMonVertStretch       = " Rozciągaj w pionie";
    ls->propMonDeInterlace      = " Usuwaj przeplot";
    ls->propBlendFrames         = " Zlej ze sobą kolejne klatki";           
    ls->propMonBrightness       = "Jasność:";
    ls->propMonColorGhosting    = " Modulator RF:";
    ls->propMonContrast         = "Kontrast:";
    ls->propMonSaturation       = "Nasycenie:";
    ls->propMonGamma            = "Gamma:";
    ls->propMonScanlines        = " Przeplot:";
    ls->propMonScanlinesBright  = "Komp. jasn.:";
    ls->propMonScanlinesBrightAuto = " Auto";
    ls->propMonScanlinesShape   = "Profil:";
    ls->propMonScanlinesDepth   = "Głębia:";
    ls->propMonScanlinesSharpness = "Ostrość:";
    ls->enumScanShapeGentle     = "Łagodny";
    ls->enumScanShapeStandard   = "Standard";
    ls->enumScanShapeSharp      = "Ostry";
    ls->enumScanShapeTrinitron  = "Trinitron";
    ls->enumScanShapeCustom     = "Wł.";
    ls->propMonHdrEnable        = "HDR";
    ls->propMonHdrPaperWhite    = "Jasność bieli:";
    ls->propMonHdrSystemMode    = "Tryb HDR systemu:";
    ls->propMonHdrRestartHint   = "Uruchom ponownie blueMSX, aby zastosować zmianę trybu HDR.";
    ls->propMonHdrRecord        = " Nagrywaj w HDR";
    ls->propMonEffectsGB        = "Efekty ";

    ls->propPerfVideoDrvGB      = "Ustawienia Video ";
    ls->propPerfVideoDispDrvText= "Sterownik obrazu:";
    ls->propPerfFrameSkipText   = "Gubienie klatek:";
    ls->propPerfAudioDrvGB      = "Ustawienia Audio ";
    ls->propPerfAudioDrvText    = "Sterownik dźwięku:";
    ls->propPerfAudioBufSzText  = "Rozmiar bufora dźwięku:";
    ls->propPerfAudioBufSzActualFmt = "(rzeczywisty bufor: %u ms)";
    ls->propPerfEmuGB           = "Emulacja ";
    ls->propPerfSyncModeText    = "Tryb synchronizacji:";
    ls->propFullscreenResText   = "Pełny ekran:";

    ls->propSndChipEmuGB        = "Emulacja dźwięku ";
    ls->propSoundChipsActive    = "Aktywny backend:";
    ls->propSoundChipsHint      = "Wiele aktywnych backendów umożliwia porównanie A/B na żywo.";
    ls->propSoundChipsYm2413GB  = " Backend MSX-MUSIC ";
    ls->propSoundChipsY8950GB   = " Backend MSX-AUDIO ";
    ls->propSndOpllAnalogText   = "Filtr analogowy:";
    ls->propSndOpllAnalogLpfText = "Odcięcie LPF:";
    ls->enumOpllFilterOff       = "Wył.";
    ls->enumOpllFilterBright    = "Jasny (LPF 12 kHz)";
    ls->enumOpllFilterClear     = "Czysty (LPF 8 kHz)";
    ls->enumOpllFilterStandard  = "Standard (LPF 5 kHz)";
    ls->enumOpllFilterSoft      = "Miękki (LPF 3.5 kHz)";
    ls->enumOpllFilterMellow    = "Ciepły (LPF 2.3 kHz)";
    ls->enumOpllFilterCustom    = "Niestandard.";
    ls->propSndMsxMusic         = " MSX-MUSIC";
    ls->propSndMsxAudio         = " MSX-AUDIO";
    ls->propSndMoonsound         = " Moonsound";
    ls->propSndMt32ToGm         = " Mapuj instrumenty MT-32 na General MIDI";

    ls->propPortsLptGB          = "Port równoległy ";
    ls->propPortsComGB          = "Port szeregowy ";
    ls->propPortsLptText        = "Port:";
    ls->propPortsCom1Text       = "Port 1:";
    ls->propPortsNone           = "Brak";
    ls->propPortsSimplCovox     = "SiMPL / Covox DAC";
    ls->propPortsFile           = "Drukuj do pliku";
    ls->propPortsComFile        = "Send to File";
    ls->propPortsOpenLogFile    = "Otwórz plik logu";
    ls->propPortsEmulateMsxPrn  = "Emulacja:";

    ls->propSetFileHistoryGB     = "Historia plików ";
    ls->propSetFileHistorySize   = "Liczba elementów w historii plików:";
    ls->propSetFileHistoryClear  = "Wyczyść historię";
    ls->propFileTypes            = " Zarejestruj .rom/.dsk/.cas/.sta w menu \"Otwórz za pomocą\"";
    ls->propOpenDefaultApps      = "Otwórz ustawienia domyślnych aplikacji Windows";
    ls->propWindowsEnvGB         = "Otoczenie Windows "; 
    ls->propSetScreenSaver       = " Nie usypiaj/wygaszaj ekranu podczas pracy blueMSX+";
    ls->propPriorityBoost       = " Użyj harmonogramu gier Windows (MMCSS) do emulacji";
    ls->propScreenshotPng       = " używaj PNG do zapisywania ekranów";  
    ls->propEjectMediaOnExit    = " Eject media when blueMSX+ exits";        // New in 2.8
    ls->propClearHistory         = "Na pewno wyczyścić historię plików?";
    ls->propOpenRomGB           = "Okno wyboru romu ";
    ls->propDefaultRomType      = "Domyślny typ romu:";
    ls->propGuessRomType        = "Odgadnij typ romu";

    ls->propSettDefSlotGB       = "Przeciągnij-i-upuść ";
    ls->propSettDefSlots        = "Włóż rom do:";
    ls->propSettDefSlot         = " Slot";
    ls->propSettDefDrives       = "Włóż dyskietkę do:";
    ls->propSettDefDrive        = " Stacji";

    ls->propThemeGB             = "Temat ";
    ls->propTheme               = "Temat:";

    ls->propCdromGB             = "CD-ROM ";         // New in 2.7
    ls->propCdromMethod         = "Access Method:";  // New in 2.7
    ls->propCdromMethodNone     = "None";            // New in 2.7
    ls->propCdromMethodIoctl    = "IOCTL";           // New in 2.7
    ls->propCdromMethodAspi     = "ASPI";            // New in 2.7
    ls->propCdromDrive          = "Drive:";          // New in 2.7

    ls->propD3DParametersGB         = "Parametry ";                // New in 2.9
    ls->propD3DAspectRatioText      = "Proporcje";               // New in 2.9
    ls->propD3DScalingFilterText     = "Filtr skalowania";
    ls->propD3DExtendBorderColorText    = " Rozszerz kolor obramowania";   // New in 2.9

    ls->propD3DCroppingGB               = "Kadrowanie ";              // New in 2.9
    ls->propD3DCroppingTypeText         = "Typ kadrowania:";         // New in 2.9
    ls->propD3DCroppingLeftText         = "Lewo:";                  // New in 2.9
    ls->propD3DCroppingRightText        = "Prawo:";                 // New in 2.9
    ls->propD3DCroppingTopText          = "Góra:";                   // New in 2.9
    ls->propD3DCroppingBottomText       = "Dół:";                // New in 2.9


    //----------------------
    // Dropdown related lines
    //----------------------

    ls->enumVideoMonColor       = "Kolorowy";
    ls->enumVideoMonGrey        = "Czarno-biały";
    ls->enumVideoMonGreen       = "Zielony";
    ls->enumVideoMonAmber       = "Bursztynowy";

    ls->enumVideoTypePAL        = "PAL";
    ls->enumVideoTypeNTSC       = "NTSC";

    ls->enumVideoEmuNone        = "Brak";
    ls->enumVideoEmuYc          = "Kabel Y/C (ostry)";
    ls->enumVideoEmuMonitor     = "Monitor";
    ls->enumVideoEmuYcBlur      = "Zaszumiony kabel Y/C (ostry)";
    ls->enumVideoEmuComp        = "Kompozytowe (rozmyte)";
    ls->enumVideoEmuCompBlur    = "Zaszumione kompozytowe";
    ls->enumVideoEmuScale2x     = "Skalowanie 2x";
    ls->enumVideoEmuHq2x        = "Hq2x";


    ls->enumVideoDrvDirectDrawHW = "DirectDraw (sprzętowy)"; 
    ls->enumVideoDrvDirectDraw  = "DirectDraw";
    ls->enumVideoDrvGDI         = "GDI";
    ls->enumVideoDrvD3D         = "Direct3D";

    ls->enumVideoFrameskip0     = "Brak";
    ls->enumVideoFrameskip1     = "1 klatka";
    ls->enumVideoFrameskip2     = "2 klatki";
    ls->enumVideoFrameskip3     = "3 klatki";
    ls->enumVideoFrameskip4     = "4 klatki";
    ls->enumVideoFrameskip5     = "5 klatek";

    ls->enumD3DARAuto           = "Auto";           // New in 2.9
    ls->enumD3DARStretch        = "Rozciągnij";        // New in 2.9
    ls->enumD3DARPAL            = "PAL";            // New in 2.9
    ls->enumD3DARNTSC           = "NTSC";           // New in 2.9
    ls->enumD3DAR11             = "1:1";            // New in 2.9
    ls->enumD3DScaleNearest          = "Najbliższy";
    ls->enumD3DScaleBilinear         = "Dwuliniowy";
    ls->enumD3DScaleSharp            = "Ostry dwuliniowy";
    ls->enumD3DScalePrescaled     = "Dwuliniowy (2x prescale)";

    ls->enumD3DCropNone         = "Brak";           // New in 2.9
    ls->enumD3DCropMSX1         = "MSX1";           // New in 2.9
    ls->enumD3DCropMSX1Plus8    = "MSX1+8";         // New in 2.9
    ls->enumD3DCropMSX2         = "MSX2";           // New in 2.9
    ls->enumD3DCropMSX2Plus8    = "MSX2+8";         // New in 2.9
    ls->enumD3DCropCustom       = "Niestandardowe";         // New in 2.9

    ls->enumSoundDrvNone        = "Brak dźwięku";
    ls->enumSoundDrvWMM         = "Driver WMM";
    ls->enumSoundDrvDirectX     = "Driver DirectX";
    ls->enumSoundDrvWasapi      = "Driver WASAPI";

    ls->enumEmuSync1ms          = "Synchronizuj z MSX";
    ls->enumEmuSyncAuto         = "Auto (szybkie)";
    ls->enumEmuSyncNone         = "None";
    ls->enumEmuSyncVblank       = "Synchronizuj z Vblank PC";
    ls->enumEmuAsyncVblank      = "Asynchronous PC Vblank";             

    ls->enumControlsJoyNone     = "Brak";
    ls->enumControlsJoyMouse    = "Mysz";
    ls->enumControlsJoyTetris2Dongle = "Dongle Tetris 2";
    ls->enumControlsJoyTMagicKeyDongle = "MagicKey Dongle";             
    ls->enumControlsJoy2Button = "Joystick 2-przyciskowy";
    ls->enumControlsJoyGunstick  = "Gun Stick";                         
    ls->enumControlsJoyAsciiLaser="ASCII Plus-X Terminator Laser";      
    ls->enumControlsArkanoidPad  ="Arkanoid Pad";                   // New in 2.7.1
    ls->enumControlsJoyColeco = "Joystick ColecoVision";

    ls->enumDiskMsx35Dbl9Sect    = "MSX 3.5\" Double Sided, 9 Sectors";     
    ls->enumDiskMsx35Dbl8Sect    = "MSX 3.5\" Double Sided, 8 Sectors";     
    ls->enumDiskMsx35Sgl9Sect    = "MSX 3.5\" Single Sided, 9 Sectors";     
    ls->enumDiskMsx35Sgl8Sect    = "MSX 3.5\" Single Sided, 8 Sectors";     
    ls->enumDiskSvi525Dbl        = "SVI-328 5.25\" Double Sided";           
    ls->enumDiskSvi525Sgl        = "SVI-328 5.25\" Single Sided";      
    ls->enumDiskSf3Sgl           = "Sega SF-7000 3\" Single Sided";  // New in 2.6              
    ls->enumDiskSize             = "Rozmiar dysku";
    ls->enumDiskFormat           = "Format:";
    ls->enumDiskFormatUnformatted= "Niesformatowany";


    //----------------------
    // Configuration related lines
    //----------------------

    ls->confTitle                = "blueMSX+ - Edytor Konfiguracji Komputerów";
    ls->confConfigText           = "Konfiguracja";
    ls->confSlotLayout           = "Układ slotów";
    ls->confMemory               = "Pamięć";
    ls->confChipEmulation        = "Emulacja układów";
    ls->confChipExtras          = "Extras";

    ls->confOpenRom             = "Otwórz ROM";
    ls->confSaveTitle            = "blueMSX+ - Zapis konfiguracji";
    ls->confSaveText             = "Czy chcesz nadpisać konfigurację:";
    ls->confSaveAsTitle         = "Zapisz konfigurację jako...";
    ls->confSaveAsMachineName    = "Nazwa komputera:";
    ls->confDiscardTitle         = "blueMSX+ - Konfiguracja";
    ls->confExitSaveTitle        = "blueMSX+ - Wyjdź z Edytora Konfiguracji";
    ls->confExitSaveText         = "Czy chcesz zignorować zmiany w bieżącej konfiguracji?";

    ls->confSlotLayoutGB         = "Układ slotów ";
    ls->confSlotExtSlotGB        = "Zewnętrzne sloty ";
    ls->confBoardGB             = "Board ";
    ls->confBoardText           = "Board Type:";
    ls->confSlotPrimary          = "Podstawowy";
    ls->confSlotExpanded         = "Rozszerzony (4 podsloty)";

    ls->confSlotCart             = "Kartridż";
    ls->confSlot                = "Slot";
    ls->confSubslot             = "Podslot";

    ls->confMemAdd               = "Dodaj...";
    ls->confMemEdit              = "Edytuj...";
    ls->confMemRemove            = "Usuń";
    ls->confMemSlot              = "Slot";
    ls->confMemAddresss          = "Adres";
    ls->confMemType              = "Typ";
    ls->confMemRomImage          = "Obraz rom";
    
    ls->confChipVideoGB          = "Obraz ";
    ls->confChipVideoChip        = "Kość obrazu:";
    ls->confChipVideoRam         = "RAM obrazu:";
    ls->confChipSoundGB          = "Dźwięk ";
    ls->confChipPsgStereoText    = " PSG Stereo";

    ls->confCmosGB                = "CMOS ";
    ls->confCmosEnable            = " Enable CMOS";
    ls->confCmosBattery           = " Use Charged Battery";

    ls->confCpuFreqGB            = "CPU Frequency ";
    ls->confZ80FreqText          = "Z80 Frequency:";
    ls->confR800FreqText         = "R800 Frequency:";
    ls->confFdcGB                = "Floppy Disk Controller ";
    ls->confCFdcNumDrivesText    = "Number of Drives:";

    ls->confEditMemTitle         = "blueMSX+ - Edytuj Mapper";
    ls->confEditMemGB            = "Konfiguracja Mappera ";
    ls->confEditMemType          = "Typ:";
    ls->confEditMemFile          = "Plik:";
    ls->confEditMemAddress       = "Adres";
    ls->confEditMemSize          = "Rozmiar";
    ls->confEditMemSlot          = "Slot";


    //----------------------
    // Shortcut lines
    //----------------------

    ls->shortcutKey             = "Hotkey";
    ls->shortcutDescription     = "Skrót";

    ls->shortcutSaveConfig      = "blueMSX+ - Zapisz konfigurację";
    ls->shortcutOverwriteConfig = "Chcesz nadpisać konfigurację skrótów?:";
    ls->shortcutCreateConfig    = "Chcesz zapisać nową konfigurację skrótów?:";
    ls->shortcutExitConfig      = "blueMSX+ - Wyjdź z edytora skrótów";
    ls->shortcutDiscardConfig   = "Czy chcesz zignorować zmiany w bieżącej konfiguracji?";
    ls->shortcutSaveConfigAs    = "blueMSX+ - Zapisz konfigurację skrótów jako...";
    ls->shortcutConfigName      = "Nazwa konfiguracji:";
    ls->shortcutNewProfile      = "< Nowy profil >";
    ls->shortcutConfigTitle     = "blueMSX+ - Edytor Mapowania Skrótów";
    ls->shortcutAssign          = "Przypisz";
    ls->shortcutPressText       = "Naciśnij przycisk(i) skrótu:";
    ls->shortcutHotkeyHint      = "(do 3 klawiszy)";
    ls->keyboardMappedHint      = "(do 3 klawiszy)";
    ls->shortcutTooltipAlsoBound = "Przypisane również do: ";
    ls->keyboardKeyFormat       = "klawisz %s";
    ls->shortcutScheme          = "Układ skrótów:";
    ls->shortcutCartInsert1     = "Włóż kartridż 1";
    ls->shortcutCartRemove1     = "Wyjmij kartridż 1";
    ls->shortcutCartInsert2     = "Włóż kartridż 2";
    ls->shortcutCartRemove2     = "Wyjmij kartridż 2";
    ls->shortcutSpecialMenu1    = "Wyświetl specjalne menu 1-go kartridża";
    ls->shortcutSpecialMenu2    = "Wyświetl specjalne menu 2-go kartridża";
    ls->shortcutCartAutoReset   = "Resetuj emulator przy wkładaniu kartridża";
    ls->shortcutDiskInsertA     = "Włóż dyskietkę A";
    ls->shortcutDiskDirInsertA  = "Podłącz folder jako dyskietkę A";
    ls->shortcutDiskRemoveA     = "Wyjmij dyskietkę A";
    ls->shortcutDiskChangeA     = "Szybka zmiana dyskietki A";
    ls->shortcutDiskAutoResetA  = "Resetuj emulator przy wkładaniu dyskietki A";
    ls->shortcutDiskInsertB     = "Włóż dyskietkę B";
    ls->shortcutDiskDirInsertB  = "Podłącz folder jako dyskietkę B";
    ls->shortcutDiskRemoveB     = "Wyjmij dyskietkę B";
    ls->shortcutCasInsert       = "Włóż kasetę";
    ls->shortcutCasEject        = "Wyjmij kasetę";
    ls->shortcutCasAutorewind   = "Przełącz auto-przewijanie kasety";
    ls->shortcutCasReadOnly     = "Przełącz kasetę na 'tylko do odczytu'";
    ls->shortcutCasSetPosition  = "Ustaw pozycję kasety";
    ls->shortcutCasRewind       = "Przewiń kasetę";
    ls->shortcutCasSave         = "Zapisz obraz kasety";
    ls->shortcutPrnFormFeed     = "Wysuń kartkę z drukarki";
    ls->shortcutCpuStateLoad    = "Wczytaj stan CPU";
    ls->shortcutCpuStateSave    = "Zapisz stan CPU";
    ls->shortcutCpuStateQload   = "Szybkie wczytanie stanu CPU";
    ls->shortcutCpuStateQsave   = "Szybki zapis stanu CPU";
    ls->shortcutAudioCapture    = "Uruchom/zatrzymaj zapis dźwięku";
    ls->shortcutScreenshotOrig  = "Zapisanie zrzutu ekranu";
    ls->shortcutScreenshotSmall = "Mały, niefiltrowany zrzut ekranu";
    ls->shortcutScreenshotLarge = "Duży, niefiltrowany zrzut ekranu";
    ls->shortcutQuit            = "Wyjście z blueMSX+";
    ls->shortcutRunPause        = "Uruchom/wstrzymaj emulację";
    ls->shortcutStop            = "Zatrzymaj emulację";
    ls->shortcutResetHard       = "Twardy Reset";
    ls->shortcutResetSoft       = "Miękki Reset";
    ls->shortcutResetClean      = "Ogólny Reset";
    ls->shortcutSize1x          = "Ustaw rozmiar okna 1x";
    ls->shortcutSize2x          = "Ustaw rozmiar okna 2x";
    ls->shortcutSize3x          = "Ustaw rozmiar okna 3x";
    ls->shortcutSize4x          = "Ustaw rozmiar okna 4x";
    ls->shortcutSize5x          = "Ustaw rozmiar okna 5x";
    ls->shortcutSize6x          = "Ustaw rozmiar okna 6x";
    ls->shortcutSize7x          = "Ustaw rozmiar okna 7x";
    ls->shortcutSize8x          = "Ustaw rozmiar okna 8x";
    ls->shortcutSizeFullscreen  = "Ustaw pełny ekran";
    ls->shortcutYm2413BackendCycle = "Przełącz backend MSX-MUSIC";
    ls->shortcutY8950BackendCycle  = "Przełącz backend MSX-AUDIO";
    ls->shortcutSizeMinimized   = "Minimalizuj okno";
    ls->shortcutToggleFullscren = "Przełączaj pełny ekran";
    ls->shortcutVolumeIncrease  = "Podgłośnij dźwięk";
    ls->shortcutVolumeDecrease  = "Ścisz dźwięk";
    ls->shortcutVolumeMute      = "Wyłącz dźwięk";
    ls->shortcutVolumeStereo    = "Przełącz mono/stereo";
    ls->shortcutSwitchMsxAudio  = "Przełącznik MSX-AUDIO";
    ls->shortcutSwitchFront     = "Przełącznik główny Panasonic";
    ls->shortcutSwitchPause     = "Pauza";
    ls->shortcutToggleMouseLock = "Podłącz/odłącz mysz PC do MSX";
    ls->shortcutEmuSpeedMax     = "Maksymalna prędkość emulacji";
    ls->shortcutEmuPlayReverse  = "Przewiń emulację wstecz";               // New in 2.8.3
    ls->shortcutEmuSpeedToggle  = "Przełącz maksymalną prędkość emulacji";
    ls->shortcutEmuSpeedNormal  = "Normalna prędkość emulacji";
    ls->shortcutEmuSpeedInc     = "Zwiększ prędkość emulacji";
    ls->shortcutEmuSpeedDec     = "Zmniejsz prędkość emulacji";
    ls->shortcutThemeSwitch     = "Zmień temat :)";
    ls->shortcutShowEmuProp     = "Wyświetl okno właściwości";
    ls->shortcutShowVideoProp   = "Wyświetl ustawienia obrazu";
    ls->shortcutShowAudioProp   = "Wyświetl ustawienia dźwięku";
    ls->shortcutShowCtrlProp    = "Wyświetl ustawienia sterowania";
    ls->shortcutShowEffectsProp = "Pokaż właściwości efektów";     // New in 2.9
    ls->shortcutShowSettProp    = "Wyświetl ustawienia";
    ls->shortcutShowPorts       = "Wyświetl właściwości portów";
    ls->shortcutShowLanguage    = "Wyświetl ustawienia języka";
    ls->shortcutShowMachines    = "Wyświetl Edytor Komputerów";
    ls->shortcutShowShortcuts   = "Wyświetl Edytor Skrótów";
    ls->shortcutShowKeyboard    = "Pokaż edytor klawiatury";
    ls->shortcutShowMixer       = "Pokaż mikser";
    ls->shortcutShowDebugger    = "Pokaż Debugger";
    ls->shortcutShowTrainer     = "Wyświetl Trainer";
    ls->shortcutShowHelp        = "Wyświetl Pomoc";
    ls->shortcutShowAbout       = "Wyświetl informacje O programie";
    ls->shortcutShowFiles       = "Pokaż właściwości pliku";
    ls->shortcutToggleSpriteEnable = "Pokaż/ukryj duszki";
    ls->shortcutToggleFdcTiming = "Przełącz przyspieszenie FDD";
    ls->shortcutToggleHddSdBoost = "Przełącz przyspieszenie HDD/SD";
    ls->shortcutToggleNoSpriteLimits = "Przełącz limit duszków";                 // New in 2.9
    ls->shortcutEnableMsxKeyboardQuirk = "Emuluj specyfikę klawiatury MSX";              // New in 2.9
    ls->shortcutToggleCpuTrace  = "Wł./wył. śledzenie CPU";
    ls->shortcutVideoLoad       = "Powtórka: Wczytaj z pliku";             // New in 2.6
    ls->shortcutVideoPlay       = "Powtórka: Odtwórz ostatnią";   // New in 2.6
    ls->shortcutVideoRecord     = "Powtórka: Nagraj";              // New in 2.6
    ls->shortcutVideoStop       = "Powtórka: Zatrzymaj";                // New in 2.6
    ls->shortcutVideoRender     = "Powtórka: Eksportuj do wideo";   // New in 2.6


    //----------------------
    // Keyboard config lines
    //----------------------

    ls->keyconfigSelectedKey    = "Wybrany klawisz:";
    ls->keyconfigMappedTo       = "Zmapowany na:";
    ls->keyconfigMappingScheme  = "Schemat mapowania:";

    
    //----------------------
    // Rom type lines
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
    ls->romTypeSvi328RsIde      = "SVI-328 RS IDE";
    ls->romTypeSvi328Prn        = "SVI-328 Printer";
    ls->romTypeSvi328Uart       = "SVI-328 Serial Port";
    ls->romTypeSvi328col80      = "SVI-328 80 Column Card";
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
