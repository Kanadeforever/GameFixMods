/*
 * version.dll v12 final — 单文件免 DRM
 *
 * 编译: rc hook_res.rc && cl /O2 /LD /MT version_proxy.c hook_res.res /Fe:version.dll /link /DEF:version.def
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

static HINSTANCE g_hinst = NULL;
static HMODULE g_real_version = NULL;

static HMODULE load_real(void) {
    if (!g_real_version) {
        char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH); lstrcatA(p, "\\version.dll");
        g_real_version = LoadLibraryA(p);
    }
    return g_real_version;
}

static const BYTE GAMEKEY[] = {
    0x63,0x72,0x26,0x7B,0xDC,0xBD,0x71,0x2D,
    0x7D,0x5A,0x3E,0xED,0x6D,0x35,0xC5,0x75
};
static const BYTE MODULUS[] = {
    0x47,0x0B,0x9E,0x40,0xAB,0x7B,0x7D,0xE9,
    0xEF,0xCE,0x47,0xEE,0xE2,0xA8,0xCF
};

static HMODULE load_hook_dll(void) {
    HRSRC hRes = FindResourceA(g_hinst, MAKEINTRESOURCEA(100), MAKEINTRESOURCEA(10));
    if (!hRes) return LoadLibraryA("SRLoaderHook.dll");

    HGLOBAL hData = LoadResource(g_hinst, hRes);
    DWORD size = SizeofResource(g_hinst, hRes);
    LPVOID pData = LockResource(hData);
    if (!pData || !size) return LoadLibraryA("SRLoaderHook.dll");

    char dll_path[MAX_PATH];
    GetModuleFileNameA(NULL, dll_path, MAX_PATH);
    char *bs = strrchr(dll_path, '\\');
    if (bs) *(bs+1) = '\0';
    lstrcatA(dll_path, "SRLoaderHook.dll");

    if (GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE hFile = CreateFileA(dll_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, pData, size, &written, NULL);
            CloseHandle(hFile);
        }
    }

    HMODULE hHook = LoadLibraryA(dll_path);
    if (hHook) DeleteFileA(dll_path);
    return hHook ? hHook : LoadLibraryA("SRLoaderHook.dll");
}

static void cleanup_paul(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *bs = strrchr(path, '\\');
    if (!bs) return;
    *(bs + 1) = '\0';

    char paul[MAX_PATH], bak[MAX_PATH];
    lstrcpyA(paul, path); lstrcatA(paul, "paul.dll");
    lstrcpyA(bak, path);  lstrcatA(bak, "paul.bak");

    if (GetFileAttributesA(paul) == INVALID_FILE_ATTRIBUTES)
        return; /* paul.dll 不存在，无需操作 */

    /* 如果 paul.bak 已存在，先删掉 */
    if (GetFileAttributesA(bak) != INVALID_FILE_ATTRIBUTES)
        DeleteFileA(bak);

    /* 尝试改名；失败则直接删除 */
    if (!MoveFileA(paul, bak))
        DeleteFileA(paul);
}

static void set_dpi_aware(void) {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) return;

    /* 优先用 PerMonitorV2 (Win10 1703+) — context 值为 -4 */
    typedef BOOL (WINAPI *FnSetDpiAwarenessContext)(int);
    FnSetDpiAwarenessContext pFn = (FnSetDpiAwarenessContext)
        GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
    if (pFn) { pFn(-4); return; }

    /* 降级: Per-Monitor DPI (Win8.1+) — PROCESS_PER_MONITOR_DPI_AWARE = 2 */
    typedef HRESULT (WINAPI *FnSetDpiAwareness)(int);
    FnSetDpiAwareness pFn2 = (FnSetDpiAwareness)
        GetProcAddress(GetModuleHandleA("shcore.dll"), "SetProcessDpiAwareness");
    if (pFn2) { pFn2(2); return; }

    /* Vista/Win7 fallback: System DPI Aware */
    typedef BOOL (WINAPI *FnSetDPIAware)(void);
    FnSetDPIAware pFn3 = (FnSetDPIAware)
        GetProcAddress(hUser32, "SetProcessDPIAware");
    if (pFn3) pFn3();
}

static DWORD WINAPI worker(LPVOID p) {
    cleanup_paul();
    set_dpi_aware();

    BYTE *gi = (BYTE *)0x400000 + 0x5E3E80;
    for (int i = 0; i < 600; i++) {
        __try { if (gi[0] && gi[0] != 0xFF && gi[0] != 0xCC) break; }
        __except(1) {}; Sleep(100);
    }

    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, 0x2000, "Global\\SRLoaderMap");
    if (!hMap) hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                          0, 0x2000, "SRLoaderMap");
    if (!hMap) return 1;

    BYTE *map = (BYTE *)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0x2000);
    if (!map) return 1;

    memset(map, 0, 0x2000);
    memcpy(map + 0x940, MODULUS, sizeof(MODULUS));
    memcpy(map + 0x970, GAMEKEY, sizeof(GAMEKEY));
    memcpy(map + 0xA00, "470B9E40AB7B7DE9EFCE47EEE2A8CF", 30);
    *(DWORD *)(map + 0x518) = 0x012F3AB9;
    *(DWORD *)(map + 0x100) = 0x012F3AB9;
    *(DWORD *)(map + 0x104) = 0x00000002;
    *(DWORD *)(map + 0x990) = 0x012F3AB9;
    *(DWORD *)(map + 0x994) = 0x00400000;
    UnmapViewOfFile(map);

    load_hook_dll();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hi, DWORD reason, LPVOID rsv) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hi);
    g_hinst = hi;
    if ((DWORD)GetModuleHandle(NULL) != 0x400000) return TRUE;
    HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (th) CloseHandle(th);
    return TRUE;
}

static FARPROC gre(const char *n) { HMODULE h = load_real(); return h ? GetProcAddress(h, n) : NULL; }
#define X(ret, name, args, ca) \
    ret WINAPI P_##name args { FARPROC _fn = gre(#name); if (!_fn) { SetLastError(127); return 0; } return ((ret (WINAPI *)args)_fn)ca; }
X(DWORD, GetFileVersionInfoSizeA, (LPCSTR a, LPDWORD b), (a, b))
X(DWORD, GetFileVersionInfoSizeW, (LPCWSTR a, LPDWORD b), (a, b))
X(BOOL,  GetFileVersionInfoA, (LPCSTR a, DWORD b, DWORD c, LPVOID d), (a, b, c, d))
X(BOOL,  GetFileVersionInfoW, (LPCWSTR a, DWORD b, DWORD c, LPVOID d), (a, b, c, d))
X(BOOL,  VerQueryValueA, (LPCVOID a, LPCSTR b, LPVOID *c, PUINT d), (a, b, c, d))
X(BOOL,  VerQueryValueW, (LPCVOID a, LPCWSTR b, LPVOID *c, PUINT d), (a, b, c, d))
X(BOOL,  VerInstallFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h), (a,b,c,d,e,f,g,h))
X(DWORD, VerFindFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h), (a,b,c,d,e,f,g,h))
