#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <cstdio>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include "../SimpleDebuggerPlugin/BlueMSXToolInterface.h"

// The pipe thread never calls the emulator. Every request is dispatched to the
// thread which loaded the plugin, where debugger snapshots are normally used.
static Interface api;
static HWND dispatchWindow;
static HANDLE worker;
static volatile LONG stopping;
static const UINT WM_MCP_REQUEST = WM_APP + 0x4d3;
static char pipeName[96];
static PSECURITY_DESCRIPTOR pipeSecurity;
static HWND statusWindow, connectionLabel, logLabel, logEdit;
static HFONT statusFont;
static HBRUSH statusBrush;
static std::vector<DWORD> clientPids;
static bool searchActive;
static std::string logText;

static void refreshStatus() {
    for (size_t i = 0; i < clientPids.size();) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, clientPids[i]);
        bool alive = process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        if (process) CloseHandle(process);
        if (alive) ++i;
        else clientPids.erase(clientPids.begin() + i);
    }
    if (!statusWindow) return;
    std::ostringstream status;
    int state = api.getState();
    status << "MCP bridge: listening  |  Clients: " << clientPids.size()
           << "  |  Emulator: " << (state == EMULATOR_STOPPED ? "stopped" :
                state == EMULATOR_PAUSED ? "paused" : "running");
    SetWindowTextA(connectionLabel, status.str().c_str());
}

static void appendLog(const std::string& message) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    char stamp[24];
    wsprintfA(stamp, "%02u:%02u:%02u  ", now.wHour, now.wMinute, now.wSecond);
    logText += stamp + message + "\r\n";
    if (logText.size() > 12000) logText.erase(0, logText.size() - 10000);
    if (logEdit) {
        SetWindowTextA(logEdit, logText.c_str());
        SendMessageA(logEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageA(logEdit, EM_SCROLLCARET, 0, 0);
    }
}

static void layoutStatus(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left - 20;
    int height = rect.bottom - rect.top;
    MoveWindow(connectionLabel, 10, 10, width, 22, TRUE);
    MoveWindow(logLabel, 10, 42, width, 22, TRUE);
    int logHeight = height - 76;
    MoveWindow(logEdit, 10, 64, width, logHeight > 40 ? logHeight : 40, TRUE);
}

static LRESULT CALLBACK statusProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CREATE) {
        HINSTANCE instance = GetModuleHandleA(NULL);
        connectionLabel = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, NULL, instance, NULL);
        logLabel = CreateWindowA("STATIC", "MCP ACTIVITY", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, NULL, instance, NULL);
        logEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, NULL, instance, NULL);
        HDC dc = GetDC(hwnd);
        int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(hwnd, dc);
        statusFont = CreateFontA(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, "Courier New");
        HWND controls[] = { connectionLabel, logLabel, logEdit };
        for (size_t i = 0; i < 3; ++i)
            SendMessageA(controls[i], WM_SETFONT,
                (WPARAM)(statusFont ? statusFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SetWindowTextA(logEdit, logText.c_str());
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }
    if (msg == WM_SIZE) { layoutStatus(hwnd); return 0; }
    if (msg == WM_TIMER) { refreshStatus(); return 0; }
    if (msg == WM_CTLCOLORSTATIC && statusBrush && !(api.isDarkMode && api.isDarkMode())) {
        SetBkColor((HDC)wparam, RGB(239, 237, 222));
        return (LRESULT)statusBrush;
    }
    if (msg == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
    if (msg == WM_DESTROY) {
        KillTimer(hwnd, 1);
        statusWindow = connectionLabel = logLabel = logEdit = NULL;
        if (statusFont) { DeleteObject(statusFont); statusFont = NULL; }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

struct Request {
    std::string command;
    std::string response;
    HANDLE done;
};

static std::string escape(const char* value) {
    std::string out = "\"";
    for (const unsigned char* p = (const unsigned char*)value; *p; ++p) {
        if (*p == '\\' || *p == '"') { out += '\\'; out += (char)*p; }
        else if (*p >= 32 && *p < 127) out += (char)*p;
        else { char b[7]; wsprintfA(b, "\\u%04x", *p); out += b; }
    }
    return out + '"';
}

static std::string error(const char* message) {
    return std::string("{\"error\":") + escape(message) + "}";
}

static std::string execute(const std::string& command) {
    std::istringstream in(command);
    std::string op;
    in >> op;
    if (op == "CLIENT_START" || op == "CLIENT_STOP") {
        DWORD pid;
        if (!(in >> pid) || pid == 0) return error("invalid client PID");
        for (size_t i = 0; i < clientPids.size(); ++i) {
            if (clientPids[i] == pid) {
                clientPids.erase(clientPids.begin() + i);
                break;
            }
        }
        if (op == "CLIENT_START") clientPids.push_back(pid);
        appendLog(std::string(op == "CLIENT_START" ? "MCP client connected: " :
            "MCP client disconnected: ") + std::to_string(pid));
        refreshStatus();
        return "{\"ok\":true}";
    }
    if (op == "SEARCH_BEGIN") {
        std::string label;
        unsigned int total;
        if (!(in >> label >> total) || label.size() > 64) return error("invalid search");
        searchActive = true;
        appendLog("RAM scan started: " + label + " (" + std::to_string(total) + " bytes)");
        return "{\"ok\":true}";
    }
    if (op == "SEARCH_PROGRESS") {
        unsigned int scanned, total;
        if (!(in >> scanned >> total)) return error("invalid progress");
        return "{\"ok\":true}";
    }
    if (op == "SEARCH_END") {
        unsigned int count;
        if (!(in >> count)) return error("invalid search result");
        searchActive = false;
        appendLog("RAM scan complete: " + std::to_string(count) + " candidates");
        return "{\"ok\":true}";
    }
    if (op == "STATE") {
        int state = api.getState();
        return std::string("{\"state\":") + (state == EMULATOR_STOPPED ? "\"stopped\"" :
            state == EMULATOR_PAUSED ? "\"paused\"" : "\"running\"") + "}";
    }
    if (op == "PAUSE" || op == "RUN" || op == "STEP") {
        if (op == "PAUSE") {
            if (api.getState() == EMULATOR_STOPPED) return error("start the emulator first");
            api.pause();
        }
        else if (op == "RUN") api.run();
        else {
            if (api.getState() != EMULATOR_PAUSED) return error("pause the emulator first");
            api.step();
        }
        appendLog(std::string("Emulator ") + (op == "PAUSE" ? "paused" :
            op == "RUN" ? "running" : "stepped"));
        return "{\"ok\":true}";
    }
    if (api.getState() != EMULATOR_PAUSED) return error("pause the emulator first");
    Snapshot* snapshot = api.create();
    if (!snapshot) return error("snapshot unavailable");
    std::string result;
    if (op == "LIST") {
        std::ostringstream out;
        out << "{\"devices\":[";
        for (int i = 0; i < api.getDeviceCount(snapshot); ++i) {
            Device* d = api.getDevice(snapshot, i);
            if (i) out << ',';
            out << "{\"index\":" << i << ",\"name\":" << escape(d->name)
                << ",\"type\":" << (int)d->type << ",\"blocks\":[";
            for (int j = 0; j < api.getMemoryBlockCount(d); ++j) {
                MemoryBlock* b = api.getMemoryBlock(d, j);
                if (j) out << ',';
                out << "{\"index\":" << j << ",\"name\":" << escape(b->name)
                    << ",\"start\":" << b->startAddress << ",\"size\":" << b->size
                    << ",\"writeProtected\":" << (b->writeProtected ? "true" : "false") << '}';
            }
            out << "]}";
        }
        out << "]}";
        result = out.str();
        if (!searchActive) appendLog("Listed debugger devices and memory blocks");
    }
    else if (op == "READ" || op == "WRITE") {
        int device, block;
        unsigned int address, size;
        if (!(in >> device >> block >> address >> size) || size == 0 || size > 2048 ||
            device < 0 || device >= api.getDeviceCount(snapshot)) result = error("invalid range or device");
        else {
            Device* d = api.getDevice(snapshot, device);
            if (block < 0 || block >= api.getMemoryBlockCount(d)) result = error("invalid block");
            else {
                MemoryBlock* b = api.getMemoryBlock(d, block);
                if (address < b->startAddress || size > b->size ||
                    address - b->startAddress > b->size - size) result = error("range outside block");
                else if (op == "READ") {
                    std::ostringstream out;
                    out << "{\"hex\":\"" << std::hex << std::setfill('0');
                    for (unsigned int i = 0; i < size; ++i)
                        out << std::setw(2) << (unsigned int)b->memory[address - b->startAddress + i];
                    out << "\"}";
                    result = out.str();
                    if (!searchActive)
                        appendLog("Read " + std::to_string(size) + " byte(s) at " +
                            std::to_string(address) + " on device " + std::to_string(device));
                }
                else if (b->writeProtected) result = error("block is write protected");
                else {
                    std::string hex;
                    in >> hex;
                    std::vector<UInt8> bytes(size);
                    bool valid = hex.size() == size * 2;
                    for (unsigned int i = 0; valid && i < size; ++i) {
                        unsigned int value;
                        std::istringstream pair(hex.substr(i * 2, 2));
                        pair >> std::hex >> value;
                        if (!pair || !pair.eof()) valid = false;
                        else bytes[i] = (UInt8)value;
                    }
                    if (!valid) result = error("invalid hex data");
                    else if (api.writeMemoryBlockSement(b, &bytes[0], address, size)) {
                        result = "{\"ok\":true}";
                        appendLog("Wrote " + std::to_string(size) + " byte(s) at " +
                            std::to_string(address) + " on device " + std::to_string(device));
                    }
                    else result = error("write rejected by device");
                }
            }
        }
    }
    else if (op == "REGISTERS") {
        int device;
        if (!(in >> device) || device < 0 || device >= api.getDeviceCount(snapshot))
            result = error("invalid device");
        else {
            Device* d = api.getDevice(snapshot, device);
            std::ostringstream out;
            out << "{\"banks\":[";
            for (int i = 0; i < api.getRegisterBankCount(d); ++i) {
                RegisterBank* bank = api.getRegisterBank(d, i);
                if (i) out << ',';
                out << "{\"name\":" << escape(bank->name) << ",\"registers\":[";
                for (unsigned int j = 0; j < bank->count; ++j) {
                    if (j) out << ',';
                    out << "{\"name\":" << escape(bank->reg[j].name) << ",\"value\":"
                        << bank->reg[j].value << ",\"width\":" << (int)bank->reg[j].width << '}';
                }
                out << "]}";
            }
            out << "]}";
            result = out.str();
            appendLog("Read registers on device " + std::to_string(device));
        }
    }
    else result = error("unknown command");
    api.destroy(snapshot);
    return result;
}

static LRESULT CALLBACK dispatch(HWND hwnd, UINT msg, WPARAM wparam, LPARAM data) {
    if (msg == WM_MCP_REQUEST) {
        Request* r = (Request*)data;
        r->response = execute(r->command);
        SetEvent(r->done);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, data);
}

static DWORD WINAPI serve(LPVOID) {
    while (!InterlockedCompareExchange(&stopping, 0, 0)) {
        SECURITY_ATTRIBUTES attributes = { sizeof(attributes), pipeSecurity, FALSE };
        HANDLE pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 8192, 8192, 0, &attributes);
        if (pipe == INVALID_HANDLE_VALUE) break;
        BOOL connected = ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && !InterlockedCompareExchange(&stopping, 0, 0)) {
            std::string line;
            char c;
            DWORD n;
            while (line.size() < 8192 && ReadFile(pipe, &c, 1, &n, NULL) && n == 1 && c != '\n')
                line += c;
            Request r;
            r.command = line;
            r.done = CreateEventA(NULL, TRUE, FALSE, NULL);
            if (!line.empty() && r.done && PostMessageA(dispatchWindow, WM_MCP_REQUEST, 0, (LPARAM)&r)) {
                if (WaitForSingleObject(r.done, INFINITE) == WAIT_OBJECT_0) {
                    r.response += '\n';
                    WriteFile(pipe, r.response.data(), (DWORD)r.response.size(), &n, NULL);
                    FlushFileBuffers(pipe);
                }
            }
            if (r.done) CloseHandle(r.done);
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

extern "C" __declspec(dllexport) int __stdcall Create12(Interface* host, char* name, int length) {
    if (length < 11) return 0;
    lstrcpynA(name, "MCP Bridge", length);
    api = *host;
    HANDLE token = NULL;
    DWORD bytes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    std::vector<char> tokenInfo(bytes);
    if (!GetTokenInformation(token, TokenUser, &tokenInfo[0], bytes, &bytes)) {
        CloseHandle(token);
        return 0;
    }
    LPSTR sid = NULL;
    BOOL gotSid = ConvertSidToStringSidA(((TOKEN_USER*)&tokenInfo[0])->User.Sid, &sid);
    CloseHandle(token);
    if (!gotSid) return 0;
    std::string sddl = std::string("D:P(A;;GA;;;") + sid + ")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1,
            &pipeSecurity, NULL)) return 0;
    WNDCLASSA cls = {};
    cls.lpfnWndProc = dispatch;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "BlueMSXPlusMcpDispatch";
    RegisterClassA(&cls);
    cls.lpfnWndProc = statusProc;
    cls.lpszClassName = "BlueMSXPlusMcpStatus";
    statusBrush = CreateSolidBrush(api.isDarkMode && api.isDarkMode() && api.getDarkBg ?
        (COLORREF)api.getDarkBg() : RGB(239, 237, 222));
    cls.hbrBackground = statusBrush;
    RegisterClassA(&cls);
    cls.lpfnWndProc = dispatch;
    cls.lpszClassName = "BlueMSXPlusMcpDispatch";
    dispatchWindow = CreateWindowExA(0, cls.lpszClassName, "", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, cls.hInstance, NULL);
    if (!dispatchWindow) { LocalFree(pipeSecurity); return 0; }
    wsprintfA(pipeName, "\\\\.\\pipe\\blueMSX-plus-mcp-%lu", GetCurrentProcessId());
    worker = CreateThread(NULL, 0, serve, NULL, 0, NULL);
    if (!worker) { DestroyWindow(dispatchWindow); dispatchWindow = NULL; LocalFree(pipeSecurity); return 0; }
    return 1;
}

extern "C" __declspec(dllexport) void __stdcall Destroy() {
    InterlockedExchange(&stopping, 1);
    CancelSynchronousIo(worker);
    HANDLE wake = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    while (MsgWaitForMultipleObjects(1, &worker, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    CloseHandle(worker);
    if (statusWindow) DestroyWindow(statusWindow);
    DestroyWindow(dispatchWindow);
    UnregisterClassA("BlueMSXPlusMcpStatus", GetModuleHandleA(NULL));
    if (statusBrush) { DeleteObject(statusBrush); statusBrush = NULL; }
    LocalFree(pipeSecurity);
}

extern "C" __declspec(dllexport) void __stdcall Show() {
    if (!statusWindow) {
        statusWindow = CreateWindowExA(0, "BlueMSXPlusMcpStatus", "MCP Bridge - blueMSX+",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 560, 300, NULL, NULL,
            GetModuleHandleA(NULL), NULL);
        if (statusWindow && api.applyDarkMode) api.applyDarkMode(statusWindow);
    }
    if (statusWindow) {
        refreshStatus();
        ShowWindow(statusWindow, SW_SHOWNORMAL);
        SetForegroundWindow(statusWindow);
    }
}
