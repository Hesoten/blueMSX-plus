// -----------------------------------------------------------------------
// file:        msxgr.h
// version:     0.5d (October 22, 2005)
//
// description: This is an unofficial API to the MSX Game Reader. Most
//              of the interface is based on assumptions, tests and some
//              reverse engineering.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2, or (at your option)
//  any later version. See COPYING for more details.
//
// Copyright 2005 Vincent van Dam (vincentd@erg.verweg.com)
//
// Modified 2026 by Hesoten for blueMSX+ fork.
// See https://github.com/Hesoten/blueMSX-plus for change history.
// -----------------------------------------------------------------------

#ifdef WII
#define _CMSXGR
#else
#include <windows.h>
#endif

#ifndef _CMSXGR
#define _CMSXGR

// Init's own failure, kept clear of the error codes MSXGR_Init returns.
#define CMSXGR_NO_LIBRARY (-0x10000)

// MSXGr.dll prototypes
typedef int   (__cdecl *_MSXGR_Init)();
typedef void  (__cdecl *_MSXGR_Uninit)();
typedef char* (__cdecl *_MSXGR_Err2Str)(int);
typedef int   (__cdecl *_MSXGR_GetVersion)();
typedef void  (__cdecl *_MSXGR_SetDebugMode)(int);
typedef bool  (__cdecl *_MSXGR_IsSlotEnable)(int);
typedef int   (__cdecl *_MSXGR_GetSlotStatus)(int,int*);
typedef int   (__cdecl *_MSXGR_ReadMemory)(int,char*,int,int);
typedef int   (__cdecl *_MSXGR_WriteMemory)(int,char*,int,int);
typedef int   (__cdecl *_MSXGR_ReadIO)(int,char*,int,int);
typedef int   (__cdecl *_MSXGR_WriteIO)(int,char*,int,int);
//typedef int   (__cdecl *_MSXGR_SetEventNotification)(int);

class CMSXGr
{
	public:
		CMSXGr() :
			hLib(NULL),
			nLastError(0),
			MSXGR_Err2Str(NULL),
			MSXGR_GetVersion(NULL),
			MSXGR_SetDebugMode(NULL),
			MSXGR_IsSlotEnable(NULL),
			MSXGR_GetSlotStatus(NULL),
			MSXGR_ReadMemory(NULL),
			MSXGR_WriteMemory(NULL),
			MSXGR_WriteIO(NULL),
			MSXGR_ReadIO(NULL)
		{};
		~CMSXGr() { Uninit(); };

		// (un)initialise the msx gamereader
		int   Init();
		void  Uninit();

		// debugging methods
		char* Err2Str(int Error);
		char* GetLastErrorStr();
		int   GetVersion();             // 0x01.00.00.06
		void  SetDebugMode(int nLevel); // 1 for msxgrlog.txt

		// msx gamereader status methods
		bool  IsSlotEnable(int nSlot);
		bool  IsCartridgeInserted(int nSlot);

		// msx gamereader control methods
		int   ReadMemory(int nSlot,char* pBuffer,int nAddress,int nLength);
		int   WriteMemory(int nSlot,char* pBuffer,int nAddress,int nLength);
		int   WriteIO(int nSlot,char* pBuffer,int nAddress,int nLength);
		int   ReadIO(int nSlot,char* pBuffer,int nAddress,int nLength);

	private:
		HINSTANCE	hLib;
		int		nLastError;

		// function pointers
		_MSXGR_Err2Str MSXGR_Err2Str;
		_MSXGR_GetVersion MSXGR_GetVersion;
		_MSXGR_SetDebugMode MSXGR_SetDebugMode;
		_MSXGR_IsSlotEnable MSXGR_IsSlotEnable;
		_MSXGR_GetSlotStatus MSXGR_GetSlotStatus;
		_MSXGR_ReadMemory MSXGR_ReadMemory;
		_MSXGR_WriteMemory MSXGR_WriteMemory;
		_MSXGR_WriteIO MSXGR_WriteIO;
		_MSXGR_ReadIO MSXGR_ReadIO;

		// not exactly sure about the meaning of the buffer,
		// replaced this method by the public IsCartridgeInserted
		// method.
		int   GetSlotStatus(int nSlot,int *pBuffer);
};

#endif
