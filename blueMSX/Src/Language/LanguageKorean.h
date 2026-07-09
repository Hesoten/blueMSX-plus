/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/Src/Language/LanguageKorean.h,v $
**
** $Revision: 1.62 $ + additions 2005/03/03
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
#ifndef LANGUAGE_KOREAN_H
#define LANGUAGE_KOREAN_H

#include "LanguageStrings.h"
 
void langInitKorean(LanguageStrings* ls)
{
    //----------------------
    // Language lines
    //----------------------

    ls->langCatalan             = "Catalan";
    ls->langChineseSimplified   = "중국어 간체";
    ls->langChineseTraditional  = "중국어 번체";
    ls->langDutch               = "네덜란드어";
    ls->langEnglish             = "영어";
    ls->langFinnish             = "핀란드어";
    ls->langFrench              = "프랑스어";
    ls->langGerman              = "독일어";
    ls->langItalian             = "이탈리아어";
    ls->langJapanese            = "일본어";
    ls->langKorean              = "한국어";
    ls->langPolish              = "폴란드어";
    ls->langPortuguese          = "포르투갈어";
    ls->langRussian             = "Russian";            // v2.8
    ls->langSpanish             = "스페인어";
    ls->langSwedish             = "스웨덴어";


    //----------------------
    // Generic lines
    //----------------------

    ls->textDevice              = "장치:";
    ls->textFilename            = "파일 이름:";
    ls->textFile                = "파일";
    ls->textNone                = "없음";
    ls->textUnknown             = "알려지지 않음";                            


    //----------------------
    // Warning and Error lines
    //----------------------

    ls->warningTitle             = "blueMSX+ - 경고";
    ls->warningDiscardChanges   = "변경한 설정이 적용되기 위해선 저장을 해야 합니다. 현재 설정에 아무런 영향을 주지않고 그냥 나가겠습니까?";
    ls->warningOverwriteFile    = "파일을 덮어 쓰시겠습니까:"; 
    ls->warningStateOldFormat   = "이전 형식의 상태 저장입니다. 정상적으로 재개되지 않을 수 있습니다. 그래도 불러오시겠습니까?";
    ls->errorTitle              = "blueMSX+ - 에러";
    ls->errorEnterFullscreen    = "전체 화면모드 진입 실패.           \n";
    ls->errorDirectXFailed      = "DirectX 오브젝트 만들기 실패.           \nGDI로 대체해서 사용합니다.\n그래픽카드 등록정보를 확인하세요.";
    ls->errorNoRomInZip         = "zip파일 내부의 rom을 찾을 수 없습니다.";
    ls->errorNoDskInZip         = "zip파일 내부의 dsk를 찾을 수 없습니다.";
    ls->errorNoCasInZip         = "zip파일 내부의 cas를 찾을 수 없습니다.";
    ls->errorDirAsDskOverflow   = "%d 개의 파일(총 %d KB)이 720 KB 디스크 이미지에 맞지 않아 건너뛰었습니다.";
    ls->errorNoHelp             = "blueMSX+ 도움말을 찾을 수 없습니다.";
    ls->errorStartEmu           = "MSX 에뮬레이터를 시작할 수 없습니다.";
    ls->errorStartEmuNoMachine           = "에뮬레이션에 사용할 MSX 머신이 설정되지 않았습니다. 옵션 -> 에뮬레이션 -> MSX 머신 에서 선택하세요.";
    ls->errorStartEmuMachinesDirMissing  = "Machines 폴더를 찾을 수 없습니다.";
    ls->errorStartEmuMachineNotFound     = "선택한 머신 구성 '%s'을(를) 찾을 수 없습니다.";
    ls->errorStartEmuConfigInvalid       = "머신 구성 '%s'의 config.ini를 읽을 수 없습니다. 파일이 손상되었거나 호환되지 않는 버전에서 온 것일 수 있습니다.";
    ls->errorMissingFiles       = "다음 파일을 로드할 수 없습니다:";
    ls->errorPortableReadonly   = "이동 장치는 읽기 전용입니다.";        
    ls->infoTitle               = "blueMSX+ 정보";
    ls->infoGameReaderRedirect  = "blueMSX+ 는 MSX Game Reader 를 직접 지원하지 않습니다 (ASCII 사의 XP 시대 정품 드라이버는 최신 Windows 에서 작동하지 않습니다).\n\n대신 MSX Game Reader - Web Dumper (Kunihiko Ohnaka 제작) 를 브라우저에서 열까요?";
    ls->infoColorDepth          = "blueMSX+ 는 16 또는 32 비트 색상 심도에서 가장 잘 작동합니다.";
    ls->errorKeyboardThemeMissing = "키보드 편집기 테마를 찾을 수 없습니다.";
    ls->errorMixerThemeMissing    = "믹서 테마를 찾을 수 없습니다.";
    ls->errorRecorderTitle      = "blueMSX+ - 녹화";
    ls->errorRecorderSaveReplay = "재생 파일을 저장하지 못했습니다:\n  %s\n\n대상 폴더가 존재하고 쓰기 가능한지 확인하세요.";
    ls->errorRecorderReplayMissing = "재생 파일을 찾을 수 없습니다:\n  %s\n\n먼저 재생을 녹화하거나 불러오기로 기존 .cap 파일을 선택하세요.";
    ls->errorRecorderRequiresDX12   = "비디오 드라이버를 Direct3D 12로 전환하고 녹화를 시작할까요?";
    ls->errorRecorderRequiresDX12Title = "blueMSX+ - 비디오 드라이버 변경";
    ls->infoRecorderComplete    = "동영상 파일 저장됨:\n  %s";
    ls->infoToastSaved          = "저장됨: %s";
    ls->infoToastAlreadyRecording   = "이미 녹화 중";
    ls->dlgRecorderPickTitle        = "blueMSX+ - 재생을 동영상으로 변환";
    ls->dlgRecorderPickSourceCap    = "변환할 재생 파일 (.cap):";
    ls->dlgRecorderPickOutputMp4    = "출력 동영상 파일 (.mp4):";
    ls->menuFileRecordVideo         = "동영상 녹화";
    ls->menuFileStopRecordVideo     = "동영상 녹화 중지";
    ls->shortcutRecordVideoStart    = "동영상 녹화: 시작";
    ls->shortcutRecordVideoStartAs  = "동영상 녹화: 다른 이름으로 시작";
    ls->shortcutRecordVideoStop     = "동영상 녹화: 중지";
    ls->shortcutRecordVideoToggle   = "동영상 녹화: 토글";
    ls->shortcutAudioCaptureAs      = "음성 녹음: 다른 이름으로 시작";
    ls->shortcutVideoRecordAs       = "리플레이: 다른 이름으로 녹화";
    ls->shortcutScreenshotAs        = "스크린샷: 다른 이름으로 저장";

    ls->propCapture                 = "녹화";
    ls->propCaptureAudioGB          = " 오디오 녹음 ";
    ls->propCaptureVideoGB          = " 동영상 녹화 ";
    ls->propCaptureScreenshotGB     = " 스크린샷 ";
    ls->propCaptureReplayGB         = " 재생 기록 ";
    ls->propCaptureSaveDir          = "저장 폴더:";
    ls->propCaptureFormat           = "형식:";
    ls->propCaptureCodec            = "코덱:";
    ls->propCaptureAutoName         = "파일 이름 자동";
    ls->propCapturePromptName       = "파일 이름 묻기";
    ls->propCaptureShowToast        = "녹화 완료 시 토스트 표시";


    //----------------------
    // File related lines
    //----------------------

    ls->fileRom                 = "롬 이미지";
    ls->fileAll                 = "모든 파일";
    ls->fileCpuState            = "CPU 상태";
    ls->fileVideoCapture        = "녹화된 영상"; 
    ls->fileDisk                = "디스크 이미지";
    ls->fileCas                 = "테이프 이미지";
    ls->fileAvi                 = "동영상 클립";    


    //----------------------
    // Menu related lines
    //----------------------

    ls->menuNoRecentFiles       = "- 열어본 파일 없음 -";
    ls->menuInsert              = "삽입";
    ls->menuEject               = "꺼내기";

    ls->menuCartGameReader      = "게임 리더";                        
    ls->menuCartIde             = "IDE";                                
    ls->menuCartBeerIde         = "Beer";                               
    ls->menuCartGIde            = "GIDE";                               
    ls->menuCartSunriseIde      = "Sunrise";                              
    ls->menuCartScsi            = "SCSI";                // New in 2.7
    ls->menuCartMegaSCSI        = "MEGA-SCSI";           // New in 2.7
    ls->menuCartWaveSCSI        = "WAVE-SCSI";           // New in 2.7
    ls->menuCartGoudaSCSI       = "Gouda SCSI";          // New in 2.7
    ls->menuJoyrexPsg           = "Joyrex PSG 카트리지"; // New in 2.9
    ls->menuCartSCC             = "SCC 카트리지";
    ls->menuCartSCCPlus         = "SCC-I 카트리지";
    ls->menuCartFMPac           = "FM-PAC 카트리지";
    ls->menuCartPac             = "PAC 카트리지";
    ls->menuCartHBI55           = "Sony HBI-55 카트리지";
    ls->menuCartInsertSpecial   = "특수 카트리지 삽입";                     
    ls->menuCartMegaRam         = "메가램";                            
    ls->menuCartExternalRam     = "외장램";
    ls->menuCartEseRam          = "Ese-RAM";             // New in 2.7
    ls->menuCartEseSCC          = "Ese-SCC";             // New in 2.7
    ls->menuCartMegaFlashRom    = "Mega Flash ROM";      // New in 2.7
    ls->menuCartFlashCart       = "플래시 카트리지";

    ls->menuDiskInsertNew       = "새로운 디스크 이미지 삽입";              
    ls->menuDiskInsertCdrom     = "시디롬 삽입";       // New in 2.7
    ls->menuDiskDirInsert       = "디렉토리 삽입";
    ls->menuDiskAutoStart       = "삽입후에 재시작";
    ls->menuCartAutoReset       = "삽입/제거 후에 재시작";

    ls->menuCasRewindAfterInsert = "삽입후에 되감기";
    ls->menuCasUseReadOnly       = "읽기전용";
    ls->lmenuCasSaveAs           = "다른 이름으로 저장...";
    ls->menuCasSetPosition      = "위치 설정";
    ls->menuCasRewind           = "되감기";

    ls->menuVideoLoad           = "불러오기...";             
    ls->menuVideoPlay           = "마지막으로 녹화한 영상 재생";   
    ls->menuVideoRecord         = "녹화";              
    ls->menuVideoRecording      = "녹화중";           
    ls->menuVideoRecAppend      = "녹화 (추가)";     
    ls->menuVideoStop           = "정지";                
    ls->menuVideoRender         = "동영상 파일로 저장";   

    ls->menuPrnFormfeed         = "폼피드";

    ls->menuZoom1x              = "1x 창";
    ls->menuZoom2x              = "2x 창";
    ls->menuZoom3x              = "3x 창";
    ls->menuZoom4x              = "4x 창";
    ls->menuZoom5x              = "5x 창";
    ls->menuZoom6x              = "6x 창";
    ls->menuZoom7x              = "7x 창";
    ls->menuZoom8x              = "8x 창";
    ls->menuZoomFullscreen      = "전체 화면";
    
    ls->menuPropsEmulation      = "에뮬레이션";
    ls->menuPropsVideo          = "비디오";
    ls->menuPropsSound          = "사운드";
    ls->menuPropsMidi           = "MIDI";
    ls->menuPropsControls       = "컨트롤";
    ls->menuPropsEffects        = "효과";               // New in 2.9
    ls->menuPropsSettings        = "외부 설정";
    ls->menuPropsFile           = "파일";
    ls->menuPropsDisk           = "디스크";               // New in 2.7
    ls->menuPropsLanguage       = "언어";
    ls->menuPropsPorts          = "포트"; 
    ls->menuPropsCapture        = "녹화";
    
    ls->menuVideoSource         = "비디오 출력 소스";                   
    ls->menuVideoSourceDefault  = "비디오 출력 소스 연결안됨";      
    ls->menuVideoChipAutodetect = "비디오 칩 자동감지";    
    ls->menuVideoInSource       = "비디오 입력 소스";                    
    ls->menuVideoInBitmap       = "비트맵 파일";                        
    
    ls->menuEthInterface        = "이더넷 인터페이스"; 

    ls->menuHelpHelp            = "도움말 항목";
    ls->menuHelpAbout           = "blueMSX+에 대하여";

    ls->menuFileCart            = "카트리지 슬롯";
    ls->menuFileDisk            = "디스크 드라이브";
    ls->menuFileCas             = "카세트";
    ls->menuFilePrn             = "프린터";
    ls->menuFileLoadState       = "CPU 상태 불러오기";
    ls->menuFileSaveState       = "CPU 상태 저장";
    ls->menuFileQLoadState      = "상태 바로 불러오기";
    ls->menuFileQSaveState      = "상태 바로 저장";
    ls->menuFileCaptureAudio    = "소리 저장";
    ls->menuFileStopAudio       = "오디오 중지";
    ls->menuFileCaptureVideo    = "영상 녹화"; 
    ls->menuFileScreenShot      = "화면 저장";
    ls->menuFileExit            = "끝내기";

    ls->menuFileHarddisk        = "하드 디스크 / SD 카드";                          
    ls->menuFileHarddiskNoPesent= "컨트롤러 존재하지 않음";             
    ls->menuFileHarddiskRemoveAll= "모든 하드 디스크 제거";    // New in 2.7

    ls->menuRunRun              = "실행";
    ls->menuRunPause            = "일시 정지";
    ls->menuRunStop             = "중지";
    ls->menuRunSoftReset        = "소프트 리셋";
    ls->menuRunHardReset        = "하드 리셋";
    ls->menuRunCleanReset       = "전체 리셋";

    ls->menuTools                = "도구";
    ls->menuToolsMachine         = "머신 편집기";
    ls->menuToolsShortcuts      = "단축키 편집기";
    ls->menuToolsCtrlEditor     = "컨트롤러 / 키보드 편집기"; 
    ls->menuToolsDebugger       = "디버거";               
    ls->menuToolsTrainer        = "트레이너";                
    ls->menuToolsTraceLogger    = "트레이스 기록";           

    ls->menuFile                = "파일";
    ls->menuRun                 = "실행";
    ls->menuWindow              = "윈도우";
    ls->menuToolsMixer          = "믹서";
    ls->menuToolsLoadMemory     = "메모리 불러오기";
    ls->menuOptions             = "옵션";
    ls->menuHelp                = "도움말";


    //----------------------
    // Dialog related lines
    //----------------------

    ls->dlgOK                   = "확인";
    ls->dlgOpen                 = "열기";
    ls->dlgCancel               = "취소";
    ls->dlgYes                  = "예";
    ls->dlgNo                   = "아니오";
    ls->dlgSave                 = "저장";
    ls->dlgSaveAs               = "다른 이름으로...";
    ls->dlgRun                  = "실행";
    ls->dlgClose                = "닫기";

    ls->dlgLoadRom              = "blueMSX+ - 카트리지에 삽입할 rom 이미지 선택";
    ls->dlgLoadDsk              = "blueMSX+ - 드라이브에 삽입할 dsk 이미지 선택";
    ls->dlgLoadCas              = "blueMSX+ - 카세트 플레이어에 넣을 cas 이미지 선택";
    ls->dlgLoadRomDskCas        = "blueMSX+ - 읽어 들일 rom, dsk, cas 이미지 선택";
    ls->dlgLoadRomDesc          = "카트리지에 삽입할 롬 이미지를 선택해 주세요:";
    ls->dlgLoadDskDesc          = "드라이브에 삽입할 첫번째 장 디스크 또는 디스크 이미지를 선택해 주세요(교환은 ALT+F9. 파일명과 숫자가 일정해야 됩니다):";
    ls->dlgLoadCasDesc          = "카세트 플레이어에 넣을 테잎 이미지를 선택해 주세요:";
    ls->dlgLoadRomDskCasDesc    = "읽어 들일 롬,디스크,또는 테잎 이미지를 선택해 주세요:";
    ls->dlgLoadState            = "CPU 상태 불러오기";
    ls->dlgLoadVideoCapture     = "녹화된 영상 불러오기";      
    ls->dlgSaveState            = "CPU 상태 저장";
    ls->dlgSaveCassette          = "blueMSX+ - 테잎 이미지 저장";
    ls->dlgSaveVideoClipAs      = "다른 이름으로 영상 클립 저장...";      
    ls->dlgSaveCaptureAudio     = "오디오 녹음 다른 이름으로 저장";
    ls->dlgSaveCaptureVideo     = "동영상 녹화 다른 이름으로 저장";
    ls->dlgSaveCaptureReplay    = "재생 다른 이름으로 저장";
    ls->dlgSaveCaptureScreenshot = "스크린샷 다른 이름으로 저장";
    ls->dlgAmountCompleted      = "저장 완료율:";          
    ls->dlgInsertRom1           = "슬롯 1에 ROM 카트리지 삽입";
    ls->dlgInsertRom2           = "슬롯 2에 ROM 카트리지 삽입";
    ls->dlgInsertDiskA          = "드라이브 A에 디스크 삽입";
    ls->dlgInsertDiskB          = "드라이브 B에 디스크 삽입";
    ls->dlgInsertHarddisk       = "하드 디스크 삽입";                   
    ls->dlgInsertCas            = "카세트 테잎 삽입";
    ls->dlgRomType              = "롬 형식:";
    ls->dlgDiskSize             = "디스크 사이즈:";             

    ls->dlgTapeTitle            = "blueMSX+ - 테잎 위치";
    ls->dlgTapeFrameText        = "테잎 위치";
    ls->dlgTapeCurrentPos       = "현재 위치";
    ls->dlgTapeTotalTime        = "총 시간";
    ls->dlgTapeSetPosText        = "테잎 위치:";
    ls->dlgTapeCustom            = "사용자 지정 파일 보기";
    ls->dlgTabPosition           = "위치";
    ls->dlgTabType               = "형식";
    ls->dlgTabFilename           = "파일 이름";
    ls->dlgZipReset             = "삽입후 재시작";

    ls->dlgAboutTitle           = "blueMSX+에 대하여";

    ls->dlgLangLangText         = "blueMSX+에 사용할 언어 선택";
    ls->dlgLangLangTitle        = "blueMSX+ - 언어";

    ls->dlgAboutAbout           = "blueMSX+에 대하여\r\n=====";
    ls->dlgAboutVersion         = "버전:";
    ls->dlgAboutBuildNumber     = "빌드:";
    ls->dlgAboutBuildDate       = "날짜:";
    ls->dlgAboutCreat           = "제작자: Daniel Vik";
    ls->dlgAboutDevel           = "개발자 분들\r\n========";
    ls->dlgAboutThanks          = "도움을 주신 분들\r\n===========";       // New in 2.7 (retranslate, see english)
    ls->dlgAboutLisence         = "라이센스\r\n"
                                  "======\r\n\r\n"
				  "이 소프트웨어는 원본 그대로 배포되어야 하며, 명시적이든 암묵적이든 "
				  "어떤 보증도 하지 않습니다.\r\n이 소프트웨어의 사용으로 일어나는 "
				  "어떠한 문제에 대해서도 제작자에게는 책임이 없습니다.\r\n\r\n"
                                  "더 자세한 것은 www.bluemsx.com에 방문해 주세요.";

    ls->dlgSavePreview          = "미리 보기";
    ls->dlgSaveDate             = "시간 저장됨:";

    ls->dlgRenderVideoCapture   = "blueMSX+ - 동영상 파일로 저장...";  


    //----------------------
    // Properties related lines
    //----------------------

    ls->propTitle               = "blueMSX+ - 속성";
    ls->propEmulation           = "에뮬레이션";
    ls->propD3D                 = "Direct3D";
    ls->propVideo               = "비디오";
    ls->propSound               = "사운드";
    ls->propMidi                = "MIDI";
    ls->propControls            = "콘트롤";
    ls->propPerformance         = "성능";
    ls->propEffects             = "효과";             // New in 2.9
    ls->propSettings             = "외부 설정";
    ls->propFile                = "파일";
    ls->propDisk                = "디스크";              // New in 2.7
    ls->propPorts               = "포트";
    
    ls->propEmuGeneralGB        = "일반 ";
    ls->propEmuFamilyText       = "MSX 머신:";
    ls->propEmuMemoryGB         = "메모리 ";
    ls->propEmuRamSizeText      = "램 크기:";
    ls->propEmuVramSizeText     = "비디오램 크기:";
    ls->propEmuSpeedGB          = "에뮬레이션 속도 ";
    ls->propEmuSpeedText        = "에뮬레이터 코어:";
    ls->propEmuVdpCmdSpeedText  = "VDP 명령 대기 시간:";
    ls->propEmuFrontSwitchGB     = "파나소닉 스위치 ";
    ls->propEmuFrontSwitch       = " 프론트 스위치";
    ls->propEmuNoSpriteLimits   = " 스프라이트 제한 해제";  // New in 2.9
    ls->propEnableMsxKeyboardQuirk = " MSX 키보드 특성 에뮬레이션";  // New in 2.9
    ls->propEmuFdcTiming        = " FDD 액세스 시 가속";
    ls->propEmuHddSdBoost       = " HDD/SD 카드 접근 중 가속";
    ls->propEmuReversePlay      = " 역재생 사용"; // New in 2.8.3
    ls->propEmuPauseSwitch      = " 일시 정지 스위치";
    ls->propEmuAudioSwitch       = " MSX-AUDIO 카트리지 스위치";
    ls->propVideoFreqText       = "비디오 주파수:";
    ls->propVideoFreqAuto       = "자동";
    ls->propSndOversampleText   = "오버샘플:";
    ls->propSndYkInGB           = "YK-01/YK-10/YK-20 입력 ";                
    ls->propSndMidiInGB         = "MIDI 입력 ";
    ls->propSndMidiOutGB        = "MIDI 출력 ";
    ls->propSndMidiChannel      = "MIDI 채널:";                      
    ls->propSndMidiAll          = "전부";                                

    ls->propMonMonGB            = "모니터 ";
    ls->propMonTypeText         = "모니터 유형:";
    ls->propMonEmuText          = "모니터 에뮬레이션:";
    ls->propVideoTypeText       = "비디오 유형:";
    ls->propMonHorizStretch      = " 수평 스트레치";
    ls->propMonVertStretch       = " 수직 스트레치";
    ls->propMonDeInterlace      = " 디인터레이스";
    ls->propBlendFrames         = " 인접한 프레임 색상혼합";           
    ls->propMonColorGhosting    = " RF모듈레이터:";
    ls->propMonBrightness       = "밝기:";
    ls->propMonContrast         = "대비:";
    ls->propMonSaturation       = "채도:";
    ls->propMonGamma            = "감마:";
    ls->propMonScanlines        = " 스캔라인:";
    ls->propMonScanlinesBright  = "밝기 보정:";
    ls->propMonScanlinesBrightAuto = " 자동";
    ls->propMonScanlinesShape   = "프리셋:";
    ls->propMonScanlinesDepth   = "깊이:";
    ls->propMonScanlinesSharpness = "선명도:";
    ls->enumScanShapeGentle     = "부드러움";
    ls->enumScanShapeStandard   = "표준";
    ls->enumScanShapeSharp      = "선명";
    ls->enumScanShapeTrinitron  = "트리니트론";
    ls->enumScanShapeCustom     = "사용자 정의";
    ls->propMonHdrEnable        = "HDR";
    ls->propMonHdrPaperWhite    = "흰색 밝기:";
    ls->propMonHdrSystemMode    = "시스템 HDR 모드:";
    ls->propMonHdrRestartHint   = "HDR 모드 변경을 적용하려면 blueMSX를 다시 시작하십시오.";
    ls->propMonHdrRecord        = " HDR로 녹화";
    ls->propMonEffectsGB        = "효과 ";

    ls->propPerfVideoDrvGB      = "비디오 드라이버 ";
    ls->propPerfVideoDispDrvText= "화면 드라이버:";
    ls->propPerfFrameSkipText   = "프레임 건너띄기:";
    ls->propPerfAudioDrvGB      = "오디오 드라이버 ";
    ls->propPerfAudioDrvText    = "사운드 드라이버:";
    ls->propPerfAudioBufSzText  = "사운드 버퍼 사이즈:";
    ls->propPerfAudioBufSzActualFmt = "(실제 버퍼: %u ms)";
    ls->propPerfEmuGB           = "클럭 에뮬레이션 ";
    ls->propPerfSyncModeText    = "동기화 모드:";
    ls->propFullscreenResText   = "전체 화면 해상도:";

    ls->propSndChipEmuGB        = "사운드 칩 에뮬레이션 ";
    ls->propSoundChipsActive    = "활성 백엔드:";
    ls->propSoundChipsHint      = "여러 백엔드가 활성화된 경우, 실행 중에 A/B 비교할 수 있습니다.";
    ls->propSoundChipsYm2413GB  = " MSX-MUSIC 백엔드 ";
    ls->propSoundChipsY8950GB   = " MSX-AUDIO 백엔드 ";
    ls->propSndOpllAnalogText   = "아날로그 필터:";
    ls->propSndOpllAnalogLpfText = "LPF 차단:";
    ls->enumOpllFilterOff       = "끔";
    ls->enumOpllFilterBright    = "밝음 (LPF 12 kHz)";
    ls->enumOpllFilterClear     = "맑음 (LPF 8 kHz)";
    ls->enumOpllFilterStandard  = "표준 (LPF 5 kHz)";
    ls->enumOpllFilterSoft      = "부드러움 (LPF 3.5 kHz)";
    ls->enumOpllFilterMellow    = "따뜻함 (LPF 2.3 kHz)";
    ls->enumOpllFilterCustom    = "사용자 정의";
    ls->propSndMsxMusic         = " MSX-MUSIC";
    ls->propSndMsxAudio         = " MSX-AUDIO";
    ls->propSndMoonsound         = " Moonsound";
    ls->propSndMt32ToGm         = " GM에 MT-32 악기배열";

    ls->propPortsLptGB          = "패러럴 포트 "; 
    ls->propPortsComGB          = "시리얼 포트 "; 
    ls->propPortsLptText        = "포트:"; 
    ls->propPortsCom1Text       = "포트 1:";
    ls->propPortsNone           = "없음";
    ls->propPortsSimplCovox     = "SiMPL / Covox DAC";
    ls->propPortsFile           = "파일에 출력하기";
    ls->propPortsComFile        = "파일에 보내기";
    ls->propPortsOpenLogFile    = "로그파일 열기";
    ls->propPortsEmulateMsxPrn  = "흉내내기:";

    ls->propSetFileHistoryGB     = "열어본 파일 목록 ";
    ls->propSetFileHistorySize   = "파일 보관 크기:";
    ls->propSetFileHistoryClear  = "목록 지우기";
    ls->propFileTypes            = " .rom/.dsk/.cas/.sta 를 \"연결 프로그램\" 메뉴에 등록";
    ls->propOpenDefaultApps      = "Windows 기본 앱 설정 열기";
    ls->propWindowsEnvGB         = "윈도우즈 환경 "; 
    ls->propSetScreenSaver       = " blueMSX+ 실행중 디스플레이 유지 (화면 끄기/절전/보호기 억제)";
    ls->propPriorityBoost        = " 에뮬레이션에 Windows 게임 스케줄러 (MMCSS) 사용";
    ls->propScreenshotPng       = " Portable Network Graphics (.png) 스크린샷 사용";  
    ls->propEjectMediaOnExit    = " Eject media when blueMSX+ exits";        // New in 2.8
    ls->propClearHistory         = "열어본 파일 목록을 지울까요?";
    ls->propOpenRomGB           = "열때 항상 현재 설정 사용 ";
    ls->propDefaultRomType      = "기본 롬 형식:";
    ls->propGuessRomType        = "자동 판단";

    ls->propSettDefSlotGB       = "마우스로 끌어서 놓을때 ";
    ls->propSettDefSlots        = "현재 슬롯에 롬 삽입:";
    ls->propSettDefSlot         = " 슬롯";
    ls->propSettDefDrives       = "현재 드라이브에 디스켓 삽입:";
    ls->propSettDefDrive       = " 드라이브";

    ls->propThemeGB             = "테마 ";
    ls->propTheme               = "기본 테마:";

    ls->propCdromGB             = "시디롬 ";         // New in 2.7
    ls->propCdromMethod         = "액세스 방법:";  // New in 2.7
    ls->propCdromMethodNone     = "없음";            // New in 2.7
    ls->propCdromMethodIoctl    = "IOCTL";           // New in 2.7
    ls->propCdromMethodAspi     = "ASPI";            // New in 2.7
    ls->propCdromDrive          = "드라이브:";          // New in 2.7

    ls->propD3DParametersGB         = "매개 변수 ";                // New in 2.9
    ls->propD3DAspectRatioText      = "화면 비율";               // New in 2.9
    ls->propD3DLinearFilteringText  = " 선형 필터링";          // New in 2.9
    ls->propD3DForceHighResText     = " 고해상도 강제";     // New in 2.9
    ls->propD3DExtendBorderColorText    = " 테두리 색 확장";   // New in 2.9

    ls->propD3DCroppingGB               = "자르기 ";              // New in 2.9
    ls->propD3DCroppingTypeText         = "자르기 유형:";         // New in 2.9
    ls->propD3DCroppingLeftText         = "왼쪽:";                  // New in 2.9
    ls->propD3DCroppingRightText        = "오른쪽:";                 // New in 2.9
    ls->propD3DCroppingTopText          = "위:";                   // New in 2.9
    ls->propD3DCroppingBottomText       = "아래:";                // New in 2.9


    //----------------------
    // Dropdown related lines
    //----------------------

    ls->enumVideoMonColor       = "컬러";
    ls->enumVideoMonGrey        = "검정과 흰색";
    ls->enumVideoMonGreen       = "녹색";
    ls->enumVideoMonAmber       = "호박색"; 

    ls->enumVideoTypePAL        = "PAL";
    ls->enumVideoTypeNTSC       = "NTSC";

    ls->enumVideoEmuNone        = "없음";
    ls->enumVideoEmuYc          = "Y/C 케이블 (선명)";
    ls->enumVideoEmuMonitor     = "모니터";
    ls->enumVideoEmuYcBlur      = "잡음있는 Y/C 케이블 (선명)";
    ls->enumVideoEmuComp        = "컴포지트 (흐릿)";
    ls->enumVideoEmuCompBlur    = "잡음있는 컴포지트 (흐릿)";
    ls->enumVideoEmuScale2x     = "Scale 2x";
    ls->enumVideoEmuHq2x        = "Hq2x";


    ls->enumVideoDrvDirectDrawHW = "DirectDraw HW 가속";
    ls->enumVideoDrvDirectDraw  = "DirectDraw";
    ls->enumVideoDrvGDI         = "GDI";
    ls->enumVideoDrvD3D         = "Direct3D";

    ls->enumVideoFrameskip0     = "없음";
    ls->enumVideoFrameskip1     = "1 프레임";
    ls->enumVideoFrameskip2     = "2 프레임";
    ls->enumVideoFrameskip3     = "3 프레임";
    ls->enumVideoFrameskip4     = "4 프레임";
    ls->enumVideoFrameskip5     = "5 프레임";

    ls->enumD3DARAuto           = "자동";           // New in 2.9
    ls->enumD3DARStretch        = "늘리기";        // New in 2.9
    ls->enumD3DARPAL            = "PAL";            // New in 2.9
    ls->enumD3DARNTSC           = "NTSC";           // New in 2.9
    ls->enumD3DAR11             = "1:1";            // New in 2.9

    ls->enumD3DCropNone         = "없음";           // New in 2.9
    ls->enumD3DCropMSX1         = "MSX1";           // New in 2.9
    ls->enumD3DCropMSX1Plus8    = "MSX1+8";         // New in 2.9
    ls->enumD3DCropMSX2         = "MSX2";           // New in 2.9
    ls->enumD3DCropMSX2Plus8    = "MSX2+8";         // New in 2.9
    ls->enumD3DCropCustom       = "사용자 지정";         // New in 2.9

    ls->enumSoundDrvNone        = "소리없음";
    ls->enumSoundDrvWMM         = "WMM 드라이버";
    ls->enumSoundDrvDirectX     = "다이렉트X 드라이버";
    ls->enumSoundDrvWasapi      = "WASAPI 드라이버";

    ls->enumEmuSync1ms          = "MSX의 속도에 동기";
    ls->enumEmuSyncAuto         = "자동 (빠름)";
    ls->enumEmuSyncNone         = "하지않음";
    ls->enumEmuSyncVblank       = "PC의 수직주파수에 동기";
    ls->enumEmuAsyncVblank      = "PC의 수직주파수에 비동기";             

    ls->enumControlsJoyNone     = "없음";
    ls->enumControlsJoyMouse    = "마우스";
    ls->enumControlsJoyTetris2Dongle = "테트리스 2 동글";
    ls->enumControlsJoyTMagicKeyDongle = "매직키 동글";             
    ls->enumControlsJoy2Button = "2버튼 조이스틱";                   
    ls->enumControlsJoyGunstick  = "건 스틱";                         
    ls->enumControlsJoyAsciiLaser="아스키 플러스-X 터미네이터 레이저";      
    ls->enumControlsArkanoidPad  ="Arkanoid Pad";                   // New in 2.7.1
    ls->enumControlsJoyColeco = "ColecoVision 조이스틱";                

    ls->enumDiskMsx35Dbl9Sect    = "MSX 3.5\" 양면, 9 섹터";     
    ls->enumDiskMsx35Dbl8Sect    = "MSX 3.5\" 양면, 8 섹터";     
    ls->enumDiskMsx35Sgl9Sect    = "MSX 3.5\" 단면, 9 섹터";     
    ls->enumDiskMsx35Sgl8Sect    = "MSX 3.5\" 단면, 8 섹터";     
    ls->enumDiskSvi525Dbl        = "SVI-328 5.25\" 양면";           
    ls->enumDiskSvi525Sgl        = "SVI-328 5.25\" 단면";  
    ls->enumDiskSf3Sgl           = "Sega SF-7000 3\" 단면";             
    ls->enumDiskSize             = "디스크 크기";
    ls->enumDiskFormat           = "포맷:";
    ls->enumDiskFormatUnformatted= "포맷 없음";


    //----------------------
    // Configuration related lines
    //----------------------

    ls->confTitle                = "blueMSX+ - 머신 설정 편집기";
    ls->confConfigText           = "머신 설정";
    ls->confSlotLayout           = "슬롯 배치";
    ls->confMemory               = "메모리";
    ls->confChipEmulation        = "칩 에뮬레이션";
    ls->confChipExtras          = "기타";

    ls->confOpenRom             = "롬 이미지 열기";
    ls->confSaveTitle            = "blueMSX+ - 설정 저장";
    ls->confSaveText             = "이미 파일이 있습니다. 기존 파일을 이 파일로 바꾸시겠습니까?";
    ls->confSaveAsTitle         = "다른 이름으로 설정 저장...";
    ls->confSaveAsMachineName    = "머신 이름:";
    ls->confDiscardTitle         = "blueMSX+ - 설정";
    ls->confExitSaveTitle        = "blueMSX+ - 머신 설정 편집기 나가기";
    ls->confExitSaveText         = "변경한 설정이 적용되기 위해선 저장을 해야 합니다. 현재 설정에 아무런 영향을 주지않고 그냥 나가겠습니까?";

    ls->confSlotLayoutGB         = "슬롯 배치 ";
    ls->confSlotExtSlotGB        = "외부 슬롯 ";
    ls->confBoardGB             = "시스템 ";
    ls->confBoardText           = "시스템 형식:";
    ls->confSlotPrimary          = "기본";
    ls->confSlotExpanded         = "확장 (4개의 서브슬롯)";

    ls->confSlotCart            = "카트리지";
    ls->confSlot                = "슬롯";
    ls->confSubslot             = "서브 슬롯";

    ls->confMemAdd               = "추가...";
    ls->confMemEdit              = "편집...";
    ls->confMemRemove            = "제거";
    ls->confMemSlot              = "슬롯";
    ls->confMemAddresss          = "주소";
    ls->confMemType              = "형식";
    ls->confMemRomImage          = "롬 이미지";
    
    ls->confChipVideoGB          = "비디오 ";
    ls->confChipVideoChip        = "비디오 칩:";
    ls->confChipVideoRam         = "비디오 램:";
    ls->confChipSoundGB          = "사운드 ";
    ls->confChipPsgStereoText    = " PSG 스테레오 변환";

    ls->confCmosGB                = "CMOS ";
    ls->confCmosEnable            = " CMOS 사용";
    ls->confCmosBattery           = " 충전지 사용";

    ls->confCpuFreqGB            = "CPU 주파수 ";
    ls->confZ80FreqText          = "Z80 주파수:";
    ls->confR800FreqText         = "R800 주파수:";
    ls->confFdcGB                = "플로피 디스크 컨트롤러 ";
    ls->confCFdcNumDrivesText    = "드라이브의 갯수:";

    ls->confEditMemTitle         = "blueMSX+ - 맵퍼 편집";
    ls->confEditMemGB            = "맵퍼 항목 ";
    ls->confEditMemType          = "형식:";
    ls->confEditMemFile          = "파일:";
    ls->confEditMemAddress       = "주소";
    ls->confEditMemSize          = "크기";
    ls->confEditMemSlot          = "슬롯";


    //----------------------
    // Shortcut lines
    //----------------------

	ls->shortcutKey             = "핫키";
    ls->shortcutDescription     = "단축키";

    ls->shortcutSaveConfig      = "blueMSX+ - 설정 저장";
    ls->shortcutOverwriteConfig = "이미 파일이 있습니다. 기존 파일을 이 파일로 바꾸시겠습니까?";
    ls->shortcutCreateConfig    = "새 단축키 설정을 저장하시겠습니까?";
    ls->shortcutExitConfig      = "blueMSX+ - 단축키 편집기 나가기";
    ls->shortcutDiscardConfig   = "변경한 설정이 적용되기 위해선 저장을 하셔야 합니다만 현재 설정에는 아무런 영향도 주지않고 그냥 나가겠습니까?";
    ls->shortcutSaveConfigAs    = "blueMSX+ - 다른이름으로 단축키 설정 저장...";
    ls->shortcutConfigName      = "설정 이름:";
    ls->shortcutNewProfile      = "< 새로운 프로필 >";
    ls->shortcutConfigTitle     = "blueMSX+ - 단축키 매핑 편집기";
    ls->shortcutAssign          = "적용";
    ls->shortcutPressText       = "단축키 새로 설정:";
    ls->shortcutScheme          = "단축키 스타일:";
    ls->shortcutCartInsert1     = "카트리지 1 삽입";
    ls->shortcutCartRemove1     = "카트리지 1 제거";
    ls->shortcutCartInsert2     = "카트리지 2 삽입";
    ls->shortcutCartRemove2     = "카트리지 2 제거";
    ls->shortcutSpecialMenu1    = "카트리지 1 - 사용자 지정 카트리지";
    ls->shortcutSpecialMenu2    = "카트리지 2 - 사용자 지정 카트리지";
    ls->shortcutCartAutoReset   = "카트리지 삽입후 에뮬레이터 재시작";
    ls->shortcutDiskInsertA     = "디스켓 A 삽입";
    ls->shortcutDiskDirInsertA  = "디스켓 A로 디렉토리 삽입";
    ls->shortcutDiskRemoveA     = "디스켓 A 꺼내기";
    ls->shortcutDiskChangeA     = "디스켓 A 빠른 교환(zip압축 파일)";
    ls->shortcutDiskAutoResetA  = "디스켓 A 삽입후 에뮬레이터 재시작";
    ls->shortcutDiskInsertB     = "디스켓 B 삽입";
    ls->shortcutDiskDirInsertB  = "디스켓 B로 디렉토리 삽입";
    ls->shortcutDiskRemoveB     = "디스켓 B 꺼내기";
    ls->shortcutCasInsert       = "카세트 삽입";
    ls->shortcutCasEject        = "카세트 꺼내기";
    ls->shortcutCasAutorewind   = "카세트 자동 되감기 기능 변환";
    ls->shortcutCasReadOnly     = "카세트 읽기전용 변환";
    ls->shortcutCasSetPosition  = "테잎 위치 설정";
    ls->shortcutCasRewind       = "카세트 되감기";
    ls->shortcutCasSave         = "카세트 이미지 저장";
    ls->shortcutPrnFormFeed     = "프린터 폼피드";
    ls->shortcutCpuStateLoad    = "CPU 상태 불러오기";
    ls->shortcutCpuStateSave    = "CPU 상태 저장";
    ls->shortcutCpuStateQload   = "상태 바로 불러오기";
    ls->shortcutCpuStateQsave   = "상태 바로 저장";
    ls->shortcutAudioCapture    = "소리 저장 시작/중지";
    ls->shortcutScreenshotOrig  = "화면 저장";
    ls->shortcutScreenshotSmall = "작은 크기로 화면 저장(원본)";
    ls->shortcutScreenshotLarge = "큰 크기로 화면 저장(원본)";
    ls->shortcutQuit            = "blueMSX+ 끝내기";
    ls->shortcutRunPause        = "에뮬레이션 시작/일시 정지";
    ls->shortcutStop            = "에뮬레이션 중지";
    ls->shortcutResetHard       = "하드 리셋";
    ls->shortcutResetSoft       = "소프트 리셋";
    ls->shortcutResetClean      = "전체 리셋";
    ls->shortcutSize1x          = "1x 창 크기 설정";
    ls->shortcutSize2x          = "2x 창 크기 설정";
    ls->shortcutSize3x          = "3x 창 크기 설정";
    ls->shortcutSize4x          = "4x 창 크기 설정";
    ls->shortcutSize5x          = "5x 창 크기 설정";
    ls->shortcutSize6x          = "6x 창 크기 설정";
    ls->shortcutSize7x          = "7x 창 크기 설정";
    ls->shortcutSize8x          = "8x 창 크기 설정";
    ls->shortcutSizeFullscreen  = "전체 화면으로 설정";
    ls->shortcutYm2413BackendCycle = "MSX-MUSIC 오디오 백엔드 전환";
    ls->shortcutY8950BackendCycle  = "MSX-AUDIO 오디오 백엔드 전환";
    ls->shortcutSizeMinimized   = "윈도우 최소화";
    ls->shortcutToggleFullscren = "전체 화면 변환";
    ls->shortcutVolumeIncrease  = "볼륨 증가";
    ls->shortcutVolumeDecrease  = "볼륨 감소";
    ls->shortcutVolumeMute      = "음소거";
    ls->shortcutVolumeStereo    = "모노/스테레오 변환";
    ls->shortcutSwitchMsxAudio  = "MSX-AUDIO 스위치 변환";
    ls->shortcutSwitchFront     = "파나소닉 프론트 스위치 변환";
    ls->shortcutSwitchPause     = "일시 정지 스위치";
    ls->shortcutToggleMouseLock = "마우스 고정 변환";
    ls->shortcutEmuSpeedMax     = "최대 에뮬레이션 속도";
    ls->shortcutEmuPlayReverse  = "에뮬레이션 되감기";                     // New in 2.8.3
    ls->shortcutEmuSpeedToggle  = "에뮬레이션 속도 최대화 변환";
    ls->shortcutEmuSpeedNormal  = "보통 에뮬레이션 속도";
    ls->shortcutEmuSpeedInc     = "에뮬레이션 속도 증가";
    ls->shortcutEmuSpeedDec     = "에뮬레이션 속도 감소";
    ls->shortcutThemeSwitch     = "테마 변환";
    ls->shortcutShowEmuProp     = "에뮬레이션 속성 보기";
    ls->shortcutShowVideoProp   = "비디오 속성 보기";
    ls->shortcutShowAudioProp   = "오디오 속성 보기";
    ls->shortcutShowCtrlProp    = "콘트롤 속성 보기";
    ls->shortcutShowEffectsProp = "효과 속성 표시";     // New in 2.9
    ls->shortcutShowSettProp    = "외부 설정 속성 보기";
    ls->shortcutShowPorts       = "포트 속성 보기";
    ls->shortcutShowLanguage    = "언어 보기";
    ls->shortcutShowMachines    = "머신 편집기 보기";
    ls->shortcutShowShortcuts   = "단축키 편집기 보기";
    ls->shortcutShowKeyboard    = "키보드 편집기 보기";
    ls->shortcutShowDebugger    = "디버거 보기"; 
    ls->shortcutShowTrainer     = "트레이너 보기"; 
    ls->shortcutShowMixer       = "믹서 보기";
    ls->shortcutShowHelp        = "도움말 보기";
    ls->shortcutShowAbout       = "blueMSX+에 대하여 보기";
    ls->shortcutShowFiles       = "파일 속성 보기";
    ls->shortcutToggleSpriteEnable = "스프라이트 보이기/숨기기 변환";
    ls->shortcutToggleFdcTiming = "FDD 액세스 가속 켜기/끄기";
    ls->shortcutToggleHddSdBoost = "HDD/SD 액세스 가속 켜기/끄기";
    ls->shortcutToggleNoSpriteLimits = "스프라이트 제한 전환";                 // New in 2.9
    ls->shortcutEnableMsxKeyboardQuirk = "MSX 키보드 특성 에뮬레이션";              // New in 2.9
    ls->shortcutToggleCpuTrace  = "CPU 트레이스 변환";
    ls->shortcutVideoLoad       = "리플레이: 파일에서 불러오기";             
    ls->shortcutVideoPlay       = "리플레이: 마지막 녹화 재생";   
    ls->shortcutVideoRecord     = "리플레이: 녹화";              
    ls->shortcutVideoStop       = "리플레이: 정지";                
    ls->shortcutVideoRender     = "리플레이: 동영상으로 내보내기";   


    //----------------------
    // Keyboard config lines
    //----------------------

    ls->keyconfigSelectedKey    = "선택된 키:";
    ls->keyconfigMappedTo       = "대응된 키:";
    ls->keyconfigMappingScheme  = "매핑 스타일:";

    
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
