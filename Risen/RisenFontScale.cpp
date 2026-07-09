// ============================================================
// Risen 2023 Font Scale ASI — 可调节字体大小
//
// Hook 目标（已在 Risen 2023 Engine.dll 上验证通过）：
//   Engine.dll + 0x00521780  eCFont::Create(LOGFONTW const*)
//
// 工作原理：
//   读取 RisenFontScale.ini（与 ASI 同目录），复制 LOGFONTW，
//   按比例缩放 lfHeight，然后调用原始 eCFont::Create。
//
// 注意事项：
//   - 本 ASI 只改字号，不改字形覆盖（glyph coverage）。
//     汉字字形请通过 data/common/gui2.pak 的字体修复解决。
//   - 修改 Scale 后需重启游戏；已创建的缓存字体不会实时变化。
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <share.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cmath>

namespace {

// ============================================================
// 常量定义
// ============================================================

/// eCFont::Create 函数在 Engine.dll 中的 RVA
constexpr uintptr_t kEngine_eCFont_Create_RVA = 0x00521780;

/// Hook 覆盖的字节数（19 字节，停在第一个相对 CALL 之前）
constexpr size_t kPatchSize_eCFont_Create = 19;

/// eCFont::Create 的函数指针类型：__fastcall，RCX=self，RDX=LOGFONTW*
using eCFontCreateFn = unsigned char (__fastcall*)(void* self, const LOGFONTW* logfont);

// ============================================================
// 全局状态
// ============================================================

/// 当前 ASI 模块的 HMODULE（用于获取路径）
HMODULE g_selfModule = nullptr;

/// 日志文件指针（关闭日志时为 nullptr）
FILE* g_log = nullptr;

/// Hook 是否已安装的原子标志
std::atomic<bool> g_installed{false};

/// 原始 eCFont::Create 函数（trampoline）
eCFontCreateFn g_original_eCFont_Create = nullptr;

/// 配置结构体，从 RisenFontScale.ini 加载
struct Config {
    int enabled = 1;          ///< 是否启用字体缩放
    double scale = 1.25;      ///< 缩放倍数（1.25 = 放大 25%）
    int minAbsHeight = 6;     ///< 缩放后最小字号（绝对值）
    int maxAbsHeight = 96;    ///< 缩放后最大字号（绝对值）
    int logEnabled = 1;       ///< 是否启用日志（1=启用 0=禁用）
    int logFirstN = 64;       ///< 只记录前 N 次字体创建（0=不记录）
};

/// 当前生效的配置
Config g_cfg;

/// 字体创建计数器（用于日志限次）
LONG g_logCount = 0;

/// ASI 所在目录（从 g_selfModule 解析）
wchar_t g_asiDir[MAX_PATH]{};

// ============================================================
// 日志输出
// ============================================================

/// 写入日志（如果日志已打开）。使用 vfprintf，自动换行 + 刷盘。
void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ============================================================
// 路径与文件初始化
// ============================================================

/// 获取 ASI 自身所在目录，存入 g_asiDir
void InitPaths() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_selfModule, path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        slash[1] = L'\0';
        wcscpy_s(g_asiDir, path);
    } else {
        g_asiDir[0] = L'\0';
    }
}

/// 以追加模式打开日志文件 RisenFontScale.log（允许其他进程同时读取）
void OpenLog() {
    wchar_t path[MAX_PATH]{};
    wcscpy_s(path, g_asiDir);
    wcscat_s(path, L"RisenFontScale.log");
    g_log = _wfsopen(path, L"a+", _SH_DENYNO);
}

/// 如果 RisenFontScale.ini 不存在，创建默认配置（含中文注释）
void EnsureDefaultIni(const wchar_t* iniPath) {
    DWORD attrs = GetFileAttributesW(iniPath);
    if (attrs != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = _wfsopen(iniPath, L"w", _SH_DENYNO);
    if (!f) return;
    fputs(
        "; RisenFontScale ASI\n"
        "; 修改 Scale 后重启游戏；已经创建/缓存的字体不会实时变大。\n"
        "[FontScale]\n"
        "; 开启/关闭字体缩放功能\n"
        "Enabled=1\n"
        "; 字体缩放倍数，1.25 = 放大 25%\n"
        "Scale=1.25\n"
        "; 缩放后最小字号（绝对值）\n"
        "MinAbsHeight=6\n"
        "; 缩放后最大字号（绝对值）\n"
        "MaxAbsHeight=96\n"
        "; 开启/关闭日志记录（1=开启 0=关闭）\n"
        "LogEnabled=1\n"
        "; 最多记录前 N 次字体创建（设为 0 关闭详细日志）\n"
        "LogFirstN=64\n",
        f);
    fclose(f);
}

/// 从 RisenFontScale.ini 读取全部配置项
void LoadConfig() {
    wchar_t ini[MAX_PATH]{};
    wcscpy_s(ini, g_asiDir);
    wcscat_s(ini, L"RisenFontScale.ini");
    EnsureDefaultIni(ini);

    // 从 INI 读取整型配置（Key，默认值）
    g_cfg.enabled = GetPrivateProfileIntW(L"FontScale", L"Enabled", 1, ini);
    g_cfg.minAbsHeight = GetPrivateProfileIntW(L"FontScale", L"MinAbsHeight", 6, ini);
    g_cfg.maxAbsHeight = GetPrivateProfileIntW(L"FontScale", L"MaxAbsHeight", 96, ini);
    g_cfg.logEnabled = GetPrivateProfileIntW(L"FontScale", L"LogEnabled", 1, ini);
    g_cfg.logFirstN = GetPrivateProfileIntW(L"FontScale", L"LogFirstN", 64, ini);

    // 从 INI 读取浮点型 Scale（用 wcstod 解析）
    wchar_t scaleBuf[64]{};
    GetPrivateProfileStringW(L"FontScale", L"Scale", L"1.25", scaleBuf, 64, ini);
    wchar_t* end = nullptr;
    double s = wcstod(scaleBuf, &end);
    if (!(s > 0.10 && s < 10.0)) s = 1.25;
    g_cfg.scale = s;

    // 边界保护
    if (g_cfg.minAbsHeight < 1) g_cfg.minAbsHeight = 1;
    if (g_cfg.maxAbsHeight < g_cfg.minAbsHeight) g_cfg.maxAbsHeight = g_cfg.minAbsHeight;

    // 只有日志启用时才写这一行，避免刚启动时 Log 空指针崩溃
    if (g_log) {
        Log("Config: Enabled=%d Scale=%.3f MinAbsHeight=%d MaxAbsHeight=%d LogEnabled=%d LogFirstN=%d",
            g_cfg.enabled, g_cfg.scale, g_cfg.minAbsHeight, g_cfg.maxAbsHeight, g_cfg.logEnabled, g_cfg.logFirstN);
    }
}

// ============================================================
// Hook 安装工具函数
// ============================================================

/// 检查地址是否在已提交的可执行内存页面内
bool IsExecutableAddress(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    DWORD p = mbi.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

/// 在目标地址写入 14 字节的绝对跳转指令（jmp [rip+0]）
/// 机器码：FF 25 00 00 00 00 + 8 字节目标地址
void WriteAbsoluteJump(void* at, void* destination) {
    unsigned char* p = static_cast<unsigned char*>(at);
    p[0] = 0xFF;
    p[1] = 0x25;
    p[2] = 0x00;
    p[3] = 0x00;
    p[4] = 0x00;
    p[5] = 0x00;
    *reinterpret_cast<void**>(p + 6) = destination;
}

/// 安装一个绝对跳转 Hook。
/// 将目标函数入口前 patchSize 字节覆写为跳转到 hook 函数，
/// 同时在 trampolineOut 返回一个 trampoline，执行原始字节后跳回原函数。
/// 使用 SEH 保护，防止内存操作异常导致崩溃。
bool InstallJumpHook(void* target, void* hook, size_t patchSize, void** trampolineOut) {
    if (!target || !hook || !trampolineOut || patchSize < 14) return false;
    if (!IsExecutableAddress(reinterpret_cast<uintptr_t>(target))) return false;

    __try {
        // 分配 trampoline 内存（可执行 + 可读写）
        const size_t trampolineSize = patchSize + 14;
        unsigned char* trampoline = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline) {
            Log("InstallJumpHook: VirtualAlloc failed");
            return false;
        }

        // 拷贝原始入口字节到 trampoline
        memcpy(trampoline, target, patchSize);

        // 在 trampoline 尾部写入 jmp [rip+0] + 8 字节目标地址
        // 这样 trampoline 执行完原始字节后跳回目标函数 patchSize 之后的代码
        trampoline[patchSize + 0] = 0xFF;
        trampoline[patchSize + 1] = 0x25;
        trampoline[patchSize + 2] = 0x00;
        trampoline[patchSize + 3] = 0x00;
        trampoline[patchSize + 4] = 0x00;
        trampoline[patchSize + 5] = 0x00;
        void* ret = static_cast<unsigned char*>(target) + patchSize;
        memcpy(trampoline + patchSize + 6, &ret, sizeof(ret));

        // 修改目标内存保护属性为可执行+可读写
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("InstallJumpHook: VirtualProtect target failed, err=%lu", GetLastError());
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        // 写入跳转指令到目标函数入口
        WriteAbsoluteJump(target, hook);
        if (patchSize > 14) memset(static_cast<unsigned char*>(target) + 14, 0x90, patchSize - 14);

        // 恢复内存保护、刷新指令缓存
        DWORD tmp = 0;
        VirtualProtect(target, patchSize, oldProtect, &tmp);
        FlushInstructionCache(GetCurrentProcess(), target, patchSize);
        FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);

        *trampolineOut = trampoline;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("InstallJumpHook: exception 0x%08lX", GetExceptionCode());
        return false;
    }
}

/// 校验 eCFont::Create 函数入口的机器码是否与预期一致（2023 版校验）
/// 如果入口不匹配则放弃 Hook，避免破坏不兼容版本
bool LooksLike_eCFont_Create_2023(const unsigned char* p) {
    // 48 89 5C 24 08 57 48 81 EC 80 00 00 00 48 8B FA 48 8B D9 E8 ...
    const unsigned char prefix[] = {
        0x48,0x89,0x5C,0x24,0x08,
        0x57,
        0x48,0x81,0xEC,0x80,0x00,0x00,0x00,
        0x48,0x8B,0xFA,
        0x48,0x8B,0xD9,
        0xE8
    };
    return memcmp(p, prefix, sizeof(prefix)) == 0;
}

// ============================================================
// 字体缩放核心逻辑
// ============================================================

/// 对给定的 LOGFONT 高度应用缩放，并钳制到 [minAbsHeight, maxAbsHeight] 范围。
/// 处理正负号（正=字符高度，负=字符宽度，GameMaker 习惯用负值）。
/// 返回缩放后的高度值。
int ScaleHeight(int h) {
    if (!g_cfg.enabled || h == 0) return h;

    const int sign = h < 0 ? -1 : 1;
    int absH = h < 0 ? -h : h;

    // 忽略超出钳制范围的异常值（不是 GUI 字体）
    if (absH < 1 || absH > 256) return h;

    int scaled = static_cast<int>(std::lround(static_cast<double>(absH) * g_cfg.scale));
    if (scaled < g_cfg.minAbsHeight) scaled = g_cfg.minAbsHeight;
    if (scaled > g_cfg.maxAbsHeight) scaled = g_cfg.maxAbsHeight;
    return sign * scaled;
}

// ============================================================
// Hook 函数：拦截 eCFont::Create
// ============================================================

/// 替换 eCFont::Create 的 Hook 函数。
/// 复制 LOGFONTW，应用缩放，然后调用原始函数。
unsigned char __fastcall Hook_eCFont_Create(void* self, const LOGFONTW* logfont) {
    if (!g_original_eCFont_Create || !logfont) {
        return g_original_eCFont_Create ? g_original_eCFont_Create(self, logfont) : 0;
    }

    // 复制原始 LOGFONTW，在副本上修改
    LOGFONTW lf = *logfont;
    const int oldHeight = lf.lfHeight;
    lf.lfHeight = ScaleHeight(lf.lfHeight);

    // 日志记录（受 logFirstN 限制）
    LONG n = InterlockedIncrement(&g_logCount);
    if (g_cfg.logEnabled && g_cfg.logFirstN > 0 && n <= g_cfg.logFirstN) {
        char face[LF_FACESIZE * 4]{};
        WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, face, sizeof(face), nullptr, nullptr);
        Log("eCFont::Create #%ld: height %d -> %d weight=%ld charset=%u face='%s'",
            n, oldHeight, lf.lfHeight, lf.lfWeight, static_cast<unsigned>(lf.lfCharSet), face);
    }

    // 调用原始函数（使用缩放后的 LOGFONTW）
    return g_original_eCFont_Create(self, &lf);
}

// ============================================================
// 初始化线程：在后台完成 Hook 安装
// ============================================================

DWORD WINAPI InitThread(LPVOID) {
    // 第一步：获取 ASI 所在目录路径
    InitPaths();

    // 第二步：加载配置（先加载，后面根据 logEnabled 决定是否开日志）
    LoadConfig();

    // 第三步：如果启用了日志，打开日志文件并写启动信息
    if (g_cfg.logEnabled) {
        OpenLog();
        Log("==== RisenFontScale ASI init ====");
    }

    // 第四步：等待 Engine.dll 加载（最多等约 30 秒）
    HMODULE engine = nullptr;
    for (int i = 0; i < 300; ++i) {
        engine = GetModuleHandleW(L"Engine.dll");
        if (engine) break;
        Sleep(100);
    }

    if (!engine) {
        Log("ERROR: Engine.dll not loaded");
        return 0;
    }

    // 计算 Hook 目标地址：Engine.dll 基址 + eCFont::Create 偏移
    unsigned char* target = reinterpret_cast<unsigned char*>(engine) + kEngine_eCFont_Create_RVA;
    Log("Engine.dll=%p eCFont::Create target=%p", engine, target);

    // 第五步：校验函数入口机器码
    __try {
        if (!LooksLike_eCFont_Create_2023(target)) {
            Log("ERROR: eCFont::Create prologue mismatch; not installing hook.");
            Log("Bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                target[0],target[1],target[2],target[3],target[4],target[5],target[6],target[7],target[8],target[9],
                target[10],target[11],target[12],target[13],target[14],target[15],target[16],target[17],target[18],target[19]);
            return 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("ERROR: exception while checking target bytes: 0x%08lX", GetExceptionCode());
        return 0;
    }

    // 第六步：安装 Hook
    void* tramp = nullptr;
    if (!InstallJumpHook(target, reinterpret_cast<void*>(&Hook_eCFont_Create), kPatchSize_eCFont_Create, &tramp)) {
        Log("ERROR: hook install failed");
        return 0;
    }

    // 保存原始函数指针（trampoline），并标记 Hook 已安装
    g_original_eCFont_Create = reinterpret_cast<eCFontCreateFn>(tramp);
    g_installed.store(true, std::memory_order_release);
    Log("OK: hook installed. trampoline=%p", tramp);
    return 0;
}

} // anonymous namespace

// ============================================================
// DllMain：ASI 入口点
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // 保存模块句柄，然后创建工作线程完成初始化
        g_selfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        // 卸载时关闭日志文件
        if (g_log) {
            Log("==== RisenFontScale ASI detach ====");
            fclose(g_log);
            g_log = nullptr;
        }
    }
    return TRUE;
}
