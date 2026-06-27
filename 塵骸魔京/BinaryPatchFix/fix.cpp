// fix.dll - Jingai Makyou Win10 mouse fix
// Loaded via exe import table. Hooks 4 IME/input functions.

#include <windows.h>

typedef HWND (WINAPI *P1)(HWND);
typedef HIMC (WINAPI *P2)(HWND);
typedef BOOL (WINAPI *P3)(HIMC, BOOL);
typedef BOOL (WINAPI *P4)(DWORD, DWORD, BOOL);

static HWND WINAPI Hk1(HWND h) { return NULL; }
static HIMC WINAPI Hk2(HWND h) { return NULL; }
static BOOL WINAPI Hk3(HIMC h, BOOL f) { return FALSE; }
static BOOL WINAPI Hk4(DWORD a, DWORD b, BOOL c) { return FALSE; }

static void jmp(void* t, void* d) {
    DWORD o; BYTE* a = (BYTE*)t;
    VirtualProtect(a, 5, PAGE_EXECUTE_READWRITE, &o);
    a[0] = 0xE9; *(DWORD*)(a+1) = (DWORD)((BYTE*)d - a - 5);
    VirtualProtect(a, 5, o, &o);
}

__declspec(dllexport) int FixInit(void) { return 1; }

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID v) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);

    HMODULE hI = GetModuleHandleA("imm32.dll");
    HMODULE hU = GetModuleHandleA("user32.dll");
    void* f;
    if (hI) {
        f = GetProcAddress(hI, "ImmGetDefaultIMEWnd"); if (f) jmp(f, Hk1);
        f = GetProcAddress(hI, "ImmGetContext");        if (f) jmp(f, Hk2);
        f = GetProcAddress(hI, "ImmSetOpenStatus");     if (f) jmp(f, Hk3);
    }
    if (hU) {
        f = GetProcAddress(hU, "AttachThreadInput");    if (f) jmp(f, Hk4);
    }
    return TRUE;
}
