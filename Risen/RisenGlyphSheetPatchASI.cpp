// RisenGlyphSheetPatchASI.cpp
// 作用：运行时修补 Risen 2023 版 Engine.dll 的 eCGlyphSheet 贴图尺寸，避免中文 glyph atlas 过小导致缺字/叠字/破图。
// 编译：x64 Developer Command Prompt 中执行：
//   cl /LD /O2 /EHsc /std:c++17 /utf-8 RisenGlyphSheetPatchASI.cpp /Fe:RisenGlyphSheetPatch.asi
// 使用：把 RisenGlyphSheetPatch.asi 和 RisenGlyphSheetPatch.ini 放到 ASI Loader 会加载的位置。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static HMODULE g_self = nullptr;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static bool g_logEnabled = true;

static std::wstring GetDirOfModule(HMODULE mod) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path);
    size_t p = s.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L".";
    return s.substr(0, p);
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static void LogUtf8(const std::string& msg) {
    if (!g_logEnabled || g_logPath.empty()) return;
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, msg.data(), (DWORD)msg.size(), &written, nullptr);
    const char nl[] = "\r\n";
    WriteFile(h, nl, 2, &written, nullptr);
    CloseHandle(h);
}

static void Logf(const char* fmt, ...) {
    if (!g_logEnabled) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogUtf8(buf);
}

static bool GetModuleRange(HMODULE mod, uint8_t*& base, size_t& size) {
    base = reinterpret_cast<uint8_t*>(mod);
    if (!base) return false;

    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    size = nt->OptionalHeader.SizeOfImage;
    return size != 0;
}

static bool MatchPattern(const uint8_t* p, const int* pat, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (pat[i] >= 0 && p[i] != static_cast<uint8_t>(pat[i])) return false;
    }
    return true;
}

static std::vector<uint8_t*> FindGlyphSheetSizeImmediates(uint8_t* base, size_t size) {
    // 2023 Engine.dll 附近特征：
    //   02 00 00 00 EB 03 48 8B D9 BA 00 01 00 00 48 8B
    // 其中 BA imm32 是：mov edx, imm32。
    // 原版 imm32 = 0x100，繁中适配改为 0x800。
    const int pat[] = {
        0x02, 0x00, 0x00, 0x00, 0xEB, 0x03, 0x48, 0x8B,
        0xD9, 0xBA, -1, -1, -1, -1, 0x48, 0x8B
    };
    constexpr size_t patLen = sizeof(pat) / sizeof(pat[0]);
    std::vector<uint8_t*> out;

    if (size < patLen) return out;
    for (size_t i = 0; i <= size - patLen; ++i) {
        uint8_t* p = base + i;
        if (MatchPattern(p, pat, patLen)) {
            out.push_back(p + 10); // imm32 起始位置
        }
    }
    return out;
}

static bool PatchImm32(uint8_t* immPtr, uint32_t value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(immPtr, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Logf("错误：VirtualProtect 失败，无法修改内存。GetLastError=%lu", GetLastError());
        return false;
    }

    *reinterpret_cast<volatile uint32_t*>(immPtr) = value;

    DWORD tmp = 0;
    VirtualProtect(immPtr, sizeof(uint32_t), oldProtect, &tmp);
    FlushInstructionCache(GetCurrentProcess(), immPtr, sizeof(uint32_t));
    return true;
}

static DWORD WINAPI PatchThread(LPVOID) {
    std::wstring dir = GetDirOfModule(g_self);
    g_iniPath = dir + L"\\RisenGlyphSheetPatch.ini";
    g_logPath = dir + L"\\RisenGlyphSheetPatch.log";

    int enable = GetPrivateProfileIntW(L"Patch", L"Enable", 1, g_iniPath.c_str());
    int targetSize = GetPrivateProfileIntW(L"Patch", L"GlyphSheetSize", 2048, g_iniPath.c_str());
    g_logEnabled = GetPrivateProfileIntW(L"Patch", L"Log", 1, g_iniPath.c_str()) != 0;

    LogUtf8("========== RisenGlyphSheetPatch 启动 ==========");
    LogUtf8("说明：本 ASI 只修改进程内存，不覆盖 Engine.dll 文件。");
    Logf("配置：Enable=%d, GlyphSheetSize=%d", enable, targetSize);

    if (!enable) {
        LogUtf8("已禁用：Enable=0，未执行补丁。");
        return 0;
    }

    if (targetSize < 256 || targetSize > 8192) {
        LogUtf8("错误：GlyphSheetSize 超出安全范围。允许范围：256 到 8192。未执行补丁。");
        return 0;
    }

    HMODULE engine = nullptr;
    for (int i = 0; i < 600; ++i) { // 最多等待约 60 秒
        engine = GetModuleHandleW(L"Engine.dll");
        if (engine) break;
        Sleep(100);
    }

    if (!engine) {
        LogUtf8("错误：等待 60 秒后仍未找到 Engine.dll，未执行补丁。");
        return 0;
    }

    uint8_t* base = nullptr;
    size_t size = 0;
    if (!GetModuleRange(engine, base, size)) {
        LogUtf8("错误：无法解析 Engine.dll 的 PE 结构。");
        return 0;
    }

    Logf("Engine.dll 已加载：base=0x%p, SizeOfImage=0x%zx", base, size);

    auto immList = FindGlyphSheetSizeImmediates(base, size);
    if (immList.empty()) {
        LogUtf8("错误：没有找到 eCGlyphSheet 尺寸特征码。可能游戏版本不同，或已经被其他补丁改过。未执行补丁。");
        return 0;
    }

    uint32_t target = static_cast<uint32_t>(targetSize);
    int patched = 0;
    int already = 0;
    int skipped = 0;

    for (uint8_t* immPtr : immList) {
        uint32_t oldValue = *reinterpret_cast<uint32_t*>(immPtr);
        uintptr_t rva = static_cast<uintptr_t>(immPtr - base);

        if (oldValue == target) {
            Logf("已经是目标值：RVA=0x%zx, 当前值=0x%X", rva, oldValue);
            ++already;
            continue;
        }

        if (oldValue != 0x100) {
            Logf("跳过：RVA=0x%zx, 当前值=0x%X，不是原版 0x100。", rva, oldValue);
            ++skipped;
            continue;
        }

        if (PatchImm32(immPtr, target)) {
            Logf("补丁成功：RVA=0x%zx, 0x100 -> 0x%X", rva, target);
            ++patched;
        } else {
            ++skipped;
        }
    }

    Logf("完成：找到=%zu，已修改=%d，已是目标=%d，跳过=%d", immList.size(), patched, already, skipped);
    LogUtf8("========== RisenGlyphSheetPatch 结束 ==========");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, PatchThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
