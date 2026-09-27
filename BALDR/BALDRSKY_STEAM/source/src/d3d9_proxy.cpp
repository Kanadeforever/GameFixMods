// BALDR SKY Steam Win11 Fix / D3D9 Proxy v0.1-test14
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
// 4. 高 DPI 缩放兼容。
//    默认把 BALDR SKY 设置为 System DPI Aware，等价于告诉 Windows：
//    “这个程序自己处理系统 DPI，不要再对它做传统 DPI 虚拟化放大”。
//    这里故意不用 Per-Monitor V2，因为 BALDR SKY 是 2009 年固定像素 UI，
//    原程序没有针对 WM_DPICHANGED / 跨显示器 DPI 切换进行过设计。
//    System DPI Aware 对老 D3D9 游戏更保守；用户仍可在 INI 里完全关闭。
//
// 5. Steam InstallScript 静默净化。
//    Steam 发行包的 install.vdf 会在 Steam 启动游戏之前向：
//      HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
//    写入当前 BaldrSky.exe = "~ DPIUNAWARE VISTARTM"。
//    这两个 AppCompat token 会在进程创建阶段强制 DPI Unaware + Vista 兼容层，
//    因而会压过 test9 的高 DPI 设置。
//
//    正常修复流程不弹窗、不要求用户手动改 Steam 文件，也不在第一次运行时强制重启游戏：
//      - 静默备份 install.vdf（仅第一次）；
//      - 只删除 AppCompatFlags\Layers 这一条 Registry 子项；
//      - 同时从当前用户注册表值中只剔除 DPIUNAWARE / VISTARTM 两个 token；
//      - 其他用户自己设置的兼容 token 保留；
//      - Steam 验证/更新如果恢复 install.vdf，下次运行会再次静默净化。
//      - 唯一例外：如果 install.vdf 整个文件缺失，会按游戏中/英文版本弹一次 MessageBox，
//        建议用户在 Steam 验证游戏文件完整性；点确定后仍继续启动游戏，不退出。
//    当前第一次进程如果已经被 Windows 套上 Shim，可能仍维持旧 DPI 行为；
//    用户已经明确接受这一点，因此兼容层不会打断游戏或额外启动第二实例。
//
// 6. 重新整理日志/诊断策略。
//    正式发布默认 EnableLog=0，因此普通玩家不会生成日志，也不会为了“纯诊断”额外安装 VEH/VCH。
//    但 INI 里 LogLevel 默认直接设为 3、EnableCrashDiagnostics 默认设为 1：
//    用户遇到问题时只需要把 EnableLog 改成 1，就会自动得到最详细日志和完整崩溃现场，
//    不需要再理解第二个、第三个诊断开关。PixelBoundaryFix 的安全回退与日志完全解耦：
//    即使日志关闭，只要内联补丁意外安装失败，精确异常恢复仍会自动安装并保护游戏。
//
// 7. 字体像素左边界前溢修复，并通过实机证明 TargetBase == EDI + 4。
// 8. 将该修复从“异常发生后的精确恢复”收敛为“异常发生前的内联边界检查”。
//    中文版在存档界面移动鼠标/切换存档槽时可以稳定触发真实崩溃，故障指令固定为
//    字体混色循环中的 `mov ecx,[edi]`，而故障地址每次变化但都落在页尾 FFFC。
//    静态反汇编确认 EDI 来自目标 32bpp 像素缓冲区基址加坐标偏移。
//    test11 不直接硬编码地址，也不粗暴吞掉 Access Violation；它运行时用机器码结构
//    唯一定位故障读指令与原函数自己的 `add edi,4` 续接点，随后只有在异常现场严格
//    满足 `EDI == TargetBase - 4` 时，才把当前越界像素视为被左边界裁剪并跳过。
//    任何其他越界/坏指针仍继续交给游戏和 Windows 处理，避免把未知 bug 掩盖掉。
//
//
// 9. 将 SteamFix、VideoOverlayFix、PixelBoundaryFix 正式提升为“核心常开功能”。
//    它们已经分别被证明是 Steam 版启动所必需、老式视频黑屏的高频兼容修复、
//    以及会在正常 UI/存档界面高频触发的真实字体像素边界崩溃修复。
//    因此不再给普通用户提供关闭开关，也不再从 INI 读取这三个选项，避免误关核心功能。
//    INI 只保留真正需要用户选择的项目：InstallScript/DPI、D3D9 后端、日志和诊断。
//
// 10. 新增 ReShade 链式加载支持，而且它与 Backend 选择完全独立。
//     游戏目录中的主入口仍然固定由本兼容层 d3d9.dll 占用。用户如果希望使用 ReShade，
//     只需把 ReShade 的 32 位 D3D9 DLL 改名为 ReShade32.dll 放在同目录，并设置：
//       [Graphics]
//       EnableReShade=1
//     兼容层会先根据 Backend 选出“最终 D3D9 后端”，然后在加载 ReShade32.dll 之前，
//     只更新同目录 ReShade.ini 的 [PROXY] / EnableProxyLibrary 与 ProxyLibrary 两个键，
//     让调用链变成：游戏 -> 本兼容层 -> ReShade32.dll -> Native/DXVK。
//     这样 ReShade 不再和本兼容层争抢 d3d9.dll 文件名，也不需要子目录。
//     如果 ReShade32.dll 缺失或加载失败，兼容层会安全回退到原来的直接后端路径。
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
// test12 起沿用至 test14 的内联像素边界补丁需要一个很小的可执行跳板。
// 这里仍然不静态导入 kernel32：VirtualAlloc / FlushInstructionCache 继续按运行时导出解析。
typedef void*   (__stdcall *PFN_VirtualAlloc)(void*, DWORD, DWORD, DWORD);
typedef BOOL    (__stdcall *PFN_FlushInstructionCache)(HANDLE, const void*, DWORD);
typedef void    (__stdcall *PFN_ExitProcess)(UINT);
typedef BOOL    (__stdcall *PFN_TerminateProcess)(HANDLE, UINT);
typedef void*   (__stdcall *PFN_SetUnhandledExceptionFilter)(void*);
typedef HANDLE  (__stdcall *PFN_CreateFileA)(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef BOOL    (__stdcall *PFN_WriteFile)(HANDLE, const void*, DWORD, DWORD*, void*);
typedef BOOL    (__stdcall *PFN_FlushFileBuffers)(HANDLE);
typedef DWORD   (__stdcall *PFN_GetModuleFileNameA)(HMODULE, char*, DWORD);
typedef UINT    (__stdcall *PFN_GetPrivateProfileIntA)(const char*, const char*, int, const char*);
// test14 的 ReShade 链式加载只需要改 ReShade.ini 中两个 [PROXY] 键。
// WritePrivateProfileStringA 会保留同一 INI 中其他 section/key，不需要我们自己重写整个文件。
typedef BOOL    (__stdcall *PFN_WritePrivateProfileStringA)(const char*, const char*, const char*, const char*);
typedef DWORD   (__stdcall *PFN_GetLastError)();
typedef HANDLE  (__stdcall *PFN_CreateThread)(void*, DWORD, DWORD (__stdcall *)(void*), void*, DWORD, DWORD*);
typedef void    (__stdcall *PFN_Sleep)(DWORD);
typedef BOOL    (__stdcall *PFN_CloseHandle)(HANDLE);

// test10 的 InstallScript 静默修复需要读写一个很小的文本文件。
// 这些 API 仍然全部在运行时解析，因此最终 DLL 依旧不需要普通 PE Import Directory。
typedef BOOL    (__stdcall *PFN_ReadFile)(HANDLE, void*, DWORD, DWORD*, void*);
typedef DWORD   (__stdcall *PFN_GetFileSize)(HANDLE, DWORD*);
typedef BOOL    (__stdcall *PFN_CopyFileA)(const char*, const char*, BOOL);
typedef BOOL    (__stdcall *PFN_MoveFileExA)(const char*, const char*, DWORD);
typedef BOOL    (__stdcall *PFN_DeleteFileA)(const char*);
typedef DWORD   (__stdcall *PFN_GetFileAttributesA)(const char*);

// 只有 install.vdf 整个文件缺失时才需要弹出一次提示。
// 使用 MessageBoxW 而不是 MessageBoxA：这样中文提示直接使用 UTF-16，
// 不受用户系统 ACP（简中/繁中/英文 Windows）影响，不会因为代码页不同出现乱码。
typedef int     (__stdcall *PFN_MessageBoxW)(HWND,const wchar_t*,const wchar_t*,UINT);

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
typedef LONG (__stdcall *PFN_RegDeleteValueA)(HKEY,const char*);

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
static const DWORD GENERIC_READ          = 0x80000000UL;
static const DWORD GENERIC_WRITE         = 0x40000000UL;
static const DWORD FILE_SHARE_READ       = 0x00000001UL;
static const DWORD FILE_SHARE_WRITE      = 0x00000002UL;
static const DWORD CREATE_ALWAYS         = 2UL;
static const DWORD OPEN_EXISTING         = 3UL;
static const DWORD MOVEFILE_REPLACE_EXISTING_VALUE = 0x00000001UL;
static const DWORD MOVEFILE_WRITE_THROUGH_VALUE    = 0x00000008UL;
static const DWORD FILE_ATTRIBUTE_NORMAL = 0x00000080UL;
static HANDLE const INVALID_HANDLE_VALUE = (HANDLE)(long)-1;
static const DWORD INVALID_FILE_ATTRIBUTES_VALUE = 0xFFFFFFFFUL;
static const DWORD ERROR_FILE_NOT_FOUND_VALUE = 2UL;
static const DWORD ERROR_PATH_NOT_FOUND_VALUE = 3UL;

// MessageBoxW 标志。没有父窗口是刻意的：兼容层第一次运行时游戏主窗口可能还没有创建。
// MB_SETFOREGROUND 只是让“文件确实缺失”的一次性提示不至于躲到 Steam 窗口后面；
// 不使用 MB_TOPMOST，避免长期压住用户其他程序。
static const UINT MB_OK_VALUE            = 0x00000000U;
static const UINT MB_ICONWARNING_VALUE   = 0x00000030U;
static const UINT MB_SETFOREGROUND_VALUE = 0x00010000U;

// VirtualQuery / VirtualProtect 所需常量。
// 先查询页面再读异常栈，可以避免“为了记录一次异常，记录器自己又读到无效地址”。
static const DWORD MEM_COMMIT       = 0x00001000UL;
static const DWORD MEM_RESERVE      = 0x00002000UL;
static const DWORD PAGE_NOACCESS    = 0x00000001UL;
static const DWORD PAGE_READWRITE   = 0x00000004UL;
static const DWORD PAGE_EXECUTE_READ= 0x00000020UL;
static const DWORD PAGE_GUARD       = 0x00000100UL;

// 注册表常量。HKEY_CURRENT_USER 是 Win32 约定的伪句柄，不需要真的打开“根键”。
static HKEY const HKEY_CURRENT_USER_VALUE = (HKEY)(ULONG_PTR)0x80000001UL;
static const DWORD KEY_QUERY_VALUE_VALUE  = 0x00000001UL;
static const DWORD KEY_SET_VALUE_VALUE    = 0x00000002UL;
static const DWORD REG_SZ_VALUE           = 1UL;
static const DWORD REG_EXPAND_SZ_VALUE    = 2UL;
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
// Windows VEH 返回 -1 表示“我们已经修正 CONTEXT，请从新的 EIP 继续执行”。
// test11 只会在严格确认“目标像素地址恰好等于目标缓冲区基址-4”时使用它。
static const LONG EXCEPTION_CONTINUE_EXECUTION_VALUE = -1;

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
static PFN_VirtualAlloc       g_VirtualAlloc = 0;
static PFN_FlushInstructionCache g_FlushInstructionCache = 0;
static PFN_CreateFileA      g_CreateFileA = 0;
static PFN_WriteFile        g_WriteFile = 0;
static PFN_FlushFileBuffers g_FlushFileBuffers = 0;
static PFN_GetModuleFileNameA g_GetModuleFileNameA = 0;
static PFN_GetPrivateProfileIntA g_GetPrivateProfileIntA = 0;
static PFN_WritePrivateProfileStringA g_WritePrivateProfileStringA = 0;
static PFN_GetLastError     g_GetLastError = 0;
static PFN_CreateThread     g_CreateThread = 0;
static PFN_Sleep            g_Sleep = 0;
static PFN_CloseHandle      g_CloseHandle = 0;
static PFN_ReadFile         g_ReadFile = 0;
static PFN_GetFileSize      g_GetFileSize = 0;
static PFN_CopyFileA        g_CopyFileA = 0;
static PFN_MoveFileExA      g_MoveFileExA = 0;
static PFN_DeleteFileA      g_DeleteFileA = 0;
static PFN_GetFileAttributesA g_GetFileAttributesA = 0;
static PFN_AddVectoredExceptionHandler g_AddVectoredExceptionHandler = 0;
static PFN_AddVectoredContinueHandler  g_AddVectoredContinueHandler = 0;
static PFN_RegDeleteValueA  g_RegDeleteValueA = 0;
static HANDLE               g_log = INVALID_HANDLE_VALUE;

// DllMain 会把“当前这个模块自己的 HMODULE”保存下来。
// 这样同一份二进制无论叫 d3d9.dll 还是 BaldrSkyWin11Fix.asi，
// 都可以根据自己的实际文件名自动找到同名 INI/LOG。
static HMODULE g_selfModule = 0;
static char g_selfPath[520] = {0};
static char g_iniPath[520] = {0};
static char g_logPath[520] = {0};
static char g_mainExePath[520] = {0};

// 用户配置。
//
// 注意：SteamFix、VideoOverlayFix、PixelBoundaryFix 已经在 test13 起被提升为“核心常开功能”，
// 所以这里故意没有这三个字段。这样既能减少配置分支，也能从代码结构上保证普通用户无法
// 因为误改 INI 而关闭会直接影响启动/视频/稳定性的基础修复。
struct UserConfig {
    int enableSteamInstallScriptFix; // 1=静默移除 install.vdf 中强制 DPI/Vista AppCompat；0=完全不碰 install.vdf/Layers
    int enableHighDpiFix;            // 1=System DPI Aware；0=完全保持 Windows/外部兼容设置
    int backendMode;                 // 0=自动，1=系统原生，2=强制 d3d9_backend.dll
    int enableReShade;               // 1=在本兼容层与最终 D3D9 后端之间插入同目录 ReShade32.dll；0=直接后端
    int enableLog;                   // 1=真正创建并写入日志；0=完全不生成日志文件
    int logLevel;                    // 0=仅错误，1=基本，2=详细，3=调试
    int enableCrashDiagnostics;      // 只有 EnableLog=1 时才安装“纯诊断” VEH/VCH；像素边界回退不受它影响
};

// test14 的发布默认值：
// - SteamInstallScriptFix=1：静默移除 Steam 强制写入的 DPIUNAWARE/VISTARTM；
// - HighDpiFix=1：System DPI Aware；
// - Backend=0：优先同目录 d3d9_backend.dll，没有则系统原生 D3D9；
// - EnableReShade=0：默认不插入 ReShade；用户需要时显式开启；
// - EnableLog=0：普通玩家默认不生成 d3d9.log；
// - LogLevel=3：一旦用户只把 EnableLog 改成 1，立刻获得最详细日志；
// - EnableCrashDiagnostics=1：同理，开日志后默认自动记录完整崩溃现场。
//
// VideoOverlayFix 的 500ms 守护间隔属于内部实现细节，不再暴露成普通用户配置。
// 这样 INI 只保留真正需要用户做选择的开关。
static UserConfig g_cfg = {1,1,0,0,0,3,1};
static const DWORD OVERLAY_GUARD_INTERVAL_MS = 500;
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
    g_VirtualAlloc       = (PFN_VirtualAlloc)ResolveExport(k32, "VirtualAlloc");
    g_FlushInstructionCache = (PFN_FlushInstructionCache)ResolveExport(k32, "FlushInstructionCache");
    g_CreateFileA        = (PFN_CreateFileA)ResolveExport(k32, "CreateFileA");
    g_WriteFile          = (PFN_WriteFile)ResolveExport(k32, "WriteFile");
    g_FlushFileBuffers   = (PFN_FlushFileBuffers)ResolveExport(k32, "FlushFileBuffers");
    g_GetModuleFileNameA = (PFN_GetModuleFileNameA)ResolveExport(k32, "GetModuleFileNameA");
    g_GetPrivateProfileIntA = (PFN_GetPrivateProfileIntA)ResolveExport(k32, "GetPrivateProfileIntA");
    g_WritePrivateProfileStringA = (PFN_WritePrivateProfileStringA)ResolveExport(k32, "WritePrivateProfileStringA");
    g_GetLastError       = (PFN_GetLastError)ResolveExport(k32, "GetLastError");
    g_CreateThread       = (PFN_CreateThread)ResolveExport(k32, "CreateThread");
    g_Sleep              = (PFN_Sleep)ResolveExport(k32, "Sleep");
    g_CloseHandle        = (PFN_CloseHandle)ResolveExport(k32, "CloseHandle");
    g_ReadFile           = (PFN_ReadFile)ResolveExport(k32, "ReadFile");
    g_GetFileSize        = (PFN_GetFileSize)ResolveExport(k32, "GetFileSize");
    g_CopyFileA          = (PFN_CopyFileA)ResolveExport(k32, "CopyFileA");
    g_MoveFileExA        = (PFN_MoveFileExA)ResolveExport(k32, "MoveFileExA");
    g_DeleteFileA        = (PFN_DeleteFileA)ResolveExport(k32, "DeleteFileA");
    g_GetFileAttributesA = (PFN_GetFileAttributesA)ResolveExport(k32, "GetFileAttributesA");
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

    // 三个核心修复（SteamFix / VideoOverlayFix / PixelBoundaryFix）从 test13 起不再读取 INI。
    // 这里只读取真正有用户选择意义的选项。
    g_cfg.enableSteamInstallScriptFix = g_GetPrivateProfileIntA("Compatibility","EnableSteamInstallScriptFix",1,g_iniPath)?1:0;
    g_cfg.enableHighDpiFix = g_GetPrivateProfileIntA("Compatibility","EnableHighDpiFix",1,g_iniPath)?1:0;
    g_cfg.backendMode = ClampInt((int)g_GetPrivateProfileIntA("Graphics","Backend",0,g_iniPath),0,2);
    g_cfg.enableReShade = g_GetPrivateProfileIntA("Graphics","EnableReShade",0,g_iniPath)?1:0;

    // 发布版默认不生成日志。用户如果遇到问题，只改 EnableLog=1 即可：
    // LogLevel 默认已经是 3，CrashDiagnostics 默认已经是 1，会自动得到最完整诊断。
    g_cfg.enableLog = g_GetPrivateProfileIntA("Diagnostics","EnableLog",0,g_iniPath)?1:0;
    g_cfg.logLevel = ClampInt((int)g_GetPrivateProfileIntA("Diagnostics","LogLevel",3,g_iniPath),0,3);
    g_cfg.enableCrashDiagnostics = g_GetPrivateProfileIntA("Diagnostics","EnableCrashDiagnostics",1,g_iniPath)?1:0;
}

// 高 DPI 初始化位于正式日志辅助函数定义之前。
// 这里先写“函数声明”，相当于提前告诉编译器：下面这些日志/字符串工具稍后会实现。
// 这样 DPI 代码可以放在配置读取之后、统一初始化之前，又不需要把整套日志函数搬位置。
static void LogText(const char* s, bool flushNow);
static void LogDetail(const char* s, bool flushNow);
static void LogError(const char* s, bool flushNow);
static void AppendText(char* b, UINT& p, UINT cap, const char* s);
static void AppendDec(char* b, UINT& p, UINT cap, DWORD v);

// test10 InstallScript 修复函数声明。真正实现放在日志辅助函数之后，
// 因为它需要写详细日志，同时又会在统一兼容层初始化阶段较早调用。
static void RepairSteamInstallScriptAndLayersOnce();

// test11 像素边界修复初始化函数声明。
// 真正实现放在通用 PE/特征码辅助函数之后，因为它需要扫描已经解包到内存中的
// BaldrSky.exe 主代码。Direct3DCreate9 第一次进入时，游戏的主 .text 已经是可执行明文，
// 所以这里不需要也不应该依赖磁盘上的加密/封装代码。
static void InitializePixelBoundaryFixBySignatureOnce();

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

// ============================================================================
// test12：文字/像素混合左边界前溢修复——内联边界检查 + 安全回退
// ============================================================================
//
// test11 已经通过中文版实机把根因彻底坐实：
//
//   EDI        = TargetBase - 4
//   Fault      = mov ecx,[edi]
//   PixelSize  = 4 bytes
//
// 也就是字体/像素混合循环在左边界外多走了恰好 1 个 32bpp 像素。
// test11 的做法是“让 AV 先发生，再由 VEH 把 EIP 改到原函数的 add edi,4”。
// 它非常适合验证根因，但不适合作为长期正常控制流：异常本身有系统开销，也会污染诊断。
//
// test12 因此把逻辑前移：
//
//   1. 运行时仍然用机器码特征唯一定位原函数；
//   2. 从同一函数前面的 `mov edx,[esp+XX] / mov edi,[edx+4]` 自动推导
//      “目标绘图对象”所在的栈偏移 XX；不把中文版 0x38 写死；
//   3. 把原来的 5 字节
//          movzx edx,word ptr [ebx]
//          mov   ecx,dword ptr [edi]
//      改成 JMP 到我们自己的极小跳板；
//   4. 跳板先执行原来的 movzx，然后比较：
//          EDI == [TargetObject+4] - 4
//      如果成立，直接跳到原函数自己的 `add edi,4`，等价于“裁掉左边界外 1 像素”；
//      不成立则执行原来的 `mov ecx,[edi]`，然后返回原代码。
//
// 正常情况下 test12 起沿用至 test14 的内联路径完全不会产生这个 Access Violation。
// 只有“特征已经找到，但内联跳板因为内存分配/写保护等原因安装失败”时，
// 才退回 test11 已实机验证的精确 VEH 恢复，避免因为补丁安装失败让用户重新崩溃。
static DWORD g_pixelFaultEip = 0;          // `mov ecx,[edi]` 的运行时地址
static DWORD g_pixelSkipEip = 0;           // 原函数自身 `add edi,4` 的运行时地址
static DWORD g_pixelSignatureStart = 0;    // 5 字节内联 JMP 的安装位置
static DWORD g_pixelReturnEip = 0;          // 被覆盖 5 字节后的正常返回地址
static BYTE  g_pixelTargetObjectStackOffset = 0; // 从机器码自动推导，例如中文版是 0x38
static DWORD g_pixelRecoveredCount = 0;    // 只有 FALLBACK 模式会增加
static volatile DWORD g_pixelInlineSkipCount = 0; // INLINE 模式每跳过一个越界像素增加一次
static DWORD g_pixelInlineLastLoggedCount = 0;
static bool  g_pixelSignatureReady = false;
static bool  g_pixelInlineInstalled = false;
static BYTE* g_pixelInlineThunk = 0;

// INLINE 跳板只做 `inc [计数器]`，不直接调用 C/C++ 日志函数。
// 原因很简单：这个代码位于字体像素热路径里，调用复杂函数会额外破坏 XMM/x87/调用者保存寄存器，
// 还可能与日志线程产生重入。只加一个 DWORD 计数最安全；真正写日志由普通线程/Direct3DCreate9
// 在稍后时机读取这个计数完成。
static void LogPixelBoundaryInlineStatsIfChanged() {
    if (!g_pixelInlineInstalled) return;
    DWORD now=g_pixelInlineSkipCount;
    if (!now || now==g_pixelInlineLastLoggedCount) return;
    g_pixelInlineLastLoggedCount=now;

    char b[320]; UINT p=0;
    AppendText(b,p,sizeof(b),"[像素边界修复][内联] 已在异常发生前跳过左边界外像素，累计次数=");
    AppendDec(b,p,sizeof(b),now);
    b[p]=0; LogDetail(b,true);
}

// test11 的精确异常恢复保留为“安装失败回退”，而不是正常工作方式。
// 只要 INLINE 已经成功安装，这个函数就拒绝恢复任何 AV；这样如果内联补丁本身有遗漏，
// CrashDiagnostics=1 时能够真实暴露问题，而不是被旧恢复路径悄悄掩盖。
static bool TryRecoverPixelBoundaryAv(EXCEPTION_POINTERS32* ep) {
    if (g_pixelInlineInstalled) return false;
    if (!g_pixelSignatureReady || !ep ||
        !ep->ExceptionRecord || !ep->ContextRecord) {
        return false;
    }

    EXCEPTION_RECORD32* er = ep->ExceptionRecord;
    CONTEXT32_MIN* c = ep->ContextRecord;

    if (c->Eip != g_pixelFaultEip) return false;
    if (er->NumberParameters < 2 || er->ExceptionInformation[0] != 0 ||
        er->ExceptionInformation[1] != c->Edi) {
        return false;
    }

    // 栈偏移不再写死 0x38，而是由同一函数的前置机器码自动推导。
    DWORD targetSlot = c->Esp + (DWORD)g_pixelTargetObjectStackOffset;
    if (!CanReadMemory(targetSlot,4)) return false;
    DWORD targetObject = *(DWORD*)(unsigned long)targetSlot;
    if (!targetObject || !CanReadMemory(targetObject + 4,4)) return false;

    DWORD targetBase = *(DWORD*)(unsigned long)(targetObject + 4);
    if (!targetBase || targetBase < 4 || c->Edi != targetBase - 4) return false;

    ++g_pixelRecoveredCount;
    if (g_pixelRecoveredCount <= 16 || (g_pixelRecoveredCount % 100) == 0) {
        char b[680]; UINT p=0;
        AppendText(b,p,sizeof(b),"[像素边界修复][回退恢复] 次数="); AppendDec(b,p,sizeof(b),g_pixelRecoveredCount);
        AppendText(b,p,sizeof(b)," EIP="); AppendHex32(b,p,sizeof(b),c->Eip);
        AppendText(b,p,sizeof(b)," EDI="); AppendHex32(b,p,sizeof(b),c->Edi);
        AppendText(b,p,sizeof(b)," TargetObject="); AppendHex32(b,p,sizeof(b),targetObject);
        AppendText(b,p,sizeof(b)," TargetBase="); AppendHex32(b,p,sizeof(b),targetBase);
        AppendText(b,p,sizeof(b)," Delta=-4 SkipEIP="); AppendHex32(b,p,sizeof(b),g_pixelSkipEip);
        b[p]=0; LogText(b,true);
    }

    c->Eip = g_pixelSkipEip;
    return true;
}

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

    // test12 起的正常路径使用内联检查，test14 继续沿用，不应再走异常恢复。
    // 这里只保留“INLINE 安装失败时”的 test11 精确回退；若回退成功，
    // CONTEXT 已经指向原函数自己的“推进到下一像素”路径。
    if (TryRecoverPixelBoundaryAv(ep))
        return EXCEPTION_CONTINUE_EXECUTION_VALUE;

    // PixelBoundaryFix 可以单独启用。若用户关闭 CrashDiagnostics，
    // 除了上面那一种严格匹配的可恢复边界 AV，其他异常不刷详细日志。
    if (!g_cfg.enableCrashDiagnostics)
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


// ============================================================================
// test12：定位像素混合边界故障，并自动推导目标对象栈偏移
// ============================================================================
//
// 故障点核心特征仍然来自已经实机确认的中文版：
//
//   0F B7 13                movzx edx,word ptr [ebx]
//   8B 0F                   mov   ecx,dword ptr [edi]
//   81 E2 00 FF FF FF       and   edx,0xFFFFFF00
//   C1 E2 10                shl   edx,16
//   3B D1                   cmp   edx,ecx
//   76 xx                   jbe   ...
//
// test12 额外验证两个同函数结构：
//
// A. 故障点前 0x100 字节内必须唯一出现：
//      8B 54 24 XX          mov edx,[esp+XX]
//      8B 7A 04             mov edi,[edx+4]
//    这直接告诉我们“目标绘图对象”的栈偏移 XX。
//
// B. 故障点后 0x100 字节内必须唯一出现：
//      89 1F                mov [edi],ebx
//      83 C7 04             add edi,4
//    后者就是越界像素应该跳到的原函数安全续接点。
//
// 三部分结构任何一项不唯一，补丁都拒绝启用。
static DWORD FindPixelBoundaryBlendSite(BYTE* image, DWORD imageSize,
                                        BYTE** outSignatureStart,
                                        BYTE** outFault,
                                        BYTE** outSkip,
                                        BYTE* outTargetStackOffset) {
    if (outSignatureStart) *outSignatureStart=0;
    if (outFault) *outFault=0;
    if (outSkip) *outSkip=0;
    if (outTargetStackOffset) *outTargetStackOffset=0;
    if (!image || imageSize < 0x220) return 0;

    const BYTE signature[] = {
        0x0F,0xB7,0x13,
        0x8B,0x0F,
        0x81,0xE2,0x00,0xFF,0xFF,0xFF,
        0xC1,0xE2,0x10,
        0x3B,0xD1,
        0x76
    };

    DWORD count=0;
    BYTE* firstStart=0;
    BYTE* firstFault=0;
    BYTE* firstSkip=0;
    BYTE firstStackOffset=0;

    for (DWORD i=0; i+sizeof(signature) <= imageSize; ++i) {
        BYTE* q=image+i;
        if (!BytesEqualSimple(q,signature,(UINT)sizeof(signature))) continue;
        BYTE* fault=q+3;

        // 先验证并推导目标对象栈偏移。
        DWORD backStart=(i>0x100)?(i-0x100):0;
        DWORD baseLoadCount=0;
        BYTE stackOffset=0;
        for (DWORD k=backStart; k+7<=i; ++k) {
            BYTE* t=image+k;
            if (t[0]==0x8B && t[1]==0x54 && t[2]==0x24 &&
                t[4]==0x8B && t[5]==0x7A && t[6]==0x04) {
                ++baseLoadCount;
                stackOffset=t[3];
            }
        }
        if (baseLoadCount!=1) continue;

        // 再验证故障点后的原函数安全续接结构。
        DWORD remain=imageSize-(i+3);
        DWORD window=(remain<0x100)?remain:0x100;
        BYTE* writeInc=0;
        DWORD writeIncCount=0;
        for (DWORD k=0; k+5<=window; ++k) {
            BYTE* t=fault+k;
            if (t[0]==0x89 && t[1]==0x1F &&
                t[2]==0x83 && t[3]==0xC7 && t[4]==0x04) {
                if (!writeInc) writeInc=t;
                ++writeIncCount;
            }
        }
        if (writeIncCount!=1 || !writeInc) continue;

        if (!firstStart) {
            firstStart=q;
            firstFault=fault;
            firstSkip=writeInc+2;
            firstStackOffset=stackOffset;
        }
        ++count;
    }

    if (count==1) {
        if (outSignatureStart) *outSignatureStart=firstStart;
        if (outFault) *outFault=firstFault;
        if (outSkip) *outSkip=firstSkip;
        if (outTargetStackOffset) *outTargetStackOffset=firstStackOffset;
    }
    return count;
}

// 写一个 x86 rel32 跳转/调用的 32 位位移。
// x86 近跳转的目标 = “下一条指令地址 + rel32”，所以位移按 32 位地址差计算即可。
static void WriteRelative32(BYTE* instruction,DWORD target) {
    DWORD next=Ptr32(instruction+5);
    DWORD rel=target-next;
    instruction[1]=(BYTE)(rel & 0xFF);
    instruction[2]=(BYTE)((rel>>8) & 0xFF);
    instruction[3]=(BYTE)((rel>>16) & 0xFF);
    instruction[4]=(BYTE)((rel>>24) & 0xFF);
}

// 为像素热路径生成一个极小 x86 跳板。
// 跳板只使用整数通用寄存器，而且会把临时使用的 ECX 恢复；
// 不调用任何 C++ 函数，不接触 x87/XMM，也不做文件/锁/堆操作。
static bool InstallPixelBoundaryInlinePatch() {
    if (!g_pixelSignatureReady || !g_VirtualAlloc || !g_VirtualProtect) return false;

    const BYTE expected[5]={0x0F,0xB7,0x13,0x8B,0x0F};
    BYTE* site=(BYTE*)(unsigned long)g_pixelSignatureStart;
    if (!BytesEqualSimple(site,expected,5)) {
        LogError("[像素边界修复][内联][错误] 补丁点原字节不符合预期；拒绝写入。",true);
        return false;
    }

    // 128 字节远大于实际需要，留出足够余量，便于未来扩展诊断而不改变分配逻辑。
    BYTE* thunk=(BYTE*)g_VirtualAlloc(0,128,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!thunk) {
        LogError("[像素边界修复][内联][错误] VirtualAlloc 跳板失败；将使用异常恢复回退。",true);
        return false;
    }

    UINT p=0;
    // 重新执行被 5 字节 JMP 覆盖掉的第一条原指令：movzx edx,word ptr [ebx]
    thunk[p++]=0x0F; thunk[p++]=0xB7; thunk[p++]=0x13;

    // ECX 在原代码下一条本来就会被 mov ecx,[edi] 覆盖。
    // 但“跳过越界像素”路径在 test11 中保留的是故障前旧 ECX，
    // 所以这里仍然 push/pop ECX，做到和已实机验证的恢复现场完全一致。
    thunk[p++]=0x51; // push ecx

    // push ecx 让 ESP 比原函数低 4 字节，所以原来的 [ESP+XX] 现在变成 [ESP+XX+4]。
    BYTE adjusted=(BYTE)(g_pixelTargetObjectStackOffset+4);
    thunk[p++]=0x8B; thunk[p++]=0x4C; thunk[p++]=0x24; thunk[p++]=adjusted; // mov ecx,[esp+adjusted]
    thunk[p++]=0x85; thunk[p++]=0xC9; // test ecx,ecx

    // 目标对象意外为 NULL 时不要新增崩溃：恢复 ECX 后按原代码走，让原程序自己处理。
    UINT jzToNormal=p; thunk[p++]=0x74; thunk[p++]=0x00;

    thunk[p++]=0x8B; thunk[p++]=0x49; thunk[p++]=0x04; // mov ecx,[ecx+4] => TargetBase
    thunk[p++]=0x83; thunk[p++]=0xE9; thunk[p++]=0x04; // sub ecx,4
    thunk[p++]=0x3B; thunk[p++]=0xF9;                 // cmp edi,ecx
    UINT jeToSkip=p; thunk[p++]=0x74; thunk[p++]=0x00;

    UINT normalLabel=p;
    thunk[p++]=0x59;                   // pop ecx
    thunk[p++]=0x8B; thunk[p++]=0x0F; // 原指令：mov ecx,[edi]
    UINT jmpReturn=p; thunk[p++]=0xE9; p+=4;

    UINT skipLabel=p;
    thunk[p++]=0x59; // pop ecx，恢复到 test11 故障发生前的 ECX

    // 只增加一个 DWORD 计数，便于普通线程稍后写日志。
    // FF 05 imm32 = inc dword ptr [absolute-address]
    thunk[p++]=0xFF; thunk[p++]=0x05;
    DWORD counterAddress=Ptr32((void*)&g_pixelInlineSkipCount);
    thunk[p++]=(BYTE)(counterAddress&0xFF);
    thunk[p++]=(BYTE)((counterAddress>>8)&0xFF);
    thunk[p++]=(BYTE)((counterAddress>>16)&0xFF);
    thunk[p++]=(BYTE)((counterAddress>>24)&0xFF);

    UINT jmpSkip=p; thunk[p++]=0xE9; p+=4;

    // 回填两个 rel8 条件跳转。
    int relNormal=(int)normalLabel-(int)(jzToNormal+2);
    int relSkip=(int)skipLabel-(int)(jeToSkip+2);
    if (relNormal < -128 || relNormal > 127 || relSkip < -128 || relSkip > 127) {
        LogError("[像素边界修复][内联][错误] 内部短跳转超出范围；将使用异常恢复回退。",true);
        return false;
    }
    thunk[jzToNormal+1]=(BYTE)(signed char)relNormal;
    thunk[jeToSkip+1]=(BYTE)(signed char)relSkip;

    WriteRelative32(thunk+jmpReturn,g_pixelReturnEip);
    WriteRelative32(thunk+jmpSkip,g_pixelSkipEip);

    // 写完以后把跳板页面从 RW 改为 RX，避免长期留下可写可执行页。
    DWORD oldThunkProtect=0;
    if (!g_VirtualProtect(thunk,128,PAGE_EXECUTE_READ,&oldThunkProtect)) {
        LogError("[像素边界修复][内联][错误] 跳板无法切换为可执行只读；将使用异常恢复回退。",true);
        return false;
    }
    if (g_FlushInstructionCache)
        g_FlushInstructionCache((HANDLE)(long)-1,thunk,p);

    BYTE patch[5]={0xE9,0,0,0,0};
    // 注意：patch[] 只是临时缓冲区，rel32 必须按“真正执行 JMP 的游戏地址 site”计算，
    // 不能拿 patch[] 自己在栈上的地址计算。
    DWORD siteRel=Ptr32(thunk)-Ptr32(site+5);
    patch[1]=(BYTE)(siteRel&0xFF);
    patch[2]=(BYTE)((siteRel>>8)&0xFF);
    patch[3]=(BYTE)((siteRel>>16)&0xFF);
    patch[4]=(BYTE)((siteRel>>24)&0xFF);

    DWORD oldSiteProtect=0;
    if (!g_VirtualProtect(site,5,PAGE_READWRITE,&oldSiteProtect)) {
        LogError("[像素边界修复][内联][错误] 原函数补丁点无法取得写权限；将使用异常恢复回退。",true);
        return false;
    }
    for (UINT i=0;i<5;++i) site[i]=patch[i];
    DWORD ignored=0;
    g_VirtualProtect(site,5,oldSiteProtect,&ignored);
    if (g_FlushInstructionCache)
        g_FlushInstructionCache((HANDLE)(long)-1,site,5);

    g_pixelInlineThunk=thunk;
    g_pixelInlineInstalled=true;

    char b[620]; UINT q=0;
    AppendText(b,q,sizeof(b),"[像素边界修复][内联][成功] 已在异常发生前安装边界检查。Patch=");
    AppendHex32(b,q,sizeof(b),g_pixelSignatureStart);
    AppendText(b,q,sizeof(b)," Thunk="); AppendHex32(b,q,sizeof(b),Ptr32(thunk));
    AppendText(b,q,sizeof(b)," Return="); AppendHex32(b,q,sizeof(b),g_pixelReturnEip);
    AppendText(b,q,sizeof(b)," Skip="); AppendHex32(b,q,sizeof(b),g_pixelSkipEip);
    AppendText(b,q,sizeof(b)," TargetStackOffset="); AppendHex32(b,q,sizeof(b),(DWORD)g_pixelTargetObjectStackOffset);
    b[q]=0; LogText(b,true);
    return true;
}

static void InitializePixelBoundaryFixBySignatureOnce() {
    static bool attempted=false;
    if (attempted) return;
    attempted=true;

    BYTE* image=(BYTE*)FindLoadedModule("BaldrSky.exe");
    DWORD imageSize=GetImageSize32(image);
    if (!image || !imageSize) {
        LogText("[像素边界修复][错误] 无法取得 BaldrSky.exe 运行时映像。",true);
        return;
    }

    BYTE* sig=0;
    BYTE* fault=0;
    BYTE* skip=0;
    BYTE stackOffset=0;
    DWORD count=FindPixelBoundaryBlendSite(image,imageSize,&sig,&fault,&skip,&stackOffset);

    char b[620]; UINT p=0;
    AppendText(b,p,sizeof(b),"[像素边界修复][详细] 完整结构命中数="); AppendDec(b,p,sizeof(b),count);
    if (sig) {
        AppendText(b,p,sizeof(b)," signature="); AppendHex32(b,p,sizeof(b),Ptr32(sig));
        AppendText(b,p,sizeof(b)," fault="); AppendHex32(b,p,sizeof(b),Ptr32(fault));
        AppendText(b,p,sizeof(b)," skip="); AppendHex32(b,p,sizeof(b),Ptr32(skip));
        AppendText(b,p,sizeof(b)," targetStackOffset="); AppendHex32(b,p,sizeof(b),(DWORD)stackOffset);
    }
    b[p]=0; LogText(b,true);

    if (count!=1 || !sig || !fault || !skip || !stackOffset) {
        LogText("[像素边界修复][警告] 特征/目标对象栈来源/安全续接不是唯一结构；为安全起见不修改代码。",true);
        return;
    }

    g_pixelSignatureStart=Ptr32(sig);
    g_pixelFaultEip=Ptr32(fault);
    g_pixelReturnEip=Ptr32(sig+5);
    g_pixelSkipEip=Ptr32(skip);
    g_pixelTargetObjectStackOffset=stackOffset;
    g_pixelSignatureReady=true;

    if (!InstallPixelBoundaryInlinePatch()) {
        LogText("[像素边界修复][回退] 内联补丁安装失败；启用 test11 已实机验证的精确异常恢复。",true);
    }
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
    g_RegDeleteValueA=(PFN_RegDeleteValueA)g_GetProcAddress(adv,"RegDeleteValueA");
    return g_RegCreateKeyExA && g_RegSetValueExA && g_RegQueryValueExA && g_RegCloseKey;
}

// ============================================================================
// Steam InstallScript Fix：静默移除 DPIUNAWARE / VISTARTM 的持久化来源
// ============================================================================
//
// Steam 发行包里的 install.vdf 会在 Steam 真正 CreateProcess(BaldrSky.exe) 之前执行。
// 已确认其中存在：
//
//   HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
//       %INSTALLDIR%\BaldrSky.exe = "~ DPIUNAWARE VISTARTM"
//
// 这意味着我们的 d3d9.dll 第一次得到控制权时，当前进程已经可能被 Windows 按
// DPI Unaware + Vista compatibility layer 创建。用户已经实机验证：手动删除该块后，
// Steam 后续不会再强制设置，窗口也恢复为原始 800x600。
//
// test10 的目标不是“在已经创建好的第一进程里和 AppCompat 打架”，而是静默消除根源：
//   1. 读取同目录 install.vdf；
//   2. 只定位包含 AppCompatFlags + Layers + BaldrSky.exe + DPIUNAWARE + VISTARTM 的子块；
//   3. 仅第一次把原文件备份为 install.vdf.baldrskywin11fix.bak；
//   4. 写临时文件，再用 MoveFileExA 原子式替换原文件；
//   5. 同时清理当前 HKCU Layers 值里的 DPIUNAWARE/VISTARTM token；
//   6. 正常情况下不弹窗、不退出、不自动重启。第一轮是否已经受 Shim 影响由 Windows 决定；
//      第二轮开始 Steam 已失去这段持久化写入来源。
//   7. 唯一例外是 install.vdf 整个文件缺失：此时按游戏版本弹一次中/英文 MessageBox，
//      提醒用户通过 Steam 验证游戏文件完整性；用户确认后游戏仍继续启动。
//
// 安全边界：
// - 不删除整个 install.vdf；
// - 不删除整个 installscript；
// - 不删除整个 Registry；
// - 只删除唯一命中的 AppCompatFlags\Layers 子项；
// - 如果候选不是唯一一处，宁可不改；
// - 注册表里只剔除 DPIUNAWARE / VISTARTM，其他用户自定义 token 原样保留。

static bool g_installScriptFixAttempted=false;

// install.vdf 正常只有几 KB。这里给 64 KiB 的静态工作区，既足够宽裕，
// 又避免在老 x86 游戏主线程栈上一次性放两个大数组。若未来 Steam 把文件扩到更大，
// 安全策略是记录并跳过，而不是截断文件。
static char g_installVdfInput[65536];
static char g_installVdfOutput[65536];

// 在 [begin,end) 范围里查找一个普通 ASCII 子串。
// 返回 -1 表示没找到；不使用 strstr 是为了继续保持无 CRT。
static int FindAsciiSubstringRange(const char* data,DWORD begin,DWORD end,const char* needle) {
    if (!data || !needle || begin>=end) return -1;
    UINT n=AsciiLen(needle);
    if (!n || (DWORD)n>end-begin) return -1;
    for (DWORD i=begin;i+(DWORD)n<=end;++i) {
        bool same=true;
        for (UINT j=0;j<n;++j) {
            if (data[i+j]!=needle[j]) { same=false; break; }
        }
        if (same) return (int)i;
    }
    return -1;
}

// 从某个位置向左找到当前文本行的第一个字符。
// 这样删除 VDF 子块时连同原有缩进一起删掉，不留下半行空白。
static DWORD FindLineStart(const char* data,DWORD pos) {
    while (pos>0 && data[pos-1]!='\n' && data[pos-1]!='\r') --pos;
    return pos;
}

// 从某个位置向右越过当前行结束符。
// 同时兼容 CRLF 与单独 LF。
static DWORD FindAfterLineEnd(const char* data,DWORD size,DWORD pos) {
    while (pos<size && data[pos]!='\r' && data[pos]!='\n') ++pos;
    if (pos<size && data[pos]=='\r') ++pos;
    if (pos<size && data[pos]=='\n') ++pos;
    return pos;
}

// VDF 是 KeyValues 文本，花括号只在“非引号字符串”里表示层级。
// 这里从 openBrace 指向的 '{' 开始配对，忽略引号内部的 { }，并理解 \" 这种转义。
static int FindMatchingVdfBrace(const char* data,DWORD size,DWORD openBrace) {
    if (!data || openBrace>=size || data[openBrace]!='{') return -1;
    int depth=0;
    bool inQuote=false;
    bool escaped=false;
    for (DWORD i=openBrace;i<size;++i) {
        char c=data[i];
        if (inQuote) {
            if (escaped) { escaped=false; continue; }
            if (c=='\\') { escaped=true; continue; }
            if (c=='\"') inQuote=false;
            continue;
        }
        if (c=='\"') { inQuote=true; continue; }
        if (c=='{') ++depth;
        else if (c=='}') {
            --depth;
            if (depth==0) return (int)i;
            if (depth<0) return -1;
        }
    }
    return -1;
}

// 小文件读取。只有完整读取成功才返回 true。
static bool ReadWholeSmallFileA(const char* path,char* buffer,DWORD capacity,DWORD* outSize) {
    if (outSize) *outSize=0;
    if (!path || !buffer || capacity<2 || !g_CreateFileA || !g_ReadFile || !g_GetFileSize || !g_CloseHandle) return false;
    HANDLE h=g_CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if (h==INVALID_HANDLE_VALUE) return false;
    DWORD size=g_GetFileSize(h,0);
    if (size==0xFFFFFFFFUL || size+1>=capacity) { g_CloseHandle(h); return false; }
    DWORD got=0;
    BOOL ok=g_ReadFile(h,buffer,size,&got,0);
    g_CloseHandle(h);
    if (!ok || got!=size) return false;
    buffer[size]=0;
    if (outSize) *outSize=size;
    return true;
}

// 小文件写入。先写临时文件，调用方再 MoveFileExA 覆盖正式 install.vdf。
static bool WriteWholeSmallFileA(const char* path,const char* data,DWORD size) {
    if (!path || !data || !g_CreateFileA || !g_WriteFile || !g_CloseHandle) return false;
    HANDLE h=g_CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if (h==INVALID_HANDLE_VALUE) return false;
    DWORD wrote=0;
    BOOL ok=g_WriteFile(h,data,size,&wrote,0);
    if (ok && g_FlushFileBuffers) g_FlushFileBuffers(h);
    g_CloseHandle(h);
    return ok && wrote==size;
}

// 大小写无关比较一个“由指针+长度表示的 token”与固定单词。
static bool TokenEqualsNoCase(const char* token,UINT tokenLen,const char* word) {
    if (!token || !word || tokenLen!=AsciiLen(word)) return false;
    for (UINT i=0;i<tokenLen;++i) {
        if (LowerAsciiSimple(token[i])!=LowerAsciiSimple(word[i])) return false;
    }
    return true;
}

// 判断当前 BaldrSky.exe 是英文 Steam 版还是中文 Steam 版。
//
// 为什么不用 Windows UI 语言：
// 用户可能在中文 Windows 上运行英文版，也可能在英文 Windows 上运行中文版。
// “提示语言随游戏”应该看游戏自身，而不是操作系统语言。
//
// 英文 Steam EXE 的 .patch 区已确认额外包含 BaldrSkyCustom.ttf，中文版没有。
// 这里再同时接受 BALDRSKY_EN 作为第二个英文语义锚点，避免未来某个英文构建只改了其中一处。
// 两者都找不到时安全回退中文，因为当前项目已验证的另一目标就是中文 Steam 版。
static bool IsEnglishBaldrSkyRuntimeImage() {
    BYTE* image=(BYTE*)FindLoadedModule("BaldrSky.exe");
    DWORD imageSize=GetImageSize32(image);
    if (!image || !imageSize) return false;
    const char* data=(const char*)image;
    if (FindAsciiSubstringRange(data,0,imageSize,"BaldrSkyCustom.ttf")>=0) return true;
    if (FindAsciiSubstringRange(data,0,imageSize,"BALDRSKY_EN")>=0) return true;
    return false;
}

// install.vdf 整个文件缺失时显示一次提示。
// 这不是修复失败条件，所以 MessageBox 返回后函数立即结束，游戏继续正常初始化。
static void ShowMissingInstallVdfMessageBoxOnce() {
    static bool shown=false;
    if (shown) return;
    shown=true;

    if (!InitWinApis() || !g_LoadLibraryA || !g_GetProcAddress) {
        LogError("[InstallScript修复][警告] install.vdf 缺失，但无法解析 MessageBoxW；游戏继续启动。",true);
        return;
    }

    HMODULE user32=(HMODULE)FindLoadedModule("user32.dll");
    if (!user32) user32=g_LoadLibraryA("user32.dll");
    PFN_MessageBoxW messageBoxW=user32 ? (PFN_MessageBoxW)g_GetProcAddress(user32,"MessageBoxW") : 0;
    if (!messageBoxW) {
        LogError("[InstallScript修复][警告] install.vdf 缺失，但 MessageBoxW 不可用；游戏继续启动。",true);
        return;
    }

    if (IsEnglishBaldrSkyRuntimeImage()) {
        messageBoxW(
            0,
            L"Steam install.vdf was not found.\n\n"
            L"Please verify the integrity of the game files in Steam to restore the missing file.\n"
            L"The game will continue to start.",
            L"BALDR SKY Win11 Compatibility Fix",
            MB_OK_VALUE|MB_ICONWARNING_VALUE|MB_SETFOREGROUND_VALUE);
        LogText("[InstallScript修复][提示] install.vdf 缺失；已显示英文完整性验证提示，游戏继续启动。",true);
    } else {
        messageBoxW(
            0,
            L"未找到 Steam 的 install.vdf。\n\n"
            L"建议在 Steam 中验证游戏文件完整性，以恢复缺失文件。\n"
            L"兼容层将继续启动游戏。",
            L"BALDR SKY Win11 兼容层",
            MB_OK_VALUE|MB_ICONWARNING_VALUE|MB_SETFOREGROUND_VALUE);
        LogText("[InstallScript修复][提示] install.vdf 缺失；已显示中文完整性验证提示，游戏继续启动。",true);
    }
}

// 清理 HKCU\...\AppCompatFlags\Layers 中当前 BaldrSky.exe 对应值。
// 只删除 DPIUNAWARE 和 VISTARTM 两个 token。
// 例如：
//   "~ DPIUNAWARE VISTARTM"            -> 整条值删除
//   "~ DPIUNAWARE VISTARTM RUNASADMIN" -> "~ RUNASADMIN"
// 这样用户自己额外设置的兼容选项不会被本修复层误删。
static void CleanCurrentExeAppCompatLayers() {
    BuildModulePathsOnce();
    if (!g_mainExePath[0] || !InitRegistryApis() || !g_RegDeleteValueA) {
        LogDetail("[InstallScript修复][详细] 注册表 API 不完整，跳过当前 Layers token 清理。",true);
        return;
    }

    const char* keyPath="Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    HKEY key=0; DWORD disposition=0;
    LONG rc=g_RegCreateKeyExA(HKEY_CURRENT_USER_VALUE,keyPath,0,0,0,KEY_QUERY_VALUE_VALUE|KEY_SET_VALUE_VALUE,0,&key,&disposition);
    if (rc!=ERROR_SUCCESS_VALUE || !key) {
        LogDetail("[InstallScript修复][详细] 当前用户 AppCompatFlags\\Layers 无法打开，跳过。",true);
        return;
    }

    char oldValue[1024]; DWORD type=0; DWORD bytes=(DWORD)sizeof(oldValue);
    rc=g_RegQueryValueExA(key,g_mainExePath,0,&type,(BYTE*)oldValue,&bytes);
    if (rc!=ERROR_SUCCESS_VALUE || (type!=REG_SZ_VALUE && type!=REG_EXPAND_SZ_VALUE) || bytes==0) {
        g_RegCloseKey(key);
        LogDetail("[InstallScript修复][详细] 当前 BaldrSky.exe 没有需要清理的 AppCompat Layers 字符串。",true);
        return;
    }
    oldValue[sizeof(oldValue)-1]=0;

    char filtered[1024]; UINT out=0; UINT i=0; int removed=0;
    while (oldValue[i]) {
        while (oldValue[i]==' ' || oldValue[i]=='\t') ++i;
        if (!oldValue[i]) break;
        UINT start=i;
        while (oldValue[i] && oldValue[i]!=' ' && oldValue[i]!='\t') ++i;
        UINT len=i-start;
        if (TokenEqualsNoCase(oldValue+start,len,"DPIUNAWARE") || TokenEqualsNoCase(oldValue+start,len,"VISTARTM")) {
            ++removed;
            continue;
        }
        if (out && out+1<sizeof(filtered)) filtered[out++]=' ';
        for (UINT j=0;j<len && out+1<sizeof(filtered);++j) filtered[out++]=oldValue[start+j];
    }
    filtered[out]=0;

    if (!removed) {
        g_RegCloseKey(key);
        LogDetail("[InstallScript修复][详细] 当前 Layers 值不含 DPIUNAWARE/VISTARTM，无需修改。",true);
        return;
    }

    // 如果过滤后只剩 KeyValues/Compat 常见的前缀“~”，它本身没有任何兼容含义，
    // 直接删除整条值最干净；否则把保留下来的其他 token 写回。
    bool onlyTilde=(filtered[0]=='~' && filtered[1]==0);
    if (filtered[0]==0 || onlyTilde) {
        rc=g_RegDeleteValueA(key,g_mainExePath);
        if (rc==ERROR_SUCCESS_VALUE)
            LogText("[InstallScript修复][成功] 已静默删除当前 BaldrSky.exe 的 DPIUNAWARE/VISTARTM AppCompat 值。",true);
        else
            LogError("[InstallScript修复][警告] 已识别 AppCompat 值，但删除失败；游戏继续启动。",true);
    } else {
        rc=g_RegSetValueExA(key,g_mainExePath,0,REG_SZ_VALUE,(const BYTE*)filtered,AsciiLen(filtered)+1);
        if (rc==ERROR_SUCCESS_VALUE) {
            char b[1200]; UINT p=0;
            AppendText(b,p,sizeof(b),"[InstallScript修复][成功] 已剔除 DPIUNAWARE/VISTARTM，保留其他 AppCompat token：");
            AppendText(b,p,sizeof(b),filtered); b[p]=0; LogText(b,true);
        } else {
            LogError("[InstallScript修复][警告] AppCompat token 过滤结果写回失败；游戏继续启动。",true);
        }
    }
    g_RegCloseKey(key);
}

static void RepairSteamInstallScriptAndLayersOnce() {
    if (g_installScriptFixAttempted) return;
    g_installScriptFixAttempted=true;

    BuildModulePathsOnce();
    if (!InitWinApis()) {
        LogError("[InstallScript修复][警告] Windows 文件 API 不完整，无法检查 install.vdf。",true);
        return;
    }

    // 无论 install.vdf 是否已经被修过，都先清理 HKCU 中 Steam 上一次可能留下的值。
    CleanCurrentExeAppCompatLayers();

    char vdfPath[520], backupPath[560], tempPath[560];
    if (!BuildSiblingFilePath("install.vdf",vdfPath,(UINT)sizeof(vdfPath))) {
        LogDetail("[InstallScript修复][详细] 无法构造 install.vdf 路径，跳过文件净化。",true);
        return;
    }
    if (!BuildSiblingFilePath("install.vdf.baldrskywin11fix.bak",backupPath,(UINT)sizeof(backupPath)) ||
        !BuildSiblingFilePath("install.vdf.baldrskywin11fix.tmp",tempPath,(UINT)sizeof(tempPath))) {
        LogDetail("[InstallScript修复][详细] 无法构造备份/临时文件路径，跳过文件净化。",true);
        return;
    }

    // 先明确区分“文件根本不存在”和“文件存在但读取失败”。
    // 只有前者才按用户要求弹 MessageBox；后者只写日志，避免把权限/异常文件误报成缺失。
    if (g_GetFileAttributesA) {
        DWORD attrs=g_GetFileAttributesA(vdfPath);
        if (attrs==INVALID_FILE_ATTRIBUTES_VALUE) {
            DWORD err=g_GetLastError ? g_GetLastError() : 0;
            if (err==ERROR_FILE_NOT_FOUND_VALUE || err==ERROR_PATH_NOT_FOUND_VALUE) {
                LogError("[InstallScript修复][警告] 同目录缺少 install.vdf；建议通过 Steam 验证游戏文件完整性。游戏仍继续启动。",true);
                ShowMissingInstallVdfMessageBoxOnce();
                return;
            }
        }
    }

    DWORD size=0;
    if (!ReadWholeSmallFileA(vdfPath,g_installVdfInput,(DWORD)sizeof(g_installVdfInput),&size)) {
        LogError("[InstallScript修复][警告] install.vdf 存在但无法完整读取，或文件超过 64 KiB；为安全起见不修改，游戏继续启动。",true);
        return;
    }

    // 不直接假定格式化空格/Tab，也不要求完整路径字符串的反斜杠写法固定。
    // 先找 AppCompatFlags，再用同一花括号子块中的其他语义词确认它就是目标项。
    DWORD search=0; int candidateCount=0; DWORD removeStart=0,removeEnd=0;
    while (search<size) {
        int hit=FindAsciiSubstringRange(g_installVdfInput,search,size,"AppCompatFlags");
        if (hit<0) break;
        DWORD h=(DWORD)hit;
        DWORD lineStart=FindLineStart(g_installVdfInput,h);
        int brace=FindAsciiSubstringRange(g_installVdfInput,h,(h+1024<size)?h+1024:size,"{");
        if (brace>=0) {
            int close=FindMatchingVdfBrace(g_installVdfInput,size,(DWORD)brace);
            if (close>=0) {
                DWORD b=(DWORD)brace, e=(DWORD)close+1;
                bool hasLayers=FindAsciiSubstringRange(g_installVdfInput,lineStart,e,"Layers")>=0;
                bool hasExe=FindAsciiSubstringRange(g_installVdfInput,b,e,"BaldrSky.exe")>=0;
                bool hasDpi=FindAsciiSubstringRange(g_installVdfInput,b,e,"DPIUNAWARE")>=0;
                bool hasVista=FindAsciiSubstringRange(g_installVdfInput,b,e,"VISTARTM")>=0;
                if (hasLayers && hasExe && hasDpi && hasVista) {
                    ++candidateCount;
                    removeStart=lineStart;
                    removeEnd=FindAfterLineEnd(g_installVdfInput,size,e);
                }
            }
        }
        search=h+1;
    }

    if (candidateCount==0) {
        LogDetail("[InstallScript修复][详细] install.vdf 中已不存在 DPIUNAWARE/VISTARTM AppCompat 子块，无需修改。",true);
        return;
    }
    if (candidateCount!=1 || removeEnd<=removeStart || removeEnd>size) {
        char b[240]; UINT p=0;
        AppendText(b,p,sizeof(b),"[InstallScript修复][警告] install.vdf 目标子块候选数=");
        AppendDec(b,p,sizeof(b),(DWORD)candidateCount);
        AppendText(b,p,sizeof(b),"；不是唯一命中，为安全起见不修改文件。"); b[p]=0; LogError(b,true);
        return;
    }

    DWORD prefix=removeStart;
    DWORD suffix=size-removeEnd;
    DWORD outSize=prefix+suffix;
    if (outSize+1>=sizeof(g_installVdfOutput)) {
        LogError("[InstallScript修复][警告] 修改后的 install.vdf 超出安全缓冲区；不修改。",true);
        return;
    }
    for (DWORD i=0;i<prefix;++i) g_installVdfOutput[i]=g_installVdfInput[i];
    for (DWORD i=0;i<suffix;++i) g_installVdfOutput[prefix+i]=g_installVdfInput[removeEnd+i];
    g_installVdfOutput[outSize]=0;

    // 备份使用 CopyFileA(..., TRUE)：TRUE 表示“备份已存在就绝不覆盖”。
    // 因此永远保留第一次自动修改前的 Steam 原文件，方便人工审计或恢复。
    if (g_CopyFileA) g_CopyFileA(vdfPath,backupPath,1);

    // 先完整写临时文件，再替换正式文件，避免在 CREATE_ALWAYS 后进程异常导致 install.vdf 半截。
    if (!WriteWholeSmallFileA(tempPath,g_installVdfOutput,outSize)) {
        if (g_DeleteFileA) g_DeleteFileA(tempPath);
        LogError("[InstallScript修复][警告] install.vdf 临时文件写入失败；原文件保持不变。",true);
        return;
    }
    if (!g_MoveFileExA || !g_MoveFileExA(tempPath,vdfPath,MOVEFILE_REPLACE_EXISTING_VALUE|MOVEFILE_WRITE_THROUGH_VALUE)) {
        if (g_DeleteFileA) g_DeleteFileA(tempPath);
        LogError("[InstallScript修复][警告] install.vdf 原子替换失败；原文件保持不变。",true);
        return;
    }

    LogText("[InstallScript修复][成功] 已静默移除 install.vdf 中强制 DPIUNAWARE/VISTARTM 的 AppCompatFlags\\Layers 子块。",true);
    LogDetail("[InstallScript修复][详细] 原始 install.vdf 仅首次备份为 install.vdf.baldrskywin11fix.bak；正常修复过程不弹窗、不自动重启。",true);
}

static bool ApplyVideoOverlayValue(bool fromGuard) {
    LoadConfigOnce();
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
        if (g_Sleep) g_Sleep(OVERLAY_GUARD_INTERVAL_MS);
        else return 0;
        ApplyVideoOverlayValue(true);
        // Overlay 守护线程本来就每隔一段时间醒一次。顺手读取一个 DWORD 计数即可，
        // 不额外创建线程，也不会让像素热路径直接执行日志 I/O。
        LogPixelBoundaryInlineStatsIfChanged();
    }
}

static void StartOverlayGuardOnce() {
    if (g_overlayGuardStarted) return;
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
        AppendDec(b,p,sizeof(b),OVERLAY_GUARD_INTERVAL_MS);
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
    LogText("BALDR SKY Steam Win11 兼容层 v0.1-t14",true);
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

    p=0; AppendText(b,p,sizeof(b),"[配置] 核心修复=SteamFix+VideoOverlayFix+PixelBoundaryFix(永久开启)");
    AppendText(b,p,sizeof(b)," SteamInstallScriptFix="); AppendDec(b,p,sizeof(b),g_cfg.enableSteamInstallScriptFix);
    AppendText(b,p,sizeof(b)," HighDpiFix="); AppendDec(b,p,sizeof(b),g_cfg.enableHighDpiFix);
    AppendText(b,p,sizeof(b)," Backend="); AppendDec(b,p,sizeof(b),g_cfg.backendMode);
    AppendText(b,p,sizeof(b)," ReShade="); AppendDec(b,p,sizeof(b),g_cfg.enableReShade);
    AppendText(b,p,sizeof(b)," EnableLog="); AppendDec(b,p,sizeof(b),g_cfg.enableLog);
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
        LogText("[安全] 当前主程序不是 BaldrSky.exe；不应用 BALDR SKY 专用 SteamFix、InstallScriptFix、VideoOverlayFix、HighDpiFix、PixelBoundaryFix 或 VEH/VCH，只保留 D3D9 后端转发。",true);
        return;
    }

    // Steam 的 install.vdf 在创建 BaldrSky.exe 之前会写入 DPIUNAWARE/VISTARTM。
    // 当前第一次进程如果已经被这些 Shim 创建出来，我们不会为了“修第一轮”而打断游戏；
    // 这里只静默净化 install.vdf 和 HKCU Layers，为下一次及后续启动建立干净环境。
    if (g_cfg.enableSteamInstallScriptFix) RepairSteamInstallScriptAndLayersOnce();
    else LogText("[InstallScript修复] 已由 INI 关闭；不修改 install.vdf 或 AppCompatFlags\\Layers。",true);

    // 高 DPI 必须尽量早于真正的图形后端和后续窗口/设备工作。
    // install.vdf 净化放在它前面只是为了先清理持久化配置；
    // 当前进程的 AppCompat 已经在 CreateProcess 阶段决定，所以第一次仍可能无法改变 DPI。
    ApplyHighDpiFixOnce();

    // SteamFix 是 Steam 版启动必需的核心修复，test13 起永久开启。
    RepairSteamPatchBySignatureOnce();

    // test12：先通过运行时结构特征定位像素混合边界故障点、目标对象栈来源和安全续接，
    // 然后优先安装“异常发生前”的内联边界检查。
    // 这个扫描发生在 Direct3DCreate9 时，此时 BaldrSky.exe 的主代码已经解包到内存。
    InitializePixelBoundaryFixBySignatureOnce();

    // VideoOverlayFix 是高频视频黑屏兼容修复，test13 起永久开启。
    StartOverlayGuardOnce();

    // test14 正常模式同样不需要为 PixelBoundaryFix 依赖 VEH：像素越界在 CPU 执行非法读取之前就被裁掉。
    // 只有两种情况才安装异常处理器：
    //   1) 用户主动打开 CrashDiagnostics；
    //   2) PixelBoundaryFix 已定位成功，但内联补丁安装失败，需要 test11 精确恢复兜底。
    bool needPixelFallback = g_pixelSignatureReady && !g_pixelInlineInstalled;

    // “纯诊断” VEH/VCH 只有在日志真正开启时才有意义。
    // 这样发布默认 EnableLog=0 时没有额外异常处理开销；用户只要改 EnableLog=1，
    // 因为 EnableCrashDiagnostics 默认就是 1，会自动得到完整 AV/栈/寄存器日志。
    bool needGenericCrashDiagnostics = g_cfg.enableLog && g_cfg.enableCrashDiagnostics;

    // PixelBoundaryFix 的应急回退是核心稳定性逻辑，不属于“日志诊断”。
    // 所以即使 EnableLog=0，只要内联补丁安装失败，仍必须安装 VEH/VCH 来执行 test11 的精确恢复。
    if (needGenericCrashDiagnostics || needPixelFallback) InstallVehOnce();
    else LogDetail("[诊断] PixelBoundaryFix 已使用内联边界检查；当前无需安装 VEH/VCH。",true);
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
static HMODULE g_finalBackendTarget=0;
static PFN_Direct3DCreate9 g_realDirect3DCreate9=0;

// ----------------------------------------------------------------------------
// test14：ReShade 链式加载
// ----------------------------------------------------------------------------
//
// 文件布局固定为同目录，不要求子目录：
//
//   BaldrSky.exe
//   d3d9.dll             <- 本兼容层，永远占游戏真正的 d3d9 入口
//   d3d9.ini
//   ReShade32.dll        <- 可选；ReShade 32 位 d3d9.dll 改名
//   ReShade.ini          <- ReShade 自己的配置；我们只管理 [PROXY] 两个键
//   d3d9_backend.dll     <- 可选；例如 DXVK x32
//
// EnableReShade 与 Backend 是两个正交选项：
//
//   Backend 只回答“最终真正执行 D3D9 的 DLL 是谁”：
//     0 = Auto：d3d9_backend.dll 能加载就用它，否则系统 d3d9.dll
//     1 = Native：强制系统 d3d9.dll
//     2 = Custom：强制 d3d9_backend.dll
//
//   EnableReShade 只回答“在本兼容层和最终后端之间，是否插入 ReShade32.dll”：
//     0 = 游戏 -> 本兼容层 -> 最终后端
//     1 = 游戏 -> 本兼容层 -> ReShade32.dll -> 最终后端
//
// ReShade 6.7 起提供官方 wrapper chaining：
//   [PROXY]
//   EnableProxyLibrary=1
//   ProxyLibrary=<下一层 DLL 路径>
//
// 因此这里必须先选好最终后端、再写 ReShade.ini、最后才 LoadLibrary(ReShade32.dll)。
// 如果先加载 ReShade 再写 INI，就太晚了：ReShade 会在自己的 DLL 初始化阶段读取配置。
//
// 我们使用“绝对路径”写 ProxyLibrary，而不是只写 d3d9_backend.dll：
// 1) 自定义后端不受 SetDllDirectory / DLL 搜索顺序污染；
// 2) 系统原生后端不能写成简单的 d3d9.dll，否则 ReShade 可能再次加载游戏目录里的本兼容层，
//    形成 d3d9.dll -> ReShade -> d3d9.dll 的递归链。

static bool ConfigureReShadeProxyIni(const char* finalBackendPath) {
    if (!finalBackendPath || !finalBackendPath[0]) return false;
    if (!g_WritePrivateProfileStringA) {
        LogError("[ReShade][错误] WritePrivateProfileStringA 不可用，无法安全配置 ReShade wrapper chain。",true);
        return false;
    }

    char iniPath[520];
    if (!BuildSiblingFilePath("ReShade.ini",iniPath,(UINT)sizeof(iniPath))) {
        LogError("[ReShade][错误] 无法构造同目录 ReShade.ini 路径。",true);
        return false;
    }

    // 只改这两个键。WritePrivateProfileStringA 会保留用户自己的 GENERAL、INPUT、DEPTH、
    // PRESET 等其他 ReShade 配置，所以不会把已经安装好的 shader/preset 设置冲掉。
    BOOL ok1=g_WritePrivateProfileStringA("PROXY","EnableProxyLibrary","1",iniPath);
    BOOL ok2=g_WritePrivateProfileStringA("PROXY","ProxyLibrary",finalBackendPath,iniPath);
    if (!ok1 || !ok2) {
        char b[760]; UINT p=0;
        AppendText(b,p,sizeof(b),"[ReShade][错误] 写入 ReShade.ini 的 [PROXY] 失败；将不加载 ReShade。路径=");
        AppendText(b,p,sizeof(b),iniPath);
        b[p]=0; LogError(b,true);
        return false;
    }

    char b[980]; UINT p=0;
    AppendText(b,p,sizeof(b),"[ReShade] 已配置 wrapper chain：ReShade.ini=");
    AppendText(b,p,sizeof(b),iniPath);
    AppendText(b,p,sizeof(b)," ProxyLibrary=");
    AppendText(b,p,sizeof(b),finalBackendPath);
    b[p]=0; LogText(b,true);
    return true;
}

// 先只解决“最终后端是谁”，不考虑 ReShade。
// 返回值既用来验证 DLL 确实可加载，也故意保留一个模块引用，避免我们确认后端可用后
// 它又在 ReShade 真正调用之前被卸载。
static HMODULE LoadFinalD3D9Target(char* outPath, UINT cap) {
    if (!outPath || cap<16) return 0;
    outPath[0]=0;

    if (g_cfg.backendMode!=1) {
        char customPath[520]; customPath[0]=0;
        bool haveCustomPath=BuildSiblingFilePath("d3d9_backend.dll",customPath,(UINT)sizeof(customPath));
        HMODULE custom=0;
        if (haveCustomPath) custom=g_LoadLibraryA(customPath);

        if (custom) {
            CopyAsciiLimited(outPath,cap,customPath);
            g_finalBackendTarget=custom;
            LogDetail("[D3D9] 最终后端选择=CUSTOM（同目录 d3d9_backend.dll）。",true);
            return custom;
        }

        if (g_cfg.backendMode==2) {
            char b[760]; UINT p=0;
            AppendText(b,p,sizeof(b),"[D3D9][错误] Backend=2，但同目录 d3d9_backend.dll 无法加载；按配置不回退系统 D3D9。路径=");
            AppendText(b,p,sizeof(b),haveCustomPath?customPath:"<无法构造路径>");
            b[p]=0; LogError(b,true);
            return 0;
        }

        LogDetail("[D3D9] Auto 模式下没有可加载的 d3d9_backend.dll，最终后端改用 Windows 原生 D3D9。",true);
    } else {
        LogText("[D3D9] Backend=1，最终后端强制使用 Windows 原生 D3D9。",true);
    }

    char systemPath[320];
    if (!BuildSystemD3D9Path(systemPath,(UINT)sizeof(systemPath))) {
        LogError("[D3D9][错误] 无法构造 Windows 系统 d3d9.dll 路径。",true);
        return 0;
    }

    HMODULE native=g_LoadLibraryA(systemPath);
    if (!native) {
        LogError("[D3D9][错误] Windows 系统 d3d9.dll 无法加载。",true);
        return 0;
    }

    CopyAsciiLimited(outPath,cap,systemPath);
    g_finalBackendTarget=native;
    return native;
}

static HMODULE LoadSelectedD3D9Backend() {
    if (g_backend) return g_backend;
    LoadConfigOnce();

    // 第一步：完全按照 Backend 语义选出最终真正的 D3D9 实现。
    // 即使 EnableReShade=1，Backend 的含义也不会改变。
    char finalPath[520]; finalPath[0]=0;
    HMODULE finalTarget=LoadFinalD3D9Target(finalPath,(UINT)sizeof(finalPath));
    if (!finalTarget) return 0;

    // 第二步：如果用户没有启用 ReShade，直接保持 test13 起的直连路径。
    if (!g_cfg.enableReShade) {
        g_backend=finalTarget;
        char b[760]; UINT p=0;
        AppendText(b,p,sizeof(b),"[D3D9] ReShade=OFF；直接加载最终后端=");
        AppendText(b,p,sizeof(b),finalPath);
        b[p]=0; LogText(b,true);
        return g_backend;
    }

    // 第三步：用户启用了 ReShade。先确认同目录确实有 ReShade32.dll。
    // 文件不存在时不让游戏因为一个可选画质功能直接启动失败，而是回退原后端。
    char reshadePath[520]; reshadePath[0]=0;
    bool haveReShadePath=BuildSiblingFilePath("ReShade32.dll",reshadePath,(UINT)sizeof(reshadePath));
    if (!haveReShadePath || !g_GetFileAttributesA ||
        g_GetFileAttributesA(reshadePath)==INVALID_FILE_ATTRIBUTES_VALUE) {
        LogError("[ReShade][警告] EnableReShade=1，但同目录没有 ReShade32.dll；已安全回退直接 D3D9 后端。",true);
        g_backend=finalTarget;
        return g_backend;
    }

    // ReShade 必须在 LoadLibrary 之前看到正确的 ProxyLibrary。
    // 如果 INI 写入失败，也直接回退，不留下半条链。
    if (!ConfigureReShadeProxyIni(finalPath)) {
        g_backend=finalTarget;
        return g_backend;
    }

    HMODULE reshade=g_LoadLibraryA(reshadePath);
    if (!reshade) {
        char b[760]; UINT p=0;
        AppendText(b,p,sizeof(b),"[ReShade][警告] ReShade32.dll 加载失败；已安全回退直接 D3D9 后端。路径=");
        AppendText(b,p,sizeof(b),reshadePath);
        b[p]=0; LogError(b,true);
        g_backend=finalTarget;
        return g_backend;
    }

    // 只接受真正导出 Direct3DCreate9 的 32 位 ReShade D3D9 wrapper。
    // 用户如果误放了 64 位 DLL、DXGI 版或其他同名文件，这里会拒绝把它当 D3D9 中间层。
    PFN_Direct3DCreate9 reshadeCreate=
        (PFN_Direct3DCreate9)g_GetProcAddress(reshade,"Direct3DCreate9");
    if (!reshadeCreate) {
        LogError("[ReShade][警告] ReShade32.dll 没有 Direct3DCreate9 导出；已安全回退直接 D3D9 后端。",true);
        g_backend=finalTarget;
        return g_backend;
    }

    g_backend=reshade;
    char b[980]; UINT p=0;
    AppendText(b,p,sizeof(b),"[ReShade][成功] 链式加载已启用：兼容层 -> ReShade32.dll -> ");
    AppendText(b,p,sizeof(b),finalPath);
    b[p]=0; LogText(b,true);
    LogHex("[ReShade][详细] ReShade32.dll 句柄=",Ptr32(reshade),true);
    return g_backend;
}

// d3d9.dll 对游戏唯一需要提供的导出。
// 当前 Steam BaldrSky.exe 静态导入只有 Direct3DCreate9；
// test9 不冒充其他它根本不会调用的接口。
extern "C" void* __stdcall Direct3DCreate9(UINT SDKVersion) {
    // 代理 DLL 模式下，第一次 Direct3DCreate9 是最稳定的“正常初始化点”：
    // 已经离开 DllMain loader lock，但游戏还没有真正开始使用 D3D9 Device。
    InitializeCompatibilityCoreOnce();

    // 某些启动流程会再次调用 Direct3DCreate9。若前一次调用后已经触发过内联裁剪，
    // 这里顺便把累计次数写进日志，帮助确认“没有 AV，但边界修复确实命中过”。
    LogPixelBoundaryInlineStatsIfChanged();

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

    // 仍然坚持 test9 的原则：最终得到的 IDirect3D9 对象原样返回。
// EnableReShade=1 时，这个对象会先经过 ReShade 自己的标准 D3D9 wrapper；
// 本兼容层仍然不修改任何 IDirect3D9 / IDirect3DDevice9 vtable。
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
