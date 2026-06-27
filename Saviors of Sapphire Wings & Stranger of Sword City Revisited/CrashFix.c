// ============================================================
// CrashFix.asi v2.2 — 输入崩溃修复
//
// 修复 GetRawInputDeviceList 的 TOCTOU 缓冲区溢出崩溃
// 手柄+键盘同时使用时不再崩溃
//
// 适用: SoR.exe / SwordOfAlien.exe / Launcher.exe
// 加载: Ultimate ASI Loader
// 配置: CrashFix.ini (与exe同目录)
// ============================================================
#include <windows.h>
#include <stdio.h>

// ============================================================
// 配置
// ============================================================
typedef struct {
    int fix_toctou;     // TOCTOU修复
    int enable_cs;      // 临界区保护
    int debug_log;      // 调试日志
} CrashFixConfig;

static CrashFixConfig g_cfg = { 1, 1, 0 };
static CRITICAL_SECTION g_cs;

// 原始函数指针
typedef UINT (WINAPI *PFN_GetRawInputDeviceList)(PRAWINPUTDEVICELIST, PUINT, UINT);
static PFN_GetRawInputDeviceList g_Orig = NULL;

// TOCTOU 状态
static UINT g_last_count = 0;
static int  g_pending_fix = 0;

// ============================================================
// 调试
// ============================================================
static void dbg(const char* fmt, ...) {
    if (!g_cfg.debug_log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0 && len < (int)sizeof(buf) - 2) {
        buf[len] = '\r';
        buf[len+1] = '\n';
        buf[len+2] = '\0';
    }
    OutputDebugStringA("[CrashFix] ");
    OutputDebugStringA(buf);
}

// ============================================================
// IAT Hook 引擎
// ============================================================
static BOOL IAT_Hook(HMODULE hModule, LPCSTR dllName, LPCSTR funcName,
                     PVOID hookFunc, PVOID* origFunc) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PBYTE)hModule + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress == 0) return FALSE;

    PIMAGE_IMPORT_DESCRIPTOR impDesc = (PIMAGE_IMPORT_DESCRIPTOR)
        ((PBYTE)hModule + impDir.VirtualAddress);

    for (; impDesc->Name != 0; impDesc++) {
        LPCSTR name = (LPCSTR)((PBYTE)hModule + impDesc->Name);
        if (_stricmp(name, dllName) != 0) continue;

        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((PBYTE)hModule + impDesc->FirstThunk);
        PIMAGE_THUNK_DATA origThunk = (PIMAGE_THUNK_DATA)((PBYTE)hModule + impDesc->OriginalFirstThunk);
        if (impDesc->OriginalFirstThunk == 0) origThunk = thunk;

        for (int i = 0; thunk[i].u1.Function != 0; i++) {
            if (origThunk[i].u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            PIMAGE_IMPORT_BY_NAME importName = (PIMAGE_IMPORT_BY_NAME)
                ((PBYTE)hModule + origThunk[i].u1.AddressOfData);
            if (strcmp((LPCSTR)importName->Name, funcName) == 0) {
                PVOID* iatEntry = (PVOID*)&thunk[i].u1.Function;
                *origFunc = (PVOID)thunk[i].u1.Function;
                DWORD oldProtect;
                MEMORY_BASIC_INFORMATION mbi;
                VirtualQuery(iatEntry, &mbi, sizeof(mbi));
                VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_READWRITE, &oldProtect);
                thunk[i].u1.Function = (ULONGLONG)hookFunc;
                DWORD unused;
                VirtualProtect(mbi.BaseAddress, mbi.RegionSize, oldProtect, &unused);
                FlushInstructionCache(GetCurrentProcess(), iatEntry, sizeof(PVOID));
                return TRUE;
            }
        }
    }
    return FALSE;
}

// ============================================================
// Hook: GetRawInputDeviceList — TOCTOU 修复
//
// 问题: 游戏调用 GetRawInputDeviceList(NULL, &count) 获取数量,
//       malloc(count*16), 然后 GetRawInputDeviceList(buf, &count)
//       两次调用之间 count 可能变化 → 缓冲区溢出
//
// 修复: 缓存第一次的 count, 第二次调用时强制截断
// ============================================================
static UINT WINAPI Hook_GetRawInputDeviceList(PRAWINPUTDEVICELIST pList, PUINT pCount, UINT cbSize) {
    if (!g_cfg.fix_toctou) {
        return g_Orig(pList, pCount, cbSize);
    }

    // 第一次: pList == NULL, 获取设备数量
    if (pList == NULL && pCount != NULL) {
        if (g_cfg.enable_cs) EnterCriticalSection(&g_cs);
        UINT result = g_Orig(NULL, pCount, cbSize);
        if (result != (UINT)-1) {
            g_last_count = *pCount;
            g_pending_fix = 1;
        }
        if (g_cfg.enable_cs) LeaveCriticalSection(&g_cs);
        return result;
    }

    // 第二次: pList != NULL, 填充设备列表 — 保护!
    if (pList != NULL && pCount != NULL && g_pending_fix) {
        if (g_cfg.enable_cs) EnterCriticalSection(&g_cs);

        UINT buf_capacity = *pCount;
        UINT safe_limit = g_last_count;
        *pCount = safe_limit > 0 ? safe_limit : buf_capacity;
        UINT result = g_Orig(pList, pCount, cbSize);

        // 即使原函数更新了 count, 也截断
        if (result != (UINT)-1 && *pCount > buf_capacity) {
            *pCount = buf_capacity;
            dbg("[TOCTOU] 设备数截断到 %u", buf_capacity);
        }

        g_pending_fix = 0;
        g_last_count = 0;
        if (g_cfg.enable_cs) LeaveCriticalSection(&g_cs);
        return result;
    }

    return g_Orig(pList, pCount, cbSize);
}

// ============================================================
// 配置加载
// ============================================================
static void LoadConfig(void) {
    g_cfg.fix_toctou = 1;
    g_cfg.enable_cs = 1;
    g_cfg.debug_log = 0;

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char* slash = strrchr(path, '\\');
    if (slash) *slash = '\0';
    strcat_s(path, sizeof(path), "\\CrashFix.ini");

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;

    char buf[16];
    if (GetPrivateProfileStringA("CrashFix", "FixTOCTOU", "1", buf, sizeof(buf), path))
        g_cfg.fix_toctou = (atoi(buf) != 0);
    if (GetPrivateProfileStringA("CrashFix", "EnableCriticalSection", "1", buf, sizeof(buf), path))
        g_cfg.enable_cs = (atoi(buf) != 0);
    if (GetPrivateProfileStringA("CrashFix", "DebugLog", "0", buf, sizeof(buf), path))
        g_cfg.debug_log = (atoi(buf) != 0);
}

// ============================================================
// DllMain
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        LoadConfig();

        dbg("=== CrashFix.asi v2.2 ===");
        dbg("TOCTOU修复=%d 临界区=%d 调试=%d",
            g_cfg.fix_toctou, g_cfg.enable_cs, g_cfg.debug_log);

        if (g_cfg.enable_cs) InitializeCriticalSection(&g_cs);

        HMODULE hGame = GetModuleHandleA(NULL);
        IAT_Hook(hGame, "user32.dll", "GetRawInputDeviceList",
                 Hook_GetRawInputDeviceList, (PVOID*)&g_Orig);

        dbg("=== 初始化完成 ===");
        break;
    }

    case DLL_PROCESS_DETACH:
        if (g_cfg.enable_cs) DeleteCriticalSection(&g_cs);
        dbg("=== 卸载 ===");
        break;
    }
    return TRUE;
}
