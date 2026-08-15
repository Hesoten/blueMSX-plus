/*****************************************************************************
** $Source: /cygdrive/d/Private/_SVNROOT/bluemsx/blueMSX/SimpleDebuggerPlugin/ToolInterface.h,v $
**
** $Revision: 1.11 $
**
** $Date: 2009-07-01 05:01:04 $
**
** More info: http://www.bluemsx.com
**
** Copyright (C) 2003-2004 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
******************************************************************************
*/
#ifndef TOOL_INTERFACE_H
#define TOOL_INTERFACE_H

#include <windows.h>

//
// See the include file below for the datastructures used in the interface
//
#include "BlueMSXToolInterface.h"

Snapshot* SnapshotCreate();
void SnapshotDestroy(Snapshot* snapshot);
EmulatorState GetEmulatorState();
int SnapshotGetDeviceCount(Snapshot* snapshot);
Device* SnapshotGetDevice(Snapshot* snapshot, int index);
int DeviceGetMemoryBlockCount(Device* device);
MemoryBlock* DeviceGetMemoryBlock(Device* device, int index);
bool DeviceWriteMemoryBlockMemory(MemoryBlock* memoryBlock, void* data, int startAddr, int size);
int DeviceGetRegisterBankCount(Device* device);
RegisterBank* DeviceGetRegisterBank(Device* device, int index);
bool DeviceWriteRegisterBankRegister(RegisterBank* regBank, int regIndex, UInt32 value);
int DeviceGetCallstackCount(Device* device);
Callstack* DeviceGetCallstack(Device* device, int index);
int DeviceGetIoPortsCount(Device* device);
IoPorts* DeviceGetIoPorts(Device* device, int index);
bool DeviceWriteIoPortsPort(IoPorts* ioPorts, int portIndex, UInt32 value);

void EmulatorRun();
void EmulatorStop();
void EmulatorPause();
void EmulatorStep();
void EmulatorStepBack();

void SetBreakpoint(UInt16 address);
void ClearBreakpoint(UInt16 address);

char* GetToolPath();
int GetEmulatorMajorVersion();
int GetEmulatorMinorVersion();
int GetEmulatorBuildNumber();

void EnableVramAccessCheck(int enable);

void SetWatchpoint(DeviceType devType, int address, WatchpointCondition condition, UInt32 referenceValue, int size);
void ClearWatchpoint(DeviceType devType, int address);

HINSTANCE GetDllHinstance();

/* Host extensions: dark mode + IFileDialog. Each helper degrades to
** no-op / FALSE / RGB(255,255,255) when loaded by an older host. */
void ApplyDarkMode(HWND hWnd);
bool IsDarkMode();
COLORREF GetDarkBg();
COLORREF GetDarkFg();
HBRUSH GetDarkBgBrush();
bool ShellOpenFileDialog(HWND owner, const char* title, const char* filter,
                         const char* initialDir, const char* defExt,
                         int* filterIndex, char* outPath, int outPathCap);
bool ShellSaveFileDialog(HWND owner, const char* title, const char* filter,
                         const char* initialDir, const char* defExt,
                         int* filterIndex, char* outPath, int outPathCap);

/* The emulator's themed message box. Use this rather than MessageBoxU, which
** the DLL build resolves to the plain system box and stays light. */
int ShowMessageBox(HWND owner, const char* text, const char* caption, UInt32 type);

//
// To be implemented by the user....
//
void OnCreateTool();
void OnDestroyTool();
void OnShowTool();
void OnEmulatorStart();
void OnEmulatorStop();
void OnEmulatorPause();
void OnEmulatorResume();
void OnEmulatorReset();
void OnEmulatorTrace(const char* message);
void OnEmulatorSetBreakpoint(UInt16 address);
void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 address);
void OnEmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address);
void OnSetLanguage(LanguageId languageId);

const char* OnGetName();

#endif
