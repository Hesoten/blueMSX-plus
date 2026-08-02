/*****************************************************************************
** $Source: /cvsroot/bluemsx/blueMSX/Src/Language/LanguageItalian.h,v $
**
** $Revision: 1.74 $
**
** $Date: 2011/01/28 02:30:00 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2008 Daniel Vik
** Italian translation by Luca Chiodi (KdL)
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
#ifndef LANGUAGE_ITALIAN_H
#define LANGUAGE_ITALIAN_H

#include "LanguageStrings.h"
 
void langInitItalian(LanguageStrings* ls) 
{
    //----------------
    // Language lines
    //----------------

    ls->langCatalan                     = "Catalano";
    ls->langChineseSimplified           = "Cinese semplificato";
    ls->langChineseTraditional          = "Cinese tradizionale";
    ls->langDutch                       = "Olandese";
    ls->langEnglish                     = "Inglese";
    ls->langFinnish                     = "Finlandese";
    ls->langFrench                      = "Francese";
    ls->langGerman                      = "Tedesco";
    ls->langItalian                     = "Italiano";
    ls->langJapanese                    = "Giapponese";
    ls->langKorean                      = "Coreano";
    ls->langPolish                      = "Polacco";
    ls->langPortuguese                  = "Portoghese";
    ls->langRussian                     = "Russo";            // v2.8
    ls->langSpanish                     = "Spagnolo";
    ls->langSwedish                     = "Svedese";


    //---------------
    // Generic lines
    //---------------

    ls->textDevice                      = "Origine:";
    ls->textFilename                    = "Nome del file:";
    ls->textFile                        = "File";
    ls->textNone                        = "Nessuna";
    ls->textUnknown                     = "Sconosciuta";


    //-------------------------
    // Warning and Error lines
    //-------------------------

    ls->warningTitle                    = "blueMSX+ - Attenzione";
    ls->warningDiscardChanges           = "Vuoi veramente annullare le modifiche effettuate?";
    ls->warningOverwriteFile            = "Vuoi veramente sovrascrivere il file: ";
    ls->warningStateOldFormat           = "Salvataggio di stato in formato vecchio. Potrebbe non riprendere correttamente. Caricare comunque?";
    ls->errorTitle                      = "blueMSX+ - Errore";
    ls->errorEnterFullscreen            = "Non riesco ad attivare la visualizzazione a schermo intero.           \n";
    ls->errorDirectXFailed              = "Non riesco a creare gli oggetti DirectX.          \nHo selezionato automaticamente le GDI.\nVerifica le proprietà video.";
    ls->errorNoRomInZip                 = "Nessun file .ROM trovato nell'archivio compresso.";
    ls->errorNoDskInZip                 = "Nessun file .DSK trovato nell'archivio compresso.";
    ls->errorCreateDiskImage            = "Impossibile creare il file immagine disco.";
    ls->errorCreateTapeImage            = "Impossibile creare il file immagine cassetta.";
    ls->errorNoCasInZip                 = "Nessun file .CAS trovato nell'archivio compresso.";
    ls->errorDirAsDskOverflow           = "%d file (%d KB totali) non sono entrati nell'immagine disco da 720 KB e sono stati saltati.";
    ls->errorNoHelp                     = "Non trovo il file della guida in linea di blueMSX+.";
    ls->errorStartEmu                   = "Avvio dell'emulatore fallito!";
    ls->errorStartEmuNoMachine          = "Nessuna macchina MSX impostata per l'emulazione. Sceglietene una da Opzioni -> Emulazione -> Modello emulato.";
    ls->errorStartEmuMachinesDirMissing = "La cartella Machines non è stata trovata.";
    ls->errorStartEmuMachineNotFound    = "La configurazione di macchina selezionata '%s' non è stata trovata.";
    ls->errorStartEmuConfigInvalid      = "Impossibile leggere config.ini della configurazione di macchina '%s'. Il file potrebbe essere danneggiato o provenire da una versione incompatibile.";
    ls->errorMissingFiles               = "Non è stato possibile caricare i seguenti file:";
    ls->errorPortableReadonly           = "Il dispositivo rimovibile è di sola lettura";
    ls->errorMidiOpenFailed             = "Impossibile aprire il dispositivo MIDI '%s'. Potrebbe essere usato da un'altra applicazione.";
    ls->infoTitle                       = "Info blueMSX+";
    ls->infoGameReaderRedirect          = "blueMSX+ non supporta direttamente MSX Game Reader (il driver originale ASCII dell'era XP non funziona più su Windows moderno).\n\nAprire MSX Game Reader - Web Dumper (di Kunihiko Ohnaka) nel browser invece?";
    ls->infoColorDepth                  = "blueMSX+ funziona al meglio con una profondità di colore di 16 o 32 bit.";
    ls->errorKeyboardThemeMissing       = "Impossibile trovare il tema dell'editor di tastiera.";
    ls->errorMixerThemeMissing          = "Impossibile trovare il tema del mixer.";
    ls->errorRecorderTitle              = "blueMSX+ - Registratore";
    ls->errorRecorderSaveReplay         = "Impossibile salvare il file replay:\n  %s\n\nVerifica che la cartella di destinazione esista e sia scrivibile.";
    ls->errorRecorderReplayMissing      = "File replay non trovato:\n  %s\n\nRegistra prima un replay, oppure usa Carica per scegliere un file .cap esistente.";
    ls->errorRecorderRequiresDX12   = "Passare al driver video Direct3D 12 e avviare la registrazione?";
    ls->errorRecorderRequiresDX12Title = "blueMSX+ - Cambio del driver video";
    ls->infoRecorderComplete            = "File video salvato:\n  %s";
    ls->infoToastSaved                  = "Salvato: %s";
    ls->infoToastAlreadyRecording   = "Già in registrazione";
    ls->infoToastMouseConnected    = "Mouse PC collegato all'MSX";
    ls->infoToastMouseDisconnected = "Mouse PC scollegato dall'MSX";
    ls->propControlsMouseSens      = "Sensibilità:";
    ls->dlgRecorderPickTitle        = "blueMSX+ - Converti replay in video";
    ls->dlgRecorderPickSourceCap    = "File replay da convertire (.cap):";
    ls->dlgRecorderPickOutputMp4    = "File video di uscita (.mp4):";
    ls->menuFileRecordVideo         = "Registra video";
    ls->menuFileStopRecordVideo     = "Ferma registrazione video";
    ls->shortcutRecordVideoStart    = "Registrazione video: Avvia";
    ls->shortcutRecordVideoStartAs  = "Registrazione video: Avvia come";
    ls->shortcutRecordVideoStop     = "Registrazione video: Ferma";
    ls->shortcutRecordVideoToggle   = "Registrazione video: Commuta";
    ls->shortcutAudioCaptureAs      = "Registrazione audio: Avvia come";
    ls->shortcutVideoRecordAs       = "Replay: Registra come";
    ls->shortcutScreenshotAs        = "Schermata: Salva con nome";

    ls->propCapture                 = "Cattura";
    ls->propCaptureAudioGB          = " Registrazione audio ";
    ls->propCaptureVideoGB          = " Registrazione video ";
    ls->propCaptureScreenshotGB     = " Schermata ";
    ls->propCaptureReplayGB         = " Registrazione replay ";
    ls->propCaptureSaveDir          = "Cartella:";
    ls->propCaptureFormat           = "Formato:";
    ls->propCaptureCodec            = "Codec:";
    ls->propCaptureAutoName         = "Nome file automatico";
    ls->propCapturePromptName       = "Chiedi nome file";
    ls->propCaptureShowToast        = "Mostra avviso al termine";


    //--------------------
    // File related lines
    //--------------------

    ls->fileRom                         = "Immagine ROM";
    ls->fileAll                         = "Tutti i file";
    ls->fileCpuState                    = "Stato della CPU";
    ls->fileVideoCapture                = "Video acquisito";                                        
    ls->fileDisk                        = "Immagine disco";
    ls->fileCas                         = "Immagine cassetta";
    ls->fileAvi                         = "Filmato";                                                


    //--------------------
    // Menu related lines
    //--------------------

    ls->menuNoRecentFiles               = "- nessun file recente -";
    ls->menuInsert                      = "Inserisci...";
    ls->menuEject                       = "Espelli";

    ls->menuCartGameReader              = "Game Reader";
    ls->menuCartIde                     = "IDE";
    ls->menuCartBeerIde                 = "Beer";
    ls->menuCartGIde                    = "GIDE";
    ls->menuCartSunriseIde              = "Sunrise";  
    ls->menuCartScsi                    = "SCSI";                // New in 2.7
    ls->menuCartMegaSCSI                = "MEGA-SCSI";           // New in 2.7
    ls->menuCartWaveSCSI                = "WAVE-SCSI";           // New in 2.7
    ls->menuCartGoudaSCSI               = "Gouda SCSI";          // New in 2.7
    ls->menuJoyrexPsg                   = "Cartuccia Joyrex PSG"; // New in 2.9
    ls->menuCartSCC                     = "Cartuccia SCC";
    ls->menuCartSCCPlus                 = "Cartuccia SCC-I";
    ls->menuCartFMPac                   = "Cartuccia FM-PAC";
    ls->menuCartPac                     = "Cartuccia PAC";
    ls->menuCartHBI55                   = "Cartuccia Sony HBI-55";
    ls->menuCartInsertSpecial           = "Inserisci speciale";
    ls->menuCartMegaRam                 = "MegaRAM";
    ls->menuCartExternalRam             = "Cartuccia RAM esterna";
    ls->menuCartEseRam                  = "Ese-RAM";             // New in 2.7
    ls->menuCartEseSCC                  = "Ese-SCC";             // New in 2.7
    ls->menuCartMegaFlashRom            = "Mega Flash ROM";      // New in 2.7
    ls->menuCartFlashCart               = "Cartucce Flash";

    ls->menuDiskInsertNew               = "Inserisci un nuovo disco...";
    ls->menuDiskInsertCdrom             = "Collega l'unità CD-ROM";       // New in 2.7
    ls->menuDiskDirInsert               = "Inserisci una cartella...";
    ls->menuDiskAutoStart               = "Riavvia quando inserisci";
    ls->menuCartAutoReset               = "Riavvia quando inserisci/rimuovi";

    ls->menuCasInsertNew                = "Inserisci una nuova cassetta...";
    ls->menuCasRewindAfterInsert        = "Riavvolgi quando inserisci";
    ls->menuCasUseReadOnly              = "Simula una cassetta di sola lettura";
    ls->lmenuCasSaveAs                  = "Salva la cassetta come...";
    ls->menuCasSetPosition              = "Imposta la posizione del nastro...";
    ls->menuCasRewind                   = "Riavvolgi";

    ls->menuVideoLoad                   = "Carica...";                                              
    ls->menuVideoPlay                   = "Riproduci l'ultimo video acquisito";                     
    ls->menuVideoRecord                 = "Acquisisci";                                             
    ls->menuVideoRecording              = "Salvataggio in corso";                                   
    ls->menuVideoRecAppend              = "Acquisisci (accoda)";                                    
    ls->menuVideoStop                   = "Interrompi";                                                  
    ls->menuVideoRender                 = "Crea un filmato...";                                     

    ls->menuPrnFormfeed                 = "Salta alla pagina successiva";

    ls->menuZoom1x                      = "Finestra 1x";
    ls->menuZoom2x                      = "Finestra 2x";
    ls->menuZoom3x                      = "Finestra 3x";
    ls->menuZoom4x                      = "Finestra 4x";
    ls->menuZoom5x                      = "Finestra 5x";
    ls->menuZoom6x                      = "Finestra 6x";
    ls->menuZoom7x                      = "Finestra 7x";
    ls->menuZoom8x                      = "Finestra 8x";
    ls->menuZoomFullscreen              = "Schermo intero";
    
    ls->menuPropsEmulation              = "Emulazione...";
    ls->menuPropsVideo                  = "Video...";
    ls->menuPropsSound                  = "Audio...";
    ls->menuPropsMidi                   = "MIDI";
    ls->menuPropsControls               = "Controlli...";
    ls->menuPropsEffects        = "Effetti";               // New in 2.9
    ls->menuPropsSettings               = "Impostazioni...";
    ls->menuPropsFile                   = "File...";
    ls->menuPropsDisk                   = "Dischi...";               // New in 2.7
    ls->menuPropsLanguage               = "Lingua...";
    ls->menuPropsPorts                  = "Porte...";
    ls->menuPropsCapture                = "Cattura";

    ls->menuVideoSource                 = "Origine uscita video";
    ls->menuVideoSourceDefault          = "Chip video non rilevato";
    ls->menuVideoChipAutodetect         = "Selezione automatica del chip video";
    ls->menuVideoInSource               = "Origine ingresso video";
    ls->menuVideoInBitmap               = "Bitmap come ingresso video...";

    ls->menuEthInterface                = "Interfaccia di rete";                                   

    ls->menuHelpHelp                    = "Guida in linea...";
    ls->menuHelpAbout                   = "Informazioni su blueMSX+...";

    ls->menuFileCart                    = "Slot di espansione";
    ls->menuFileDisk                    = "Unità disco floppy";
    ls->menuFileCas                     = "Cassetta";
    ls->menuFilePrn                     = "Stampante";
    ls->menuFileLoadState               = "Carica lo stato della CPU...";
    ls->menuFileSaveState               = "Salva lo stato della CPU...";
    ls->menuFileQLoadState              = "Carica rapidamente lo stato";
    ls->menuFileQSaveState              = "Salva rapidamente lo stato";
    ls->menuFileCaptureAudio            = "Acquisisci il flusso audio";
    ls->menuFileStopAudio               = "Ferma audio";
    ls->menuFileCaptureVideo            = "Flusso video";                                           
    ls->menuFileScreenShot              = "Salva una schermata";
    ls->menuFileExit                    = "Esci";

    ls->menuFileHarddisk                = "Disco rigido / Scheda SD";
    ls->menuFileHarddiskNoPesent        = "Controller non presente";
    ls->menuFileHarddiskRemoveAll       = "Espelli tutti i dispositivi";    // New in 2.7

    ls->menuRunRun                      = "Esegui";
    ls->menuRunPause                    = "Sospendi";
    ls->menuRunStop                     = "Interrompi";
    ls->menuRunSoftReset                = "Riavvia software";
    ls->menuRunHardReset                = "Riavvia hardware";
    ls->menuRunCleanReset               = "Espelli e riavvia";

    ls->menuToolsMachine                = "Gestione hardware...";
    ls->menuToolsShortcuts              = "Gestione tasti rapidi...";
    ls->menuToolsCtrlEditor             = "Periferiche di ingresso...";               
    ls->menuToolsMixer                  = "Mixer audio...";
    ls->menuToolsLoadMemory             = "Carico di memoria";
    ls->menuToolsDebugger               = "Debugger...";
    ls->menuToolsTrainer                = "Trainer...";
    ls->menuToolsTraceLogger            = "Trace logger...";

    ls->menuFile                        = "File";
    ls->menuRun                         = "Emulazione";
    ls->menuWindow                      = "Finestra";
    ls->menuOptions                     = "Opzioni";
    ls->menuTools                       = "Strumenti";
    ls->menuHelp                        = "Aiuto";


    //----------------------
    // Dialog related lines
    //----------------------

    ls->dlgOK                           = "OK";
    ls->dlgOpen                         = "Apri...";
    ls->dlgCancel                       = "Annulla";
    ls->dlgYes                          = "Sì";
    ls->dlgNo                           = "No";
    ls->dlgSave                         = "Salva";
    ls->dlgSaveAs                       = "Salva come...";
    ls->dlgRun                          = "Esegui";
    ls->dlgClose                        = "Chiudi";

    ls->dlgLoadRom                      = "blueMSX+ - Seleziona un'immagine ROM da caricare";
    ls->dlgLoadDsk                      = "blueMSX+ - Seleziona un'immagine disco da caricare";
    ls->dlgLoadCas                      = "blueMSX+ - Seleziona un'immagine cassetta da caricare";
    ls->dlgLoadRomDskCas                = "blueMSX+ - Seleziona un'immagine ROM, disco, o cassetta da caricare";
    ls->dlgLoadRomDesc                  = "Scegli un'immagine ROM da caricare:";
    ls->dlgLoadDskDesc                  = "Scegli un'immagine disco da caricare:";
    ls->dlgLoadCasDesc                  = "Scegli un'immagine cassetta da caricare:";
    ls->dlgLoadRomDskCasDesc            = "Scegli un'immagine ROM, disco, o cassetta da caricare:";
    ls->dlgLoadState                    = "Carica lo stato della CPU";
    ls->dlgLoadVideoCapture             = "Carica un video acquisito";                              
    ls->dlgSaveState                    = "Salva lo stato della CPU";
    ls->dlgSaveCassette                 = "blueMSX+ - Salva la cassetta come file immagine";
    ls->dlgSaveVideoClipAs              = "Salva il filmato con nome";                              
    ls->dlgSaveCaptureAudio             = "Salva registrazione audio come";
    ls->dlgSaveCaptureVideo             = "Salva registrazione video come";
    ls->dlgSaveCaptureReplay            = "Salva replay come";
    ls->dlgSaveCaptureScreenshot        = "Salva schermata come";
    ls->dlgAmountCompleted              = "Salvataggio in corso:";                                  
    ls->dlgInsertRom1                   = "Inserisci una cartuccia nello slot 1";
    ls->dlgInsertRom2                   = "Inserisci una cartuccia nello slot 2";
    ls->dlgInsertDiskA                  = "Inserisci un disco nell'unità A";
    ls->dlgInsertDiskB                  = "Inserisci un disco nell'unità B";
    ls->dlgInsertHarddisk               = "Inserisci un disco rigido";
    ls->dlgInsertCas                    = "Inserisci una cassetta nel registratore";
    ls->dlgCreateCas                    = "Crea una nuova cassetta";
    ls->dlgRomType                      = "Tipo ROM:";
    ls->dlgDiskSize                     = "Dimensione:";                                            

    ls->dlgTapeTitle                    = "blueMSX+ - Imposta la posizione del nastro";
    ls->dlgTapeFrameText                = "Posizione del nastro";
    ls->dlgTapeCurrentPos               = "Posizione corrente";
    ls->dlgTapeTotalTime                = "Durata complessiva";
    ls->dlgTapeCustom                   = "Mostra i file personalizzati";
    ls->dlgTapeSetPosText               = "Posizione del nastro:";
    ls->dlgTabPosition                  = "Posizione";
    ls->dlgTabType                      = "Tipo";
    ls->dlgTabFilename                  = "Nome del file";
    ls->dlgZipReset                     = "Riavvia quando inserisci";

    ls->dlgAboutTitle                   = "blueMSX+ - Informazioni";

    ls->dlgLangLangText                 = "Scegli una lingua per l'interfaccia di blueMSX+";
    ls->dlgLangLangTitle                = "blueMSX+ - Lingua";

    ls->dlgAboutAbout                   = "INFORMAZIONI\r\n==========";
    ls->dlgAboutVersion                 = "Versione:";
    ls->dlgAboutBuildNumber             = "Build:";
    ls->dlgAboutBuildDate               = "Data:";
    ls->dlgAboutCreat                   = "Realizzato da Daniel Vik";
    ls->dlgAboutDevel                   = "SVILUPPATORI\r\n=========";
    ls->dlgAboutThanks                  = "HANNO CONTRIBUITO\r\n==============";       // New in 2.7 (retranslate, see english)
    ls->dlgAboutLisence                 = "TRADUZIONE ITALIANA\r\n===============\r\n\r\nLuca Chiodi (KdL)\r\n\r\n\r\n"
                                          "LICENZA\r\n=====\r\n\r\n"
                                          "Questo programma è fornito \"così com'è\", senza alcuna esplicita o implicita "
                                          "garanzia. In nessun caso l'autore potrà essere ritenuto responsabile per qualunque "
                                          "danno derivante dall'uso di questo programma.\r\n\r\n"
                                          "Visita il sito www.bluemsx.com per maggiori dettagli.";

    ls->dlgSavePreview                  = "Visualizza anteprima";
    ls->dlgSaveDate                     = "Data:";

    ls->dlgRenderVideoCapture           = "blueMSX+ - Elaborazione in corso...";                     


    //--------------------------
    // Properties related lines
    //--------------------------

    ls->propTitle                       = "blueMSX+ - Proprietà";
    ls->propEmulation                   = "Emulazione";
    ls->propD3D                         = "Direct3D";
    ls->propVideo                       = "Video";
    ls->propSound                       = "Audio";
    ls->propMidi                        = "MIDI";
    ls->propControls                    = "Controlli";
    ls->propPerformance                 = "Prestazioni";
    ls->propEffects                     = "Effetti";             // New in 2.9
    ls->propSettings                    = "Impostazioni";
    ls->propFile                        = "File";
    ls->propDisk                        = "Dischi";              // New in 2.7
    ls->propPorts                       = "Porte";

    ls->propEmuGeneralGB                = "Generale ";
    ls->propEmuFamilyText               = "Modello emulato:";
    ls->propEmuMemoryGB                 = "Memoria ";
    ls->propEmuRamSizeText              = "Dimensione della RAM:";
    ls->propEmuVramSizeText             = "Dimensione della VRAM:";
    ls->propEmuSpeedGB                  = "Velocità di emulazione ";
    ls->propEmuSpeedText                = "Core emulatore:";
    ls->propEmuVdpCmdSpeedText  = "Attesa comandi VDP:";
    ls->propEmuFrontSwitchGB            = "Opzioni Panasonic ";
    ls->propEmuFrontSwitch              = " Interruttore frontale";
    ls->propEmuNoSpriteLimits   = " Disabilita limite sprite";  // New in 2.9
    ls->propEnableMsxKeyboardQuirk = " Emula peculiarità tastiera MSX";  // New in 2.9
    ls->propEmuBoostText                = "Accelera l'accesso ai dispositivi:";
    ls->propEmuFdcTiming                = " FDD";
    ls->propEmuCasBoost                 = " Cassetta";
    ls->propEmuHddSdBoost               = " Scheda HDD/SD";
    ls->propEmuReversePlay              = " Abilita la riproduzione a ritroso"; // New in 2.8.3
    ls->propEmuPauseSwitch              = " Tasto pausa";
    ls->propEmuAudioSwitch              = " Cartuccia MSX-AUDIO";
    ls->propVideoFreqText               = "Frequenza video:";
    ls->propVideoFreqAuto               = "Automatica";
    ls->propSndOversampleText           = "Sovracampionamento:";
    ls->propSndYkInGB                   = "Ingresso YK-01/YK-10/YK-20 ";
    ls->propSndMidiInGB                 = "Ingresso MIDI ";
    ls->propSndMidiOutGB                = "Uscita MIDI ";
    ls->propSndMidiChannel              = "Canale MIDI:";
    ls->propSndMidiAll                  = "Tutti";

    ls->propMonMonGB                    = "Monitor ";
    ls->propMonTypeText                 = "Tipo di schermo:";
    ls->propMonEmuText                  = "Segnale video emulato:";
    ls->propVideoTypeText               = "Video:";
    ls->propMonHorizStretch             = " Allungamento orizzontale";
    ls->propMonVertStretch              = " Allungamento verticale";
    ls->propMonDeInterlace              = " Deinterlacciato";
    ls->propBlendFrames                 = " Fusione fotogrammi";
    ls->propMonBrightness               = "Luminosità:";
    ls->propMonContrast                 = "Contrasto:";
    ls->propMonSaturation               = "Saturazione:";
    ls->propMonGamma                    = "Gamma:";
    ls->propMonScanlines                = " Scanline:";
    ls->propMonScanlinesBright          = "Comp. lumin.:";
    ls->propMonScanlinesBrightAuto         = " Auto";
    ls->propMonScanlinesShape           = "Preimp.:";
    ls->propMonScanlinesDepth           = "Profondità:";
    ls->propMonScanlinesSharpness         = "Nitidezza:";
    ls->enumScanShapeGentle             = "Morbido";
    ls->enumScanShapeStandard           = "Standard";
    ls->enumScanShapeSharp              = "Nitido";
    ls->enumScanShapeTrinitron          = "Trinitron";
    ls->enumScanShapeCustom             = "Person.";
    ls->propMonHdrEnable                = "HDR";
    ls->propMonHdrPaperWhite            = "Luminosità bianco:";
    ls->propMonHdrSystemMode            = "Modalità HDR sistema:";
    ls->propMonHdrRestartHint           = "Riavvia blueMSX per applicare il cambio di modalità HDR.";
    ls->propMonHdrRecord                = " Registra in HDR";
    ls->propMonColorGhosting            = " Modulatore-RF:";
    ls->propMonEffectsGB                = "Effetti ";

    ls->propPerfVideoDrvGB              = "Video ";
    ls->propPerfVideoDispDrvText        = "Driver corrente:";
    ls->propPerfFrameSkipText           = "Salto fotogrammi:";
    ls->propPerfAudioDrvGB              = "Audio ";
    ls->propPerfAudioDrvText            = "Driver corrente:";
    ls->propPerfAudioBufSzText          = "Durata del buffer:";
    ls->propPerfAudioBufSzActualFmt     = "(buffer reale: %u ms)";
    ls->propPerfEmuGB                   = "Emulazione ";
    ls->propPerfSyncModeText            = "Modalità di sincronizzazione:";
    ls->propFullscreenResText           = "Risoluzione a schermo intero:";

    ls->propSndChipEmuGB                = "Emulazione chip sonoro ";
    ls->propSoundChipsActive            = "Backend attivo:";
    ls->propSoundChipsHint              = "Più backend attivi permettono confronto A/B in tempo reale.";
    ls->propSoundChipsYm2413GB          = " Backend MSX-MUSIC ";
    ls->propSoundChipsY8950GB           = " Backend MSX-AUDIO ";
    ls->propSndOpllAnalogText           = "Filtro analogico:";
    ls->propSndOpllAnalogLpfText        = "Taglio LPF:";
    ls->enumOpllFilterOff               = "Spento";
    ls->enumOpllFilterBright            = "Brillante (LPF 12 kHz)";
    ls->enumOpllFilterClear             = "Chiaro (LPF 8 kHz)";
    ls->enumOpllFilterStandard          = "Standard (LPF 5 kHz)";
    ls->enumOpllFilterSoft              = "Morbido (LPF 3.5 kHz)";
    ls->enumOpllFilterMellow            = "Caldo (LPF 2.3 kHz)";
    ls->enumOpllFilterCustom            = "Person.";
    ls->propSndMsxMusic                 = " MSX-MUSIC";
    ls->propSndMsxAudio                 = " MSX-AUDIO";
    ls->propSndMoonsound                = " Moonsound";
    ls->propSndMt32ToGm                 = " Accorda gli strumenti MT-32 come General MIDI";

    ls->propPortsLptGB                  = "Porta parallela ";
    ls->propPortsComGB                  = "Porte seriali ";
    ls->propPortsLptText                = "Porta:";
    ls->propPortsCom1Text               = "Porta 1:";
    ls->propPortsNone                   = "Nessuna";
    ls->propPortsSimplCovox             = "SiMPL / Covox DAC";
    ls->propPortsFile                   = "Stampa su file";
    ls->propPortsComFile                = "Invia a file";
    ls->propPortsOpenLogFile            = "Apri un file di rapporto";
    ls->propPortsEmulateMsxPrn          = "Emulazione:";

    ls->propSetFileHistoryGB            = "File recenti ";
    ls->propSetFileHistorySize          = "Numero di file recenti:";
    ls->propSetFileHistoryClear         = "Svuota la cronologia";
    ls->propFileTypes                   = " Registra .rom/.dsk/.cas/.sta nel menu \"Apri con\"";
    ls->propOpenDefaultApps             = "Apri le impostazioni delle app predefinite di Windows";
    ls->propWindowsEnvGB                = "Ambiente Windows ";
    ls->propSetScreenSaver              = " Schermo sempre attivo (blocca screensaver/sospensione)";
    ls->propPriorityBoost               = " Usa lo scheduler giochi di Windows (MMCSS) per l'emulazione";
    ls->propScreenshotPng               = " Utilizza il formato PNG per il salvataggio delle schermate";
    ls->propEjectMediaOnExit            = " Espelli tutti i supporti quando esci da blueMSX+";                      // New in 2.8
    ls->propClearHistory                = "Vuoi veramente svuotare la cronologia dei file recenti?";
    ls->propOpenRomGB                   = "Esecuzione delle immagini ROM ";
    ls->propDefaultRomType              = "Tipo predefinito:";
    ls->propGuessRomType                = "Selezione automatica";

    ls->propSettDefSlotGB               = "Trascinamento ";
    ls->propSettDefSlots                = "Inserisci la cartuccia nello:";
    ls->propSettDefSlot                 = " Slot";
    ls->propSettDefDrives               = "Inserisci il disco nella:";
    ls->propSettDefDrive                = " Unità";

    ls->propThemeGB                     = "Temi ";
    ls->propTheme                       = "Tema corrente:";

    ls->propCdromGB                     = "Unità CD-ROM ";       // New in 2.7
    ls->propCdromMethod                 = "Metodo di accesso:";  // New in 2.7
    ls->propCdromMethodNone             = "Nessuno";             // New in 2.7
    ls->propCdromMethodIoctl            = "IOCTL";               // New in 2.7
    ls->propCdromMethodAspi             = "ASPI";                // New in 2.7
    ls->propCdromDrive                  = "Lettera:";            // New in 2.7

    ls->propD3DParametersGB         = "Parametri ";                // New in 2.9
    ls->propD3DAspectRatioText      = "Proporzioni";               // New in 2.9
    ls->propD3DScalingFilterText     = "Filtro di scala";
    ls->propD3DExtendBorderColorText    = " Estendi colore bordo";   // New in 2.9

    ls->propD3DCroppingGB               = "Ritaglio ";              // New in 2.9
    ls->propD3DCroppingTypeText         = "Tipo di ritaglio:";         // New in 2.9
    ls->propD3DCroppingLeftText         = "Sinistra:";                  // New in 2.9
    ls->propD3DCroppingRightText        = "Destra:";                 // New in 2.9
    ls->propD3DCroppingTopText          = "Alto:";                   // New in 2.9
    ls->propD3DCroppingBottomText       = "Basso:";                // New in 2.9


    //------------------------
    // Dropdown related lines
    //------------------------

    ls->enumVideoMonColor               = "Colore";
    ls->enumVideoMonGrey                = "Bianco e nero";
    ls->enumVideoMonGreen               = "Fosfori verdi";
    ls->enumVideoMonAmber               = "Fosfori ambra";

    ls->enumVideoTypePAL                = "PAL";
    ls->enumVideoTypeNTSC               = "NTSC";

    ls->enumVideoEmuNone                = "Nessuno";
    ls->enumVideoEmuMonitor             = "RGB analogico";
    ls->enumVideoEmuYc                  = "S-Video";
    ls->enumVideoEmuYcBlur              = "S-Video rumoroso";
    ls->enumVideoEmuComp                = "Composito";
    ls->enumVideoEmuCompBlur            = "Composito rumoroso";
    ls->enumVideoEmuScale2x             = "In scala 2:1";
    ls->enumVideoEmuHq2x                = "In scala 2:1 ad alta qualità";


    ls->enumVideoDrvDirectDrawHW        = "DirectDraw HW accelerato";
    ls->enumVideoDrvDirectDraw          = "DirectDraw";
    ls->enumVideoDrvGDI                 = "GDI";
    ls->enumVideoDrvD3D         = "Direct3D";

    ls->enumVideoFrameskip0             = "Nessuno";
    ls->enumVideoFrameskip1             = "1 fotogramma";
    ls->enumVideoFrameskip2             = "2 fotogrammi";
    ls->enumVideoFrameskip3             = "3 fotogrammi";
    ls->enumVideoFrameskip4             = "4 fotogrammi";
    ls->enumVideoFrameskip5             = "5 fotogrammi";

    ls->enumD3DARAuto           = "Auto";           // New in 2.9
    ls->enumD3DARStretch        = "Allunga";        // New in 2.9
    ls->enumD3DARPAL            = "PAL";            // New in 2.9
    ls->enumD3DARNTSC           = "NTSC";           // New in 2.9
    ls->enumD3DAR11             = "1:1";            // New in 2.9
    ls->enumD3DScaleNearest          = "Più vicino";
    ls->enumD3DScaleBilinear         = "Bilineare";
    ls->enumD3DScaleSharp            = "Bilineare nitido";
    ls->enumD3DScalePrescaled     = "Bilineare (2x prescale)";

    ls->enumD3DCropNone         = "Nessuno";           // New in 2.9
    ls->enumD3DCropMSX1         = "MSX1";           // New in 2.9
    ls->enumD3DCropMSX1Plus8    = "MSX1+8";         // New in 2.9
    ls->enumD3DCropMSX2         = "MSX2";           // New in 2.9
    ls->enumD3DCropMSX2Plus8    = "MSX2+8";         // New in 2.9
    ls->enumD3DCropCustom       = "Personalizzato";         // New in 2.9

    ls->enumSoundDrvNone                = "Nessuno";
    ls->enumSoundDrvWMM                 = "Driver WMM";
    ls->enumSoundDrvDirectX             = "Driver DirectX";
    ls->enumSoundDrvWasapi              = "Driver WASAPI";

    ls->enumEmuSync1ms                  = "Sincrona su refresh MSX";
    ls->enumEmuSyncAuto                 = "Automatica (veloce)";
    ls->enumEmuSyncNone                 = "Nessuna";
    ls->enumEmuSyncVblank               = "Sincrona a Vblank PC";
    ls->enumEmuAsyncVblank              = "Vblank PC asincrona";

    ls->enumControlsJoyNone             = "Nessuno";
    ls->enumControlsJoyMouse            = "Mouse a 2 tasti";
    ls->enumControlsJoyTetris2Dongle    = "Chiave di protezione Tetris 2";
    ls->enumControlsJoyTMagicKeyDongle  = "Chiave di protezione MagicKey";
    ls->enumControlsJoy2Button          = "Joystick/Gamepad a 2 tasti";
    ls->enumControlsJoyGunstick         = "Gun Stick";
    ls->enumControlsJoyAsciiLaser       = "ASCII Plus-X Terminator Laser";
    ls->enumControlsArkanoidPad         = "Arkanoid Pad";                   // New in 2.7.1
    ls->enumControlsJoyColeco           = "Joystick ColecoVision";

    ls->enumDiskMsx35Dbl9Sect           = "MSX 3.5\" doppia faccia, 9 settori";
    ls->enumDiskMsx35Dbl8Sect           = "MSX 3.5\" doppia faccia, 8 settori";
    ls->enumDiskMsx35Sgl9Sect           = "MSX 3.5\" singola faccia, 9 settori";
    ls->enumDiskMsx35Sgl8Sect           = "MSX 3.5\" singola faccia, 8 settori";
    ls->enumDiskSvi525Dbl               = "SVI-328 5.25\" doppia faccia";
    ls->enumDiskSvi525Sgl               = "SVI-328 5.25\" singola faccia";
    ls->enumDiskSf3Sgl                  = "Sega SF-7000 3\" singola faccia";                        
    ls->enumDiskSize                    = "Dimensione disco";
    ls->enumDiskFormat                  = "Formato:";
    ls->enumDiskFormatUnformatted       = "Non formattato";


    //-----------------------------
    // Configuration related lines
    //-----------------------------

    ls->confTitle                       = "blueMSX+ - Gestione hardware";
    ls->confConfigText                  = "Modello corrente:";
    ls->confSlotLayout                  = "Disposizione degli slot";
    ls->confMemory                      = "Configurazione della memoria";
    ls->confChipEmulation               = "Chip da emulare";
    ls->confChipExtras                  = "Altre opzioni";

    ls->confOpenRom                     = "Seleziona un'immagine ROM";
    ls->confSaveTitle                   = "blueMSX+ - Salva il modello corrente";
    ls->confSaveText                    = "Vuoi veramente sovrascrivere il modello";
    ls->confSaveAsTitle                 = "Salva il modello con nome";
    ls->confSaveAsMachineName           = "Nome del modello:";
    ls->confDiscardTitle                = "blueMSX+ - Configurazione";
    ls->confExitSaveTitle               = "blueMSX+ - Esci dalla gestione hardware";
    ls->confExitSaveText                = "Vuoi veramente annullare le modifiche effettuate?";

    ls->confSlotLayoutGB                = "Disposizione degli slot interni ";
    ls->confSlotExtSlotGB               = "Slot di espansione esterni ";
    ls->confBoardGB                     = "Sistema ";
    ls->confBoardText                   = "Tipo:";
    ls->confSlotPrimary                 = "Primario";
    ls->confSlotExpanded                = "Espanso (4 subslot)";

    ls->confSlotCart                    = "Cartuccia";
    ls->confSlot                        = "Slot";
    ls->confSubslot                     = "Subslot";

    ls->confMemAdd                      = "Aggiungi...";
    ls->confMemEdit                     = "Modifica...";
    ls->confMemRemove                   = "Elimina";
    ls->confMemSlot                     = "Slot";
    ls->confMemAddresss                 = "Indirizzo";
    ls->confMemType                     = "Tipo";
    ls->confMemRomImage                 = "Immagine ROM";
    
    ls->confChipVideoGB                 = "Video ";
    ls->confChipVideoChip               = "Chip:";
    ls->confChipVideoRam                = "VRAM:";
    ls->confChipSoundGB                 = "Audio ";
    ls->confChipPsgStereoText           = " PSG Stereo"; // New in 2.8.3

    ls->confCmosGB                      = "CMOS ";
    ls->confCmosEnable                  = " Abilita il CMOS";
    ls->confCmosBattery                 = " Simula una batteria carica";

    ls->confCpuFreqGB                   = "Frequenza di clock delle CPU ";
    ls->confZ80FreqText                 = "ZiLOG Z80:";
    ls->confR800FreqText                = "RISC R800:";
    ls->confFdcGB                       = "Controller del disco floppy ";
    ls->confCFdcNumDrivesText           = "Numero di unità connesse:";

    ls->confEditMemTitle                = "blueMSX+ - Effettua la mappatura";
    ls->confEditMemGB                   = "Dettagli mappatura ";
    ls->confEditMemType                 = "Tipo:";
    ls->confEditMemFile                 = "File:";
    ls->confEditMemAddress              = "Indirizzo:";
    ls->confEditMemSize                 = "Dimensione:";
    ls->confEditMemSlot                 = "Slot:";


    //----------------
    // Shortcut lines
    //----------------

    ls->shortcutKey                     = "Azione";
    ls->shortcutDescription             = "Tasti rapidi";

    ls->shortcutSaveConfig              = "blueMSX+ - Salva lo schema corrente";
    ls->shortcutOverwriteConfig         = "Vuoi veramente sovrascrivere lo schema";
    ls->shortcutCreateConfig            = "Vuoi salvare la nuova configurazione dei tasti";
    ls->shortcutExitConfig              = "blueMSX+ - Esci dalla gestione tasti rapidi";
    ls->shortcutDiscardConfig           = "Vuoi veramente annullare le modifiche effettuate?";
    ls->shortcutSaveConfigAs            = "blueMSX+ - Salva lo schema con nome";
    ls->shortcutConfigName              = "Nome dello schema:";
    ls->shortcutNewProfile              = "< Nuovo >";
    ls->shortcutConfigTitle             = "blueMSX+ - Gestione tasti rapidi";
    ls->shortcutAssign                  = "Assegna";
    ls->shortcutPressText               = "Combinazione scelta:";
    ls->shortcutScheme                  = "Schema corrente:";
    ls->shortcutCartInsert1             = "Inserisci una cartuccia nello slot 1";
    ls->shortcutCartRemove1             = "Rimuovi la cartuccia dallo slot 1";
    ls->shortcutCartInsert2             = "Inserisci una cartuccia nello slot 2";
    ls->shortcutCartRemove2             = "Rimuovi la cartuccia dallo slot 2";
    ls->shortcutSpecialMenu1            = "Visualizza il menù inserisci speciale dello slot 1";
    ls->shortcutSpecialMenu2            = "Visualizza il menù inserisci speciale dello slot 2";
    ls->shortcutCartAutoReset           = "Riavvia quando inserisci una cartuccia";
    ls->shortcutDiskInsertA             = "Inserisci un disco nell'unità A";
    ls->shortcutDiskDirInsertA          = "Inserisci una cartella come disco dell'unità A";
    ls->shortcutDiskRemoveA             = "Espelli il disco dall'unità A";
    ls->shortcutDiskChangeA             = "Cambia rapidamente il disco nell'unità A";
    ls->shortcutDiskAutoResetA          = "Riavvia quando inserisci un disco nell'unità A";
    ls->shortcutDiskInsertB             = "Inserisci un disco nell'unità B";
    ls->shortcutDiskDirInsertB          = "Inserisci una cartella come disco dell'unità B";
    ls->shortcutDiskRemoveB             = "Espelli il disco dall'unità B";
    ls->shortcutCasInsert               = "Inserisci una cassetta nel registratore";
    ls->shortcutCasEject                = "Rimuovi la cassetta dal registratore";
    ls->shortcutCasAutorewind           = "Riavvolgi il nastro quando inserisci una cassetta";
    ls->shortcutCasReadOnly             = "Simula una cassetta di sola lettura";
    ls->shortcutCasSetPosition          = "Imposta la posizione del nastro";
    ls->shortcutCasRewind               = "Riavvolgi il nastro";
    ls->shortcutCasSave                 = "Salva la cassetta come file immagine";
    ls->shortcutPrnFormFeed             = "Salta alla pagina di stampa successiva";
    ls->shortcutCpuStateLoad            = "Carica lo stato della CPU";
    ls->shortcutCpuStateSave            = "Salva lo stato della CPU";
    ls->shortcutCpuStateQload           = "Carica rapidamente lo stato della CPU";
    ls->shortcutCpuStateQsave           = "Salva rapidamente lo stato della CPU";
    ls->shortcutAudioCapture            = "Avvia/Interrompi l'acquisizione del flusso audio";
    ls->shortcutScreenshotOrig          = "Salva una schermata";
    ls->shortcutScreenshotSmall         = "Salva una schermata ridotta non filtrata";
    ls->shortcutScreenshotLarge         = "Salva una schermata intera non filtrata";
    ls->shortcutQuit                    = "Esci da blueMSX+";
    ls->shortcutRunPause                = "Esegui/Sospendi l'emulazione";
    ls->shortcutStop                    = "Interrompi l'emulazione";
    ls->shortcutResetHard               = "Riavvia hardware";
    ls->shortcutResetSoft               = "Riavvia software";
    ls->shortcutResetClean              = "Espelli e riavvia";
    ls->shortcutSize1x                  = "Imposta finestra 1x";
    ls->shortcutSize2x                  = "Imposta finestra 2x";
    ls->shortcutSize3x                  = "Imposta finestra 3x";
    ls->shortcutSize4x                  = "Imposta finestra 4x";
    ls->shortcutSize5x                  = "Imposta finestra 5x";
    ls->shortcutSize6x                  = "Imposta finestra 6x";
    ls->shortcutSize7x                  = "Imposta finestra 7x";
    ls->shortcutSize8x                  = "Imposta finestra 8x";
    ls->shortcutSizeFullscreen          = "Passa a schermo intero";
    ls->shortcutYm2413BackendCycle      = "Cambia backend audio MSX-MUSIC";
    ls->shortcutY8950BackendCycle       = "Cambia backend audio MSX-AUDIO";
    ls->shortcutSizeMinimized           = "Riduci a icona la finestra";
    ls->shortcutToggleFullscren         = "Visualizza a schermo intero o finestra";
    ls->shortcutVolumeIncrease          = "Aumenta il volume";
    ls->shortcutVolumeDecrease          = "Diminuisci il volume";
    ls->shortcutVolumeMute              = "Azzera/Ripristina il volume";
    ls->shortcutVolumeStereo            = "Seleziona mono o stereo";
    ls->shortcutSwitchMsxAudio          = "Interruttore MSX-AUDIO Panasonic";
    ls->shortcutSwitchFront             = "Interruttore frontale Panasonic";
    ls->shortcutSwitchPause             = "Tasto pausa Panasonic";
    ls->shortcutToggleMouseLock         = "Collega/scollega mouse PC all'MSX";
    ls->shortcutEmuSpeedMax             = "Esegui temporaneamente alla massima velocità";
    ls->shortcutEmuSpeedToggle          = "Esegui alla massima velocità di emulazione";
    ls->shortcutEmuSpeedNormal          = "Ripristina la normale velocità di emulazione";
    ls->shortcutEmuPlayReverse          = "Emulazione a ritroso";                     // New in 2.8.3
    ls->shortcutEmuSpeedInc             = "Aumenta la velocità di emulazione";
    ls->shortcutEmuSpeedDec             = "Diminuisci la velocità di emulazione";
    ls->shortcutThemeSwitch             = "Cambia il tema corrente";
    ls->shortcutShowEmuProp             = "Mostra le proprietà di emulazione";
    ls->shortcutShowVideoProp           = "Mostra le proprietà del video";
    ls->shortcutShowAudioProp           = "Mostra le proprietà dell'audio";
    ls->shortcutShowCtrlProp            = "Mostra le proprietà dei controlli";
    ls->shortcutShowEffectsProp = "Mostra proprietà effetti";     // New in 2.9
    ls->shortcutShowSettProp            = "Mostra le proprietà delle impostazioni";
    ls->shortcutShowPorts               = "Mostra le proprietà delle porte";
    ls->shortcutShowLanguage            = "Visualizza il menù della lingua";
    ls->shortcutShowMachines            = "Visualizza la gestione hardware";
    ls->shortcutShowShortcuts           = "Visualizza la gestione tasti rapidi";
    ls->shortcutShowKeyboard            = "Configura le periferiche di ingresso";
    ls->shortcutShowDebugger            = "Visualizza il debugger";
    ls->shortcutShowTrainer             = "Visualizza il trainer";
    ls->shortcutShowMixer               = "Configura il mixer audio";
    ls->shortcutShowHelp                = "Visualizza la guida in linea";
    ls->shortcutShowAbout               = "Visualizza le informazioni su blueMSX+";
    ls->shortcutShowFiles               = "Mostra le proprietà dei file";
    ls->shortcutToggleSpriteEnable      = "Mostra/Nascondi gli sprite";
    ls->shortcutToggleFdcTiming         = "Commuta accelerazione FDD";
    ls->shortcutToggleHddSdBoost        = "Commuta accelerazione HDD/SD";
    ls->shortcutToggleNoSpriteLimits = "Attiva/disattiva limite sprite";                 // New in 2.9
    ls->shortcutEnableMsxKeyboardQuirk = "Emula peculiarità tastiera MSX";              // New in 2.9
    ls->shortcutToggleCpuTrace          = "Avvia/Interrompi l'azione del trace logger";
    ls->shortcutVideoLoad               = "Replay: Carica da file";                              
    ls->shortcutVideoPlay               = "Replay: Riproduci l'ultimo";                     
    ls->shortcutVideoRecord             = "Replay: Registra";                             
    ls->shortcutVideoStop               = "Replay: Interrompi";                  
    ls->shortcutVideoRender             = "Replay: Esporta in video";                                        


    //-----------------------
    // Keyboard config lines
    //-----------------------

    ls->keyconfigSelectedKey            = "Tasto scelto:";
    ls->keyconfigMappedTo               = "Assegnato a:";
    ls->keyconfigMappingScheme          = "Configurazione dei tasti:";


    //----------------
    // Rom type lines
    //----------------

    ls->romTypeStandard                 = "Standard";
    ls->romTypeZenima80                 = "Zemina 80 in 1";
    ls->romTypeZenima90                 = "Zemina 90 in 1";
    ls->romTypeZenima126                = "Zemina 126 in 1";
    ls->romTypeSccMirrored              = "SCC mirrored";
    ls->romTypeSccExtended              = "SCC extended";
    ls->romTypeKonamiGeneric            = "Konami Generic";
    ls->romTypeMirrored                 = "Mirrored ROM";
    ls->romTypeNormal                   = "Normal ROM";
    ls->romTypeDiskPatch                = "Normal + Disk Patch";
    ls->romTypeCasPatch                 = "Normal + Cassette Patch";
    ls->romTypeTc8566afFdc              = "TC8566AF Disk Controller";
    ls->romTypeTc8566afTrFdc            = "TC8566AF Turbo-R Disk Controller";
    ls->romTypeMicrosolFdc              = "Microsol Disk Controller";
    ls->romTypeNationalFdc              = "National Disk Controller";
    ls->romTypePhilipsFdc               = "Philips Disk Controller";
    ls->romTypeSvi707Fdc                = "SVI-707 Disk Controller";
    ls->romTypeSvi738Fdc                = "SVI-738 Disk Controller";
    ls->romTypeMappedRam                = "Mapped RAM";
    ls->romTypeMirroredRam1k            = "1kB Mirrored RAM";
    ls->romTypeMirroredRam2k            = "2kB Mirrored RAM";
    ls->romTypeNormalRam                = "Normal RAM";
    ls->romTypeExtRam16                 = "16kB External RAM";
    ls->romTypeExtRam32                 = "32kB External RAM";
    ls->romTypeExtRam48                 = "48kB External RAM";
    ls->romTypeExtRam64                 = "64kB External RAM";
    ls->romTypeTurborPause              = "Turbo-R Pause";
    ls->romTypeF4deviceNormal           = "F4 Device Normal";
    ls->romTypeF4deviceInvert           = "F4 Device Inverted";
    ls->romTypeTurborTimer              = "Turbo-R Timer";
    ls->romTypeNormal4000               = "Normal 4000h";
    ls->romTypeNormalC000               = "Normal C000h";
    ls->romTypeExtRam                   = "External RAM";
    ls->romTypeExtRam512                = "512kB External RAM";
    ls->romTypeExtRam1mb                = "1MB External RAM";
    ls->romTypeExtRam2mb                = "2MB External RAM";
    ls->romTypeExtRam4mb                = "4MB External RAM";
    ls->romTypeSvi328Cart               = "SVI-328 Cartridge";
    ls->romTypeSvi328Fdc                = "SVI-328 Disk Controller";
    ls->romTypeSvi328RsIde              = "SVI-328 RS IDE";
    ls->romTypeSvi328Prn                = "SVI-328 Printer";
    ls->romTypeSvi328Uart               = "SVI-328 Serial Port";
    ls->romTypeSvi328col80              = "SVI-328 80 Column Card";
    ls->romTypeSvi727col80              = "SVI-727 80 Column Card";
    ls->romTypeColecoCart               = "Coleco Cartridge";
    ls->romTypeSg1000Cart               = "SG-1000 Cartridge";
    ls->romTypeSc3000Cart               = "SC-3000 Cartridge";
    ls->romTypeMsxPrinter               = "MSX Printer";
    ls->romTypeTurborPcm                = "Turbo-R PCM Chip";
    ls->romTypeNms8280Digitiz           = "Philips NMS-8280 Digitizer";
    ls->romTypeHbiV1Digitiz             = "Sony HBI-V1 Digitizer";


    //--------------------------------------------------------
    // Debug type lines
    // Note: Only needs translation if debugger is translated
    //--------------------------------------------------------

    ls->dbgMemVisible                   = "Visible Memory";
    ls->dbgMemRamNormal                 = "Normal";
    ls->dbgMemRamMapped                 = "Mapped";
    ls->dbgMemYmf278                    = "YMF278 Sample RAM";
    ls->dbgMemAy8950                    = "AY8950 Sample RAM";
    ls->dbgMemScc                       = "Memory";

    ls->dbgCallstack                    = "Callstack";

    ls->dbgRegs                         = "Registers";
    ls->dbgRegsCpu                      = "CPU Registers";
    ls->dbgRegsYmf262                   = "YMF262 Registers";
    ls->dbgRegsYmf278                   = "YMF278 Registers";
    ls->dbgRegsAy8950                   = "AY8950 Registers";
    ls->dbgRegsYm2413                   = "YM2413 Registers";

    ls->dbgDevRamMapper                 = "RAM Mapper";
    ls->dbgDevRam                       = "RAM";
    ls->dbgDevF4Device                  = "F4 Device";
    ls->dbgDevKorean80                  = "Korean 80";
    ls->dbgDevKorean90                  = "Korean 90";
    ls->dbgDevKorean128                 = "Korean 128";
    ls->dbgDevFdcMicrosol               = "Microsol FDC";
    ls->dbgDevPrinter                   = "Printer";
    ls->dbgDevSviFdc                    = "SVI FDC";
    ls->dbgDevSviPrn                    = "SVI Printer";
    ls->dbgDevSvi80Col                  = "SVI 80 Column";
    ls->dbgDevRtc                       = "RTC";
    ls->dbgDevTrPause                   = "TR Pause";


    //----------------------------------------------------
    // Debug type lines
    // Note: Can only be translated to european languages
    //----------------------------------------------------

    ls->aboutScrollThanksTo             = "Un ringraziamento speciale a:  ";
    ls->aboutScrollAndYou               = "ed a TE !!!!";
};

#endif
