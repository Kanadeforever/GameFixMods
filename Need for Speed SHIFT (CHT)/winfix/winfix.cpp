/*
 * winfix.dll — 独立窗口管理插件
 *
 * 功能: 无边框 / 居中 / 置顶 / 隐藏鼠标 / 锁定鼠标 / F10切换 / 高DPI
 * 配置: winfix.ini (与 DLL 同目录)
 * 加载: 放游戏目录, version.dll LoadLibrary 或改名 dinput8.dll 均可
 *
 * 编译: cl /O2 /LD /MT winfix.cpp /Fe:winfix.dll /link user32.lib
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <atomic>

// ============================================================
// Config (read from winfix.ini)
// ============================================================
static bool  g_borderless   = true;
static bool  g_centerWindow = true;
static bool  g_alwaysOnTop  = false;
static bool  g_hideCursor   = false;
static bool  g_lockCursor   = false;
static int   g_toggleKey    = VK_F10;  // 0=disable
static int   g_pollInterval = 1000;

// ============================================================
// Window state
// ============================================================
static HWND              g_hWnd             = NULL;
static bool              g_borderlessActive = false;
static LONG              g_savedStyle       = 0;
static LONG              g_savedExStyle     = 0;
static RECT              g_savedRect        = {};
static std::atomic<bool> g_workerRunning(true);
static bool              g_topmostApplied   = false;
static RECT              g_lastWinRect      = {};

static constexpr LONG kStylesToRemove =
    WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME |
    WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
static constexpr LONG kExStylesToRemove =
    WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

// ============================================================
// INI reader (minimal, no CRT dependency beyond sscanf)
// ============================================================
static int IniReadInt(const char* section, const char* key, int def) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* bs = strrchr(path, '\\');
    if (bs) *(bs+1) = '\0';
    strcat(path, "winfix.ini");
    char buf[64];
    int r = GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
    if (r > 0) return atoi(buf);
    return def;
}

// ============================================================
// Helpers
// ============================================================
static RECT GetMonitorFullRect(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    return mi.rcMonitor;
}
static bool IsFullscreen(HWND hwnd) {
    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (s & WS_POPUP) && !(s & WS_CAPTION);
}

// ============================================================
// Find game window
// ============================================================
struct FindCtx { DWORD pid; HWND hwnd; bool fallback; };
static BOOL CALLBACK FindProc(HWND hwnd, LPARAM lp) {
    FindCtx* c = (FindCtx*)lp;
    DWORD pid; GetWindowThreadProcessId(hwnd, &pid);
    if (pid != c->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    RECT r; if (!GetWindowRect(hwnd, &r)) return TRUE;
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;
    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (c->fallback) {
        if (!(s & WS_CHILD)) { c->hwnd = hwnd; return FALSE; }
    } else {
        if (s & WS_CAPTION) { c->hwnd = hwnd; return FALSE; }
    }
    return TRUE;
}
static HWND FindGameWindow() {
    FindCtx c = { GetCurrentProcessId(), NULL, false };
    EnumWindows(FindProc, (LPARAM)&c);
    if (!c.hwnd) { c.fallback = true; EnumWindows(FindProc, (LPARAM)&c); }
    return c.hwnd;
}

// ============================================================
// Borderless
// ============================================================
static void StripStyles(HWND hwnd) {
    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG e = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE,   s & ~kStylesToRemove);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (e & ~kExStylesToRemove) | WS_EX_APPWINDOW);
}
static void RestoreStyles(HWND hwnd, LONG style, LONG exStyle) {
    SetWindowLongPtrW(hwnd, GWL_STYLE,   style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
}
static void EnableBorderless(HWND hwnd) {
    if (IsFullscreen(hwnd)) return;
    if (!g_borderlessActive) {
        g_savedStyle   = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
        g_savedExStyle = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_savedRect);
    }
    StripStyles(hwnd);
    RECT mr = GetMonitorFullRect(hwnd);
    SetWindowPos(hwnd, NULL, mr.left, mr.top,
        mr.right - mr.left, mr.bottom - mr.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_borderlessActive = true;
}
static void DisableBorderless(HWND hwnd) {
    RestoreStyles(hwnd, g_savedStyle, g_savedExStyle);
    SetWindowPos(hwnd, NULL,
        g_savedRect.left, g_savedRect.top,
        g_savedRect.right - g_savedRect.left,
        g_savedRect.bottom - g_savedRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_borderlessActive = false;
}

// ============================================================
// Center window
// ============================================================
static void CenterWindow(HWND hwnd) {
    RECT wr;
    if (!GetWindowRect(hwnd, &wr)) return;
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return;
    int cx = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    int cy = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
    if (cx < 0) cx = 0; if (cy < 0) cy = 0;
    SetWindowPos(hwnd, NULL, cx, cy, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ============================================================
// Cursor
// ============================================================
static void ApplyCursorLock(HWND hwnd) {
    if (GetForegroundWindow() != hwnd) return;
    RECT r;
    if (GetClientRect(hwnd, &r)) {
        POINT tl = { r.left, r.top }, br = { r.right, r.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT cr = { tl.x, tl.y, br.x, br.y };
        ClipCursor(&cr);
    }
}

// ============================================================
// Toggle key state machine
// ============================================================
enum ToggleState { IDLE, PRESSED };
static ToggleState g_toggleState = IDLE;
static void PollToggleKey() {
    if (!g_hWnd || g_toggleKey == 0) return;
    if (GetForegroundWindow() != g_hWnd) { g_toggleState = IDLE; return; }
    bool down = (GetAsyncKeyState(g_toggleKey) & 0x8000) != 0;
    switch (g_toggleState) {
    case IDLE:    if (down) g_toggleState = PRESSED; break;
    case PRESSED: if (!down) {
        g_borderlessActive ? DisableBorderless(g_hWnd) : EnableBorderless(g_hWnd);
        g_toggleState = IDLE;
    } break;
    }
}

// ============================================================
// Worker thread
// ============================================================
static DWORD WINAPI WorkerThread(LPVOID) {
    Sleep(500);
    bool appliedInitial = false;
    int  slowTick = 0, slowEvery = g_pollInterval / 50;
    if (slowEvery < 1) slowEvery = 1;
    bool cursorHidden = false;

    while (g_workerRunning.load(std::memory_order_acquire)) {
        if (g_hWnd && !IsWindow(g_hWnd)) {
            g_hWnd = NULL; g_topmostApplied = false;
            if (cursorHidden) { ShowCursor(TRUE); cursorHidden = false; }
            ClipCursor(NULL);
        }
        if (!g_hWnd) { g_hWnd = FindGameWindow(); if (g_hWnd) appliedInitial = false; }

        if (g_hWnd) {
            PollToggleKey();
            if (g_lockCursor && GetForegroundWindow() == g_hWnd) ApplyCursorLock(g_hWnd);
            else if (g_lockCursor) ClipCursor(NULL);
            if (g_hideCursor && GetForegroundWindow() == g_hWnd && !cursorHidden) {
                ShowCursor(FALSE); cursorHidden = true;
            } else if (!g_hideCursor && cursorHidden) {
                ShowCursor(TRUE); cursorHidden = false;
            }
        }

        if (++slowTick >= slowEvery) {
            slowTick = 0;
            if (g_hWnd) {
                if (!appliedInitial) {
                    appliedInitial = true;
                    if (g_borderless) EnableBorderless(g_hWnd);
                    if (g_centerWindow && !g_borderless) {
                        CenterWindow(g_hWnd);
                        GetWindowRect(g_hWnd, &g_lastWinRect);
                    }
                    if (g_alwaysOnTop && !g_topmostApplied) {
                        SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                        g_topmostApplied = true;
                    }
                }
                if (g_borderlessActive && !IsFullscreen(g_hWnd)) {
                    LONG s = (LONG)GetWindowLongPtrW(g_hWnd, GWL_STYLE);
                    if (s & WS_CAPTION) {
                        StripStyles(g_hWnd);
                        RECT mr = GetMonitorFullRect(g_hWnd);
                        SetWindowPos(g_hWnd, NULL, mr.left, mr.top,
                            mr.right - mr.left, mr.bottom - mr.top,
                            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    }
                }
                if (g_alwaysOnTop && !g_topmostApplied) {
                    SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    g_topmostApplied = true;
                }
                if (g_centerWindow && !g_borderlessActive) {
                    RECT wr; if (GetWindowRect(g_hWnd, &wr)) {
                        int w = wr.right - wr.left, h = wr.bottom - wr.top;
                        int ow = g_lastWinRect.right - g_lastWinRect.left;
                        int oh = g_lastWinRect.bottom - g_lastWinRect.top;
                        if (w != ow || h != oh) {
                            SetRect(&g_lastWinRect, wr.left, wr.top, wr.right, wr.bottom);
                            CenterWindow(g_hWnd);
                        }
                    }
                }
            }
        }
        Sleep(50);
    }
    if (cursorHidden) ShowCursor(TRUE);
    ClipCursor(NULL);
    return 0;
}

// ============================================================
// DPI awareness
// ============================================================
static void ApplyDPIAwareness() {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) return;
    // 1) PerMonitorV2 (Win10 1703+)
    typedef BOOL (WINAPI *FnCtx)(int);
    FnCtx pCtx = (FnCtx)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
    if (pCtx) { pCtx(-4); return; }
    // 2) Per-Monitor (Win8.1+)
    HMODULE hShcore = GetModuleHandleA("shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *FnAw)(int);
        FnAw pAw = (FnAw)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (pAw) { pAw(2); return; }
    }
    // 3) System (Vista/Win7)
    typedef BOOL (WINAPI *FnSys)(void);
    FnSys pSys = (FnSys)GetProcAddress(hUser32, "SetProcessDPIAware");
    if (pSys) pSys();
}

// ============================================================
// DllMain
// ============================================================
BOOL WINAPI DllMain(HINSTANCE hi, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hi);
    if ((DWORD)GetModuleHandle(NULL) != 0x400000) return TRUE;

    // Read config
    g_borderless   = IniReadInt("Window", "Borderless", 1) != 0;
    g_centerWindow = IniReadInt("Window", "CenterWindow", 1) != 0;
    g_alwaysOnTop  = IniReadInt("Window", "AlwaysOnTop", 0) != 0;
    g_hideCursor   = IniReadInt("Window", "HideCursor", 0) != 0;
    g_lockCursor   = IniReadInt("Window", "LockCursor", 0) != 0;
    g_toggleKey    = IniReadInt("Window", "ToggleKey", VK_F10);
    g_pollInterval = IniReadInt("Window", "PollInterval", 1000);
    if (g_pollInterval < 500) g_pollInterval = 500;

    ApplyDPIAwareness();
    CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    return TRUE;
}
