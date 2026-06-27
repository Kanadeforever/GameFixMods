/**
 * BinaryDomain Locale Forcer - ASI Plugin (v1.0)
 * ==================================================
 * 功能: 拦截 RegQueryValueExW，当游戏查询 "locale" 注册表值时，
 *       始终返回 "0411" (日语)，无视 Steam 写入的实际值 (1033)。
 *
 * 原理: IAT Hook — 修改游戏导入表中 RegQueryValueExW 的函数指针，
 *       指向我们的 Hook 函数。无需修改游戏原文件，无需外部库依赖。
 *
 * 目标: BinaryDomain.exe (32-bit PE, ~2012 Sega)
 * 构建: Visual Studio 2017/2019/2022 开发者命令提示符下运行 build.bat
 *       编译产物: BinaryDomainLocaleFix.asi → 放入游戏目录
 *       需要配合 Ultimate ASI Loader 使用 (dinput8.dll 放入游戏目录)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>

// ============================================================
// 调试开关: 设为 1 启用 OutputDebugString 日志
//          (使用 DebugView 查看: https://learn.microsoft.com/sysinternals/debugview)
// ============================================================
#define BD_DEBUG 1

#if BD_DEBUG
    #define DBG_PRINT(fmt, ...) do { \
        char _buf[512]; \
        _snprintf_s(_buf, sizeof(_buf), "[BDFix] " fmt "\n", ##__VA_ARGS__); \
        OutputDebugStringA(_buf); \
    } while(0)
#else
    #define DBG_PRINT(...) ((void)0)
#endif

// ============================================================
// 原始函数指针 (RegQueryValueExW)
// ============================================================
typedef LSTATUS (WINAPI *RegQueryValueExW_t)(
    HKEY    hKey,
    LPCWSTR lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE  lpData,
    LPDWORD lpcbData
);

static RegQueryValueExW_t g_OriginalRegQueryValueExW = nullptr;

// ============================================================
// 原始函数指针 (GetUserDefaultLCID — 辅助拦截)
// ============================================================
typedef LCID (WINAPI *GetUserDefaultLCID_t)(void);
static GetUserDefaultLCID_t g_OriginalGetUserDefaultLCID = nullptr;

// ============================================================
// Hook 函数: 拦截 RegQueryValueExW
// ============================================================
static LSTATUS WINAPI Hook_RegQueryValueExW(
    HKEY    hKey,
    LPCWSTR lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE  lpData,
    LPDWORD lpcbData)
{
    // 仅拦截 "locale" 值查询 (大小写不敏感)
    if (lpValueName && _wcsicmp(lpValueName, L"locale") == 0)
    {
        DBG_PRINT("Intercepted RegQueryValueExW for 'locale' — forcing 0411");

        // 伪造返回值: 始终返回字符串 "0411" (REG_SZ)
        const wchar_t* szForcedLocale = L"0411";
        DWORD dwSize = (DWORD)((wcslen(szForcedLocale) + 1) * sizeof(wchar_t));

        // 设置值类型为 REG_SZ
        if (lpType)
            *lpType = REG_SZ;

        // 检查缓冲区是否足够
        if (!lpData || !lpcbData || *lpcbData < dwSize)
        {
            // 缓冲区不足 — 返回所需大小，让调用方重新分配
            if (lpcbData)
                *lpcbData = dwSize;
            DBG_PRINT("  Buffer too small; reporting required size = %u bytes", dwSize);
            return ERROR_MORE_DATA;
        }

        // 缓冲区充足 — 写入伪造数据
        wcscpy_s((wchar_t*)lpData, *lpcbData / sizeof(wchar_t), szForcedLocale);
        if (lpcbData)
            *lpcbData = dwSize;

        DBG_PRINT("  Forced locale value: \"%ls\" (%u bytes)", szForcedLocale, dwSize);
        return ERROR_SUCCESS;
    }

    // 非 "locale" — 原样透传给原始 API
    return g_OriginalRegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

// ============================================================
// Hook 函数: 拦截 GetUserDefaultLCID (辅助 — 兜底拦截)
// 如果游戏使用系统 API 而非注册表获取地区, 此钩子确保返回 0x0411
// ============================================================
static LCID WINAPI Hook_GetUserDefaultLCID(void)
{
    DBG_PRINT("Intercepted GetUserDefaultLCID — forcing 0x0411 (was 0x%04X)", g_OriginalGetUserDefaultLCID());
    return 0x0411;  // MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN), SORT_DEFAULT)
}

// ============================================================
// IAT Hook 工具: 查找并替换指定 DLL 导入函数指针
// ============================================================
static bool PatchIAT(const char* szTargetDll, const char* szFuncName,
                     void* pHookFunc, void** ppOriginal)
{
    // 获取当前进程模块基址 (即 BinaryDomain.exe)
    HMODULE hMod = GetModuleHandleW(NULL);
    if (!hMod)
    {
        DBG_PRINT("ERROR: GetModuleHandleW(NULL) failed");
        return false;
    }

    // 解析 PE 头
    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)hMod;
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        DBG_PRINT("ERROR: Invalid DOS header");
        return false;
    }

    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + pDos->e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE)
    {
        DBG_PRINT("ERROR: Invalid NT header");
        return false;
    }

    // 获取导入表目录
    DWORD dwImportRVA = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!dwImportRVA)
    {
        DBG_PRINT("ERROR: No import directory");
        return false;
    }

    PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod + dwImportRVA);

    // 遍历所有导入 DLL
    for (int i = 0; pImportDesc[i].Name != 0; i++)
    {
        const char* szDllName = (const char*)((BYTE*)hMod + pImportDesc[i].Name);

        DBG_PRINT("Scanning import: %s", szDllName);

        if (_stricmp(szDllName, szTargetDll) != 0)
            continue;   // 不是目标 DLL

        // 找到目标 DLL，遍历其导入函数
        // 注意: OriginalFirstThunk 和 FirstThunk 在加载后可能都指向 IAT
        PIMAGE_THUNK_DATA pThunkIAT = (PIMAGE_THUNK_DATA)((BYTE*)hMod + pImportDesc[i].FirstThunk);
        PIMAGE_THUNK_DATA pThunkINT = (PIMAGE_THUNK_DATA)((BYTE*)hMod + pImportDesc[i].OriginalFirstThunk);

        // 如果 OriginalFirstThunk 为 0 (某些链接器), 回退使用 FirstThunk
        if (pImportDesc[i].OriginalFirstThunk == 0)
            pThunkINT = pThunkIAT;

        for (int j = 0; pThunkIAT[j].u1.Function != 0; j++)
        {
            PROC pFuncAddr = (PROC)pThunkIAT[j].u1.Function;

            // 检查是否按名称导入 (非序数)
            if (!(pThunkINT[j].u1.Ordinal & IMAGE_ORDINAL_FLAG))
            {
                PIMAGE_IMPORT_BY_NAME pByName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod + pThunkINT[j].u1.AddressOfData);
                const char* szName = (const char*)pByName->Name;

                if (strcmp(szName, szFuncName) == 0)
                {
                    // === 找到目标函数! ===
                    DBG_PRINT("FOUND %s!%s at IAT[%d] = 0x%p", szTargetDll, szFuncName, j, pFuncAddr);

                    // 保存原始地址
                    *ppOriginal = pFuncAddr;

                    // 修改 IAT 条目指向我们的 Hook
                    DWORD dwOldProtect;
                    if (!VirtualProtect(&pThunkIAT[j].u1.Function, sizeof(PVOID), PAGE_READWRITE, &dwOldProtect))
                    {
                        DBG_PRINT("ERROR: VirtualProtect RW failed, GLE=%u", GetLastError());
                        return false;
                    }

                    pThunkIAT[j].u1.Function = (ULONG_PTR)pHookFunc;

                    DWORD dwDummy;
                    VirtualProtect(&pThunkIAT[j].u1.Function, sizeof(PVOID), dwOldProtect, &dwDummy);

                    DBG_PRINT("IAT patched: 0x%p → 0x%p (Hook_RegQueryValueExW)", pFuncAddr, pHookFunc);
                    return true;
                }
            }
            else
            {
                // 按序数导入 — 跳过 (RegQueryValueExW 总是按名称导入)
                DBG_PRINT("  Skipping ordinal import #%u", (DWORD)(pThunkINT[j].u1.Ordinal & 0xFFFF));
            }
        }

        DBG_PRINT("Function %s not found in %s imports", szFuncName, szTargetDll);
        return false;
    }

    DBG_PRINT("DLL %s not found in import table", szTargetDll);
    return false;
}

// ============================================================
// DllMain — ASI 入口点
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    (void)lpReserved;  // unused in this plugin
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        // 禁止 thread attach/detach 通知 (减少开销)
        DisableThreadLibraryCalls(hModule);

        DBG_PRINT("========================================");
        DBG_PRINT("BinaryDomain Locale Forcer v1.0 loading");
        DBG_PRINT("========================================");

        // 保存原始 RegQueryValueExW 地址 (用于非 locale 查询透传)
        HMODULE hAdvapi32 = GetModuleHandleW(L"advapi32.dll");
        if (!hAdvapi32)
        {
            DBG_PRINT("ERROR: advapi32.dll not loaded");
            return FALSE;
        }

        g_OriginalRegQueryValueExW = (RegQueryValueExW_t)GetProcAddress(hAdvapi32, "RegQueryValueExW");
        if (!g_OriginalRegQueryValueExW)
        {
            DBG_PRINT("ERROR: GetProcAddress(RegQueryValueExW) failed");
            return FALSE;
        }

        DBG_PRINT("Original RegQueryValueExW at 0x%p", g_OriginalRegQueryValueExW);

        // --- Hook 1: RegQueryValueExW (主要拦截 — 注册表 "locale" 值) ---
        void* pOriginal1 = nullptr;
        if (!PatchIAT("advapi32.dll", "RegQueryValueExW",
                      (void*)Hook_RegQueryValueExW, &pOriginal1))
        {
            DBG_PRINT("WARNING: RegQueryValueExW IAT hook failed — trying fallback");
            // 不致命 — 如果 GetUserDefaultLCID hook 成功, 仍有机会拦截
        }

        // --- Hook 2: GetUserDefaultLCID (辅助拦截 — 系统默认地区 API) ---
        // 保存原始地址 (用于日志记录实际系统 LCID)
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (hKernel32)
        {
            g_OriginalGetUserDefaultLCID = (GetUserDefaultLCID_t)GetProcAddress(hKernel32, "GetUserDefaultLCID");
        }

        void* pOriginal2 = nullptr;
        if (!PatchIAT("kernel32.dll", "GetUserDefaultLCID",
                      (void*)Hook_GetUserDefaultLCID, &pOriginal2))
        {
            DBG_PRINT("WARNING: GetUserDefaultLCID IAT hook failed");
            // 不致命 — 主 Hook (RegQueryValueExW) 可能已足够
        }

        DBG_PRINT("========================================");
        DBG_PRINT("Hook installation complete!");
        DBG_PRINT("  RegQueryValueExW  hook: %s", pOriginal1 ? "OK" : "FAILED");
        DBG_PRINT("  GetUserDefaultLCID hook: %s", pOriginal2 ? "OK" : "FAILED");
        DBG_PRINT("All locale queries will return 0411");
        DBG_PRINT("========================================");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        // ASI 卸载时无需手动恢复 IAT — 进程退出时会自动清理
        DBG_PRINT("BinaryDomain Locale Forcer unloaded");
    }

    return TRUE;
}
