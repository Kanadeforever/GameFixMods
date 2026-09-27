// BALDR SKY Steam Win11 Fix / D3D9 Proxy v0.1-test9
// ============================================================================
// 这一版同时解决两个上一轮已经明确的问题：
//
// 1. test6 的 SteamFix 本身成功，但“为了诊断而替换 IDirect3DDevice9 的 119 项 vtable”
//    会干扰 Windows 原生 D3D9。test8 完全删除 Device/CreateDevice vtable Hook。
//    游戏从 Direct3DCreate9 得到什么对象，我们就原样返回什么对象，不再改它的函数表。
//
// 2. 用户要求后续正式修复不要依赖中文版固定地址。
//    test8 因此不再用 0x00BC0067 / 0x00BC007D / 0x00BC004E 这类绝对地址定位。
//    它改为从 BaldrSky.exe 的运行时映像里扫描 Steam 补丁的“语义特征”：
//      GDI32.dll
//      BaldrUtil.dll
//      InitSteam
//      CloseSteam
//      UpdateSteam
//      ResetAchievements
//      GetAchievement
//    分别定位这些字符串以后，再通过引用关系验证真正的初始化机器码。
//    英文版允许在它们之间插入额外字体/设备初始化字符串。
//    只有字符串、指令结构、函数指针槽之间全部互相对得上时才会修改内存。
//
// 3. D3D9 后端和 SteamFix 完全解耦。
//    - 如果游戏目录存在并能加载 d3d9_backend.dll：使用它。
//      用户可以把 DXVK x32 的 d3d9.dll 改名为 d3d9_backend.dll。
//    - 如果没有：自动加载 Windows 系统原生 32 位 d3d9.dll。
//    同一份修复 DLL 因此既支持原生 DX9，也支持 DXVK。
//
// 4. test9 新增“高 DPI 缩放兼容”。
//    默认把 BALDR SKY 设置为 System DPI Aware，等价于告诉 Windows：
//    “这个程序自己处理系统 DPI，不要再对它做传统 DPI 虚拟化放大”。
//    这里故意不用 Per-Monitor V2，因为 BALDR SKY 是 2009 年固定像素 UI，
//    原程序没有针对 WM_DPICHANGED / 跨显示器 DPI 切换进行过设计。
//    System DPI Aware 对老 D3D9 游戏更保守；用户仍可在 INI 里完全关闭。
//
// 5. test9 将轻量 VEH/VCH 崩溃诊断改为“默认关闭、可在 INI 打开”。
//    原因不是删除诊断能力，而是 test8 已经证明游戏和 DXVK/系统组件可能产生
//    能被自身处理的 first-chance 异常。正式候选默认记录这些异常反而容易误导。
//    将来若中文版或英文版真的再次出现退出问题，只需把 INI 诊断开关改回 1。
//
// 源码不依赖 Windows SDK、MinGW 头文件或 C/C++ 运行库。
// 所有 Windows API 都在运行时从 kernel32.dll 的导出表解析。
// 这样当前构建环境可以直接生成 Win32/x86 DLL，同时最终文件没有额外 Runtime 依赖。
// ============================================================================

extern "C" {

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned int   UINT;
typedef int            BOOL;
typedef long           HRESULT;
typedef long           LONG;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          HWND;
typedef void*          HKEY;
typedef unsigned long  ULONG_PTR;
typedef DWORD          REGSAM;

#ifndef __stdcall
#define __stdcall __attribute__((stdcall))
#endif
#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif

// Windows API 的函数指针类型。
// 因为没有 Windows import library，所以不是直接调用 LoadLibraryA/WriteFile，
// 而是先从 kernel32.dll 的导出表里自己找到函数地址，再通过这些指针调用。
typedef HMODULE (__stdcall *PFN_LoadLibraryA)(const char*);
typedef UINT    (__stdcall *PFN_GetSystemDirectoryA)(char*, UINT);
typedef void*   (__stdcall *PFN_GetProcAddress)(HMODULE, const char*);
typedef DWORD   (__stdcall *PFN_GetCurrentThreadId)();
typedef DWORD   (__stdcall *PFN_VirtualQuery)(const void*, void*, DWORD);
typedef BOOL    (__stdcall *PFN_VirtualProtect)(void*, DWORD, DWORD, DWORD*);
typedef void    (__stdcall *PFN_ExitProcess)(UINT);
typedef BOOL    (__stdcall *PFN_TerminateProcess)(HANDLE, UINT);
typedef void*   (__stdcall *PFN_SetUnhandledExceptionFilter)(void*);
typedef HANDLE  (__stdcall *PFN_CreateFileA)(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef BOOL    (__stdcall *PFN_WriteFile)(HANDLE, const void*, DWORD, DWORD*, void*);
typedef BOOL    (__stdcall *PFN_FlushFileBuffers)(HANDLE);
typedef DWORD   (__stdcall *PFN_GetModuleFileNameA)(HMODULE, char*, DWORD);
typedef UINT    (__stdcall *PFN_GetPrivateProfileIntA)(const char*, const char*, int, const char*);
typedef DWORD   (__stdcall *PFN_GetLastError)();
typedef HANDLE  (__stdcall *PFN_CreateThread)(void*, DWORD, DWORD (__stdcall *)(void*), void*, DWORD, DWORD*);
typedef void    (__stdcall *PFN_Sleep)(DWORD);
typedef BOOL    (__stdcall *PFN_CloseHandle)(HANDLE);

// 高 DPI 相关 API。
//
// 这两个函数都来自 user32.dll：
// - SetProcessDpiAwarenessContext：Windows 10 新接口，可以显式指定 System Aware。
// - SetProcessDPIAware：Windows Vista 起就存在的旧接口，同样把进程设成 System Aware。
//
// 为什么两个都准备：
// 新系统优先用更明确的新接口；如果系统没有该导出，就自动退回旧接口。
// 整个过程不要求 Windows SDK，因为这里只需要按 Win32 ABI 声明函数指针。
typedef BOOL    (__stdcall *PFN_SetProcessDpiAwarenessContext)(void*);
typedef BOOL    (__stdcall *PFN_SetProcessDPIAware)();

// 注册表 API。视频 Overlay 修复只访问当前用户 HKCU，完全不需要管理员权限。
typedef LONG (__stdcall *PFN_RegCreateKeyExA)(HKEY,const char*,DWORD,char*,DWORD,REGSAM,void*,HKEY*,DWORD*);
typedef LONG (__stdcall *PFN_RegSetValueExA)(HKEY,const char*,DWORD,DWORD,const BYTE*,DWORD);
typedef LONG (__stdcall *PFN_RegQueryValueExA)(HKEY,const char*,DWORD*,DWORD*,BYTE*,DWORD*);
typedef LONG (__stdcall *PFN_RegCloseKey)(HKEY);

// Windows VEH（Vectored Exception Handler）注册函数。
// 我们只观察异常，然后返回 CONTINUE_SEARCH，不会把异常吃掉。
typedef void*   (__stdcall *PFN_AddVectoredExceptionHandler)(DWORD, void*);
typedef void*   (__stdcall *PFN_AddVectoredContinueHandler)(DWORD, void*);

// 系统 D3D9 / 后端的 Direct3DCreate9。
typedef void* (__stdcall *PFN_Direct3DCreate9)(UINT);

// IDirect3D9::CreateDevice 的真实 32 位调用格式。
typedef HRESULT (__stdcall *PFN_CreateDevice)(
    void* self,
    UINT Adapter,
    int DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    void* pPresentationParameters,
    void** ppReturnedDeviceInterface);

}

// ----------------------------------------------------------------------------
// Windows / 文件常量
// ----------------------------------------------------------------------------
static const DWORD GENERIC_WRITE         = 0x40000000UL;
static const DWORD FILE_SHARE_READ       = 0x00000001UL;
static const DWORD FILE_SHARE_WRITE      = 0x00000002UL;
static const DWORD CREATE_ALWAYS         = 2UL;
static const DWORD FILE_ATTRIBUTE_NORMAL = 0x00000080UL;
static HANDLE const INVALID_HANDLE_VALUE = (HANDLE)(long)-1;

// VirtualQuery / VirtualProtect 所需常量。
// 先查询页面再读异常栈，可以避免“为了记录一次异常，记录器自己又读到无效地址”。
static const DWORD MEM_COMMIT       = 0x00001000UL;
static const DWORD PAGE_NOACCESS    = 0x00000001UL;
static const DWORD PAGE_READWRITE   = 0x00000004UL;
static const DWORD PAGE_GUARD       = 0x00000100UL;

// 注册表常量。HKEY_CURRENT_USER 是 Win32 约定的伪句柄，不需要真的打开“根键”。
static HKEY const HKEY_CURRENT_USER_VALUE = (HKEY)(ULONG_PTR)0x80000001UL;
static const DWORD KEY_QUERY_VALUE_VALUE  = 0x00000001UL;
static const DWORD KEY_SET_VALUE_VALUE    = 0x00000002UL;
static const DWORD REG_DWORD_VALUE        = 4UL;
static const LONG  ERROR_SUCCESS_VALUE    = 0L;

// DPI_AWARENESS_CONTEXT 在 Win32 API 中本质是特殊 HANDLE 常量。
// -2 = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE。
//
// test9 明确只使用 System Aware，不使用 Per-Monitor V2。
// 原因见 ApplyHighDpiFixOnce() 上方的详细说明。
static void* const DPI_AWARENESS_CONTEXT_SYSTEM_AWARE_VALUE = (void*)(long)-2;

// ----------------------------------------------------------------------------
// D3DPRESENT_PARAMETERS 的 Win32 布局
// ----------------------------------------------------------------------------
// 这里只定义日志需要读取的结构。
// D3D9 的 enum 在 Win32 ABI 中也是 32 位整数，所以可以直接使用 int/DWORD。
struct D3DPRESENT_PARAMETERS32 {
    UINT  BackBufferWidth;
    UINT  BackBufferHeight;
    int   BackBufferFormat;
    UINT  BackBufferCount;
    int   MultiSampleType;
    DWORD MultiSampleQuality;
    int   SwapEffect;
    HWND  hDeviceWindow;
    BOOL  Windowed;
    BOOL  EnableAutoDepthStencil;
    int   AutoDepthStencilFormat;
    DWORD Flags;
    UINT  FullScreen_RefreshRateInHz;
    UINT  PresentationInterval;
};

// ----------------------------------------------------------------------------
// Win32 异常与线程上下文结构
// ----------------------------------------------------------------------------
// 这些布局来自 32 位 Windows ABI。VEH 触发时 Windows 会把异常记录和寄存器现场
// 通过 EXCEPTION_POINTERS32 传进来。我们只读取字段，不修改任何寄存器。
struct EXCEPTION_RECORD32 {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    EXCEPTION_RECORD32* ExceptionRecord;
    void* ExceptionAddress;
    DWORD NumberParameters;
    DWORD ExceptionInformation[15];
};
struct FLOATING_SAVE_AREA32 {
    DWORD ControlWord; DWORD StatusWord; DWORD TagWord; DWORD ErrorOffset;
    DWORD ErrorSelector; DWORD DataOffset; DWORD DataSelector;
    BYTE RegisterArea[80]; DWORD Cr0NpxState;
};
struct CONTEXT32_MIN {
    DWORD ContextFlags;
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    FLOATING_SAVE_AREA32 FloatSave;
    DWORD SegGs, SegFs, SegEs, SegDs;
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
    DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
    BYTE ExtendedRegisters[512];
};
struct EXCEPTION_POINTERS32 {
    EXCEPTION_RECORD32* ExceptionRecord;
    CONTEXT32_MIN* ContextRecord;
};

// Windows x86 MEMORY_BASIC_INFORMATION 布局。
// RegionSize 在 32 位进程里就是 32 位 SIZE_T。
struct MEMORY_BASIC_INFORMATION32 {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    DWORD RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};

typedef LONG (__stdcall *PFN_UnhandledExceptionFilter)(EXCEPTION_POINTERS32*);
static const DWORD EXCEPTION_ACCESS_VIOLATION_CODE = 0xC0000005UL;
static const LONG EXCEPTION_CONTINUE_SEARCH_VALUE = 0;

// ----------------------------------------------------------------------------
// 极小 PE / PEB 解析器
// ----------------------------------------------------------------------------
// Windows 正常程序会通过导入表找到 kernel32 函数。
// 本 DLL 为了做到“Import Directory = 0”，改为从 PEB 的模块链表找到 kernel32，
// 然后自己读 PE Export Directory。

struct LIST_ENTRY32 { LIST_ENTRY32* Flink; LIST_ENTRY32* Blink; };
struct UNICODE_STRING32 { WORD Length; WORD MaximumLength; unsigned short* Buffer; };
struct PEB_LDR_DATA32 {
    DWORD Length;
    BYTE Initialized;
    BYTE pad1[3];
    void* SsHandle;
    LIST_ENTRY32 InLoadOrderModuleList;
    LIST_ENTRY32 InMemoryOrderModuleList;
};
struct LDR_DATA_TABLE_ENTRY32_PART {
    LIST_ENTRY32 InLoadOrderLinks;
    LIST_ENTRY32 InMemoryOrderLinks;
    LIST_ENTRY32 InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    DWORD SizeOfImage;
    UNICODE_STRING32 FullDllName;
    UNICODE_STRING32 BaseDllName;
};
struct IMAGE_EXPORT_DIRECTORY_MIN {
    DWORD Characteristics;
    DWORD TimeDateStamp;
    WORD  MajorVersion;
    WORD  MinorVersion;
    DWORD Name;
    DWORD Base;
    DWORD NumberOfFunctions;
    DWORD NumberOfNames;
    DWORD AddressOfFunctions;
    DWORD AddressOfNames;
    DWORD AddressOfNameOrdinals;
};

// x86 / WOW64 进程中 FS:[0x30] 指向 PEB。
static void* GetPeb32() {
    void* peb;
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));
    return peb;
}

static char LowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

// 把 Windows 的 UTF-16 DLL 名与简单 ASCII 名比较。
static bool WideNameEqualsAscii(const UNICODE_STRING32& u, const char* ascii) {
    if (!u.Buffer || !ascii) return false;
    UINT chars = (UINT)(u.Length / 2);
    UINT i = 0;
    for (; i < chars; ++i) {
        char a = ascii[i];
        if (!a) return false;
        unsigned short w = u.Buffer[i];
        if (w > 0x7F) return false;
        if (LowerAscii((char)w) != LowerAscii(a)) return false;
    }
    return ascii[i] == 0;
}

// 从当前进程已经加载的模块中找某个 DLL。
static void* FindLoadedModule(const char* dllName) {
    BYTE* peb = (BYTE*)GetPeb32();
    if (!peb) return 0;
    PEB_LDR_DATA32* ldr = *(PEB_LDR_DATA32**)(peb + 0x0C);
    if (!ldr) return 0;
    LIST_ENTRY32* head = &ldr->InMemoryOrderModuleList;
    LIST_ENTRY32* node = head->Flink;
    while (node && node != head) {
        LDR_DATA_TABLE_ENTRY32_PART* entry =
            (LDR_DATA_TABLE_ENTRY32_PART*)((BYTE*)node - 0x08);
        if (WideNameEqualsAscii(entry->BaseDllName, dllName)) return entry->DllBase;
        node = node->Flink;
    }
    return 0;
}

static bool AsciiEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

static UINT AsciiLen(const char* s) {
    UINT n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

// 从一个已经加载的 PE32 模块的 Export Directory 里按名字找函数。
// Windows 的 kernel32 很多导出其实会转发到 KERNELBASE，所以这里也处理 forwarder。
static void* ResolveExport(void* moduleBase, const char* name, int depth = 0) {
    if (!moduleBase || !name || depth > 4) return 0;
    BYTE* base = (BYTE*)moduleBase;
    DWORD peOff = *(DWORD*)(base + 0x3C);
    BYTE* nt = base + peOff;
    BYTE* optional = nt + 24;
    WORD magic = *(WORD*)optional;
    if (magic != 0x10B) return 0;
    DWORD exportRva  = *(DWORD*)(optional + 96);
    DWORD exportSize = *(DWORD*)(optional + 100);
    if (!exportRva || !exportSize) return 0;
    IMAGE_EXPORT_DIRECTORY_MIN* exp =
        (IMAGE_EXPORT_DIRECTORY_MIN*)(base + exportRva);
    DWORD* names = (DWORD*)(base + exp->AddressOfNames);
    WORD* ords   = (WORD*)(base + exp->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* n = (const char*)(base + names[i]);
        if (!AsciiEquals(n, name)) continue;
        WORD ordIndex = ords[i];
        if (ordIndex >= exp->NumberOfFunctions) return 0;
        DWORD rva = funcs[ordIndex];
        if (rva >= exportRva && rva < exportRva + exportSize) {
            const char* fwd = (const char*)(base + rva);
            char mod[64]; char fn[128]; UINT mi = 0;
            while (*fwd && *fwd != '.' && mi < 58) mod[mi++] = *fwd++;
            if (*fwd != '.') return 0;
            ++fwd;
            mod[mi++]='.'; mod[mi++]='d'; mod[mi++]='l'; mod[mi++]='l'; mod[mi]=0;
            UINT fi = 0;
            while (*fwd && fi < 127) fn[fi++] = *fwd++;
            fn[fi]=0;
            if (fn[0]=='#') return 0;
            void* fmod = FindLoadedModule(mod);
            if (!fmod) return 0;
            return ResolveExport(fmod, fn, depth + 1);
        }
        return base + rva;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// 动态取得 Windows API
// ----------------------------------------------------------------------------
static PFN_LoadLibraryA     g_LoadLibraryA = 0;
static PFN_GetSystemDirectoryA g_GetSystemDirectoryA = 0;
static PFN_GetProcAddress   g_GetProcAddress = 0;
static PFN_GetCurrentThreadId g_GetCurrentThreadId = 0;
static PFN_VirtualQuery       g_VirtualQuery = 0;
static PFN_VirtualProtect     g_VirtualProtect = 0;
static PFN_CreateFileA      g_CreateFileA = 0;
static PFN_WriteFile        g_WriteFile = 0;
static PFN_FlushFileBuffers g_FlushFileBuffers = 0;
static PFN_GetModuleFileNameA g_GetModuleFileNameA = 0;
static PFN_GetPrivateProfileIntA g_GetPrivateProfileIntA = 0;
static PFN_GetLastError     g_GetLastError = 0;
static PFN_CreateThread     g_CreateThread = 0;
static PFN_Sleep            g_Sleep = 0;
static PFN_CloseHandle      g_CloseHandle = 0;
static PFN_AddVectoredExceptionHandler g_AddVectoredExceptionHandler = 0;
static PFN_AddVectoredContinueHandler  g_AddVectoredContinueHandler = 0;
static HANDLE               g_log = INVALID_HANDLE_VALUE;

// DllMain 会把“当前这个模块自己的 HMODULE”保存下来。
// 这样同一份二进制无论叫 d3d9.dll 还是 BaldrSkyWin11Fix.asi，
// 都可以根据自己的实际文件名自动找到同名 INI/LOG。
static HMODULE g_selfModule = 0;
static char g_selfPath[520] = {0};
static char g_iniPath[520] = {0};
static char g_logPath[520] = {0};
static char g_mainExePath[520] = {0};

// 用户配置。默认值都偏向“开箱即用”，但每一项都能在同名 INI 中关闭。
struct UserConfig {
    int enableSteamFix;
    int enableVideoOverlayFix;
    int enableHighDpiFix;     // 1=System DPI Aware；0=完全保持 Windows/外部兼容设置
    int backendMode;          // 0=自动，1=系统原生，2=强制 d3d9_backend.dll
    int enableLog;
    int logLevel;             // 0=仅错误，1=基本，2=详细，3=调试
    int enableCrashDiagnostics;
    int overlayGuardIntervalMs;
};

// test9 的长期默认值：
// - SteamFix=1：中英文 Steam 版已经实机/静态确认同一根因；
// - VideoOverlayFix=1：英文版已实机确认退出后 EnableOverlays 仍保持为 0；
// - HighDpiFix=1：用户明确要求正式候选默认开启高 DPI 缩放兼容；
// - CrashDiagnostics=0：正式候选不默认把可恢复 first-chance 异常刷进日志，
//   真遇到新问题时再由 INI 打开。
static UserConfig g_cfg = {1,1,1,0,1,2,0,500};
static bool g_configLoaded = false;

static bool InitWinApis() {
    if (g_LoadLibraryA && g_GetProcAddress && g_CreateFileA && g_WriteFile) return true;

    // kernel32 在普通 Win32 进程里一定已经存在。
    // 我们从它自己的导出表取地址，避免给修复 DLL 增加普通 Import Directory。
    void* k32 = FindLoadedModule("kernel32.dll");
    if (!k32) return false;

    g_LoadLibraryA       = (PFN_LoadLibraryA)ResolveExport(k32, "LoadLibraryA");
    g_GetSystemDirectoryA= (PFN_GetSystemDirectoryA)ResolveExport(k32, "GetSystemDirectoryA");
    g_GetProcAddress     = (PFN_GetProcAddress)ResolveExport(k32, "GetProcAddress");
    g_GetCurrentThreadId = (PFN_GetCurrentThreadId)ResolveExport(k32, "GetCurrentThreadId");
    g_VirtualQuery       = (PFN_VirtualQuery)ResolveExport(k32, "VirtualQuery");
    g_VirtualProtect     = (PFN_VirtualProtect)ResolveExport(k32, "VirtualProtect");
    g_CreateFileA        = (PFN_CreateFileA)ResolveExport(k32, "CreateFileA");
    g_WriteFile          = (PFN_WriteFile)ResolveExport(k32, "WriteFile");
    g_FlushFileBuffers   = (PFN_FlushFileBuffers)ResolveExport(k32, "FlushFileBuffers");
    g_GetModuleFileNameA = (PFN_GetModuleFileNameA)ResolveExport(k32, "GetModuleFileNameA");
    g_GetPrivateProfileIntA = (PFN_GetPrivateProfileIntA)ResolveExport(k32, "GetPrivateProfileIntA");
    g_GetLastError       = (PFN_GetLastError)ResolveExport(k32, "GetLastError");
    g_CreateThread       = (PFN_CreateThread)ResolveExport(k32, "CreateThread");
    g_Sleep              = (PFN_Sleep)ResolveExport(k32, "Sleep");
    g_CloseHandle        = (PFN_CloseHandle)ResolveExport(k32, "CloseHandle");
    g_AddVectoredExceptionHandler = (PFN_AddVectoredExceptionHandler)ResolveExport(k32, "AddVectoredExceptionHandler");
    g_AddVectoredContinueHandler  = (PFN_AddVectoredContinueHandler)ResolveExport(k32, "AddVectoredContinueHandler");

    return g_LoadLibraryA && g_GetProcAddress && g_CreateFileA && g_WriteFile;
}

// ----------------------------------------------------------------------------
// 模块路径、同名 INI/LOG 与用户配置
// ----------------------------------------------------------------------------
//
// 配置文件和日志跟随当前代理 DLL 名称：
//   d3d9.dll -> d3d9.ini / d3d9.log
//
// 文件名不写死在多个位置，而是从“当前模块自己的完整路径”动态换扩展名。
// 这样以后即使代理 DLL 改名，配置/日志仍能自动跟随，不需要再改源码常量。

static char LowerAsciiSimple(char c) {
    if (c>='A' && c<='Z') return (char)(c+('a'-'A'));
    return c;
}

static bool EndsWithAsciiNoCase(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    UINT a=AsciiLen(s), b=AsciiLen(suffix);
    if (b>a) return false;
    for (UINT i=0;i<b;++i) {
        if (LowerAsciiSimple(s[a-b+i]) != LowerAsciiSimple(suffix[i])) return false;
    }
    return true;
}

static void CopyAsciiLimited(char* dst, UINT cap, const char* src) {
    if (!dst || !cap) return;
    UINT i=0;
    while (src && src[i] && i+1<cap) { dst[i]=src[i]; ++i; }
    dst[i]=0;
}

static bool ReplaceModuleExtension(const char* fullPath, const char* newExt, char* out, UINT cap) {
    if (!fullPath || !newExt || !out || cap<8) return false;
    UINT n=AsciiLen(fullPath);
    if (n+8>=cap) return false;
    CopyAsciiLimited(out,cap,fullPath);

    // 从后向前找最后一个点；遇到目录分隔符就停止。
    // 这样像 C:\Games\foo.bar\d3d9.dll 也只会替换真正文件扩展名。
    int dot=-1;
    for (int i=(int)n-1;i>=0;--i) {
        if (out[i]=='\\' || out[i]=='/') break;
        if (out[i]=='.') { dot=i; break; }
    }
    if (dot<0) dot=(int)n;

    UINT p=(UINT)dot;
    for (UINT i=0; newExt[i]; ++i) {
        if (p+1>=cap) return false;
        out[p++]=newExt[i];
    }
    out[p]=0;
    return true;
}

static void BuildModulePathsOnce() {
    if (g_selfPath[0]) return;
    if (!InitWinApis() || !g_GetModuleFileNameA) return;

    // g_selfModule 是 DllMain 保存的当前 d3d9 代理 DLL 模块句柄。
    // 传 NULL 则取得主 EXE 路径，用于 Video Overlay 注册表的“值名称”。
    g_GetModuleFileNameA(g_selfModule,g_selfPath,(DWORD)sizeof(g_selfPath));
    g_GetModuleFileNameA(0,g_mainExePath,(DWORD)sizeof(g_mainExePath));
    ReplaceModuleExtension(g_selfPath,".ini",g_iniPath,(UINT)sizeof(g_iniPath));
    ReplaceModuleExtension(g_selfPath,".log",g_logPath,(UINT)sizeof(g_logPath));
}

// 根据“当前这个代理 DLL 自己所在的目录”拼出一个同目录文件的绝对路径。
//
// 为什么不能简单写 LoadLibraryA("d3d9_backend.dll")：
// Windows 对没有目录的 DLL 名会按 DLL 搜索顺序查找。正常情况下应用目录优先，
// 但系统策略、SetDllDirectory、其他注入模块都可能改变搜索行为。
// 对一个兼容层来说，用户既然把 DXVK 改名成 d3d9_backend.dll 放在修复 DLL 旁边，
// 最安全、最可预测的行为就是只加载“这个明确的兄弟文件”，而不是让系统到别处找同名 DLL。
//
// 例子：
//   g_selfPath = C:\Games\Baldr Sky\d3d9.dll
//   fileName   = d3d9_backend.dll
//   结果       = C:\Games\Baldr Sky\d3d9_backend.dll
static bool BuildSiblingFilePath(const char* fileName, char* out, UINT cap) {
    if (!fileName || !out || cap<8) return false;

    // 先确保已经取得当前 DLL 的完整路径。
    BuildModulePathsOnce();
    if (!g_selfPath[0]) return false;

    // 找最后一个目录分隔符。lastSlash 保存“文件名开始前”的位置。
    int lastSlash=-1;
    for (UINT i=0; g_selfPath[i]; ++i) {
        if (g_selfPath[i]=='\\' || g_selfPath[i]=='/') lastSlash=(int)i;
    }

    // 正常通过绝对路径加载的模块一定有目录；即使极端情况下没有，
    // 也允许从输出开头直接写 fileName，而不是越界。
    UINT p=0;
    if (lastSlash>=0) {
        for (int i=0; i<=lastSlash; ++i) {
            if (p+1>=cap) return false;
            out[p++]=g_selfPath[i];
        }
    }

    // 把目标兄弟文件名接在目录后面，并保证最后有 NUL。
    for (UINT i=0; fileName[i]; ++i) {
        if (p+1>=cap) return false;
        out[p++]=fileName[i];
    }
    out[p]=0;
    return true;
}

// 检查当前进程的主 EXE 是否就是 BaldrSky.exe。
//
// 为什么要做这个额外保护：
// 修复 DLL 放在游戏目录以后，同目录的 StartUpTool.exe 或其他辅助程序理论上也可能
// 因为自己使用 Direct3D9 而加载到本地 d3d9.dll。
// 如果不区分主程序，我们就可能把 SteamFix 或视频注册表修复错误施加到辅助工具。
//
// 因此“会修改游戏内存或注册表”的逻辑只允许在主程序文件名确实为 BaldrSky.exe 时运行。
// 比较只忽略 ASCII 大小写，不接受其他文件名。
static bool IsBaldrSkyMainProcess() {
    BuildModulePathsOnce();
    if (!g_mainExePath[0]) return false;

    // 从完整路径中找到最后一个目录分隔符，取出纯文件名。
    // 例如 C:\\Games\\Baldr Sky\\BaldrSky.exe 最后只比较 BaldrSky.exe。
    const char* baseName=g_mainExePath;
    for (const char* p=g_mainExePath; *p; ++p) {
        if (*p=='\\' || *p=='/') baseName=p+1;
    }

    const char* wanted="BaldrSky.exe";
    UINT actualLen=AsciiLen(baseName);
    UINT wantedLen=AsciiLen(wanted);
    if (actualLen!=wantedLen) return false;

    for (UINT i=0;i<actualLen;++i) {
        if (LowerAsciiSimple(baseName[i])!=LowerAsciiSimple(wanted[i])) return false;
    }
    return true;
}

static int ClampInt(int v,int lo,int hi) {
    if (v<lo) return lo;
    if (v>hi) return hi;
    return v;
}

static void LoadConfigOnce() {
    if (g_configLoaded) return;
    g_configLoaded=true;
    BuildModulePathsOnce();

    // 如果 GetPrivateProfileIntA 不可用，保持上面结构体里的安全默认值。
    if (!g_GetPrivateProfileIntA || !g_iniPath[0]) return;

    g_cfg.enableSteamFix = g_GetPrivateProfileIntA("Compatibility","EnableSteamFix",1,g_iniPath)?1:0;
    g_cfg.enableVideoOverlayFix = g_GetPrivateProfileIntA("Compatibility","EnableVideoOverlayFix",1,g_iniPath)?1:0;
    g_cfg.enableHighDpiFix = g_GetPrivateProfileIntA("Compatibility","EnableHighDpiFix",1,g_iniPath)?1:0;
    g_cfg.backendMode = ClampInt((int)g_GetPrivateProfileIntA("Graphics","Backend",0,g_iniPath),0,2);

    g_cfg.enableLog = g_GetPrivateProfileIntA("Diagnostics","EnableLog",1,g_iniPath)?1:0;
    g_cfg.logLevel = ClampInt((int)g_GetPrivateProfileIntA("Diagnostics","LogLevel",2,g_iniPath),0,3);
    g_cfg.enableCrashDiagnostics = g_GetPrivateProfileIntA("Diagnostics","EnableCrashDiagnostics",0,g_iniPath)?1:0;
    g_cfg.overlayGuardIntervalMs = ClampInt((int)g_GetPrivateProfileIntA("Diagnostics","OverlayGuardIntervalMs",500,g_iniPath),250,60000);
}

// 高 DPI 初始化位于正式日志辅助函数定义之前。
// 这里先写“函数声明”，相当于提前告诉编译器：下面这些日志/字符串工具稍后会实现。
// 这样 DPI 代码可以放在配置读取之后、统一初始化之前，又不需要把整套日志函数搬位置。
static void LogText(const char* s, bool flushNow);
static void LogDetail(const char* s, bool flushNow);
static void LogError(const char* s, bool flushNow);
static void AppendText(char* b, UINT& p, UINT cap, const char* s);
static void AppendDec(char* b, UINT& p, UINT cap, DWORD v);

// ----------------------------------------------------------------------------
// 高 DPI 缩放兼容：默认 System DPI Aware
// ----------------------------------------------------------------------------
// Windows 对没有 DPI manifest 的老程序会启用 DPI virtualization。
// 简单理解：游戏以为自己画的是 800x600，但 Windows 可能先把窗口/坐标当成
// “逻辑像素”，再按照 125% / 150% 等桌面缩放比例进行二次放大。
// 对普通办公软件这能让老程序“看起来没那么小”，但对固定像素 D3D9 游戏，
// 用户更常希望 800x600 就是实际 800x600 物理像素，再由游戏/DXVK/外部缩放器处理。
//
// 用户明确要求从正式候选开始默认加入高 DPI 缩放兼容，并允许 INI 关闭。
// test9 因此选择最保守的 System DPI Aware，而不是 Per-Monitor V2：
//
//   System DPI Aware：
//     - 进程启动时按主显示器 DPI 工作；
//     - 关闭传统 DPI virtualization；
//     - 不要求 2009 年程序正确处理 WM_DPICHANGED。
//
//   Per-Monitor V2（本项目不用）：
//     - 窗口跨显示器时 DPI 可以动态改变；
//     - Windows 会期待应用正确响应 DPI 变化；
//     - 对没有为此设计的旧游戏，反而更容易出现窗口尺寸/鼠标坐标回归。
//
// 调用时机也很重要。
// 我们在第一次 Direct3DCreate9 入口、真正加载系统/DXVK 后端之前调用。
// 对 BALDR SKY 来说这已经非常早；如果外部 manifest/兼容层更早固定了 DPI 模式，
// Windows 可能拒绝再次修改，这时只记录警告，不强行覆盖。
static bool g_highDpiAttempted=false;

static void ApplyHighDpiFixOnce() {
    // “Once” 很重要：Direct3DCreate9 可能被程序或库调用多次，
    // DPI awareness 属于进程级状态，没有必要每次重复设置。
    if (g_highDpiAttempted) return;
    g_highDpiAttempted=true;

    if (!g_cfg.enableHighDpiFix) {
        LogText("[DPI] 高 DPI 缩放兼容已由 INI 关闭；保持 Windows/外部兼容设置决定的 DPI 行为。",true);
        return;
    }

    if (!g_LoadLibraryA || !g_GetProcAddress) {
        LogError("[DPI][警告] Windows API 解析不完整，无法设置 System DPI Aware；游戏继续启动。",true);
        return;
    }

    // user32.dll 在 BALDR SKY 创建窗口时本来就需要。
    // 如果此刻还没加载，说明我们的 Direct3DCreate9 代理确实非常早被调用；
    // 这里已经离开 DllMain/Loader Lock，可以安全 LoadLibraryA。
    HMODULE user32=(HMODULE)FindLoadedModule("user32.dll");
    if (!user32) user32=g_LoadLibraryA("user32.dll");
    if (!user32) {
        LogError("[DPI][警告] 无法加载 user32.dll，未能设置 System DPI Aware；游戏继续启动。",true);
        return;
    }

    PFN_SetProcessDpiAwarenessContext setContext=
        (PFN_SetProcessDpiAwarenessContext)g_GetProcAddress(user32,"SetProcessDpiAwarenessContext");
    PFN_SetProcessDPIAware setLegacy=
        (PFN_SetProcessDPIAware)g_GetProcAddress(user32,"SetProcessDPIAware");

    // Windows 10 优先使用新接口。-2 就是 SYSTEM_AWARE。
    // 成功后直接结束，不再调用旧接口，避免无意义的第二次进程级设置。
    if (setContext) {
        if (setContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE_VALUE)) {
            LogText("[DPI][成功] 已设置 System DPI Aware；Windows 传统 DPI 虚拟化已关闭。",true);
            return;
        }

        // 新接口存在但失败时，最常见原因是进程 DPI awareness 已经被 manifest、
        // EXE 兼容属性或其他更早加载的组件固定。我们记录 LastError 后仍尝试旧接口；
        // 如果旧接口也失败，保持既有状态即可，不把 DPI 设置失败升级成启动失败。
        DWORD err=g_GetLastError?g_GetLastError():0;
        char b[220]; UINT p=0;
        AppendText(b,p,sizeof(b),"[DPI][详细] SetProcessDpiAwarenessContext(SystemAware) 未生效，LastError=");
        AppendDec(b,p,sizeof(b),err); b[p]=0; LogDetail(b,true);
    }

    // Vista/7/8 等系统没有新接口时走这里。SetProcessDPIAware 的语义就是 System Aware。
    if (setLegacy) {
        if (setLegacy()) {
            LogText("[DPI][成功] 已通过兼容接口设置 System DPI Aware；Windows 传统 DPI 虚拟化已关闭。",true);
            return;
        }

        DWORD err=g_GetLastError?g_GetLastError():0;
        char b[220]; UINT p=0;
        AppendText(b,p,sizeof(b),"[DPI][警告] System DPI Aware 未能由兼容层设置，LastError=");
        AppendDec(b,p,sizeof(b),err);
        AppendText(b,p,sizeof(b),"；保留当前进程已有 DPI 状态。 ");
        b[p]=0; LogError(b,true);
        return;
    }

    LogError("[DPI][警告] 当前 Windows 没有可用的 DPI awareness API；保留系统默认行为。",true);
}

// ----------------------------------------------------------------------------
// 日志辅助函数
// ----------------------------------------------------------------------------
// 不使用 printf/std::string。每一行都是手工把数字转换成字符后 WriteFile。
// 这样 DLL 不需要 C Runtime。
static void EnsureLog() {
    LoadConfigOnce();
    if (!g_cfg.enableLog) return;
    if (g_log != INVALID_HANDLE_VALUE) return;
    if (!InitWinApis()) return;
    BuildModulePathsOnce();
    if (!g_logPath[0]) return;

    // 每次进程启动都重新创建日志，避免旧测试与本轮内容粘在一起。
    // FILE_SHARE_READ/WRITE 允许用户在游戏运行时直接打开日志查看。
    g_log = g_CreateFileA(
        g_logPath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
}

static void WriteRaw(const char* s, UINT len, bool flushNow = false) {
    LoadConfigOnce();
    if (!g_cfg.enableLog) return;
    EnsureLog();
    if (g_log == INVALID_HANDLE_VALUE || !g_WriteFile || !s || !len) return;
    DWORD written = 0;
    g_WriteFile(g_log, s, len, &written, 0);
    if (flushNow && g_FlushFileBuffers) g_FlushFileBuffers(g_log);
}

// level：0=错误，1=基本，2=详细，3=调试。
// 用户把 LogLevel 调低时，修复逻辑本身完全不受影响，只减少日志噪音。
static void LogTextLevel(int level,const char* s, bool flushNow = false) {
    LoadConfigOnce();
    if (!g_cfg.enableLog || level>g_cfg.logLevel) return;
    WriteRaw(s, AsciiLen(s), false);
    WriteRaw("\r\n", 2, flushNow);
}

static void LogText(const char* s, bool flushNow = false) { LogTextLevel(1,s,flushNow); }
static void LogDetail(const char* s, bool flushNow = false) { LogTextLevel(2,s,flushNow); }
static void LogDebug(const char* s, bool flushNow = false) { LogTextLevel(3,s,flushNow); }
static void LogError(const char* s, bool flushNow = false) { LogTextLevel(0,s,flushNow); }

static void AppendChar(char* b, UINT& p, UINT cap, char c) {
    if (p + 1 < cap) b[p++] = c;
}
static void AppendText(char* b, UINT& p, UINT cap, const char* s) {
    while (s && *s && p + 1 < cap) b[p++] = *s++;
}
static void AppendHex32(char* b, UINT& p, UINT cap, DWORD v) {
    static const char* h = "0123456789ABCDEF";
    AppendText(b,p,cap,"0x");
    for (int i=7;i>=0;--i) AppendChar(b,p,cap,h[(v>>(i*4))&0xF]);
}
static void AppendDec(char* b, UINT& p, UINT cap, DWORD v) {
    char tmp[16]; UINT n=0;
    if (!v) { AppendChar(b,p,cap,'0'); return; }
    while (v && n<15) { tmp[n++]=(char)('0'+(v%10)); v/=10; }
    while (n) AppendChar(b,p,cap,tmp[--n]);
}
static DWORD Ptr32(const void* p) { return (DWORD)(unsigned long)p; }

// 当前线程编号。D3D9 是用 D3DCREATE_MULTITHREADED 建立的，而 test3 日志已经出现
// 两套渲染路径互相穿插，所以从 test4 起每个关键事件都必须带 Thread ID。
static DWORD CurrentTid() {
    return g_GetCurrentThreadId ? g_GetCurrentThreadId() : 0;
}

// 先询问 Windows：从 addr 开始的 bytes 是否处在同一个、已提交且可读取的页面区域。
// 这不是为了判断“是不是代码”，只是保证诊断器不会盲目解引用坏指针。
static bool CanReadMemory(DWORD addr, DWORD bytes) {
    if (!g_VirtualQuery || !addr || !bytes) return false;
    MEMORY_BASIC_INFORMATION32 mbi;
    DWORD got = g_VirtualQuery((const void*)(unsigned long)addr, &mbi, (DWORD)sizeof(mbi));
    if (got < (DWORD)sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    DWORD base = Ptr32(mbi.BaseAddress);
    DWORD end = base + mbi.RegionSize;
    if (end < base) return false;
    if (addr < base || addr + bytes < addr || addr + bytes > end) return false;
    return true;
}

// 判断一个地址是否落在当前 BaldrSky.exe 映像里。
// 如果 [ESP] 是一次 `call 0` 压入的返回地址，它通常应该落在这里。
static bool IsInMainImage(DWORD addr) {
    BYTE* base = (BYTE*)FindLoadedModule("BaldrSky.exe");
    if (!base) return false;
    DWORD peOff = *(DWORD*)(base + 0x3C);
    BYTE* nt = base + peOff;
    BYTE* optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return false;
    DWORD imageSize = *(DWORD*)(optional + 56);
    DWORD imageBase = Ptr32(base);
    return addr >= imageBase && addr < imageBase + imageSize;
}


static void LogHex(const char* key, DWORD value, bool flushNow = false) {
    char b[160]; UINT p=0;
    AppendText(b,p,sizeof(b),key); AppendHex32(b,p,sizeof(b),value); b[p]=0;
    LogText(b,flushNow);
}
static void LogDec(const char* key, DWORD value, bool flushNow = false) {
    char b[160]; UINT p=0;
    AppendText(b,p,sizeof(b),key); AppendDec(b,p,sizeof(b),value); b[p]=0;
    LogText(b,flushNow);
}

// 把一个字节写成两位十六进制。只用于显示运行时代码字节。
static void AppendHex8(char* b, UINT& p, UINT cap, BYTE v) {
    static const char* h = "0123456789ABCDEF";
    AppendChar(b,p,cap,h[(v >> 4) & 0xF]);
    AppendChar(b,p,cap,h[v & 0xF]);
}

// caller 是刚刚真实执行过的游戏代码返回地址，因此 caller 附近一定是当前进程
// 已经映射的可执行页面。test2 证明磁盘 EXE 同地址仍是壳/加密数据，所以这里直接把
// 运行时真实机器码抄进日志，后续即可离线反汇编。
static void LogRuntimeCodeWindow(DWORD caller) {
    static DWORD dumpedCaller = 0;
    if (!caller || dumpedCaller == caller) return;
    dumpedCaller = caller;

    BYTE* start = (BYTE*)(unsigned long)(caller - 24);
    char b[640]; UINT p=0;
    AppendText(b,p,sizeof(b),"RuntimeCode around caller=");
    AppendHex32(b,p,sizeof(b),caller);
    AppendText(b,p,sizeof(b)," start=");
    AppendHex32(b,p,sizeof(b),caller - 24);
    AppendText(b,p,sizeof(b)," bytes=");
    for (UINT i=0;i<64 && p+4<sizeof(b);++i) {
        AppendHex8(b,p,sizeof(b),start[i]);
        if (i != 63) AppendChar(b,p,sizeof(b),' ');
    }
    b[p]=0;
    LogText(b,true);
}

// test4 的 VEH 仍然只观察，不修改 CONTEXT，也不返回 CONTINUE_EXECUTION。
// 与 test3 不同的是，现在把 Thread ID、ESP 原始栈以及 [ESP] 返回地址一起记录。
//
// 为什么 [ESP] 极其重要：
// x86 的 `call 某地址` 会先把“call 后的下一条指令地址”压进栈顶，再跳转。
// 如果目标函数指针恰好是 0，那么 CPU 到 EIP=0 时，[ESP] 往往就是谁发起了这次 call。
static DWORD g_avSequence = 0;

static void LogFaultStack(DWORD avSeq, DWORD tid, CONTEXT32_MIN* c) {
    if (!c) return;
    DWORD esp = c->Esp;
    if (!CanReadMemory(esp, 32 * 4)) {
        char b[220]; UINT p=0;
        AppendText(b,p,sizeof(b),"AV#"); AppendDec(b,p,sizeof(b),avSeq);
        AppendText(b,p,sizeof(b)," TID="); AppendDec(b,p,sizeof(b),tid);
        AppendText(b,p,sizeof(b)," StackUnreadable ESP="); AppendHex32(b,p,sizeof(b),esp);
        b[p]=0; LogText(b,true);
        return;
    }

    DWORD* stack = (DWORD*)(unsigned long)esp;
    DWORD ret = stack[0];
    {
        char b[260]; UINT p=0;
        AppendText(b,p,sizeof(b),"AV#"); AppendDec(b,p,sizeof(b),avSeq);
        AppendText(b,p,sizeof(b)," TID="); AppendDec(b,p,sizeof(b),tid);
        AppendText(b,p,sizeof(b)," FaultReturnAddress=[ESP]="); AppendHex32(b,p,sizeof(b),ret);
        AppendText(b,p,sizeof(b)," MainImage="); AppendDec(b,p,sizeof(b),IsInMainImage(ret)?1:0);
        b[p]=0; LogText(b,true);
    }

    // 一次写 4 个 DWORD，共保存栈顶 128 字节。
    // 就算 [ESP] 不是直接返回地址，后面的值也常能暴露上一层/上几层调用者。
    for (DWORD row=0; row<8; ++row) {
        char b[360]; UINT p=0;
        AppendText(b,p,sizeof(b),"AV#"); AppendDec(b,p,sizeof(b),avSeq);
        AppendText(b,p,sizeof(b)," TID="); AppendDec(b,p,sizeof(b),tid);
        AppendText(b,p,sizeof(b)," STACK+"); AppendHex32(b,p,sizeof(b),row*16);
        for (DWORD j=0;j<4;++j) {
            AppendChar(b,p,sizeof(b),' ');
            AppendHex32(b,p,sizeof(b),stack[row*4+j]);
        }
        b[p]=0; LogText(b,true);
    }

    // 只有确认 [ESP] 落在 BaldrSky.exe 映像里才读取其附近机器码，避免诊断器
    // 把随机栈数据误当地址后造成第二次异常。
    if (IsInMainImage(ret) && ret >= 24 && CanReadMemory(ret-24,64)) {
        LogRuntimeCodeWindow(ret);
    }
}

static LONG __stdcall ProbeVectoredExceptionHandler(EXCEPTION_POINTERS32* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH_VALUE;
    EXCEPTION_RECORD32* er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION_CODE)
        return EXCEPTION_CONTINUE_SEARCH_VALUE;

    DWORD avSeq = ++g_avSequence;
    DWORD tid = CurrentTid();
    CONTEXT32_MIN* c = ep->ContextRecord;

    char b[520]; UINT p=0;
    AppendText(b,p,sizeof(b),"================ VEH AV#"); AppendDec(b,p,sizeof(b),avSeq);
    AppendText(b,p,sizeof(b)," TID="); AppendDec(b,p,sizeof(b),tid);
    AppendText(b,p,sizeof(b)," ExceptionAddress="); AppendHex32(b,p,sizeof(b),Ptr32(er->ExceptionAddress));
    if (er->NumberParameters >= 2) {
        AppendText(b,p,sizeof(b)," AccessType="); AppendHex32(b,p,sizeof(b),er->ExceptionInformation[0]);
        AppendText(b,p,sizeof(b)," AccessAddress="); AppendHex32(b,p,sizeof(b),er->ExceptionInformation[1]);
    }
    if (c) {
        AppendText(b,p,sizeof(b)," EIP="); AppendHex32(b,p,sizeof(b),c->Eip);
        AppendText(b,p,sizeof(b)," ESP="); AppendHex32(b,p,sizeof(b),c->Esp);
        AppendText(b,p,sizeof(b)," EBP="); AppendHex32(b,p,sizeof(b),c->Ebp);
    }
    b[p]=0; LogText(b,true);

    if (c) {
        char r[520]; UINT q=0;
        AppendText(r,q,sizeof(r),"AV#"); AppendDec(r,q,sizeof(r),avSeq);
        AppendText(r,q,sizeof(r)," TID="); AppendDec(r,q,sizeof(r),tid);
        AppendText(r,q,sizeof(r)," REG EAX="); AppendHex32(r,q,sizeof(r),c->Eax);
        AppendText(r,q,sizeof(r)," EBX="); AppendHex32(r,q,sizeof(r),c->Ebx);
        AppendText(r,q,sizeof(r)," ECX="); AppendHex32(r,q,sizeof(r),c->Ecx);
        AppendText(r,q,sizeof(r)," EDX="); AppendHex32(r,q,sizeof(r),c->Edx);
        AppendText(r,q,sizeof(r)," ESI="); AppendHex32(r,q,sizeof(r),c->Esi);
        AppendText(r,q,sizeof(r)," EDI="); AppendHex32(r,q,sizeof(r),c->Edi);
        r[q]=0; LogText(r,true);
        LogFaultStack(avSeq,tid,c);
    }

    LogText("================ VEH CONTINUE SEARCH ================",true);
    return EXCEPTION_CONTINUE_SEARCH_VALUE;
}

// Vectored Continue Handler 只会观察异常处理后的“继续执行”阶段。
// 如果 test3 中的 EIP=0 AV 被游戏自己的 SEH 恢复，这里可以看到恢复后的 EIP。
static LONG __stdcall ProbeVectoredContinueHandler(EXCEPTION_POINTERS32* ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH_VALUE;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION_CODE)
        return EXCEPTION_CONTINUE_SEARCH_VALUE;

    char b[320]; UINT p=0;
    AppendText(b,p,sizeof(b),"VCH AV handled/resume TID="); AppendDec(b,p,sizeof(b),CurrentTid());
    AppendText(b,p,sizeof(b)," OriginalExceptionAddress=");
    AppendHex32(b,p,sizeof(b),Ptr32(ep->ExceptionRecord->ExceptionAddress));
    AppendText(b,p,sizeof(b)," ResumeEIP="); AppendHex32(b,p,sizeof(b),ep->ContextRecord->Eip);
    AppendText(b,p,sizeof(b)," ResumeESP="); AppendHex32(b,p,sizeof(b),ep->ContextRecord->Esp);
    b[p]=0; LogText(b,true);
    return EXCEPTION_CONTINUE_SEARCH_VALUE;
}

static void InstallVehOnce() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    if (!g_AddVectoredExceptionHandler) {
        LogText("VEH: AddVectoredExceptionHandler 不可用。",true);
        return;
    }
    void* handle = g_AddVectoredExceptionHandler(1, (void*)&ProbeVectoredExceptionHandler);
    LogHex("[诊断] VEH句柄=",Ptr32(handle),true);
    if (g_AddVectoredContinueHandler) {
        void* ch = g_AddVectoredContinueHandler(1, (void*)&ProbeVectoredContinueHandler);
        LogHex("[诊断] VCH句柄=",Ptr32(ch),true);
    }
}

static void LogPresentParameters(const D3DPRESENT_PARAMETERS32* p) {
    if (!p) { LogText("  PresentParameters=NULL"); return; }
    LogDec("  BackBufferWidth=", p->BackBufferWidth);
    LogDec("  BackBufferHeight=", p->BackBufferHeight);
    LogHex("  BackBufferFormat=", (DWORD)p->BackBufferFormat);
    LogDec("  BackBufferCount=", p->BackBufferCount);
    LogHex("  MultiSampleType=", (DWORD)p->MultiSampleType);
    LogDec("  MultiSampleQuality=", p->MultiSampleQuality);
    LogHex("  SwapEffect=", (DWORD)p->SwapEffect);
    LogHex("  hDeviceWindow=", Ptr32(p->hDeviceWindow));
    LogDec("  Windowed=", (DWORD)p->Windowed);
    LogDec("  EnableAutoDepthStencil=", (DWORD)p->EnableAutoDepthStencil);
    LogHex("  AutoDepthStencilFormat=", (DWORD)p->AutoDepthStencilFormat);
    LogHex("  Flags=", p->Flags);
    LogDec("  FullScreen_RefreshRateInHz=", p->FullScreen_RefreshRateInHz);
    LogHex("  PresentationInterval=", p->PresentationInterval);
}

// ============================================================================
// test8：Steam .patch 特征码 / 语义签名定位
// ============================================================================
//
// 这里不是“搜索 85 C9 然后改掉”。
// 85 C9（test ecx,ecx）在普通 x86 程序里太常见，单独拿它做特征码非常危险。
//
// test8 使用三层验证：
//
// 第一层：独立语义字符串。
// 中文版与英文版都包含同一组 Steam 桥名字，但英文版会在它们之间插入
// 字体/设备初始化相关字符串，所以绝不能要求这些字符串物理连续。
// 我们分别定位 BaldrUtil.dll / InitSteam / CloseSteam / UpdateSteam /
// ResetAchievements / GetAchievement，再用机器码交叉引用把它们重新关联起来。
//
// 第二层：初始化指令结构。
// 我们要求找到：
//   lea ecx, "GDI32.dll"
//   push ecx
//   call [同一个 LoadLibraryA IAT 槽]
//   test ecx,ecx       <- 错误点 1
//   ...
//   lea ecx, "BaldrUtil.dll"
//   push ecx
//   call [同一个 LoadLibraryA IAT 槽]
//   test ecx,ecx       <- 错误点 2
//
// 第三层：UpdateSteam trampoline。
// 我们先从 GetProcAddress 初始化序列推导 UpdateSteam 函数指针槽地址，
// 再要求 trampoline 里确实存在：
//   push eax/ebx/ecx/edx
//   call [刚刚推导出的 UpdateSteam 槽]
//   pop edx/ecx/ebx/eax
//   push esi
//   mov esi,ecx
//   call rel32
//   jmp rel32
//
// 最后一条 jmp 的旧目标不是写死的。
// 已确认 trampoline 重放了 8 字节原指令，而旧补丁只按 5 字节 detour 返回，
// 所以正确继续地址永远是“旧 jmp 目标 + 3”。
// 这样即使英文版 EXE 的代码整体搬家，只要 Steam 补丁结构相同也能定位。
// ============================================================================

typedef void (__stdcall *PFN_SteamPatchInit)();

static bool BytesEqualSimple(const BYTE* a, const BYTE* b, UINT n) {
    if (!a || !b) return false;
    for (UINT i=0; i<n; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// 从小端 4 字节读取一个 32 位整数。
// x86 指令里的绝对地址和 rel32 位移都是这种排列。
static DWORD ReadU32(const BYTE* p) {
    if (!p) return 0;
    return ((DWORD)p[0]) |
           ((DWORD)p[1] << 8) |
           ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

static void WriteU32(BYTE* p, DWORD v) {
    if (!p) return;
    p[0]=(BYTE)(v & 0xFF);
    p[1]=(BYTE)((v >> 8) & 0xFF);
    p[2]=(BYTE)((v >> 16) & 0xFF);
    p[3]=(BYTE)((v >> 24) & 0xFF);
}

// 取得当前 PE 映像的 SizeOfImage。
// 之后所有扫描都限制在 BaldrSky.exe 自己的映像范围内，绝不会全进程乱扫。
static DWORD GetImageSize32(BYTE* base) {
    if (!base) return 0;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    DWORD peOff=ReadU32(base+0x3C);
    BYTE* nt=base+peOff;
    if (nt[0]!='P' || nt[1]!='E' || nt[2]!=0 || nt[3]!=0) return 0;
    BYTE* optional=nt+24;
    if (*(WORD*)optional != 0x10B) return 0; // 0x10B = PE32
    return ReadU32(optional+56);
}

// 在 [begin, begin+size) 中找完整字节串，并返回命中数量。
// firstHit 只保存第一处；调用者必须检查 count==1 才能把它当成安全定位结果。
static DWORD FindByteSequence(BYTE* begin, DWORD size,
                              const BYTE* needle, DWORD needleSize,
                              BYTE** firstHit) {
    if (firstHit) *firstHit=0;
    if (!begin || !needle || !needleSize || size < needleSize) return 0;

    DWORD count=0;
    for (DWORD i=0; i<=size-needleSize; ++i) {
        if (!BytesEqualSimple(begin+i,needle,needleSize)) continue;
        if (count==0 && firstHit) *firstHit=begin+i;
        ++count;
    }
    return count;
}

struct SteamSignatureLayout {
    BYTE* imageBase;
    DWORD imageSize;

    DWORD baldrUtilStringVA;
    DWORD initSteamStringVA;
    DWORD closeSteamStringVA;
    DWORD updateSteamStringVA;
    DWORD resetAchievementsStringVA;
    DWORD getAchievementStringVA;

    BYTE* gdiLoadSite;
    BYTE* baldrLoadSite;
    BYTE* helperStart;
    BYTE* testGdi;
    BYTE* testBaldr;
    DWORD loadLibraryIatVA;

    DWORD initSteamSlotVA;
    DWORD updateSteamSlotVA;
    BYTE* initEntry;

    BYTE* trampoline;
    BYTE* trampolineJmp;
    DWORD trampolineOldTarget;
    DWORD trampolineNewTarget;
};

// 判断一个绝对地址是否落在当前 EXE 映像范围内。
// 从机器码中读出来的数字只有通过这个检查，才允许被当成指针继续使用。
static bool IsVAInsideImage(BYTE* image, DWORD imageSize, DWORD va, DWORD needBytes) {
    if (!image || !imageSize) return false;
    DWORD base=Ptr32(image);
    if (va < base) return false;
    DWORD off=va-base;
    if (off > imageSize) return false;
    if (needBytes > imageSize-off) return false;
    return true;
}

// 比较映像中的 NUL 结尾 ASCII 字符串。
// DLL 不依赖 CRT，因此不用 strcmp，而是逐字节比较。
static bool ImageAsciiEquals(BYTE* image, DWORD imageSize, DWORD va, const char* text) {
    if (!text) return false;
    DWORD n=AsciiLen(text)+1;
    if (!IsVAInsideImage(image,imageSize,va,n)) return false;
    BYTE* q=image+(va-Ptr32(image));
    for (DWORD i=0;i<n;++i) {
        if (q[i]!=(BYTE)text[i]) return false;
    }
    return true;
}

// 在 BaldrSky.exe 映像中查找一个完整 NUL 结尾 ASCII 字符串。
// 返回的是命中数量；只有 count==1 时才把地址写给调用者。
static DWORD FindAsciiStringExact(BYTE* image, DWORD imageSize,
                                  const char* text, DWORD* outVA) {
    if (outVA) *outVA=0;
    if (!image || !text) return 0;
    BYTE* first=0;
    DWORD count=FindByteSequence(
        image,imageSize,(const BYTE*)text,AsciiLen(text)+1,&first);
    if (count==1 && first && outVA) *outVA=Ptr32(first);
    return count;
}

// 找：
//   lea ecx, <DLL名字字符串>
//   push ecx
//   call dword ptr [LoadLibraryA IAT槽]
//   test ecx,ecx
//
// 这比直接搜索 85 C9 安全很多，因为还同时验证 DLL 名、调用形式和 IAT 槽。
// expectedLoadIatVA==0 表示第一次搜索，不预设 IAT；非0则要求同一个 IAT 槽。
static DWORD FindLoadLibraryStringSite(BYTE* image, DWORD imageSize,
                                       const char* dllName,
                                       DWORD expectedLoadIatVA,
                                       BYTE** outSite,
                                       DWORD* outLoadIatVA) {
    if (outSite) *outSite=0;
    if (outLoadIatVA) *outLoadIatVA=0;
    if (!image || !dllName || imageSize<15) return 0;

    DWORD count=0;
    BYTE* first=0;
    DWORD firstIat=0;

    for (DWORD i=0;i<=imageSize-15;++i) {
        BYTE* q=image+i;
        if (q[0]!=0x8D || q[1]!=0x0D) continue;
        DWORD stringVA=ReadU32(q+2);
        if (!ImageAsciiEquals(image,imageSize,stringVA,dllName)) continue;
        if (q[6]!=0x51 || q[7]!=0xFF || q[8]!=0x15) continue;
        DWORD iatVA=ReadU32(q+9);
        if (expectedLoadIatVA && iatVA!=expectedLoadIatVA) continue;
        if (q[13]!=0x85 || q[14]!=0xC9) continue;

        if (!first) { first=q; firstIat=iatVA; }
        ++count;
    }

    if (count==1) {
        if (outSite) *outSite=first;
        if (outLoadIatVA) *outLoadIatVA=firstIat;
    }
    return count;
}

// 找 “push <slot> ; push <functionName> ; call rel32”。
// 中英文 Steam 补丁都用这一模板把 GetProcAddress 结果写入函数指针槽。
static DWORD FindSteamProcSlot(BYTE* image, DWORD imageSize,
                               DWORD functionNameVA,
                               DWORD* outSlotVA,
                               BYTE** outCallSite,
                               BYTE** outHelperTarget) {
    if (outSlotVA) *outSlotVA=0;
    if (outCallSite) *outCallSite=0;
    if (outHelperTarget) *outHelperTarget=0;
    if (!image || imageSize<15) return 0;

    DWORD count=0;
    DWORD slot=0;
    BYTE* callSite=0;
    BYTE* helper=0;

    for (DWORD i=0;i<=imageSize-15;++i) {
        BYTE* q=image+i;
        if (q[0]!=0x68 || q[5]!=0x68 || q[10]!=0xE8) continue;
        if (ReadU32(q+6)!=functionNameVA) continue;

        DWORD candidateSlot=ReadU32(q+1);
        if (!IsVAInsideImage(image,imageSize,candidateSlot,4)) continue;

        LONG rel=(LONG)ReadU32(q+11);
        BYTE* candidateHelper=q+15+rel;
        if (candidateHelper<image || candidateHelper>=image+imageSize) continue;

        if (!callSite) { slot=candidateSlot; callSite=q; helper=candidateHelper; }
        ++count;
    }

    if (count==1) {
        if (outSlotVA) *outSlotVA=slot;
        if (outCallSite) *outCallSite=callSite;
        if (outHelperTarget) *outHelperTarget=helper;
    }
    return count;
}

// 找 Steam init 入口。
//
// 中文 Steam EXE 的 init 很短：
//   call loaderHelper
//   cmp [InitSteamSlot],0
//   ...
//   call [InitSteamSlot]
//   ret
//
// 英文 Steam EXE 在 loaderHelper 与 InitSteam 之间额外加入：
//   - 自定义字体资源加载；
//   - InitDive1；
//   - LoadDynamicStringZone；
//   - InitFont / UnloadFont；
// 等代码。
//
// 因此不能再要求“call helper 后的第 5 字节立刻就是 InitSteam 检查”。
// 真正跨版本稳定的语义是：
//   1. 某个小函数首先（或很靠前）call 到刚刚定位出的 loader helper；
//   2. 在后面一个有限范围内，确实通过 `FF 15 <InitSteamSlot>` 调用了
//      同一个、由 `InitSteam` 字符串交叉引用推导出的函数槽；
//   3. 这段小函数在有限范围内能看到 RET。
//
// 范围限制为 192 字节：足够覆盖英文版新增逻辑，又不会把远处另一个函数误并进来。
// 最终仍要求全映像只有一个候选，任何多命中都拒绝修改。
static DWORD FindSteamInitEntry(BYTE* image, DWORD imageSize,
                                BYTE* helperStart, DWORD initSlotVA,
                                BYTE** outEntry) {
    if (outEntry) *outEntry=0;
    if (!image || !helperStart || !initSlotVA || imageSize<16) return 0;

    DWORD count=0;
    BYTE* first=0;

    for (DWORD i=0;i+5<imageSize;++i) {
        BYTE* q=image+i;
        if (q[0]!=0xE8) continue;

        // E8 后面 4 字节是相对于“下一条指令”的有符号位移。
        LONG rel=(LONG)ReadU32(q+1);
        if (q+5+rel!=helperStart) continue;

        DWORD remain=imageSize-i;
        DWORD window=(remain<192)?remain:192;
        bool sawInitCall=false;
        bool sawRet=false;

        // 从 helper 调用之后开始看。
        // InitSteam 调用编码为 FF 15 <绝对IAT/槽地址>。
        for (DWORD k=5;k<window;++k) {
            if (q[k]==0xC3) sawRet=true;

            if (k+6<=window &&
                q[k]==0xFF && q[k+1]==0x15 &&
                ReadU32(q+k+2)==initSlotVA) {
                sawInitCall=true;
            }
        }

        if (!sawInitCall || !sawRet) continue;

        if (!first) first=q;
        ++count;
    }

    if (count==1 && outEntry) *outEntry=first;
    return count;
}

// 找每帧 UpdateSteam trampoline。
// 函数槽来自上一层语义定位，因此这里不依赖任何固定地址。
static DWORD FindUpdateTrampoline(BYTE* image, DWORD imageSize,
                                  DWORD updateSlotVA,
                                  BYTE** outTrampoline,
                                  BYTE** outJmp,
                                  DWORD* outOldTarget,
                                  DWORD* outNewTarget) {
    if (outTrampoline) *outTrampoline=0;
    if (outJmp) *outJmp=0;
    if (outOldTarget) *outOldTarget=0;
    if (outNewTarget) *outNewTarget=0;
    if (!image || imageSize<27) return 0;

    DWORD count=0;
    BYTE* first=0;
    BYTE* firstJmp=0;
    DWORD oldTarget=0;
    DWORD newTarget=0;

    for (DWORD i=0;i<=imageSize-27;++i) {
        BYTE* q=image+i;
        if (q[0]!=0x50 || q[1]!=0x53 || q[2]!=0x51 || q[3]!=0x52) continue;
        if (q[4]!=0xFF || q[5]!=0x15 || ReadU32(q+6)!=updateSlotVA) continue;
        if (q[10]!=0x5A || q[11]!=0x59 || q[12]!=0x5B || q[13]!=0x58) continue;
        if (q[14]!=0x56 || q[15]!=0x89 || q[16]!=0xCE || q[17]!=0xE8) continue;
        if (q[22]!=0xE9) continue;

        BYTE* jmp=q+22;
        LONG oldRel=(LONG)ReadU32(jmp+1);
        DWORD target=Ptr32(jmp+5)+oldRel;
        DWORD corrected=target+3; // 8字节重放 - 5字节detour = +3

        if (!first) {
            first=q; firstJmp=jmp; oldTarget=target; newTarget=corrected;
        }
        ++count;
    }

    if (count==1) {
        if (outTrampoline) *outTrampoline=first;
        if (outJmp) *outJmp=firstJmp;
        if (outOldTarget) *outOldTarget=oldTarget;
        if (outNewTarget) *outNewTarget=newTarget;
    }
    return count;
}

// 把两字节或四字节的小补丁写进已加载映像。
// 写之前调用者已经做过语义定位；这里仍会再验证 expected，形成第二层保险。
static bool PatchVerifiedRuntimeBytes(BYTE* at,
                                      const BYTE* expected,
                                      const BYTE* replacement,
                                      UINT n,
                                      const char* label) {
    if (!at || !expected || !replacement || !n || !g_VirtualProtect) return false;

    char line[420]; UINT p=0;
    AppendText(line,p,sizeof(line),"SteamFix "); AppendText(line,p,sizeof(line),label);
    AppendText(line,p,sizeof(line)," address="); AppendHex32(line,p,sizeof(line),Ptr32(at));

    if (!BytesEqualSimple(at,expected,n)) {
        AppendText(line,p,sizeof(line)," status=SKIP byte-mismatch");
        line[p]=0; LogText(line,true);
        return false;
    }

    DWORD oldProtect=0;
    if (!g_VirtualProtect(at,n,PAGE_READWRITE,&oldProtect)) {
        AppendText(line,p,sizeof(line)," status=FAIL VirtualProtect");
        line[p]=0; LogText(line,true);
        return false;
    }

    for (UINT i=0;i<n;++i) at[i]=replacement[i];

    DWORD ignored=0;
    g_VirtualProtect(at,n,oldProtect,&ignored);

    AppendText(line,p,sizeof(line)," status=PATCHED");
    line[p]=0; LogText(line,true);
    return true;
}

// 完整定位 Steam 补丁。
// 任何核心特征不是唯一命中都会返回 false，绝不“取第一处凑合用”。
static bool LocateSteamPatchBySignature(SteamSignatureLayout* out) {
    if (!out) return false;

    BYTE* raw=(BYTE*)out;
    for (UINT i=0;i<(UINT)sizeof(SteamSignatureLayout);++i) raw[i]=0;

    BYTE* image=(BYTE*)FindLoadedModule("BaldrSky.exe");
    DWORD imageSize=GetImageSize32(image);
    if (!image || !imageSize) {
        LogText("[SteamFix][错误] 无法取得 BaldrSky.exe 映像范围。",true);
        return false;
    }
    out->imageBase=image;
    out->imageSize=imageSize;

    // 1) 关键字符串分别唯一定位。
    //    英文版会在这些字符串之间插入字体/设备导出，因此绝不再要求物理连续。
    struct NamedStringResult { const char* name; DWORD* outVA; } names[] = {
        {"BaldrUtil.dll",     &out->baldrUtilStringVA},
        {"InitSteam",        &out->initSteamStringVA},
        {"CloseSteam",       &out->closeSteamStringVA},
        {"UpdateSteam",      &out->updateSteamStringVA},
        {"ResetAchievements",&out->resetAchievementsStringVA},
        {"GetAchievement",   &out->getAchievementStringVA},
    };

    for (UINT i=0;i<(UINT)(sizeof(names)/sizeof(names[0]));++i) {
        DWORD count=FindAsciiStringExact(image,imageSize,names[i].name,names[i].outVA);
        char b[300]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] 字符串 "); AppendText(b,p,sizeof(b),names[i].name);
        AppendText(b,p,sizeof(b)," 命中数="); AppendDec(b,p,sizeof(b),count);
        if (*(names[i].outVA)) { AppendText(b,p,sizeof(b)," address="); AppendHex32(b,p,sizeof(b),*(names[i].outVA)); }
        b[p]=0; LogText(b,true);
        if (count!=1 || !*(names[i].outVA)) return false;
    }

    // 2) 先用唯一的 BaldrUtil.dll 引用确定真正的 LoadLibraryA IAT 槽。
    DWORD baldrIat=0;
    DWORD baldrCount=FindLoadLibraryStringSite(
        image,imageSize,"BaldrUtil.dll",0,&out->baldrLoadSite,&baldrIat);

    // 3) GDI32.dll 在英文 EXE 中可能还会出现在普通导入表，不能要求字符串全局唯一。
    //    这里要求“代码引用 GDI32.dll + 使用和 BaldrUtil 完全相同的 LoadLibraryA IAT槽”。
    DWORD gdiIat=0;
    DWORD gdiCount=0;
    if (baldrCount==1 && baldrIat) {
        gdiCount=FindLoadLibraryStringSite(
            image,imageSize,"GDI32.dll",baldrIat,&out->gdiLoadSite,&gdiIat);
    }

    {
        char b[360]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] LoadLibrary结构 BaldrUtil="); AppendDec(b,p,sizeof(b),baldrCount);
        AppendText(b,p,sizeof(b)," GDI32="); AppendDec(b,p,sizeof(b),gdiCount);
        if (baldrIat) { AppendText(b,p,sizeof(b)," IAT="); AppendHex32(b,p,sizeof(b),baldrIat); }
        b[p]=0; LogText(b,true);
    }

    if (baldrCount!=1 || gdiCount!=1 || baldrIat!=gdiIat || !baldrIat) return false;
    out->loadLibraryIatVA=baldrIat;
    out->testBaldr=out->baldrLoadSite+13;
    out->testGdi=out->gdiLoadSite+13;

    // Steam loader helper 在 GDI32 LoadLibrary 结构前 7 字节有：C6 05 <flag> 01。
    if (out->gdiLoadSite<image+7) return false;
    out->helperStart=out->gdiLoadSite-7;
    if (out->helperStart[0]!=0xC6 || out->helperStart[1]!=0x05 || out->helperStart[6]!=0x01) {
        LogText("[SteamFix][错误] loader helper 起始结构不匹配。",true);
        return false;
    }

    // 4) 根据 InitSteam / UpdateSteam 字符串引用推导两个函数指针槽。
    BYTE* initCall=0; BYTE* updateCall=0;
    BYTE* initGpHelper=0; BYTE* updateGpHelper=0;
    DWORD initSlotCount=FindSteamProcSlot(
        image,imageSize,out->initSteamStringVA,&out->initSteamSlotVA,&initCall,&initGpHelper);
    DWORD updateSlotCount=FindSteamProcSlot(
        image,imageSize,out->updateSteamStringVA,&out->updateSteamSlotVA,&updateCall,&updateGpHelper);

    {
        char b[420]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] Proc槽 InitSteam="); AppendDec(b,p,sizeof(b),initSlotCount);
        if (out->initSteamSlotVA) { AppendText(b,p,sizeof(b)," slot="); AppendHex32(b,p,sizeof(b),out->initSteamSlotVA); }
        AppendText(b,p,sizeof(b)," UpdateSteam="); AppendDec(b,p,sizeof(b),updateSlotCount);
        if (out->updateSteamSlotVA) { AppendText(b,p,sizeof(b)," slot="); AppendHex32(b,p,sizeof(b),out->updateSteamSlotVA); }
        b[p]=0; LogText(b,true);
    }

    if (initSlotCount!=1 || updateSlotCount!=1 ||
        !out->initSteamSlotVA || !out->updateSteamSlotVA ||
        !initGpHelper || initGpHelper!=updateGpHelper) return false;

    // GetProcAddress helper 再做一次函数头结构验证，防止撞到普通 push/push/call 序列。
    if (initGpHelper[0]!=0x55 || initGpHelper[1]!=0x89 || initGpHelper[2]!=0xE5 ||
        initGpHelper[3]!=0x8D || initGpHelper[4]!=0x4D || initGpHelper[5]!=0x08) {
        LogText("[SteamFix][错误] GetProcAddress helper 结构不匹配。",true);
        return false;
    }

    // 5) 定位 init 入口。
    DWORD entryCount=FindSteamInitEntry(
        image,imageSize,out->helperStart,out->initSteamSlotVA,&out->initEntry);
    {
        char b[280]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] Steam初始化入口命中数="); AppendDec(b,p,sizeof(b),entryCount);
        if (out->initEntry) { AppendText(b,p,sizeof(b)," address="); AppendHex32(b,p,sizeof(b),Ptr32(out->initEntry)); }
        b[p]=0; LogText(b,true);
    }
    if (entryCount!=1 || !out->initEntry) return false;

    // 6) 定位 UpdateSteam trampoline，并从旧目标自动计算 +3 的正确续接点。
    DWORD trampolineCount=FindUpdateTrampoline(
        image,imageSize,out->updateSteamSlotVA,
        &out->trampoline,&out->trampolineJmp,
        &out->trampolineOldTarget,&out->trampolineNewTarget);
    {
        char b[420]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] UpdateSteam trampoline命中数="); AppendDec(b,p,sizeof(b),trampolineCount);
        if (out->trampoline) {
            AppendText(b,p,sizeof(b)," address="); AppendHex32(b,p,sizeof(b),Ptr32(out->trampoline));
            AppendText(b,p,sizeof(b)," oldTarget="); AppendHex32(b,p,sizeof(b),out->trampolineOldTarget);
            AppendText(b,p,sizeof(b)," newTarget="); AppendHex32(b,p,sizeof(b),out->trampolineNewTarget);
        }
        b[p]=0; LogText(b,true);
    }
    if (trampolineCount!=1 || !out->trampoline || !out->trampolineJmp) return false;

    LogText("[SteamFix][成功] 版本无关语义签名全部唯一通过。",true);
    return true;
}

static DWORD ReadAbsoluteSlot(DWORD slotVA) {
    if (!slotVA || !CanReadMemory(slotVA,4)) return 0;
    return *(DWORD*)(unsigned long)slotVA;
}

static void RepairSteamPatchBySignatureOnce() {
    static bool done=false;
    if (done) return;
    done=true;

    SteamSignatureLayout s;
    if (!LocateSteamPatchBySignature(&s)) {
        LogText("[SteamFix][错误] 特征定位失败或不是唯一命中；为安全起见不修改任何代码。",true);
        return;
    }

    // 两处错误检查：test ecx,ecx -> test eax,eax
    const BYTE oldTest[2]={0x85,0xC9};
    const BYTE newTest[2]={0x85,0xC0};

    bool p1=PatchVerifiedRuntimeBytes(
        s.testGdi,oldTest,newTest,2,
        "LoadLibraryA(GDI32) test ECX->EAX");

    bool p2=PatchVerifiedRuntimeBytes(
        s.testBaldr,oldTest,newTest,2,
        "LoadLibraryA(BaldrUtil) test ECX->EAX");

    // trampoline 的 E9 opcode 保持不变，只改 4 字节 rel32。
    // expectedRel 是当前旧目标相对 jmp+5 的位移；
    // correctedRel 对应 oldTarget+3。
    BYTE oldRel[4];
    BYTE newRel[4];
    DWORD currentRel=ReadU32(s.trampolineJmp+1);
    DWORD correctedRel=s.trampolineNewTarget - Ptr32(s.trampolineJmp+5);
    WriteU32(oldRel,currentRel);
    WriteU32(newRel,correctedRel);

    bool p3=PatchVerifiedRuntimeBytes(
        s.trampolineJmp+1,oldRel,newRel,4,
        "UpdateSteam trampoline continuation +3 bytes");

    {
        char b[260]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix] 补丁汇总 p1=");
        AppendDec(b,p,sizeof(b),p1?1:0);
        AppendText(b,p,sizeof(b)," p2="); AppendDec(b,p,sizeof(b),p2?1:0);
        AppendText(b,p,sizeof(b)," p3="); AppendDec(b,p,sizeof(b),p3?1:0);
        b[p]=0; LogText(b,true);
    }

    DWORD updateBefore=ReadAbsoluteSlot(s.updateSteamSlotVA);
    {
        char b[260]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix][详细] 修复前/重初始化前 UpdateSteam槽=");
        AppendHex32(b,p,sizeof(b),s.updateSteamSlotVA);
        AppendText(b,p,sizeof(b)," value="); AppendHex32(b,p,sizeof(b),updateBefore);
        b[p]=0; LogText(b,true);
    }

    // 记录重初始化后的 LastError，主要用于判断 BaldrUtil.dll 是否因为缺少
    // x86 VC++ Runtime、steam_api.dll 等依赖而无法被 Windows Loader 加载。
    // 注意：LastError 只有在最终仍失败时才作为辅助诊断，不把它当成成功路径判断依据。
    DWORD initLastError=0;

    if (!updateBefore) {
        PFN_SteamPatchInit init=(PFN_SteamPatchInit)s.initEntry;
        LogHex("[SteamFix] 正在调用特征定位到的 Steam 初始化入口=",Ptr32((void*)init),true);
        init();
        if (g_GetLastError) initLastError=g_GetLastError();
        LogText("[SteamFix] Steam 初始化入口已返回。",true);
    } else {
        LogText("[SteamFix] UpdateSteam 已初始化，不重复执行 Steam init。",true);
    }

    DWORD updateAfter=ReadAbsoluteSlot(s.updateSteamSlotVA);
    {
        char b[300]; UINT p=0;
        AppendText(b,p,sizeof(b),"[SteamFix] 修复后 UpdateSteam=");
        AppendHex32(b,p,sizeof(b),updateAfter);
        AppendText(b,p,sizeof(b)," BaldrUtilModule=");
        AppendHex32(b,p,sizeof(b),Ptr32(FindLoadedModule("BaldrUtil.dll")));
        b[p]=0; LogText(b,true);
    }

    if (p1 && p2 && p3 && updateAfter) {
        LogText("[SteamFix][成功] 特征码定位、三处修复与 UpdateSteam 初始化全部成功。",true);
    } else {
        LogText("[SteamFix][警告] 特征码已定位，但修复/初始化没有全部成功。",true);

        // 如果函数槽仍然是 0，最常见的下一层原因就是 BaldrUtil.dll 没有真正加载成功。
        // BaldrUtil.dll 本身依赖 steam_api.dll、MSVCP140.dll、VCRUNTIME140.dll 和 UCRT。
        // 这些是外部运行库问题，不能靠篡改游戏机器码安全解决，所以只给出明确诊断。
        if (!updateAfter) {
            char b[420]; UINT p=0;
            AppendText(b,p,sizeof(b),"[SteamFix][诊断] UpdateSteam 仍为 NULL；Steam init 返回后的 LastError=");
            AppendHex32(b,p,sizeof(b),initLastError);
            AppendText(b,p,sizeof(b),"。请优先检查 BaldrUtil.dll / steam_api.dll 与 x86 VC++ 2015-2022 Runtime。 ");
            b[p]=0; LogError(b,true);
        }
    }
}

// ============================================================================
// Video Overlay Fix：修复老式视频“有声音但黑屏”
// ============================================================================
//
// Windows 会在：
//   HKCU\Software\Microsoft\Direct3D\Shims\EnableOverlays
// 下，用“程序完整路径”作为值名称保存 REG_DWORD。
// BALDR SKY 社区长期实机结论是：当前 BaldrSky.exe 对应值为 1 时，
// 老式视频播放路径可能黑屏；改成 0 后恢复。
//
// 这里的设计原则：
// 1. 只修改当前 BaldrSky.exe 这一条值，不碰其他程序。
// 2. 只写 HKCU，不需要管理员权限。
// 3. 启动时立即校正为 0。
// 4. 另起一个极轻量守护线程定期检查；只有值被外部改回非 0 时才再次写入。
//    这样能处理“Windows/显卡兼容层运行中又把该值写回 1”的老问题。

static PFN_RegCreateKeyExA g_RegCreateKeyExA=0;
static PFN_RegSetValueExA g_RegSetValueExA=0;
static PFN_RegQueryValueExA g_RegQueryValueExA=0;
static PFN_RegCloseKey g_RegCloseKey=0;
static bool g_overlayGuardStarted=false;

static bool InitRegistryApis() {
    if (g_RegCreateKeyExA && g_RegSetValueExA && g_RegQueryValueExA && g_RegCloseKey) return true;
    if (!InitWinApis() || !g_LoadLibraryA || !g_GetProcAddress) return false;

    HMODULE adv=(HMODULE)FindLoadedModule("advapi32.dll");
    if (!adv) adv=g_LoadLibraryA("advapi32.dll");
    if (!adv) return false;

    g_RegCreateKeyExA=(PFN_RegCreateKeyExA)g_GetProcAddress(adv,"RegCreateKeyExA");
    g_RegSetValueExA=(PFN_RegSetValueExA)g_GetProcAddress(adv,"RegSetValueExA");
    g_RegQueryValueExA=(PFN_RegQueryValueExA)g_GetProcAddress(adv,"RegQueryValueExA");
    g_RegCloseKey=(PFN_RegCloseKey)g_GetProcAddress(adv,"RegCloseKey");
    return g_RegCreateKeyExA && g_RegSetValueExA && g_RegQueryValueExA && g_RegCloseKey;
}

static bool ApplyVideoOverlayValue(bool fromGuard) {
    LoadConfigOnce();
    if (!g_cfg.enableVideoOverlayFix) return true;
    BuildModulePathsOnce();
    if (!g_mainExePath[0]) return false;
    if (!InitRegistryApis()) {
        if (!fromGuard) LogError("[视频修复][错误] 无法解析注册表 API，EnableOverlays 未处理。",true);
        return false;
    }

    const char* keyPath="Software\\Microsoft\\Direct3D\\Shims\\EnableOverlays";
    HKEY key=0;
    DWORD disposition=0;
    LONG rc=g_RegCreateKeyExA(
        HKEY_CURRENT_USER_VALUE,keyPath,0,0,0,
        KEY_QUERY_VALUE_VALUE|KEY_SET_VALUE_VALUE,
        0,&key,&disposition);
    if (rc!=ERROR_SUCCESS_VALUE || !key) {
        if (!fromGuard) LogError("[视频修复][错误] 无法打开 HKCU\\Software\\Microsoft\\Direct3D\\Shims\\EnableOverlays。",true);
        return false;
    }

    DWORD oldValue=0xFFFFFFFFUL;
    DWORD type=0;
    DWORD size=4;
    LONG q=g_RegQueryValueExA(key,g_mainExePath,0,&type,(BYTE*)&oldValue,&size);
    bool alreadyZero=(q==ERROR_SUCCESS_VALUE && type==REG_DWORD_VALUE && size==4 && oldValue==0);

    if (!alreadyZero) {
        DWORD zero=0;
        LONG w=g_RegSetValueExA(key,g_mainExePath,0,REG_DWORD_VALUE,(const BYTE*)&zero,4);
        if (w!=ERROR_SUCCESS_VALUE) {
            g_RegCloseKey(key);
            if (!fromGuard) LogError("[视频修复][错误] 写入 EnableOverlays=0 失败。",true);
            return false;
        }

        char b[760]; UINT p=0;
        AppendText(b,p,sizeof(b),fromGuard?"[视频修复][守护] 检测到 EnableOverlays 被改回非0，已重新设为0。程序=":"[视频修复] 已把当前程序 EnableOverlays 设为0。程序=");
        AppendText(b,p,sizeof(b),g_mainExePath);
        if (q==ERROR_SUCCESS_VALUE) {
            AppendText(b,p,sizeof(b)," 原值="); AppendHex32(b,p,sizeof(b),oldValue);
        } else {
            AppendText(b,p,sizeof(b)," 原值=不存在");
        }
        b[p]=0; LogText(b,true);
    } else if (!fromGuard) {
        char b[700]; UINT p=0;
        AppendText(b,p,sizeof(b),"[视频修复] EnableOverlays 已经是0，无需改写。程序=");
        AppendText(b,p,sizeof(b),g_mainExePath);
        b[p]=0; LogDetail(b,true);
    }

    g_RegCloseKey(key);
    return true;
}

static DWORD __stdcall OverlayGuardThread(void*) {
    // 线程无需高频运行。默认每 500 毫秒检查一次，足够及时发现“播放视频后被写回 1”的情况，
    // 同时几乎没有 CPU/磁盘/注册表负担。
    for (;;) {
        if (g_Sleep) g_Sleep((DWORD)g_cfg.overlayGuardIntervalMs);
        else return 0;
        if (!g_cfg.enableVideoOverlayFix) return 0;
        ApplyVideoOverlayValue(true);
    }
}

static void StartOverlayGuardOnce() {
    if (g_overlayGuardStarted || !g_cfg.enableVideoOverlayFix) return;
    g_overlayGuardStarted=true;

    ApplyVideoOverlayValue(false);
    if (!g_CreateThread) {
        LogError("[视频修复][警告] CreateThread 不可用，只完成一次性 EnableOverlays=0，未启动守护。",true);
        return;
    }

    DWORD tid=0;
    HANDLE th=g_CreateThread(0,0,&OverlayGuardThread,0,0,&tid);
    if (th) {
        char b[260]; UINT p=0;
        AppendText(b,p,sizeof(b),"[视频修复] Overlay 守护线程已启动，TID=");
        AppendDec(b,p,sizeof(b),tid);
        AppendText(b,p,sizeof(b),"，检查间隔(ms)=");
        AppendDec(b,p,sizeof(b),(DWORD)g_cfg.overlayGuardIntervalMs);
        b[p]=0; LogDetail(b,true);
        if (g_CloseHandle) g_CloseHandle(th);
    } else {
        LogError("[视频修复][警告] Overlay 守护线程创建失败；一次性修复已经生效。",true);
    }
}

// ============================================================================
// 统一兼容层初始化：在第一次 Direct3DCreate9 前执行一次
// ============================================================================
static bool g_coreInitialized=false;
static bool g_startupLogged=false;

static void LogStartupSummaryOnce() {
    if (g_startupLogged) return;
    g_startupLogged=true;

    LogText("============================================================",true);
    LogText("BALDR SKY Steam Win11 兼容层 v0.1-t9",true);
    LogText("============================================================",true);
    LogText("[说明] By Luminous 20260927编译&发布",true);
    LogText("[说明] 如果你是整合包里看到的，很遗憾我没发过，出了问题找做整合包的",true);
    LogText("[说明] 如果你是学习版里看到的，很遗憾这个插件不支持任何学习版",true);
    LogText("[说明] 本人也不会为插件做学习版的支持，学习版请自行解决。",true);
    LogText("[说明] 若遇到问题，可来 https://github.com/Kanadeforever/GameFixMods/issues 反馈，能帮你解决尽量帮",true);
    LogText("[说明] 帮不了就自求多福吧，说明就到这里，have fun.",true);

    char b[900]; UINT p=0;
    AppendText(b,p,sizeof(b),"[启动] 加载模式=");
    AppendText(b,p,sizeof(b),"D3D9 代理 DLL");
    AppendText(b,p,sizeof(b),"；模块="); AppendText(b,p,sizeof(b),g_selfPath);
    b[p]=0; LogText(b,true);

    p=0; AppendText(b,p,sizeof(b),"[配置] INI="); AppendText(b,p,sizeof(b),g_iniPath);
    AppendText(b,p,sizeof(b),"；日志="); AppendText(b,p,sizeof(b),g_logPath);
    b[p]=0; LogText(b,true);

    p=0; AppendText(b,p,sizeof(b),"[配置] SteamFix="); AppendDec(b,p,sizeof(b),g_cfg.enableSteamFix);
    AppendText(b,p,sizeof(b)," VideoOverlayFix="); AppendDec(b,p,sizeof(b),g_cfg.enableVideoOverlayFix);
    AppendText(b,p,sizeof(b)," HighDpiFix="); AppendDec(b,p,sizeof(b),g_cfg.enableHighDpiFix);
    AppendText(b,p,sizeof(b)," Backend="); AppendDec(b,p,sizeof(b),g_cfg.backendMode);
    AppendText(b,p,sizeof(b)," CrashDiagnostics="); AppendDec(b,p,sizeof(b),g_cfg.enableCrashDiagnostics);
    AppendText(b,p,sizeof(b)," LogLevel="); AppendDec(b,p,sizeof(b),g_cfg.logLevel);
    b[p]=0; LogText(b,true);

    p=0; AppendText(b,p,sizeof(b),"[环境] 主程序="); AppendText(b,p,sizeof(b),g_mainExePath);
    b[p]=0; LogDetail(b,true);

}

static void InitializeCompatibilityCoreOnce() {
    if (g_coreInitialized) return;
    g_coreInitialized=true;

    InitWinApis();
    LoadConfigOnce();
    EnsureLog();
    LogStartupSummaryOnce();

    // 安全边界：同目录的 StartUpTool.exe 等辅助程序如果也加载本 d3d9.dll，
    // 只得到透明 D3D9 转发，不允许执行任何 BALDR SKY 专用的内存/注册表修复。
    if (!IsBaldrSkyMainProcess()) {
        LogText("[安全] 当前主程序不是 BaldrSky.exe；不应用 SteamFix、VideoOverlayFix、HighDpiFix 或 VEH/VCH，只保留 D3D9 后端转发。",true);
        return;
    }

    // 高 DPI 必须尽量早于真正的图形后端和后续窗口/设备工作。
    // 因此主程序身份验证通过后，第一个执行的兼容动作就是 DPI 设置。
    ApplyHighDpiFixOnce();

    if (g_cfg.enableSteamFix) {
        RepairSteamPatchBySignatureOnce();
    } else {
        LogText("[SteamFix] 已由 INI 关闭，不扫描/修改 Steam 补丁。",true);
    }

    if (g_cfg.enableVideoOverlayFix) StartOverlayGuardOnce();
    else LogText("[视频修复] 已由 INI 关闭。",true);

    if (g_cfg.enableCrashDiagnostics) InstallVehOnce();
    else LogText("[诊断] VEH/VCH 崩溃诊断已由 INI 关闭。",true);
}

// ============================================================================
// test9：D3D9 后端选择
// ============================================================================
//
// 同一个 d3d9.dll 同时支持两种模式：
//
// A. 游戏目录存在可加载的 d3d9_backend.dll
//    -> 把它当后端。
//       例如用户可把 DXVK x32 的 d3d9.dll 改名为 d3d9_backend.dll。
//
// B. 没有 d3d9_backend.dll，或者它加载失败
//    -> 使用 Windows 系统原生 d3d9.dll。
//
// 这里完全不改 IDirect3D9 / IDirect3DDevice9 的 vtable。
// Direct3DCreate9 返回什么对象，就原样交还给游戏。
// ============================================================================

static bool BuildSystemD3D9Path(char* outPath, UINT cap) {
    if (!outPath || cap < 16 || !g_GetSystemDirectoryA) return false;

    UINT n=g_GetSystemDirectoryA(outPath,cap);
    if (!n || n>=cap) return false;

    if (outPath[n-1]!='\\') {
        if (n+1>=cap) return false;
        outPath[n++]='\\';
    }

    const char* name="d3d9.dll";
    for (UINT i=0; name[i]; ++i) {
        if (n+1>=cap) return false;
        outPath[n++]=name[i];
    }
    outPath[n]=0;
    return true;
}

static HMODULE g_backend=0;
static PFN_Direct3DCreate9 g_realDirect3DCreate9=0;

static HMODULE LoadSelectedD3D9Backend() {
    if (g_backend) return g_backend;
    LoadConfigOnce();

    // Backend=0：自动。优先 d3d9_backend.dll，没有则系统原生。
    // Backend=1：强制系统原生，即使游戏目录存在 d3d9_backend.dll 也忽略。
    // Backend=2：强制自定义；如果 d3d9_backend.dll 不存在则明确失败，不静默回退。
    if (g_cfg.backendMode!=1) {
        // 只尝试加载“当前代理 DLL 同目录”的 d3d9_backend.dll。
        // 这样 Backend=Auto/Custom 都不会因为 DLL 搜索路径被其他程序改变而误装载别处同名文件。
        char customPath[520];
        customPath[0]=0;
        bool haveCustomPath=BuildSiblingFilePath("d3d9_backend.dll",customPath,(UINT)sizeof(customPath));
        HMODULE custom=0;
        if (haveCustomPath) custom=g_LoadLibraryA(customPath);

        if (custom) {
            g_backend=custom;
            char b[700]; UINT p=0;
            AppendText(b,p,sizeof(b),"[D3D9] 后端=CUSTOM，已加载=");
            AppendText(b,p,sizeof(b),customPath);
            b[p]=0; LogText(b,true);
            LogHex("[D3D9][详细] 自定义后端句柄=",Ptr32(g_backend),true);
            return g_backend;
        }

        if (g_cfg.backendMode==2) {
            char b[760]; UINT p=0;
            AppendText(b,p,sizeof(b),"[D3D9][错误] Backend=2，但同目录 d3d9_backend.dll 无法加载；按配置不回退系统 D3D9。路径=");
            AppendText(b,p,sizeof(b),haveCustomPath?customPath:"<无法构造路径>");
            b[p]=0; LogError(b,true);
            return 0;
        }

        LogDetail("[D3D9] 同目录没有可加载的 d3d9_backend.dll，自动回退 Windows 原生 D3D9。",true);
    } else {
        LogText("[D3D9] Backend=1，强制使用 Windows 原生 D3D9。",true);
    }

    char systemPath[320];
    if (!BuildSystemD3D9Path(systemPath,(UINT)sizeof(systemPath))) {
        LogError("[D3D9][错误] 无法构造 Windows 系统 d3d9.dll 路径。",true);
        return 0;
    }

    {
        char b[420]; UINT p=0;
        AppendText(b,p,sizeof(b),"[D3D9] 系统后端路径=");
        AppendText(b,p,sizeof(b),systemPath);
        b[p]=0; LogText(b,true);
    }

    g_backend=g_LoadLibraryA(systemPath);
    LogHex("[D3D9][详细] 原生后端句柄=",Ptr32(g_backend),true);
    return g_backend;
}

// d3d9.dll 对游戏唯一需要提供的导出。
// 当前 Steam BaldrSky.exe 静态导入只有 Direct3DCreate9；
// test9 不冒充其他它根本不会调用的接口。
extern "C" void* __stdcall Direct3DCreate9(UINT SDKVersion) {
    // 代理 DLL 模式下，第一次 Direct3DCreate9 是最稳定的“正常初始化点”：
    // 已经离开 DllMain loader lock，但游戏还没有真正开始使用 D3D9 Device。
    InitializeCompatibilityCoreOnce();

    LogDec("[D3D9] Direct3DCreate9 SDKVersion=",SDKVersion,true);

    if (!InitWinApis()) {
        LogError("[D3D9][错误] 无法解析所需 Windows API。",true);
        return 0;
    }

    if (!LoadSelectedD3D9Backend()) {
        LogError("[D3D9][错误] 没有可用的 D3D9 后端。",true);
        return 0;
    }

    if (!g_realDirect3DCreate9) {
        g_realDirect3DCreate9=
            (PFN_Direct3DCreate9)g_GetProcAddress(g_backend,"Direct3DCreate9");
        LogHex("[D3D9][详细] 后端 Direct3DCreate9=",Ptr32((void*)g_realDirect3DCreate9),true);
    }

    if (!g_realDirect3DCreate9) {
        LogError("[D3D9][错误] 选中的 D3D9 后端没有 Direct3DCreate9。",true);
        return 0;
    }

    void* real=g_realDirect3DCreate9(SDKVersion);
    LogHex("[D3D9][详细] 后端 IDirect3D9=",Ptr32(real),true);

    // 仍然坚持 test9 的原则：真实 IDirect3D9 对象原样返回。
    // 不再碰 COM vtable，避免重演 test6 对原生 D3D9 的诊断污染。
    return real;
}

// DllMain 只保存当前 DLL 的模块句柄。
// 所有可能涉及文件、注册表、LoadLibrary 或复杂初始化的工作，都延后到
// Direct3DCreate9 第一次被游戏调用时执行，避免在 Windows Loader Lock 内做危险操作。
extern "C" BOOL __stdcall DllMain(void* module,DWORD reason,void*) {
    if (reason==1) { // DLL_PROCESS_ATTACH
        // DllMain 运行在 Windows Loader Lock 内。
        // 在这里调用 LoadLibrary、读 INI、写注册表或创建复杂对象都可能造成死锁。
        // test9 的正式交付形态只有 d3d9 代理 DLL，因此这里只保存自己的模块句柄。
        // 真正初始化统一延后到 Direct3DCreate9：那时已经离开 Loader Lock，
        // 同时又仍早于系统/自定义 D3D9 后端真正创建对象，适合先完成 SteamFix 与视频修复。
        g_selfModule=(HMODULE)module;
    }
    return 1;
}
