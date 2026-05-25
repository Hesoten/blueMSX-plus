#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <windows.h>
#include "BlueMSXToolInterface.h"
#include "ToolInterface.h"

static ToolSnapshotCreate              toolSnapshotCreate;
static ToolSnapshotDestroy             toolSnapshotDestroy;
static ToolGetEmulatorState            toolGetState;
static ToolSnapshotGetDeviceCount      toolSnapshotGetDeviceCount;
static ToolSnapshotGetDevice           toolSnapshotGetDevice;

static ToolDeviceGetMemoryBlockCount   toolDeviceGetMemoryBlockCount;
static ToolDeviceGetMemoryBlock        toolDeviceGetMemoryBlock;
static ToolDeviceWriteMemory           toolDeviceWriteMemory;
static ToolDeviceGetRegisterBankCount  toolDeviceGetRegisterBankCount;
static ToolDeviceGetRegisterBank       toolDeviceGetRegisterBank;
static ToolDeviceWriteRegister         toolDeviceWriteRegister;
static ToolDeviceGetCallstackCount     toolDeviceGetCallstackCount;
static ToolDeviceGetCallstack          toolDeviceGetCallstack;
static ToolDeviceGetIoPortsCount       toolDeviceGetIoPortsCount;
static ToolDeviceGetIoPorts            toolDeviceGetIoPorts;
static ToolDeviceWriteIoPort           toolDeviceWriteIoPort;
static ToolAction                      toolDeviceRun;
static ToolAction                      toolDeviceStop;
static ToolAction                      toolDevicePause;
static ToolAction                      toolDeviceStep;
static ToolBreakpoint                  toolSetBreakpoint;
static ToolBreakpoint                  toolClearBreakpoint;
static ToolPath                        toolGetToolDirectory;
static ToolEmulatorVersion             toolGetEmulatorVersion;
static ToolEnableVramAccessCheck       toolEnableVramAccessCheck;
static ToolSetWatchpoint               toolSetWatchpoint;
static ToolClearWatchpoint             toolClearWatchpoint;
static ToolAction                      toolDeviceStepBack;

/* the newer ABI (zero on hosts that predate these entries -- always null-check before use). */
static ToolApplyDarkMode               toolApplyDarkMode;
static ToolIsDarkMode                  toolIsDarkMode;
static ToolGetDarkColor                toolGetDarkBg;
static ToolGetDarkColor                toolGetDarkFg;
static ToolGetDarkBrush                toolGetDarkBgBrush;
static ToolShellOpenFile               toolShellOpenFileDialog;
static ToolShellSaveFile               toolShellSaveFileDialog;

static HINSTANCE hInstance;


Snapshot* SnapshotCreate() {
    return toolSnapshotCreate();
}

EmulatorState GetEmulatorState()
{
    return toolGetState();
}

void SnapshotDestroy(Snapshot* snapshot) {
    toolSnapshotDestroy(snapshot);
}

int SnapshotGetDeviceCount(Snapshot* snapshot) {
    return toolSnapshotGetDeviceCount(snapshot);
}

Device* SnapshotGetDevice(Snapshot* snapshot, int index) {
    return toolSnapshotGetDevice(snapshot, index);
}

int DeviceGetMemoryBlockCount(Device* device) {
    return toolDeviceGetMemoryBlockCount(device);
}

MemoryBlock* DeviceGetMemoryBlock(Device* device, int index) {
    return toolDeviceGetMemoryBlock(device, index);
}

bool DeviceWriteMemoryBlockMemory(MemoryBlock* memoryBlock, void* data, int startAddr, int size) {
    return toolDeviceWriteMemory(memoryBlock, data, startAddr, size) != 0;
}

int DeviceGetRegisterBankCount(Device* device) {
    return toolDeviceGetRegisterBankCount(device);
}

RegisterBank* DeviceGetRegisterBank(Device* device, int index) {
    return toolDeviceGetRegisterBank(device, index);
}

bool DeviceWriteRegisterBankRegister(RegisterBank* regBank, int regIndex, UInt32 value) {
    return toolDeviceWriteRegister(regBank, regIndex, value) != 0;
}

int DeviceGetCallstackCount(Device* device) {
    return toolDeviceGetCallstackCount(device);
}

Callstack* DeviceGetCallstack(Device* device, int index) {
    return toolDeviceGetCallstack(device, index);
}

int DeviceGetIoPortsCount(Device* device) {
    return toolDeviceGetIoPortsCount(device);
}

IoPorts* DeviceGetIoPorts(Device* device, int index) {
    return toolDeviceGetIoPorts(device, index);
}

bool DeviceWriteIoPortsPort(IoPorts* ioPorts, int portIndex, UInt32 value) {
    return toolDeviceWriteIoPort(ioPorts, portIndex, value) != 0;
}

void EmulatorRun() {
    toolDeviceRun();
}

void EmulatorStop() {
    toolDeviceStop();
}

void EmulatorPause() {
    toolDevicePause();
}

void EmulatorStep() {
    toolDeviceStep();
}

void SetBreakpoint(UInt16 address) {
    toolSetBreakpoint(address);
}

void ClearBreakpoint(UInt16 address) {
    toolClearBreakpoint(address);
}

void EnableVramAccessCheck(int enable) {
    toolEnableVramAccessCheck(enable);
}

void SetWatchpoint(DeviceType devType, int address, WatchpointCondition condition, UInt32 referenceValue, int size) {
    toolSetWatchpoint(devType, address, condition, referenceValue, size);
}

void ClearWatchpoint(DeviceType devType, int address) {
    toolClearWatchpoint(devType, address);
}

void EmulatorStepBack() {
    toolDeviceStepBack();
}

char* GetToolPath() {
    return toolGetToolDirectory();
}

int GetEmulatorMajorVersion() {
    int major, minor, buildNumber;
    toolGetEmulatorVersion(&major, &minor, &buildNumber);
    return major;
}

int GetEmulatorMinorVersion() {
    int major, minor, buildNumber;
    toolGetEmulatorVersion(&major, &minor, &buildNumber);
    return minor;
}

int GetEmulatorBuildNumber()
{
    int major, minor, buildNumber;
    toolGetEmulatorVersion(&major, &minor, &buildNumber);
    return buildNumber;
}

HINSTANCE GetDllHinstance()
{
    return hInstance;
}

/* these helpers are safe to call on hosts that predate them (no-op / FALSE). */
void ApplyDarkMode(HWND hWnd) {
    if (toolApplyDarkMode != NULL) toolApplyDarkMode(hWnd);
}

bool IsDarkMode() {
    return toolIsDarkMode != NULL && toolIsDarkMode() != 0;
}

COLORREF GetDarkBg() {
    return toolGetDarkBg != NULL ? (COLORREF)toolGetDarkBg() : RGB(255, 255, 255);
}

COLORREF GetDarkFg() {
    return toolGetDarkFg != NULL ? (COLORREF)toolGetDarkFg() : RGB(0, 0, 0);
}

HBRUSH GetDarkBgBrush() {
    return toolGetDarkBgBrush != NULL ? toolGetDarkBgBrush() : NULL;
}

bool ShellOpenFileDialog(HWND owner, const char* title, const char* filter,
                         const char* initialDir, const char* defExt,
                         int* filterIndex, char* outPath, int outPathCap) {
    return toolShellOpenFileDialog != NULL &&
           toolShellOpenFileDialog(owner, title, filter, initialDir, defExt,
                                   filterIndex, outPath, outPathCap) != 0;
}

bool ShellSaveFileDialog(HWND owner, const char* title, const char* filter,
                         const char* initialDir, const char* defExt,
                         int* filterIndex, char* outPath, int outPathCap) {
    return toolShellSaveFileDialog != NULL &&
           toolShellSaveFileDialog(owner, title, filter, initialDir, defExt,
                                   filterIndex, outPath, outPathCap) != 0;
}

extern "C" __declspec(dllexport) int __stdcall Create12(Interface* toolInterface, char* name, int length)
{
    strcpy(name, OnGetName());
    
    toolSnapshotCreate              = toolInterface->create;
    toolSnapshotDestroy             = toolInterface->destroy;
    toolGetState                    = toolInterface->getState;
    toolSnapshotGetDeviceCount      = toolInterface->getDeviceCount;
    toolSnapshotGetDevice           = toolInterface->getDevice;
    toolDeviceGetMemoryBlockCount   = toolInterface->getMemoryBlockCount;
    toolDeviceGetMemoryBlock        = toolInterface->getMemoryBlock;
    toolDeviceWriteMemory           = toolInterface->writeMemoryBlockSement;
    toolDeviceGetRegisterBankCount  = toolInterface->getRegisterBankCount;
    toolDeviceGetRegisterBank       = toolInterface->getRegisterBank;
    toolDeviceWriteRegister         = toolInterface->writeRegisterBankRegister;
    toolDeviceGetCallstackCount     = toolInterface->getCallstackCount;
    toolDeviceGetCallstack          = toolInterface->getCallstack;
    toolDeviceGetIoPortsCount       = toolInterface->getIoPortsCount;
    toolDeviceGetIoPorts            = toolInterface->getIoPorts;
    toolDeviceWriteIoPort           = toolInterface->writeIoPortsPort;
    toolDeviceRun                   = toolInterface->run;
    toolDeviceStop                  = toolInterface->stop;
    toolDevicePause                 = toolInterface->pause;
    toolDeviceStep                  = toolInterface->step;
    toolSetBreakpoint               = toolInterface->setBreakpoint;
    toolClearBreakpoint             = toolInterface->clearBreakpoint;
    toolGetToolDirectory            = toolInterface->getToolDirectory;
    toolGetEmulatorVersion          = toolInterface->getEmulatorVersion;
    toolEnableVramAccessCheck       = toolInterface->enableVramAccessCheck;
    toolSetWatchpoint               = toolInterface->setWatchpoint;
    toolClearWatchpoint             = toolInterface->clearWatchpoint;
    toolDeviceStepBack              = toolInterface->stepBack;
    toolApplyDarkMode               = toolInterface->applyDarkMode;
    toolIsDarkMode                  = toolInterface->isDarkMode;
    toolGetDarkBg                   = toolInterface->getDarkBg;
    toolGetDarkFg                   = toolInterface->getDarkFg;
    toolGetDarkBgBrush              = toolInterface->getDarkBgBrush;
    toolShellOpenFileDialog         = toolInterface->shellOpenFileDialog;
    toolShellSaveFileDialog         = toolInterface->shellSaveFileDialog;

    OnCreateTool();

    return 1;
}

extern "C" __declspec(dllexport) void __stdcall Destroy()
{
    OnDestroyTool();
}

extern "C" __declspec(dllexport) void __stdcall Show()
{
    OnShowTool();
}

extern "C" __declspec(dllexport) void __stdcall NotifyEmulatorStart()
{
    OnEmulatorStart();
}

extern "C" __declspec(dllexport) void __stdcall NotifyEmulatorStop()
{
    OnEmulatorStop();
}

extern "C" __declspec(dllexport) void __stdcall NotifyEmulatorPause()
{
    OnEmulatorPause();
}

extern "C" __declspec(dllexport) void __stdcall NotifyEmulatorResume()
{
    OnEmulatorResume();
}

extern "C" __declspec(dllexport) void __stdcall NotifyEmulatorReset()
{
    OnEmulatorReset();
}

extern "C" __declspec(dllexport) void __stdcall EmulatorTrace(const char* message)
{
    OnEmulatorTrace(message);
}

extern "C" __declspec(dllexport) void __stdcall EmulatorSetBreakpoint(UInt16 slot, UInt16 page, UInt16 address)
{
    if (page == 0xffff) {
        if (slot == 0xffff) {
            OnEmulatorSetBreakpoint(address);
        }
        else {
            OnEmulatorSetBreakpoint(slot, address);
        }
    }
    else {
        if (slot == 0xffff) {
            OnEmulatorSetBreakpoint(address);
        }
        else {
            OnEmulatorSetBreakpoint(slot, page, address);
        }
    }
}

__declspec(dllexport) void __stdcall SetLanguage(int languageId)
{
    OnSetLanguage((LanguageId)languageId);
}

__declspec(dllexport) const char* __stdcall GetName()
{
    return OnGetName();
}


extern "C" int APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if(fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
    }

    return TRUE;
}
