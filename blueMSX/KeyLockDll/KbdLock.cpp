#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <windows.h>

#pragma data_seg(".shared")
HHOOK g_hMsgHook = NULL;
HHOOK g_hKeyHook = NULL;

bool initialized = false;

int   g_iFlags = 0;
#pragma data_seg()

const int KILL_WINKEY =  0x0001;
const int KILL_CTRLESC = 0x0002;
const int KILL_COMBOS =  0x0004;
const int KILL_CONTEXT = 0x0008;
const int KILL_CONVERT = 0x0010; // Add JAPANESE special key remove function 

#ifndef VK_OEM_COPY
#define VK_OEM_COPY 0xf2
#endif

LRESULT CALLBACK Msg_HookProc(int iCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK Key_HookProc(int iCode, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) void Kill(int iFlags);


extern "C" BOOL __stdcall _DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/)
{
    if(fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }

    return TRUE;
}


LRESULT CALLBACK Msg_HookProc(int iCode, WPARAM wParam, LPARAM lParam)
{
    if(wParam != PM_NOREMOVE && iCode >= 0) {
        if(((MSG*)lParam)->message == WM_SYSCOMMAND && ((MSG*)lParam)->wParam == SC_TASKLIST) {
            if((g_iFlags & KILL_WINKEY) && !(g_iFlags & KILL_CTRLESC) && !((GetAsyncKeyState(VK_CONTROL) & 32768) && (GetAsyncKeyState(VK_ESCAPE) & 32768))) {
                ((MSG*)lParam)->message = WM_NULL;
            }
            else if((g_iFlags & KILL_WINKEY) && (g_iFlags & KILL_CTRLESC)) {
                ((MSG*)lParam)->message = WM_NULL;
            }
            else if(!(g_iFlags & KILL_WINKEY) && (g_iFlags & KILL_CTRLESC) && GetAsyncKeyState(VK_CONTROL) && GetAsyncKeyState(VK_ESCAPE)) {
                ((MSG*)lParam)->message = WM_NULL;
            }
        }
        else if(((MSG*)lParam)->message == WM_CONTEXTMENU && GetAsyncKeyState(VK_APPS) && (g_iFlags & KILL_CONTEXT)) {
            ((MSG*)lParam)->message = WM_NULL;
        }
        else if(((MSG*)lParam)->message == WM_HOTKEY && (LOWORD(((MSG*)lParam)->lParam) & MOD_WIN) && (g_iFlags & KILL_COMBOS)) {
            ((MSG*)lParam)->message = WM_NULL;
        }
    }

    return CallNextHookEx(g_hMsgHook, iCode, wParam, lParam);
}


LRESULT CALLBACK Key_HookProc(int iCode, WPARAM wParam, LPARAM lParam)
{
    if(iCode != HC_NOREMOVE && iCode >= 0) {
        if((wParam == VK_LWIN || wParam == VK_RWIN) && (g_iFlags & KILL_COMBOS)) {
            return 1;
        }

        if(wParam == VK_APPS && (g_iFlags & KILL_CONTEXT)) {
            return 1;
        }
    }

    // Add JAPANESE special key remove function 
    if((wParam == VK_NONCONVERT || // Henkan 
        wParam == VK_CONVERT || // MuHenkan 
        wParam == VK_OEM_COPY) // katakana/Hiragana 
        && (g_iFlags & KILL_CONVERT)) 
    { 
        return 1; 
    } 

    return CallNextHookEx(g_hKeyHook, iCode, wParam, lParam);
}

extern "C" __declspec(dllexport) void Kill(int iFlags)
{
    if (iFlags == 0) {
        if (initialized) {
            if(g_hMsgHook) {
                UnhookWindowsHookEx(g_hMsgHook);
            }
            if(g_hKeyHook) {
                UnhookWindowsHookEx(g_hKeyHook);
            }
            initialized = false;
        }

        return;
    }

    g_iFlags = iFlags;

    if (!initialized) {
        HINSTANCE hInst = GetModuleHandle("kbdlock.dll");
        g_hMsgHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)Msg_HookProc, hInst, 0);
        g_hKeyHook = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)Key_HookProc, hInst, 0);
        if(!g_hMsgHook) {
            g_hMsgHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)Msg_HookProc, hInst, 0);
        }
        initialized = true;
    }
}

extern "C" __declspec(dllexport) void enable()
{
    g_iFlags = 0x1f; // Add JAPANESE special key remove function
    
    if (!initialized) {
        HINSTANCE hInst = GetModuleHandle("kbdlock.dll");
        g_hMsgHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)Msg_HookProc, hInst, 0);
        g_hKeyHook = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)Key_HookProc, hInst, 0);
        if(!g_hMsgHook) {
            g_hMsgHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)Msg_HookProc, hInst, 0);
        }
        initialized = true;
    }
}

extern "C" __declspec(dllexport) void disable()
{
    g_iFlags = 0;

    if (initialized) {
        if(g_hMsgHook) {
            UnhookWindowsHookEx(g_hMsgHook);
        }
        if(g_hKeyHook) {
            UnhookWindowsHookEx(g_hKeyHook);
        }
        initialized = false;
    }
}
