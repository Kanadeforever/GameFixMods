/*
   POP1 Chinese patch ASI bridge - configurable edition v8（窗口化 vtable bridge 版）
   ================================================================================
   无需修改任何磁盘文件，通过 IAT Hook + 虚拟文件系统 + D3D9 vtable 桥接，
   实现汉化补丁的运行时注入，兼容优化补丁（dxwrapper/Xidi/DSOAL）的窗口化环境。

   核心功能：
   1. 虚拟 POPData.BF — 内存中提供汉化版 POPData.BF（可选叠加游戏手柄文本覆盖），
      不修改磁盘上任何文件。
   2. 文件系统 IAT Hook — 拦截 CreateFile/ReadFile 等调用，重定向到内存中的
      虚拟 BF 数据和 GBK 字库路径。
   3. D3D9 桥接 — 通过 vtable patch 或 IAT hook，让 out.dll 的字体渲染 D3D9
      wrapper 与 dxwrapper 的窗口化 wrapper 共存。
   4. 安全字幕缩放 — 只修改 out.dll 中字号传递参数和居中宽度计算，不破坏字幕记录表。
   5. DEP/NX 修复 — 确保 out.dll 的代码拷贝页可执行。
   6. 字库路径重定向 — 将 GBK 路径 \\.\字库\*.dds 重定向到 HHGC\Fonts\*.dds。
*/

/* ============================================================
   文件概述
   本文件是一个 ASI（Automatic Script Injector）插件，通过 DllMain
   或 InitializeASI 入口自动加载。核心策略是：
   - 通过 PEB 手动解析 kernel32 导出函数，避免静态导入依赖。
   - 在内存中虚拟汉化版 POPData.BF，通过 IAT Hook 拦截文件操作。
   - 将 GBK 编码的中文字库路径重定向到实际字体文件路径。
   - 通过 D3D9 vtable 桥接让 out.dll 的字体渲染与 dxwrapper 窗口化共存。
   ============================================================ */

/* ============================================================
   类型定义 — 避免依赖 windows.h，无 CRT 环境下手动定义所需类型
   注意：由于当前上下文可能没有标准 C 运行时，所有类型和 API 函数
   均通过手动声明和 PEB 遍历来解析，不依赖任何导入库。
   ============================================================ */
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef long           BOOL;
typedef void*          PVOID;

/* ============================================================
   内核32 API 函数指针类型 — 不依赖导入表，通过 PEB 手动解析
   这些函数指针在 resolve_kernel32_exports() 中被统一初始化。
   使用函数指针而非静态导入的原因：ASI 插件运行在无 CRT 初始化的
   早期上下文中，手动解析可以避免导入表依赖和链接问题。
   ============================================================ */
typedef PVOID (__stdcall *LoadLibraryA_t)(const char* path);
typedef PVOID (__stdcall *GetModuleHandleA_t)(const char* name);
typedef PVOID (__stdcall *GetProcAddress_t)(PVOID module, const char* name);
typedef DWORD (__stdcall *GetModuleFileNameA_t)(PVOID module, char* buf, DWORD size);
typedef DWORD (__stdcall *GetModuleFileNameW_t)(PVOID module, unsigned short* buf, DWORD size);
typedef BOOL  (__stdcall *SetCurrentDirectoryA_t)(const char* path);
typedef BOOL  (__stdcall *SetCurrentDirectoryW_t)(const unsigned short* path);
typedef BOOL  (__stdcall *VirtualProtect_t)(PVOID addr, DWORD size, DWORD newProtect, DWORD* oldProtect);
typedef DWORD (__stdcall *VirtualQuery_t)(PVOID addr, PVOID mbi, DWORD len);
typedef PVOID (__stdcall *CreateThread_t)(PVOID sa, DWORD stack, DWORD (__stdcall *start)(PVOID), PVOID param, DWORD flags, DWORD* tid);
typedef void  (__stdcall *Sleep_t)(DWORD ms);
typedef void  (__stdcall *OutputDebugStringA_t)(const char* text);
typedef DWORD (__stdcall *GetLastError_t)(void);
typedef void  (__stdcall *SetLastError_t)(DWORD err);
typedef PVOID (__stdcall *CreateFileA_t)(const char* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file);
typedef PVOID (__stdcall *CreateFileW_t)(const unsigned short* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file);
typedef BOOL  (__stdcall *ReadFile_t)(PVOID h, PVOID buf, DWORD bytes_to_read, DWORD* bytes_read, PVOID overlapped);
typedef DWORD (__stdcall *SetFilePointer_t)(PVOID h, long dist, long* dist_high, DWORD method);
typedef BOOL  (__stdcall *CloseHandle_t)(PVOID h);
typedef DWORD (__stdcall *GetFileType_t)(PVOID h);
typedef DWORD (__stdcall *GetFileSize_t)(PVOID h, DWORD* high);
typedef PVOID (__stdcall *CreateFileMappingA_t)(PVOID h, PVOID attr, DWORD protect, DWORD max_high, DWORD max_low, const char* name);
typedef PVOID (__stdcall *CreateFileMappingW_t)(PVOID h, PVOID attr, DWORD protect, DWORD max_high, DWORD max_low, const unsigned short* name);
typedef PVOID (__stdcall *MapViewOfFile_t)(PVOID hmap, DWORD access, DWORD off_high, DWORD off_low, DWORD bytes);
typedef BOOL  (__stdcall *UnmapViewOfFile_t)(PVOID addr);
typedef DWORD (__stdcall *GetFileAttributesA_t)(const char* name);
typedef DWORD (__stdcall *GetFileAttributesW_t)(const unsigned short* name);

/* ============================================================
   常量和模拟的 Windows API 宏
   ============================================================ */
#define TRUE 1
#define FALSE 0
#define DLL_PROCESS_ATTACH 1
#define PAGE_NOACCESS          0x01
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD             0x100
#define MEM_COMMIT             0x1000
#define MAX_PATH_CHARS         260
#define INVALID_HANDLE_VALUE_PTR ((PVOID)(-1))
#define GENERIC_WRITE          0x40000000
#define GENERIC_READ           0x80000000
#define FILE_SHARE_READ        0x00000001
#define FILE_SHARE_WRITE       0x00000002
#define OPEN_EXISTING          3
#define FILE_ATTRIBUTE_NORMAL  0x80
#define FILE_TYPE_DISK         0x0001
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#define FILE_BEGIN             0
#define FILE_CURRENT           1
#define FILE_END               2

/* x86 内存基本信息结构体（模拟 windows.h 中的 MEMORY_BASIC_INFORMATION）
   用于 VirtualQuery 的手动调用，判断内存页的状态和保护属性 */
typedef struct _MBI32 {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    DWORD RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MBI32;

/* ============================================================
   全局状态
   注意：所有全局变量在 ASI 加载时即初始化，随后在 DllMain /
   InitializeASI 中被逐步填充。由于 ASI 插件可能在任何时刻被
   加载，这些变量必须通过线程安全的原子操作或一次性初始化来保护。
   ============================================================ */

static volatile long g_started = 0;  /* 修复线程是否已启动 */
static volatile long g_loaded = 0;   /* out.dll 是否已加载 */

/* 游戏根目录路径（ANSI 和 UTF-16 双版本，支持中文路径） */
static char g_game_dir[MAX_PATH_CHARS];
static unsigned short g_game_dir_w[MAX_PATH_CHARS];

/* 内核32 API 函数指针表 — 在 resolve_kernel32_exports() 中统一初始化 */
static LoadLibraryA_t         pLoadLibraryA = 0;
static GetModuleHandleA_t     pGetModuleHandleA = 0;
static GetProcAddress_t       pGetProcAddress = 0;
static GetModuleFileNameA_t   pGetModuleFileNameA = 0;
static GetModuleFileNameW_t   pGetModuleFileNameW = 0;
static SetCurrentDirectoryA_t pSetCurrentDirectoryA = 0;
static SetCurrentDirectoryW_t pSetCurrentDirectoryW = 0;
static VirtualProtect_t       pVirtualProtect = 0;
static VirtualQuery_t         pVirtualQuery = 0;
static CreateThread_t         pCreateThread = 0;
static Sleep_t                pSleep = 0;
static OutputDebugStringA_t   pOutputDebugStringA = 0;
static GetLastError_t         pGetLastError = 0;
static SetLastError_t         pSetLastError = 0;
static CreateFileA_t          pCreateFileA = 0;
static CreateFileW_t          pCreateFileW = 0;
static ReadFile_t             pReadFile = 0;
static SetFilePointer_t       pSetFilePointer = 0;
static CloseHandle_t          pCloseHandle = 0;
static GetFileType_t          pGetFileType = 0;
static GetFileSize_t          pGetFileSize = 0;
static CreateFileMappingA_t   pCreateFileMappingA = 0;
static CreateFileMappingW_t   pCreateFileMappingW = 0;
static MapViewOfFile_t        pMapViewOfFile = 0;
static UnmapViewOfFile_t      pUnmapViewOfFile = 0;
static GetFileAttributesA_t   pGetFileAttributesA = 0;
static GetFileAttributesW_t   pGetFileAttributesW = 0;

/* out.dll 原始 CreateFileA 指针 — 用于 out.dll 自己的路径回退 hook */
static CreateFileA_t          pOutOriginalCreateFileA = 0;

/* ============================================================
   内嵌 POPData.BF 数据 — 从汇编 .incbin 翻译为 C 数组
   由 linker 放置在自定义段（.rdA/.rdB）中，通过地址差计算大小
   ============================================================ */
extern const unsigned char g_popdata_chs_start[];    /* 纯汉化版 POPData.BF */
extern const unsigned char g_popdata_chs_end[];      /* 纯汉化版结束标记 */
extern const unsigned char g_popdata_mixed_start[];  /* 叠加游戏手柄覆盖版 */
extern const unsigned char g_popdata_mixed_end[];    /* 手柄覆盖版结束标记 */

/* ============================================================
   虚拟文件系统（VFILE）— 用魔数地址模拟文件句柄
   VFILE_BASE 范围内的指针视为虚拟文件句柄（即内存中 BF 数据的引用）
   VMAP_BASE 范围内的指针视为虚拟内存映射句柄
   ============================================================ */

#define VFILE_BASE ((DWORD)0x0BADF000)
#define VMAP_BASE  ((DWORD)0x0BAE0000)
#define MAX_VIRTUAL_FILES 8

typedef struct VFILE_STATE_ { DWORD used; DWORD pos; } VFILE_STATE;
static VFILE_STATE g_vfiles[MAX_VIRTUAL_FILES];
static DWORD g_vmaps[MAX_VIRTUAL_FILES];

/* ============================================================
   pop1_chs_compat.ini 配置选项（默认值保守，与已知可工作的手柄覆盖版一致）
   通过 load_config_once() 从 scripts\pop1_chs_compat.ini 读取
   ============================================================ */
static int g_cfg_loaded = 0;
static int g_cfg_enabled = 1;
static int g_cfg_virtualize_popdata = 1;
static int g_cfg_gamepad_overlay = 1;
static int g_cfg_font_redirect = 1;
static int g_cfg_d3d9_fix = 1;

/* D3D9Fix=2 窗口化 vtable 桥接状态
   目标：让 dxwrapper 作为外层窗口化 wrapper，out.dll 作为内层中文字体渲染 wrapper
   通过 patch IDirect3D9 对象的 vtable（替换 CreateDevice 入口）实现共存
   核心数据结构：
   - g_d3d9_next_direct3dcreate9：保存真实的 Direct3DCreate9 函数地址
   - g_patched_d3d9_object：被 patch 的 IDirect3D9 对象
   - g_patched_d3d9_orig_vtbl：原始 vtable 指针
   - g_patched_d3d9_vtbl_clone：克隆的 vtable（CreateDevice 被替换为桥接函数）
   - g_fake_out_d3d9_object：伪造的 out.dll D3D9 对象（含 vtable 和下层对象指针）
   - g_in_create_device_bridge：递归防止标志 */
static PVOID g_d3d9_next_direct3dcreate9 = 0;
static PVOID g_patched_d3d9_object = 0;
static DWORD* g_patched_d3d9_orig_vtbl = 0;
static DWORD  g_patched_d3d9_vtbl_clone[20];
static DWORD  g_fake_out_d3d9_object[2];
static volatile long g_in_create_device_bridge = 0;
static int g_cfg_subtitle_patch = 0;
static int g_cfg_subtitle_scale_percent = 100;
static int g_cfg_subtitle_y_percent = 83;
static char g_cfg_font_folder[128] = "HHGC\\Fonts";
int _fltused = 0;

/* ============================================================
   基础辅助函数
   ============================================================ */

/* 获取 PEB（进程环境块）—— x86 专用，通过 fs 段寄存器访问
   原理：在 x86 Windows 中，fs:[0x30] 始终指向当前线程的 PEB。
   这个地址是获取进程模块链表、加载地址等信息的基础。
   返回：PEB 指针，失败返回 0 */
static PVOID get_peb(void)
{
    PVOID peb;
    __asm {
        mov eax, fs:[0x30]
        mov peb, eax
    }
    return peb;
}

/* 不区分大小写的字符串比较（仅 ASCII 字符集）
   a、b：要比较的两个字符串
   返回：1 表示相等，0 表示不等
   注意：只对 ASCII 字符正确（A-Z 与 a-z），多字节字符直接按字节比较 */
static int ascii_equal(const char* a, const char* b)
{
    while (*a && *b) { if (*a != *b) return 0; ++a; ++b; }
    return (*a == 0 && *b == 0);
}

/* 将大写 ASCII 字母转为小写（用于不区分大小写的路径比较） */
static char lower_a(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static unsigned short lower_w(unsigned short c) { return (c >= 'A' && c <= 'Z') ? (unsigned short)(c + 32) : c; }
/* 判断字符是否为路径分隔符（正斜杠或反斜杠都接受） */
static int is_slash(char c) { return c == '\\' || c == '/'; }
static int is_slash_w(unsigned short c) { return c == '\\' || c == '/'; }

/* ============================================================
   手动 PE 解析 — 不调用 GetProcAddress，直接解析模块的导出表
   find_export：在指定模块的 PE 导出表中按名称搜索函数
   find_loaded_proc：遍历 PEB 模块链表，在所有已加载模块中搜索函数
   ============================================================ */

/* 在指定 PE 模块中按名称查找导出函数地址
   module_base: 模块基址（如 kernel32.dll 的基址）
   wanted_name: 要查找的函数名（ASCII，不区分大小写）
   返回：函数地址，或 0（未找到/前向导出）
   工作原理：
   1. 验证 DOS 头（MZ）和 NT 头（PE\0\0）签名
   2. 从可选头中读取数据目录表的导出表 RVA
   3. 遍历导出名称表，与 wanted_name 逐一比较
   4. 通过序号表找到对应的函数地址 RVA
   注意：前向导出（RVA 落在导出表范围内）视为无效，返回 0 */
static PVOID find_export(PVOID module_base, const char* wanted_name)
{
    BYTE* base = (BYTE*)module_base;
    BYTE* nt;
    BYTE* optional;
    BYTE* exp;
    DWORD export_rva, export_size, number_of_names, address_of_functions, address_of_names, address_of_ordinals;
    DWORD* funcs;
    DWORD* names;
    WORD* ords;
    DWORD i;
    if (!base) return 0;
    if (*(WORD*)(base + 0x00) != 0x5A4D) return 0;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return 0;
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return 0;
    export_rva  = *(DWORD*)(optional + 96);
    export_size = *(DWORD*)(optional + 100);
    if (!export_rva || !export_size) return 0;
    exp = base + export_rva;
    number_of_names      = *(DWORD*)(exp + 24);
    address_of_functions = *(DWORD*)(exp + 28);
    address_of_names     = *(DWORD*)(exp + 32);
    address_of_ordinals  = *(DWORD*)(exp + 36);
    funcs = (DWORD*)(base + address_of_functions);
    names = (DWORD*)(base + address_of_names);
    ords  = (WORD*)(base + address_of_ordinals);
    for (i = 0; i < number_of_names; ++i) {
        const char* name = (const char*)(base + names[i]);
        if (ascii_equal(name, wanted_name)) {
            DWORD rva = funcs[ords[i]];
            if (rva >= export_rva && rva < export_rva + export_size) return 0;
            return (PVOID)(base + rva);
        }
    }
    return 0;
}

/* 在所有已加载模块中搜索指定函数
   proc_name: 要查找的函数名
   返回：第一个匹配的函数地址，或 0
   遍历 PEB->Ldr->InMemoryOrderModuleList，对每个模块调用 find_export */
static PVOID find_loaded_proc(const char* proc_name)
{
    BYTE* peb = (BYTE*)get_peb();
    BYTE* ldr;
    PVOID head, flink;
    if (!peb) return 0;
    ldr = *(BYTE**)(peb + 0x0C);
    if (!ldr) return 0;
    head = (PVOID)(ldr + 0x14);
    flink = *(PVOID*)head;
    while (flink && flink != head) {
        BYTE* entry = (BYTE*)flink;
        PVOID dll_base = *(PVOID*)(entry + 0x10);
        PVOID proc = find_export(dll_base, proc_name);
        if (proc) return proc;
        flink = *(PVOID*)flink;
    }
    return 0;
}

/* 统一初始化 kernel32 API 函数指针表
   通过 find_loaded_proc 逐一查找所有需要的 API，填充全局函数指针。
   每个指针仅在首次调用时解析一次（if-not-null 守卫）。
   注意：这个函数可能在 DllMain 中调用，所以必须只依赖 PEB 遍历，
   不能使用任何可能尚未加载的函数。 */
static void resolve_kernel32_exports(void)
{
    if (!pLoadLibraryA)         pLoadLibraryA         = (LoadLibraryA_t)find_loaded_proc("LoadLibraryA");
    if (!pGetModuleHandleA)     pGetModuleHandleA     = (GetModuleHandleA_t)find_loaded_proc("GetModuleHandleA");
    if (!pGetProcAddress)       pGetProcAddress       = (GetProcAddress_t)find_loaded_proc("GetProcAddress");
    if (!pGetModuleFileNameA)   pGetModuleFileNameA   = (GetModuleFileNameA_t)find_loaded_proc("GetModuleFileNameA");
    if (!pGetModuleFileNameW)   pGetModuleFileNameW   = (GetModuleFileNameW_t)find_loaded_proc("GetModuleFileNameW");
    if (!pSetCurrentDirectoryA) pSetCurrentDirectoryA = (SetCurrentDirectoryA_t)find_loaded_proc("SetCurrentDirectoryA");
    if (!pSetCurrentDirectoryW) pSetCurrentDirectoryW = (SetCurrentDirectoryW_t)find_loaded_proc("SetCurrentDirectoryW");
    if (!pVirtualProtect)       pVirtualProtect       = (VirtualProtect_t)find_loaded_proc("VirtualProtect");
    if (!pVirtualQuery)         pVirtualQuery         = (VirtualQuery_t)find_loaded_proc("VirtualQuery");
    if (!pCreateThread)         pCreateThread         = (CreateThread_t)find_loaded_proc("CreateThread");
    if (!pSleep)                pSleep                = (Sleep_t)find_loaded_proc("Sleep");
    if (!pOutputDebugStringA)   pOutputDebugStringA   = (OutputDebugStringA_t)find_loaded_proc("OutputDebugStringA");
    if (!pGetLastError)         pGetLastError         = (GetLastError_t)find_loaded_proc("GetLastError");
    if (!pSetLastError)         pSetLastError         = (SetLastError_t)find_loaded_proc("SetLastError");
    if (!pCreateFileA)          pCreateFileA          = (CreateFileA_t)find_loaded_proc("CreateFileA");
    if (!pCreateFileW)          pCreateFileW          = (CreateFileW_t)find_loaded_proc("CreateFileW");
    if (!pReadFile)             pReadFile             = (ReadFile_t)find_loaded_proc("ReadFile");
    if (!pSetFilePointer)       pSetFilePointer       = (SetFilePointer_t)find_loaded_proc("SetFilePointer");
    if (!pCloseHandle)          pCloseHandle          = (CloseHandle_t)find_loaded_proc("CloseHandle");
    if (!pGetFileType)          pGetFileType          = (GetFileType_t)find_loaded_proc("GetFileType");
    if (!pGetFileSize)          pGetFileSize          = (GetFileSize_t)find_loaded_proc("GetFileSize");
    if (!pCreateFileMappingA)   pCreateFileMappingA   = (CreateFileMappingA_t)find_loaded_proc("CreateFileMappingA");
    if (!pCreateFileMappingW)   pCreateFileMappingW   = (CreateFileMappingW_t)find_loaded_proc("CreateFileMappingW");
    if (!pMapViewOfFile)        pMapViewOfFile        = (MapViewOfFile_t)find_loaded_proc("MapViewOfFile");
    if (!pUnmapViewOfFile)      pUnmapViewOfFile      = (UnmapViewOfFile_t)find_loaded_proc("UnmapViewOfFile");
    if (!pGetFileAttributesA)   pGetFileAttributesA   = (GetFileAttributesA_t)find_loaded_proc("GetFileAttributesA");
    if (!pGetFileAttributesW)   pGetFileAttributesW   = (GetFileAttributesW_t)find_loaded_proc("GetFileAttributesW");
}

/* 通过 OutputDebugString 输出调试信息（仅在调试器附加时可见） */
static void dbg(const char* s) { if (pOutputDebugStringA) pOutputDebugStringA(s); }

/* 自定义 strlen 实现，避免依赖 CRT */
static int my_strlen(const char* s) { int n = 0; if (!s) return 0; while (s[n]) ++n; return n; }
/* 宽字符版本 strlen */
static int w_strlen(const unsigned short* s) { int n = 0; if (!s) return 0; while (s[n]) ++n; return n; }

/* 截断路径字符串到最后的分隔符位置（即取目录部分）
   例如 "C:\dir\file.txt" -> "C:\dir"
   原地修改输入 buffer */
static void trim_to_dir(char* path)
{
    int i = my_strlen(path) - 1;
    while (i >= 0) { if (is_slash(path[i])) { path[i] = 0; return; } --i; }
    path[0] = 0;
}

/* 宽字符版本的 trim_to_dir */
static void trim_to_dir_w(unsigned short* path)
{
    int i = w_strlen(path) - 1;
    while (i >= 0) { if (is_slash_w(path[i])) { path[i] = 0; return; } --i; }
    path[0] = 0;
}

/* 初始化并缓存游戏根目录路径
   通过 GetModuleFileName 获取本模块（ASI/DLL）的完整路径，
   然后截取到目录部分作为游戏根目录。
   同时保存 ANSI 和 UTF-16 两种格式，支持中文路径。
   后续所有路径重定向都基于这个根目录。 */
static void init_game_dir(void)
{
    /* 如果已缓存过路径，直接设置当前目录后返回 */
    if (g_game_dir[0] || g_game_dir_w[0]) {
        if (g_game_dir_w[0] && pSetCurrentDirectoryW) pSetCurrentDirectoryW(g_game_dir_w);
        else if (g_game_dir[0] && pSetCurrentDirectoryA) pSetCurrentDirectoryA(g_game_dir);
        return;
    }
    if (pGetModuleFileNameW && pGetModuleFileNameW(0, g_game_dir_w, MAX_PATH_CHARS) > 0) {
        trim_to_dir_w(g_game_dir_w);
        if (g_game_dir_w[0] && pSetCurrentDirectoryW) pSetCurrentDirectoryW(g_game_dir_w);
    }
    if (pGetModuleFileNameA && pGetModuleFileNameA(0, g_game_dir, MAX_PATH_CHARS) > 0) {
        trim_to_dir(g_game_dir);
        if (!g_game_dir_w[0] && g_game_dir[0] && pSetCurrentDirectoryA) pSetCurrentDirectoryA(g_game_dir);
    }
}


/* 不区分大小写比较前缀：检查 a 是否以 b 开头（仅 ASCII） */
static int ci_equal_prefix(const char* a, const char* b)
{
    while (*b) {
        if (lower_a(*a) != lower_a(*b)) return 0;
        ++a; ++b;
    }
    return 1;
}

/* 跳过字符串前导的空白字符（空格和制表符） */
static char* skip_spaces(char* p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

/* 在 INI 文件缓冲区中查找指定键的值
   buf：已读取的 INI 文件内容
   key：要查找的键名（不区分大小写）
   返回：指向键对应的值字符串的指针，或 0（未找到）
   解析规则：
   - 跳过注释行（以 ; 或 # 开头）和节标题（以 [ 开头）
   - 格式为 key = value，= 前后允许空白
   - 返回值指向 = 后的第一个非空白字符 */
static char* find_ini_value(char* buf, const char* key)
{
    char* p = buf;
    int keylen = my_strlen(key);
    while (*p) {
        char* line = skip_spaces(p);
        char* q;
        if (*line != ';' && *line != '#' && *line != '[' && ci_equal_prefix(line, key)) {
            q = line + keylen;
            q = skip_spaces(q);
            if (*q == '=') {
                ++q;
                q = skip_spaces(q);
                return q;
            }
        }
        while (*p && *p != '\n' && *p != '\r') ++p;
        while (*p == '\n' || *p == '\r') ++p;
    }
    return 0;
}

/* 解析 INI 配置中的整数值
   v：要解析的字符串，def：解析失败时的默认值
   支持可选的负号前缀，如 "-123" */
static int parse_int_value(char* v, int def)
{
    int sign = 1;
    int n = 0;
    int got = 0;
    if (!v) return def;
    v = skip_spaces(v);
    if (*v == '-') { sign = -1; ++v; }
    while (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); ++v; got = 1; }
    return got ? n * sign : def;
}

/* 解析 INI 配置中的布尔值
   接受：1/0, true/false, yes/no, on/off
   不匹配时返回默认值 */
static int parse_bool_value(char* v, int def)
{
    if (!v) return def;
    v = skip_spaces(v);
    if (*v == '1') return 1;
    if (*v == '0') return 0;
    if (ci_equal_prefix(v, "true") || ci_equal_prefix(v, "yes") || ci_equal_prefix(v, "on")) return 1;
    if (ci_equal_prefix(v, "false") || ci_equal_prefix(v, "no") || ci_equal_prefix(v, "off")) return 0;
    return def;
}

/* 解析 INI 配置中的字符串值（去除首尾空白，支持双引号包裹） */
static void parse_string_value(char* v, char* dst, DWORD cap)
{
    DWORD i = 0;
    if (!v || !dst || cap < 2) return;
    v = skip_spaces(v);
    if (*v == '"') ++v;
    while (*v && *v != '\r' && *v != '\n' && *v != ';' && *v != '#') {
        if (*v == '"') break;
        if (i + 1 < cap) dst[i++] = *v;
        ++v;
    }
    while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '\t')) --i;
    dst[i] = 0;
}

/* 将配置参数限制在安全范围内，防止用户设置极端值导致异常 */
static void clamp_config(void)
{
    if (g_cfg_d3d9_fix < 0) g_cfg_d3d9_fix = 0;
    if (g_cfg_d3d9_fix > 2) g_cfg_d3d9_fix = 2;
    if (g_cfg_subtitle_scale_percent < 25) g_cfg_subtitle_scale_percent = 25;
    if (g_cfg_subtitle_scale_percent > 300) g_cfg_subtitle_scale_percent = 300;
    if (g_cfg_subtitle_y_percent < 40) g_cfg_subtitle_y_percent = 40;
    if (g_cfg_subtitle_y_percent > 98) g_cfg_subtitle_y_percent = 98;
    if (!g_cfg_font_folder[0]) {
        g_cfg_font_folder[0] = 'H'; g_cfg_font_folder[1] = 'H'; g_cfg_font_folder[2] = 'G'; g_cfg_font_folder[3] = 'C';
        g_cfg_font_folder[4] = '\\'; g_cfg_font_folder[5] = 'F'; g_cfg_font_folder[6] = 'o'; g_cfg_font_folder[7] = 'n';
        g_cfg_font_folder[8] = 't'; g_cfg_font_folder[9] = 's'; g_cfg_font_folder[10] = 0;
    }
}

/* 从 scripts\pop1_chs_compat.ini（或根目录）加载配置
   使用静态缓冲区，仅执行一次（g_cfg_loaded 守卫）。
   先尝试 scripts\ 子目录，再尝试根目录。
   读取后解析所有配置项并钳制到安全范围。 */
static void load_config_once(void)
{
    static char buf[4096];
    PVOID h;
    DWORD br = 0;
    if (g_cfg_loaded) return;
    g_cfg_loaded = 1;
    /* 在读取 INI 之前必须先解析 kernel32 API，因为 createFile/ReadFile 都需要 */

    resolve_kernel32_exports();
    init_game_dir();
    if (!pCreateFileA || !pReadFile || !pCloseHandle) { clamp_config(); return; }

    h = pCreateFileA("scripts\\pop1_chs_compat.ini", GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE_PTR) h = pCreateFileA("pop1_chs_compat.ini", GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE_VALUE_PTR) {
        if (pReadFile(h, buf, sizeof(buf)-1, &br, 0)) buf[br < sizeof(buf) ? br : sizeof(buf)-1] = 0;
        pCloseHandle(h);
        if (br) {
            g_cfg_enabled = parse_bool_value(find_ini_value(buf, "Enabled"), g_cfg_enabled);
            g_cfg_virtualize_popdata = parse_bool_value(find_ini_value(buf, "VirtualizePOPData"), g_cfg_virtualize_popdata);
            g_cfg_gamepad_overlay = parse_bool_value(find_ini_value(buf, "GamepadOverlay"), g_cfg_gamepad_overlay);
            g_cfg_font_redirect = parse_bool_value(find_ini_value(buf, "FontRedirect"), g_cfg_font_redirect);
            g_cfg_d3d9_fix = parse_int_value(find_ini_value(buf, "D3D9Fix"), g_cfg_d3d9_fix);
            g_cfg_subtitle_patch = parse_bool_value(find_ini_value(buf, "SubtitlePatch"), g_cfg_subtitle_patch);
            g_cfg_subtitle_scale_percent = parse_int_value(find_ini_value(buf, "SubtitleScalePercent"), g_cfg_subtitle_scale_percent);
            g_cfg_subtitle_y_percent = parse_int_value(find_ini_value(buf, "SubtitleYPercent"), g_cfg_subtitle_y_percent);
            parse_string_value(find_ini_value(buf, "FontFolder"), g_cfg_font_folder, sizeof(g_cfg_font_folder));
        }
    }
    clamp_config();
}

/* 安全地写入受保护内存页面中的单个字节
   临时将目标页设置为可读写执行，写入后恢复原始保护属性
   用于 out.dll 的运行时补丁 */
static void write_protected_byte(BYTE* addr, BYTE val)
{
    DWORD oldp = 0;
    if (!pVirtualProtect || !addr) return;
    if (pVirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &oldp)) {
        *addr = val;
        pVirtualProtect(addr, 1, oldp, &oldp);
    }
}

/* 安全地写入受保护内存页面中的 4 字节 DWORD */
static void write_protected_dword(BYTE* addr, DWORD val)
{
    DWORD oldp = 0;
    if (!pVirtualProtect || !addr) return;
    if (pVirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldp)) {
        *(DWORD*)addr = val;
        pVirtualProtect(addr, 4, oldp, &oldp);
    }
}

/* 应用字幕缩放补丁到 out.dll
   通过修 out.dll 中固定的机器码位置来改变渲染参数：
   - 修改字体大小传递参数（offset 0x3D64 和 0x3D9C）
   - 修改字幕垂直位置参数（offset 0x233F4）
   关键安全约束：不修改 RVA 0x3CC8，那里存储的是字幕记录表步长，
   而非字体大小，修改会破坏字幕解析 */
static void apply_subtitle_patches(void)
{
    BYTE* out;
    int size;
    float y;
    DWORD yraw;
    if (!g_cfg_enabled || !g_cfg_subtitle_patch || !pGetModuleHandleA) return;
    out = (BYTE*)pGetModuleHandleA("out.dll");
    if (!out) out = (BYTE*)pGetModuleHandleA("OUT.DLL");
    if (!out) return;

    size = (36 * g_cfg_subtitle_scale_percent + 50) / 100;
    if (size < 8) size = 8;
    if (size > 120) size = 120;

    /* Safe subtitle scaling for this out.dll build.

       Important: do NOT patch RVA 0x3CC8.  That 0x24 is the stride of
       out.dll's subtitle timing/record table, not a font size.  Patching it
       makes out.dll read the wrong subtitle entry and can hide subtitles.

       The actual draw call at RVA 0x3D9B pushes 0x24 as the font height passed
       into the bitmap-font renderer at RVA 0x4DE0.  RVA 0x3D62 uses the same
       0x24 only for center-alignment width calculation. */
    if (out[0x3D62] == 0x6B && out[0x3D63] == 0xC0 && out[0x3D64] == 0x24)
        write_protected_byte(out + 0x3D64, (BYTE)size);
    if (out[0x3D9B] == 0x6A && out[0x3D9C] == 0x24)
        write_protected_byte(out + 0x3D9C, (BYTE)size);

    /* Default is 83, matching out.dll's original 0.83 screen-height subtitle baseline. */
    y = ((float)g_cfg_subtitle_y_percent) / 100.0f;
    yraw = *(DWORD*)&y;
    write_protected_dword(out + 0x233F4, yraw);
}

/* 跳过路径开头的 "./" 和 ".\" 前缀以及前导分隔符 */
static const char* skip_dot_slash(const char* p)
{
    while (p && p[0] == '.' && is_slash(p[1])) p += 2;
    while (p && is_slash(p[0])) ++p;
    return p;
}

/* 判断路径是否为绝对路径（支持 UNC 路径如 \\server\share 和盘符路径如 C:\） */
static int is_abs_path(const char* p)
{
    if (!p || !p[0]) return 0;
    if (is_slash(p[0]) && is_slash(p[1])) return 1;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') return 1;
    return 0;
}

/* 将相对路径拼接到游戏根目录后形成完整路径
   dst：输出缓冲区，cap：缓冲区大小，rel：相对路径
   返回：1 表示成功拼接，0 表示失败（绝对路径或不合法输入）
   如果 rel 已是绝对路径，直接返回失败 */
static int build_game_path(char* dst, DWORD cap, const char* rel)
{
    DWORD i = 0, j = 0;
    const char* r;
    if (!dst || cap < 4 || !g_game_dir[0] || !rel || !rel[0] || is_abs_path(rel)) return 0;
    r = skip_dot_slash(rel);
    while (g_game_dir[i] && i + 1 < cap) { dst[i] = g_game_dir[i]; ++i; }
    if (i && !is_slash(dst[i-1]) && i + 1 < cap) dst[i++] = '\\';
    while (r[j] && i + 1 < cap) { dst[i++] = r[j++]; }
    dst[i] = 0;
    return r[0] != 0;
}

/* 检查指针是否指向可读的内存区域（防止因访问非法地址而崩溃）
   p：目标指针，min_size：要求的最小可读字节数
   通过 VirtualQuery 检查内存页的状态和保护属性 */
static int readable_ptr(PVOID p, DWORD min_size)
{
    MBI32 mbi;
    if (!pVirtualQuery || !p) return 0;
    if (!pVirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & PAGE_GUARD) return 0;
    if ((mbi.Protect & 0xFF) == PAGE_NOACCESS) return 0;
    if (min_size && (DWORD)((BYTE*)p - (BYTE*)mbi.BaseAddress) + min_size > mbi.RegionSize) return 0;
    return 1;
}

/* 将指定内存区域设置为可读写执行（用于 out.dll 代码拷贝页的 DEP 修复）
   base：起始地址，size：区域大小
   只有合理的范围（>=0x100 且 <=64MB）才会被执行 */
static int make_executable(PVOID base, DWORD size)
{
    DWORD oldp = 0;
    if (!pVirtualProtect || !base || size < 0x100 || size > 0x04000000) return 0;
    if (!readable_ptr(base, 16)) return 0;
    return pVirtualProtect(base, size, PAGE_EXECUTE_READWRITE, &oldp) ? 1 : 0;
}

/* 将包含指定地址的内存页设置为可执行（如果当前不可执行）
   用于修复 out.dll 中代码拷贝页的 DEP 保护，确保跳转目标可执行 */
static int protect_region_containing(PVOID p)
{
    MBI32 mbi;
    DWORD oldp = 0;
    DWORD prot;
    if (!pVirtualQuery || !pVirtualProtect || !p) return 0;
    if (!pVirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    prot = mbi.Protect & 0xFF;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || prot == PAGE_NOACCESS) return 0;
    if (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY) return 0;
    return pVirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &oldp) ? 1 : 0;
}

/* 使 out.dll 中某个 Hook 对象的代码拷贝页可执行
   对象结构为：[vtable_ptr, info_ptr]，
   info 结构体中 offset+4 为代码拷贝地址，offset+0x0C 为大小
   这是因为 out.dll 使用了 VirtualAlloc 分配代码拷贝，但默认不含执行权限 */
static int protect_out_hook_object(PVOID obj)
{
    BYTE* info;
    PVOID copy_base;
    DWORD copy_size;
    if (!readable_ptr(obj, 8)) return 0;
    info = *(BYTE**)((BYTE*)obj + 4);
    if (!readable_ptr(info, 0x10)) return 0;
    copy_base = *(PVOID*)(info + 4);
    copy_size = *(DWORD*)(info + 0x0C);
    return make_executable(copy_base, copy_size);
}

/* 遍历 out.dll 中所有已知的 Hook 对象，修复它们的 DEP/NX 问题
   从 out.dll 的固定偏移（0x2B144/0x2B14C/0x2B148/0x2B150）读取
   对象指针，然后逐一点亮执行权限 */
static int protect_out_dll_copies(void)
{
    BYTE* out;
    int changed = 0;
    if (!pGetModuleHandleA) return 0;
    out = (BYTE*)pGetModuleHandleA("out.dll");
    if (!out) out = (BYTE*)pGetModuleHandleA("OUT.DLL");
    if (!out) return 0;
    if (readable_ptr(out + 0x2B144, 4)) changed += protect_out_hook_object(*(PVOID*)(out + 0x2B144));
    if (readable_ptr(out + 0x2B14C, 4)) changed += protect_out_hook_object(*(PVOID*)(out + 0x2B14C));
    if (readable_ptr(out + 0x2B148, 4)) changed += protect_region_containing(*(PVOID*)(out + 0x2B148));
    if (readable_ptr(out + 0x2B150, 4)) changed += protect_region_containing(*(PVOID*)(out + 0x2B150));
    return changed;
}

/* 根据配置选择当前使用的 POPData 版本
   如果 g_cfg_gamepad_overlay 为 1，使用叠加了游戏手柄文本覆盖的版本 */
static const BYTE* active_popdata_start(void)
{
    if (g_cfg_gamepad_overlay) return g_popdata_mixed_start;
    return g_popdata_chs_start;
}

/* 当前激活的 POPData 结束标记 */
static const BYTE* active_popdata_end(void)
{
    if (g_cfg_gamepad_overlay) return g_popdata_mixed_end;
    return g_popdata_chs_end;
}

/* 当前激活的 POPData 大小（字节） */
static DWORD popdata_size(void) { return (DWORD)(active_popdata_end() - active_popdata_start()); }

/* 自定义内存拷贝（避免依赖 CRT 的 memcpy） */
static void my_memcopy(BYTE* dst, const BYTE* src, DWORD n)
{
    DWORD i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}

/* 判断 ASCII 文件名是否以 "popdata.bf" 结尾（不区分大小写）
   用于拦截所有对 POPData.BF 的文件操作请求 */
static int ends_with_popdata_A(const char* p)
{
    const char* s;
    const char* last;
    const char tail[] = "popdata.bf";
    int i;
    if (!p) return 0;
    s = skip_dot_slash(p);
    last = s;
    while (*s) { if (is_slash(*s) || *s == ':') last = s + 1; ++s; }
    for (i = 0; tail[i]; ++i) { if (!last[i] || lower_a(last[i]) != tail[i]) return 0; }
    return last[i] == 0;
}

/* 宽字符版本的 ends_with_popdata（用于 Unicode API 路径拦截） */
static int ends_with_popdata_W(const unsigned short* p)
{
    const unsigned short* s;
    const unsigned short* last;
    const char tail[] = "popdata.bf";
    int i;
    if (!p) return 0;
    s = p; last = s;
    while (*s) { if (*s == '\\' || *s == '/' || *s == ':') last = s + 1; ++s; }
    for (i = 0; tail[i]; ++i) { if (!last[i] || lower_w(last[i]) != (unsigned short)tail[i]) return 0; }
    return last[i] == 0;
}

/* 分配一个虚拟文件句柄（本质是一个魔数范围内的指针）
   用于标识内存中的 POPData 数据，替代真实的文件句柄
   返回：虚拟句柄，或 INVALID_HANDLE_VALUE_PTR（已用完） */
static PVOID alloc_vfile(void)
{
    int i;
    for (i = 0; i < MAX_VIRTUAL_FILES; ++i) {
        if (!g_vfiles[i].used) { g_vfiles[i].used = 1; g_vfiles[i].pos = 0; return (PVOID)(VFILE_BASE + (DWORD)(i * 4)); }
    }
    return INVALID_HANDLE_VALUE_PTR;
}

/* 将虚拟文件句柄转换为 g_vfiles 数组的索引
   通过检查魔数范围和对齐来验证句柄合法性 */
static int vfile_slot(PVOID h)
{
    DWORD v = (DWORD)h;
    if (v < VFILE_BASE || v >= VFILE_BASE + MAX_VIRTUAL_FILES * 4) return -1;
    if ((v - VFILE_BASE) & 3) return -1;
    v = (v - VFILE_BASE) / 4;
    if (!g_vfiles[v].used) return -1;
    return (int)v;
}

/* 分配虚拟内存映射句柄（用于模拟 CreateFileMapping 返回的句柄）
   file_slot：关联的虚拟文件槽索引 */
static PVOID alloc_vmap(int file_slot)
{
    int i;
    for (i = 0; i < MAX_VIRTUAL_FILES; ++i) {
        if (!g_vmaps[i]) { g_vmaps[i] = (DWORD)(file_slot + 1); return (PVOID)(VMAP_BASE + (DWORD)(i * 4)); }
    }
    return 0;
}

/* 将虚拟内存映射句柄转换为 g_vmaps 数组的索引 */
static int vmap_slot(PVOID h)
{
    DWORD v = (DWORD)h;
    if (v < VMAP_BASE || v >= VMAP_BASE + MAX_VIRTUAL_FILES * 4) return -1;
    if ((v - VMAP_BASE) & 3) return -1;
    v = (v - VMAP_BASE) / 4;
    if (!g_vmaps[v]) return -1;
    return (int)v;
}

/* 判断指针是否指向任意一个 POPData 数据块的内存范围
   用于 UnmapViewOfFile 等操作中识别需要特殊处理的虚拟映射 */
static int ptr_inside_popdata(PVOID p)
{
    DWORD a = (DWORD)p;
    DWORD s1 = (DWORD)g_popdata_chs_start;
    DWORD e1 = (DWORD)g_popdata_chs_end;
    DWORD s2 = (DWORD)g_popdata_mixed_start;
    DWORD e2 = (DWORD)g_popdata_mixed_end;
    return (a >= s1 && a < e1) || (a >= s2 && a < e2);
}

/* 检查文件名是否为 POPData.BF，如果是则返回虚拟文件句柄
   这是虚拟文件系统的入口点：将磁盘文件请求转换为内存数据访问
   写操作（GENERIC_WRITE）直接放行，不做虚拟化 */
static PVOID maybe_virtual_popdata_A(const char* name, DWORD access)
{
    if (g_cfg_enabled && g_cfg_virtualize_popdata && ends_with_popdata_A(name) && !(access & GENERIC_WRITE)) {
        dbg("[pop1_chs_compat] virtualized POPData.BF\n");
        return alloc_vfile();
    }
    return 0;
}

/* 宽字符版本的 maybe_virtual_popdata */
static PVOID maybe_virtual_popdata_W(const unsigned short* name, DWORD access)
{
    if (g_cfg_enabled && g_cfg_virtualize_popdata && ends_with_popdata_W(name) && !(access & GENERIC_WRITE)) {
        dbg("[pop1_chs_compat] virtualized POPData.BF (W)\n");
        return alloc_vfile();
    }
    return 0;
}

/* 在 GBK 编码的路径字符串中搜索中文字库路径前缀 "\字库\"
   out.dll 使用 GBK 双字节编码的中文字符来访问字库目录，
   GBK 编码中 "字库" = 0xD7D6 0xBFE2
   返回：匹配到 "字库\" 后的文件名部分（如 "font_c_12.dds"），或 0 */
static const char* find_gbk_ziku_tail_A(const char* name)
{
    const unsigned char* p = (const unsigned char*)name;
    if (!p) return 0;
    while (p[0]) {
        if (p[0] == 0xD7 && p[1] == 0xD6 && p[2] == 0xBF && p[3] == 0xE2) {
            const unsigned char* q = p + 4;
            if (*q == '\\' || *q == '/') ++q;
            return (const char*)q;
        }
        ++p;
    }
    return 0;
}

/* 判断文件名是否为字体资源（ascii.dds 或 font_c_N.dds 格式）
   用于识别 out.dll 中需要路径重定向的字体文件 */
static int is_font_resource_tail_A(const char* tail)
{
    int i, j;
    const char ascii_name[] = "ascii.dds";
    const char prefix[] = "font_c_";
    const char suffix[] = ".dds";
    if (!tail || !tail[0]) return 0;
    for (i = 0; ascii_name[i]; ++i) { if (lower_a(tail[i]) != ascii_name[i]) break; }
    if (!ascii_name[i] && tail[i] == 0) return 1;
    for (i = 0; prefix[i]; ++i) { if (lower_a(tail[i]) != prefix[i]) return 0; }
    if (tail[i] < '0' || tail[i] > '9') return 0;
    while (tail[i] >= '0' && tail[i] <= '9') ++i;
    for (j = 0; suffix[j]; ++j, ++i) { if (lower_a(tail[i]) != suffix[j]) return 0; }
    return tail[i] == 0;
}

/* 将 ASCII 字符串追加到宽字符缓冲区（用于拼接路径） */
static int append_ascii_w(unsigned short* dst, DWORD cap, DWORD* pi, const char* text)
{
    DWORD i;
    if (!dst || !pi || !text) return 0;
    for (i = 0; text[i]; ++i) { if (*pi + 1 >= cap) return 0; dst[(*pi)++] = (unsigned short)(unsigned char)text[i]; }
    dst[*pi] = 0;
    return 1;
}

/* 将字体文件名（ASCII）追加到宽字符路径缓冲区
   要求 tail 中不包含非 ASCII 字符 */
static int append_font_tail_w(unsigned short* dst, DWORD cap, DWORD* pi, const char* tail)
{
    DWORD j = 0;
    if (!dst || !pi || !tail || !tail[0]) return 0;
    while (tail[j] && *pi + 1 < cap) { unsigned char c = (unsigned char)tail[j++]; if (c >= 0x80) return 0; dst[(*pi)++] = (unsigned short)c; }
    if (tail[j] != 0) return 0;
    dst[*pi] = 0;
    return 1;
}

/* 将 GBK 编码的 "字库" 路径重定向到 HHGC\Fonts\ 目录（宽字符版本）
   例如：\.\字库\font_c_0.dds -> D:\POP\HHGC\Fonts\font_c_0.dds
   这是字库路径重定向的核心函数，让 out.dll 从指定字体目录加载 */
static int build_font_w_path_hhgc_fonts_from_gbk_A(unsigned short* dst, DWORD cap, const char* name)
{
    const char* tail = find_gbk_ziku_tail_A(name);
    DWORD i = 0;
    if (!dst || cap < 32 || !tail || !is_font_resource_tail_A(tail) || !g_game_dir_w[0] || !g_cfg_font_redirect) return 0;
    while (g_game_dir_w[i] && i + 1 < cap) { dst[i] = g_game_dir_w[i]; ++i; }
    if (i && !is_slash_w(dst[i-1]) && i + 1 < cap) dst[i++] = '\\';
    if (!append_ascii_w(dst, cap, &i, g_cfg_font_folder)) return 0;
    if (i && !is_slash_w(dst[i-1]) && i + 1 < cap) dst[i++] = '\\';
    return append_font_tail_w(dst, cap, &i, tail);
}

/* 将 GBK 编码的 "字库" 路径重定向到 字库\ 目录
   这是备用方案：如果 HHGC\Fonts 不存在，回退到游戏根目录下的 字库\ 文件夹 */
static int build_font_w_path_ziku_from_gbk_A(unsigned short* dst, DWORD cap, const char* name)
{
    const char* tail = find_gbk_ziku_tail_A(name);
    DWORD i = 0;
    if (!dst || cap < 16 || !tail || !is_font_resource_tail_A(tail) || !g_game_dir_w[0]) return 0;
    while (g_game_dir_w[i] && i + 1 < cap) { dst[i] = g_game_dir_w[i]; ++i; }
    if (i && !is_slash_w(dst[i-1]) && i + 1 < cap) dst[i++] = '\\';
    if (i + 4 >= cap) return 0;
    dst[i++] = 0x5B57; dst[i++] = 0x5E93; dst[i++] = '\\';
    return append_font_tail_w(dst, cap, &i, tail);
}

/* 尝试用宽字符 API 打开 GBK 路径中引用的字体文件
   先尝试 HHGC\Fonts 目录，失败后再尝试 字库\ 目录
   这是 CreateFile 桥接钩子中处理中文字库路径的核心逻辑 */
static PVOID try_open_gbk_font_with_CreateFileW(const char* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file)
{
    unsigned short wpath[MAX_PATH_CHARS];
    PVOID h;
    if (!pCreateFileW) return 0;
    init_game_dir();
    if (build_font_w_path_hhgc_fonts_from_gbk_A(wpath, MAX_PATH_CHARS, name)) {
        h = pCreateFileW(wpath, access, share, sec, creation, flags, template_file);
        if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    }
    if (build_font_w_path_ziku_from_gbk_A(wpath, MAX_PATH_CHARS, name)) {
        h = pCreateFileW(wpath, access, share, sec, creation, flags, template_file);
        if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    }
    return 0;
}

/* ============================================================
   文件系统 IAT 桥接函数
   这些函数替换目标模块 IAT 中的原始 kernel32 函数入口，
   实现透明拦截：先检查是否为虚拟 POPData 或字库路径重定向，
   如果不是则放行到原始 API。
   ============================================================ */

/* CreateFileA 桥接钩子
   拦截流程：
   1. 检查是否为 POPData.BF -> 返回虚拟句柄
   2. 尝试用原始 API 打开（支持中文路径编码问题）
   3. 如果失败，尝试将 GBK 字库路径重定向到 HHGC\Fonts
   4. 如果仍是相对路径，拼接游戏根目录后再试 */
static PVOID __stdcall bridge_CreateFileA_hook(const char* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file)
{
    PVOID vh = maybe_virtual_popdata_A(name, access);
    PVOID h;
    DWORD err = 0;
    char fixed[MAX_PATH_CHARS];
    if (vh) return vh;
    if (!pCreateFileA) return INVALID_HANDLE_VALUE_PTR;
    h = pCreateFileA(name, access, share, sec, creation, flags, template_file);
    if (h != INVALID_HANDLE_VALUE_PTR) return h;
    /* 原始 CreateFileA 失败：保存错误码，尝试字库路径重定向 */
    if (pGetLastError) err = pGetLastError();
    h = try_open_gbk_font_with_CreateFileW(name, access, share, sec, creation, flags, template_file);
    if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    /* 路径是相对路径：拼接游戏根目录后重试，同时再次尝试字库路径和 POPData 虚拟化 */
    if (build_game_path(fixed, MAX_PATH_CHARS, name)) {
        vh = maybe_virtual_popdata_A(fixed, access);
        if (vh) return vh;
        h = pCreateFileA(fixed, access, share, sec, creation, flags, template_file);
        if (h != INVALID_HANDLE_VALUE_PTR) return h;
        h = try_open_gbk_font_with_CreateFileW(fixed, access, share, sec, creation, flags, template_file);
        if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    }
    /* 所有尝试都失败：恢复原始错误码并返回失败 */
    if (pSetLastError) pSetLastError(err);
    return INVALID_HANDLE_VALUE_PTR;
}

/* CreateFileW 桥接钩子（宽字符版本）
   主要处理 POPData.BF 的虚拟化，字库路径重定向由 ASCII 版本完成 */
static PVOID __stdcall bridge_CreateFileW_hook(const unsigned short* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file)
{
    PVOID vh = maybe_virtual_popdata_W(name, access);
    if (vh) return vh;
    if (!pCreateFileW) return INVALID_HANDLE_VALUE_PTR;
    return pCreateFileW(name, access, share, sec, creation, flags, template_file);
}

/* ReadFile 桥接钩子
   如果句柄是虚拟文件，则从内存中的 POPData 拷贝数据；否则调用原始 ReadFile。
   这是虚拟文件系统的核心数据通路。 */
static BOOL __stdcall bridge_ReadFile_hook(PVOID h, PVOID buf, DWORD bytes_to_read, DWORD* bytes_read, PVOID overlapped)
{
    int slot = vfile_slot(h);
    if (slot >= 0) {
        DWORD size = popdata_size();
        DWORD left, n;
        if (bytes_read) *bytes_read = 0;
        if (!buf) return FALSE;
        if (g_vfiles[slot].pos > size) g_vfiles[slot].pos = size;
        left = size - g_vfiles[slot].pos;
        n = bytes_to_read < left ? bytes_to_read : left;
        if (n) my_memcopy((BYTE*)buf, active_popdata_start() + g_vfiles[slot].pos, n);
        g_vfiles[slot].pos += n;
        if (bytes_read) *bytes_read = n;
        return TRUE;
    }
    if (!pReadFile) return FALSE;
    return pReadFile(h, buf, bytes_to_read, bytes_read, overlapped);
}

/* SetFilePointer 桥接钩子
   模拟虚拟文件的指针移动（支持 FILE_BEGIN/FILE_CURRENT/FILE_END）
   超出文件范围时截断到边界 */
static DWORD __stdcall bridge_SetFilePointer_hook(PVOID h, long dist, long* dist_high, DWORD method)
{
    int slot = vfile_slot(h);
    if (slot >= 0) {
        long base, np;
        DWORD size = popdata_size();
        if (method == FILE_CURRENT) base = (long)g_vfiles[slot].pos;
        else if (method == FILE_END) base = (long)size;
        else base = 0;
        if (dist_high && *dist_high != 0) { if (pSetLastError) pSetLastError(0x57); return 0xFFFFFFFF; }
        np = base + dist;
        if (np < 0) np = 0;
        if ((DWORD)np > size) np = (long)size;
        g_vfiles[slot].pos = (DWORD)np;
        if (dist_high) *dist_high = 0;
        return g_vfiles[slot].pos;
    }
    if (!pSetFilePointer) return 0xFFFFFFFF;
    return pSetFilePointer(h, dist, dist_high, method);
}

/* CloseHandle 桥接钩子
   清理虚拟文件或虚拟映射槽位，如果是真实句柄则调用原始 CloseHandle */
static BOOL __stdcall bridge_CloseHandle_hook(PVOID h)
{
    int slot = vfile_slot(h);
    int ms = vmap_slot(h);
    if (slot >= 0) { g_vfiles[slot].used = 0; g_vfiles[slot].pos = 0; return TRUE; }
    if (ms >= 0) { g_vmaps[ms] = 0; return TRUE; }
    if (!pCloseHandle) return FALSE;
    return pCloseHandle(h);
}

/* GetFileType 桥接钩子
   虚拟文件始终报告为磁盘文件类型，确保调用者正确识别 */
static DWORD __stdcall bridge_GetFileType_hook(PVOID h)
{
    if (vfile_slot(h) >= 0) return FILE_TYPE_DISK;
    if (!pGetFileType) return 0;
    return pGetFileType(h);
}

/* GetFileSize 桥接钩子
   虚拟文件始终返回内存中 POPData 的实际大小 */
static DWORD __stdcall bridge_GetFileSize_hook(PVOID h, DWORD* high)
{
    if (vfile_slot(h) >= 0) { if (high) *high = 0; return popdata_size(); }
    if (!pGetFileSize) return 0xFFFFFFFF;
    return pGetFileSize(h, high);
}

/* CreateFileMappingA 桥接钩子
   对于虚拟文件，返回一个虚拟映射句柄而非真实内存映射对象 */
static PVOID __stdcall bridge_CreateFileMappingA_hook(PVOID h, PVOID attr, DWORD protect, DWORD max_high, DWORD max_low, const char* name)
{
    int slot = vfile_slot(h);
    if (slot >= 0) return alloc_vmap(slot);
    if (!pCreateFileMappingA) return 0;
    return pCreateFileMappingA(h, attr, protect, max_high, max_low, name);
}

/* CreateFileMappingW 桥接钩子（宽字符版本） */
static PVOID __stdcall bridge_CreateFileMappingW_hook(PVOID h, PVOID attr, DWORD protect, DWORD max_high, DWORD max_low, const unsigned short* name)
{
    int slot = vfile_slot(h);
    if (slot >= 0) return alloc_vmap(slot);
    if (!pCreateFileMappingW) return 0;
    return pCreateFileMappingW(h, attr, protect, max_high, max_low, name);
}

/* MapViewOfFile 桥接钩子
   将虚拟映射句柄转换为 POPData 内存的直接指针
   此时调用者获得的是 BF 数据在内存中的真实地址 */
static PVOID __stdcall bridge_MapViewOfFile_hook(PVOID hmap, DWORD access, DWORD off_high, DWORD off_low, DWORD bytes)
{
    int ms = vmap_slot(hmap);
    DWORD size = popdata_size();
    (void)access;
    if (ms >= 0) {
        if (off_high != 0 || off_low > size) return 0;
        if (bytes && off_low + bytes > size) return 0;
        return (PVOID)(active_popdata_start() + off_low);
    }
    if (!pMapViewOfFile) return 0;
    return pMapViewOfFile(hmap, access, off_high, off_low, bytes);
}

/* UnmapViewOfFile 桥接钩子
   如果地址在 POPData 内存范围内则静默放行，不执行真正的解除映射 */
static BOOL __stdcall bridge_UnmapViewOfFile_hook(PVOID addr)
{
    if (ptr_inside_popdata(addr)) return TRUE;
    if (!pUnmapViewOfFile) return FALSE;
    return pUnmapViewOfFile(addr);
}

/* GetFileAttributesA 桥接钩子
   对 POPData.BF 始终返回 FILE_ATTRIBUTE_ARCHIVE（文件存在），
   让调用者无需真正读取磁盘即可获得文件存在性确认 */
static DWORD __stdcall bridge_GetFileAttributesA_hook(const char* name)
{
    if (ends_with_popdata_A(name)) return FILE_ATTRIBUTE_ARCHIVE;
    if (!pGetFileAttributesA) return INVALID_FILE_ATTRIBUTES;
    return pGetFileAttributesA(name);
}

/* GetFileAttributesW 桥接钩子（宽字符版本） */
static DWORD __stdcall bridge_GetFileAttributesW_hook(const unsigned short* name)
{
    if (ends_with_popdata_W(name)) return FILE_ATTRIBUTE_ARCHIVE;
    if (!pGetFileAttributesW) return INVALID_FILE_ATTRIBUTES;
    return pGetFileAttributesW(name);
}

/* out.dll 专用的 CreateFileA 钩子（与通用桥接钩子类似，但使用 out.dll 原始的
   CreateFileA 指针 pOutOriginalCreateFileA，避免钩子递归循环）
   这是因为 out.dll 的 IAT 被替换后，其自身调用需要走原始入口 */
static PVOID __stdcall out_CreateFileA_hook(const char* name, DWORD access, DWORD share, PVOID sec, DWORD creation, DWORD flags, PVOID template_file)
{
    PVOID vh = maybe_virtual_popdata_A(name, access);
    PVOID h;
    DWORD err = 0;
    char fixed[MAX_PATH_CHARS];
    CreateFileA_t fn = pOutOriginalCreateFileA ? pOutOriginalCreateFileA : pCreateFileA;
    if (vh) return vh;
    if (!fn) return INVALID_HANDLE_VALUE_PTR;
    h = fn(name, access, share, sec, creation, flags, template_file);
    if (h != INVALID_HANDLE_VALUE_PTR) return h;
    if (pGetLastError) err = pGetLastError();
    h = try_open_gbk_font_with_CreateFileW(name, access, share, sec, creation, flags, template_file);
    if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    if (build_game_path(fixed, MAX_PATH_CHARS, name)) {
        vh = maybe_virtual_popdata_A(fixed, access);
        if (vh) return vh;
        h = fn(fixed, access, share, sec, creation, flags, template_file);
        if (h != INVALID_HANDLE_VALUE_PTR) return h;
        h = try_open_gbk_font_with_CreateFileW(fixed, access, share, sec, creation, flags, template_file);
        if (h && h != INVALID_HANDLE_VALUE_PTR) return h;
    }
    if (pSetLastError) pSetLastError(err);
    return INVALID_HANDLE_VALUE_PTR;
}

/* ============================================================
   IAT 修改基础设施
   ============================================================ */

/* 在指定模块的 IAT（导入地址表）中，将特定函数名对应的导入项
   替换为自定义的钩子函数地址
   module：要修改的 PE 模块基址
   proc_name：要拦截的函数名
   hook：替代函数地址
   返回：成功修改的 IAT 条目数 */
static int hook_one_iat(PVOID module, const char* proc_name, PVOID hook)
{
    BYTE* base = (BYTE*)module;
    BYTE* nt;
    BYTE* optional;
    DWORD import_rva;
    BYTE* desc;
    int changed = 0;
    if (!base || !pVirtualProtect) return 0;
    if (*(WORD*)(base + 0x00) != 0x5A4D) return 0;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return 0;
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return 0;
    /* 读取数据目录中导入表的 RVA */
    import_rva = *(DWORD*)(optional + 104);
    if (!import_rva) return 0;
    desc = base + import_rva;
    /* 遍历每个导入描述符（每个描述符对应一个被导入的 DLL） */
    while (*(DWORD*)(desc + 12)) {
        DWORD lookup_rva = *(DWORD*)(desc + 0);
        DWORD iat_rva = *(DWORD*)(desc + 16);
        DWORD* lookup;
        DWORD* iat;
        DWORD n = 0;
        if (!lookup_rva) lookup_rva = iat_rva;
        lookup = (DWORD*)(base + lookup_rva);
        iat = (DWORD*)(base + iat_rva);
        /* 遍历该 DLL 的所有导入函数 */
        while (lookup[n]) {
            DWORD thunk = lookup[n];
            if (!(thunk & 0x80000000)) {
                /* 按名称导入：thunk 指向 IMAGE_IMPORT_BY_NAME 结构 */
                const char* proc = (const char*)(base + thunk + 2);
                if (ascii_equal(proc, proc_name)) {
                    DWORD oldp = 0;
                    if ((PVOID)iat[n] != hook && pVirtualProtect(&iat[n], 4, PAGE_EXECUTE_READWRITE, &oldp)) {
                        iat[n] = (DWORD)hook;
                        pVirtualProtect(&iat[n], 4, oldp, &oldp);
                        changed++;
                    }
                }
            }
            ++n;
        }
        desc += 20;
    }
    return changed;
}

/* 专门替换 out.dll 的 CreateFileA IAT 入口
   先保存原始 CreateFileA 到 pOutOriginalCreateFileA，再设置钩子
   保存原始地址是为了让 out.dll 自己的回退路径仍然能访问真实文件 */
static int hook_out_createfile_iat(void)
{
    PVOID out;
    int changed = 0;
    if (!pGetModuleHandleA) return 0;
    out = pGetModuleHandleA("out.dll");
    if (!out) out = pGetModuleHandleA("OUT.DLL");
    if (!out) return 0;
    /* Preserve out.dll's original CreateFileA target for its own path fallback hook. */
    if (!pOutOriginalCreateFileA) pOutOriginalCreateFileA = pCreateFileA;
    changed += hook_one_iat(out, "CreateFileA", (PVOID)out_CreateFileA_hook);
    return changed;
}

/* 对指定模块，批量替换所有文件系统相关的 IAT 钩子
   包括 CreateFile、ReadFile、CloseHandle 等全套文件操作 API */
static int hook_file_iat_for_module(PVOID module)
{
    int changed = 0;
    if (!module) return 0;
    changed += hook_one_iat(module, "CreateFileA", (PVOID)bridge_CreateFileA_hook);
    changed += hook_one_iat(module, "CreateFileW", (PVOID)bridge_CreateFileW_hook);
    changed += hook_one_iat(module, "ReadFile", (PVOID)bridge_ReadFile_hook);
    changed += hook_one_iat(module, "SetFilePointer", (PVOID)bridge_SetFilePointer_hook);
    changed += hook_one_iat(module, "CloseHandle", (PVOID)bridge_CloseHandle_hook);
    changed += hook_one_iat(module, "GetFileType", (PVOID)bridge_GetFileType_hook);
    changed += hook_one_iat(module, "GetFileSize", (PVOID)bridge_GetFileSize_hook);
    changed += hook_one_iat(module, "CreateFileMappingA", (PVOID)bridge_CreateFileMappingA_hook);
    changed += hook_one_iat(module, "CreateFileMappingW", (PVOID)bridge_CreateFileMappingW_hook);
    changed += hook_one_iat(module, "MapViewOfFile", (PVOID)bridge_MapViewOfFile_hook);
    changed += hook_one_iat(module, "UnmapViewOfFile", (PVOID)bridge_UnmapViewOfFile_hook);
    changed += hook_one_iat(module, "GetFileAttributesA", (PVOID)bridge_GetFileAttributesA_hook);
    changed += hook_one_iat(module, "GetFileAttributesW", (PVOID)bridge_GetFileAttributesW_hook);
    return changed;
}

/* ============================================================
   D3D9 桥接模块
   目标：在窗口化环境（dxwrapper）下，让 out.dll 的 D3D9 wrapper
   仍然能正常工作。支持两种模式：
   - 模式 1（全屏）：out.dll 作为外层 wrapper
   - 模式 2（窗口化 vtable 桥接）：dxwrapper 作为外层，通过 patch
     IDirect3D9 对象 vtable 让 out.dll 包装内部的 CreateDevice
   ============================================================ */

/* 获取 out.dll 的模块基址（兼容大小写） */
static PVOID get_out_module(void)
{
    PVOID out;
    if (!pGetModuleHandleA) return 0;
    out = pGetModuleHandleA("out.dll");
    if (!out) out = pGetModuleHandleA("OUT.DLL");
    return out;
}

/* 从 out.dll 中读取已保存的原始 Direct3DCreate9 指针
   并返回 out.dll 内部 D3D9 钩子的地址
   out.dll 会在初始化时保存原始 Direct3DCreate9 到 0x2B148 */
static PVOID get_out_d3dcreate9_hook_if_ready(void)
{
    BYTE* out = (BYTE*)get_out_module();
    PVOID saved_original;
    if (!out) return 0;
    if (!readable_ptr(out + 0x2B148, 4)) return 0;
    saved_original = *(PVOID*)(out + 0x2B148);
    if (!saved_original) return 0;
    return (PVOID)(out + 0x4996);
}

/* 获取 PE 模块的内存映像大小（用于判断指针是否在模块范围内） */
static DWORD module_size_of_image(PVOID module)
{
    BYTE* base = (BYTE*)module;
    BYTE* nt;
    BYTE* optional;
    if (!base) return 0;
    if (*(WORD*)(base + 0x00) != 0x5A4D) return 0;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return 0;
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return 0;
    return *(DWORD*)(optional + 56); /* PE32 OptionalHeader.SizeOfImage */
}

/* 判断指针是否在指定 PE 模块的地址范围内 */
static int ptr_inside_module(PVOID ptr, PVOID module)
{
    DWORD p = (DWORD)ptr;
    DWORD b = (DWORD)module;
    DWORD sz = module_size_of_image(module);
    if (!ptr || !module || !sz) return 0;
    return p >= b && p < b + sz;
}

/* 判断指针是否在指定名称的模块地址范围内 */
static int ptr_inside_named_module(PVOID ptr, const char* name)
{
    PVOID mod;
    if (!pGetModuleHandleA || !ptr || !name) return 0;
    mod = pGetModuleHandleA(name);
    return mod && ptr_inside_module(ptr, mod);
}

/* 判断指针是否在任意版本的 dxwrapper 模块中 */
static int ptr_inside_any_dxwrapper(PVOID ptr)
{
    if (ptr_inside_named_module(ptr, "dxwrapper.asi")) return 1;
    if (ptr_inside_named_module(ptr, "DXWRAPPER.ASI")) return 1;
    return 0;
}

/* 判断给定模块是否为 dxwrapper（通过比较模块基址） */
static int module_is_dxwrapper(PVOID module)
{
    if (!module || !pGetModuleHandleA) return 0;
    if (module == pGetModuleHandleA("dxwrapper.asi")) return 1;
    if (module == pGetModuleHandleA("DXWRAPPER.ASI")) return 1;
    return 0;
}

/* 设置 out.dll 的 D3D9 CreateDevice next 指针
   这个指针让 out.dll 知道在创建 D3D9 设备时应调用的下一个函数是谁。
   在 vtable 桥接模式下，这个函数被设置为 dxwrapper 的 Direct3DCreate9，
   实现 out.dll -> dxwrapper -> d3d9.dll 的调用链。 */
static int set_out_d3dcreate9_next(PVOID next)
{
    BYTE* out = (BYTE*)get_out_module();
    PVOID hook = get_out_d3dcreate9_hook_if_ready();
    DWORD oldp = 0;
    if (!out || !next || !pVirtualProtect) return 0;
    if (hook && next == hook) return 0;
    if (!readable_ptr(out + 0x2B148, 4)) return 0;
    if (*(PVOID*)(out + 0x2B148) == next) return 1;
    if (pVirtualProtect(out + 0x2B148, 4, PAGE_EXECUTE_READWRITE, &oldp)) {
        *(PVOID*)(out + 0x2B148) = next;
        pVirtualProtect(out + 0x2B148, 4, oldp, &oldp);
        dbg("[pop1_chs_compat] D3D9Fix=2 chained out.dll wrapper after dxwrapper\n");
        return 1;
    }
    return 0;
}


static PVOID get_real_d3d9_direct3dcreate9_export(void);
typedef PVOID (__stdcall *Direct3DCreate9_t)(DWORD sdk_version);
typedef long  (__stdcall *OutCreateDevice_t)(PVOID self, DWORD adapter, DWORD device_type, PVOID focus_window, DWORD behavior_flags, PVOID presentation_parameters, PVOID* returned_device_interface);

/* 检查 IDirect3D9 对象的 vtable 是否已被我们的桥接补丁接管 */
static int d3d9_object_vtable_patched(PVOID obj)
{
    if (!obj || !readable_ptr(obj, 4)) return 0;
    return *(DWORD**)obj == g_patched_d3d9_vtbl_clone;
}

/* IDirect3D9::CreateDevice 的 vtable 桥接函数
   当 dxwrapper 调用 IDirect3D9 对象的 CreateDevice 时，会先进入此函数。
   核心算法：
   1. 构建一个假的 out.dll IDirect3D9 包装对象（fake_out_d3d9_object），
      让 out.dll 认为自己在处理自己的 D3D9 对象。
   2. 临时将底层 dxwrapper 对象恢复为原始 vtable（防止递归）。
   3. 调用 out.dll 的 CreateDevice 包装器（out+0x74D0），out.dll 在其中
      调用 [self+4]->vtable[16] 即 dxwrapper 的 CreateDevice。
   4. out.dll 返回后，将 vtable 重新替换为我们的克隆。
   这实现了：out.dll（字体渲染）-> dxwrapper（窗口化）-> d3d9.dll（实际渲染） */
static long __stdcall bridge_IDirect3D9_CreateDevice(PVOID self, DWORD adapter, DWORD device_type, PVOID focus_window, DWORD behavior_flags, PVOID presentation_parameters, PVOID* returned_device_interface)
{
    BYTE* out = (BYTE*)get_out_module();
    OutCreateDevice_t out_create_device;
    DWORD oldp = 0;
    long hr;

    /* 安全检查：如果设备不可用或正在桥接中（防递归），直接调用原始 CreateDevice */
    if (!self || !out || !g_patched_d3d9_orig_vtbl || g_in_create_device_bridge) {
        /* Fallback: call the original vtable entry directly if possible. */
        if (g_patched_d3d9_orig_vtbl && g_patched_d3d9_orig_vtbl[16]) {
            OutCreateDevice_t orig = (OutCreateDevice_t)g_patched_d3d9_orig_vtbl[16];
            return orig(self, adapter, device_type, focus_window, behavior_flags, presentation_parameters, returned_device_interface);
        }
        return (long)0x8876086C; /* D3DERR_INVALIDCALL-ish fallback */
    }

    /* out.dll 内部 IDirect3D9::CreateDevice 包装函数 */
    out_create_device = (OutCreateDevice_t)(out + 0x74D0); /* out.dll IDirect3D9 wrapper CreateDevice */

    /* Build a tiny fake out.dll IDirect3D9 wrapper around the dxwrapper/system object.
       g_fake_out_d3d9_object[0] = out.dll 的 IDirect3D9 wrapper vtable 地址
       g_fake_out_d3d9_object[1] = dxwrapper/system 的真实 IDirect3D9 对象指针
       out.dll 的 CreateDevice 通过 [self+4] 获取真实 D3D9 对象，再通过
       [self+4]->vtable[16] 调用下层 CreateDevice。我们伪造的 double-indirection
       让 out.dll 觉得自己在包装真实 D3D9 对象，实际包装的是 dxwrapper 的对象。 */
    g_fake_out_d3d9_object[0] = (DWORD)(out + 0x23424); /* out.dll IDirect3D9 wrapper vtable */
    g_fake_out_d3d9_object[1] = (DWORD)self;            /* real IDirect3D9 behind out.dll */

    /* 防止递归：out.dll 的 CreateDevice 会调用 [self+4]->vtable[16]（即真实 D3D9），
       但我们把 self 的 vtable 替换成了自己的克隆，导致递归调用。
       解决方案：临时恢复底层 dxwrapper 对象的原始 vtable，让 out.dll 能直接调用
       到真正的 D3D9 CreateDevice，调用完成后再重新安装我们的克隆 vtable。 */
    g_in_create_device_bridge = 1;
    if (pVirtualProtect) {
        pVirtualProtect(self, 4, PAGE_EXECUTE_READWRITE, &oldp);
        *(DWORD**)self = g_patched_d3d9_orig_vtbl;
        pVirtualProtect(self, 4, oldp, &oldp);
    } else {
        *(DWORD**)self = g_patched_d3d9_orig_vtbl;
    }

    hr = out_create_device((PVOID)g_fake_out_d3d9_object, adapter, device_type, focus_window, behavior_flags, presentation_parameters, returned_device_interface);

    if (pVirtualProtect) {
        pVirtualProtect(self, 4, PAGE_EXECUTE_READWRITE, &oldp);
        *(DWORD**)self = g_patched_d3d9_vtbl_clone;
        pVirtualProtect(self, 4, oldp, &oldp);
    } else {
        *(DWORD**)self = g_patched_d3d9_vtbl_clone;
    }
    g_in_create_device_bridge = 0;

    dbg("[pop1_chs_compat] D3D9Fix=2 vtable CreateDevice bridge executed\n");
    return hr;
}

/* 补丁 IDirect3D9 对象的 vtable
   复制原始 vtable 到本地数组 g_patched_d3d9_vtbl_clone，
   将第 17 项（CreateDevice）替换为我们的桥接函数。
   然后将对象指针指向克隆 vtable。 */
static int patch_d3d9_object_vtable(PVOID obj)
{
    DWORD* vtbl;
    DWORD oldp = 0;
    int i;
    if (!obj || !pVirtualProtect) return 0;
    if (!readable_ptr(obj, 4)) return 0;
    vtbl = *(DWORD**)obj;
    if (!readable_ptr(vtbl, 17 * 4)) return 0;
    if (vtbl[16] == (DWORD)bridge_IDirect3D9_CreateDevice) return 1;

    for (i = 0; i < 20; ++i) g_patched_d3d9_vtbl_clone[i] = vtbl[i];
    g_patched_d3d9_vtbl_clone[16] = (DWORD)bridge_IDirect3D9_CreateDevice;
    g_patched_d3d9_object = obj;
    g_patched_d3d9_orig_vtbl = vtbl;

    if (pVirtualProtect(obj, 4, PAGE_EXECUTE_READWRITE, &oldp)) {
        *(DWORD**)obj = g_patched_d3d9_vtbl_clone;
        pVirtualProtect(obj, 4, oldp, &oldp);
        dbg("[pop1_chs_compat] D3D9Fix=2 patched IDirect3D9 vtable CreateDevice\n");
        return 1;
    }
    return 0;
}

/* Direct3DCreate9 包装函数（vtable 桥接模式入口）
   调用真正的 Direct3DCreate9（通常是 dxwrapper 的），
   然后立即 patch 返回的 IDirect3D9 对象的 vtable
   这样后续所有 CreateDevice 调用都经过我们的桥接 */
static PVOID __stdcall bridge_Direct3DCreate9_vtable(DWORD sdk_version)
{
    Direct3DCreate9_t next = (Direct3DCreate9_t)g_d3d9_next_direct3dcreate9;
    PVOID obj;
    if (!next) {
        PVOID real = get_real_d3d9_direct3dcreate9_export();
        next = (Direct3DCreate9_t)real;
    }
    if (!next) return 0;
    obj = next(sdk_version);
    if (obj) patch_d3d9_object_vtable(obj);
    return obj;
}

/* GetProcAddress 钩子 — 拦截对 Direct3DCreate9 的运行时查询
   在模式 1（全屏）下：直接返回 out.dll 的 D3D9 钩子地址
   在模式 2（窗口化）下：保存真实地址到 g_d3d9_next_direct3dcreate9，
   然后返回 bridge_Direct3DCreate9_vtable 以建立 vtable 桥接 */
static PVOID __stdcall bridge_GetProcAddress_hook(PVOID module, const char* name)
{
    PVOID hook;
    PVOID ret;
    if (!pGetProcAddress) return 0;
    if (!g_cfg_enabled || !g_cfg_d3d9_fix || (DWORD)name <= 0xFFFF || !ascii_equal(name, "Direct3DCreate9")) {
        return pGetProcAddress(module, name);
    }

    hook = get_out_d3dcreate9_hook_if_ready();
    if (!hook) return pGetProcAddress(module, name);

    if (g_cfg_d3d9_fix == 1) {
        return hook;
    }

    ret = pGetProcAddress(module, name);

    if (g_cfg_d3d9_fix == 2) {
        if (ret && ret != (PVOID)bridge_Direct3DCreate9_vtable) {
            g_d3d9_next_direct3dcreate9 = ret;
            dbg("[pop1_chs_compat] D3D9Fix=2 GetProcAddress vtable bridge armed\n");
            return (PVOID)bridge_Direct3DCreate9_vtable;
        }
        return ret;
    }

    return ret;
}


/* 模式 2 专用的 dxwrapper IAT 钩子
   在 dxwrapper 的 IAT 中：
   1. 将 Direct3DCreate9 替换为 out.dll 的 D3D9 钩子
   2. 将 GetProcAddress 替换为 bridge_GetProcAddress_hook
   从而建立 dxwrapper -> out.dll -> bridge -> d3d9.dll 的调用链 */
static int hook_d3d9_iat_for_dxwrapper_inner_out(PVOID module)
{
    BYTE* base = (BYTE*)module;
    BYTE* nt;
    BYTE* optional;
    DWORD import_rva;
    BYTE* desc;
    PVOID d3d_hook = get_out_d3dcreate9_hook_if_ready();
    int changed = 0;
    if (!module || !d3d_hook || !pVirtualProtect || !module_is_dxwrapper(module)) return 0;

    if (*(WORD*)(base + 0x00) != 0x5A4D) return 0;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return 0;
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return 0;
    import_rva = *(DWORD*)(optional + 104);
    if (!import_rva) return 0;

    desc = base + import_rva;
    while (*(DWORD*)(desc + 12)) {
        DWORD lookup_rva = *(DWORD*)(desc + 0);
        DWORD iat_rva = *(DWORD*)(desc + 16);
        DWORD* lookup;
        DWORD* iat;
        DWORD n = 0;
        if (!lookup_rva) lookup_rva = iat_rva;
        lookup = (DWORD*)(base + lookup_rva);
        iat = (DWORD*)(base + iat_rva);
        while (lookup[n]) {
            DWORD thunk = lookup[n];
            if (!(thunk & 0x80000000)) {
                const char* proc = (const char*)(base + thunk + 2);
                if (ascii_equal(proc, "Direct3DCreate9")) {
                    PVOID current = (PVOID)iat[n];
                    DWORD oldp = 0;
                    if (current && current != d3d_hook) {
                        set_out_d3dcreate9_next(current);
                        if (pVirtualProtect(&iat[n], 4, PAGE_EXECUTE_READWRITE, &oldp)) {
                            iat[n] = (DWORD)d3d_hook;
                            pVirtualProtect(&iat[n], 4, oldp, &oldp);
                            changed++;
                            dbg("[pop1_chs_compat] D3D9Fix=2 patched dxwrapper IAT: dxwrapper -> out.dll -> d3d9\n");
                        }
                    }
                } else if (ascii_equal(proc, "GetProcAddress") && pGetProcAddress) {
                    DWORD oldp = 0;
                    if ((PVOID)iat[n] != (PVOID)bridge_GetProcAddress_hook) {
                        if (pVirtualProtect(&iat[n], 4, PAGE_EXECUTE_READWRITE, &oldp)) {
                            iat[n] = (DWORD)bridge_GetProcAddress_hook;
                            pVirtualProtect(&iat[n], 4, oldp, &oldp);
                            changed++;
                            dbg("[pop1_chs_compat] D3D9Fix=2 patched dxwrapper GetProcAddress\n");
                        }
                    }
                }
            }
            ++n;
        }
        desc += 20;
    }
    return changed;
}

/* 模式 1（全屏）：直接替换模块中的 Direct3DCreate9 IAT 为 out.dll 的钩子
   此时 out.dll 作为最外层 D3D9 wrapper 工作 */
static int hook_d3d9_iat_for_module_mode1(PVOID module)
{
    PVOID d3d_hook = get_out_d3dcreate9_hook_if_ready();
    int changed = 0;
    if (!module || !d3d_hook) return 0;
    changed += hook_one_iat(module, "Direct3DCreate9", d3d_hook);
    if (pGetProcAddress) changed += hook_one_iat(module, "GetProcAddress", (PVOID)bridge_GetProcAddress_hook);
    return changed;
}

/* 模式 2（窗口化 vtable 桥接）：在模块 IAT 中设置 vtable 桥接
   - 保留 dxwrapper 的 Direct3DCreate9 作为真实构造函数
   - 将调用者的 Direct3DCreate9 IAT 替换为我们的 bridge_Direct3DCreate9_vtable
   - 桥接函数会 patch 返回的 IDirect3D9 对象的 CreateDevice vtable
   - 最终 out.dll 包装 dxwrapper 处理后的 IDirect3DDevice9 */
static int hook_d3d9_iat_for_module_mode2(PVOID module)
{
    BYTE* base = (BYTE*)module;
    BYTE* nt;
    BYTE* optional;
    DWORD import_rva;
    BYTE* desc;
    int changed = 0;
    if (!module || !pVirtualProtect) return 0;

    /* Mode 2 now means windowed vtable bridge:
       - Keep dxwrapper's Direct3DCreate9 as the real constructor.
       - Patch callers to call our bridge first.
       - The bridge patches the returned IDirect3D9 object's CreateDevice vtable.
       - out.dll wraps the final IDirect3DDevice9 after dxwrapper has applied windowing. */
    if (pGetProcAddress) changed += hook_one_iat(module, "GetProcAddress", (PVOID)bridge_GetProcAddress_hook);

    if (*(WORD*)(base + 0x00) != 0x5A4D) return changed;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return changed;
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return changed;
    import_rva = *(DWORD*)(optional + 104);
    if (!import_rva) return changed;

    desc = base + import_rva;
    while (*(DWORD*)(desc + 12)) {
        DWORD lookup_rva = *(DWORD*)(desc + 0);
        DWORD iat_rva = *(DWORD*)(desc + 16);
        DWORD* lookup;
        DWORD* iat;
        DWORD n = 0;
        if (!lookup_rva) lookup_rva = iat_rva;
        lookup = (DWORD*)(base + lookup_rva);
        iat = (DWORD*)(base + iat_rva);
        while (lookup[n]) {
            DWORD thunk = lookup[n];
            if (!(thunk & 0x80000000)) {
                const char* proc = (const char*)(base + thunk + 2);
                if (ascii_equal(proc, "Direct3DCreate9")) {
                    PVOID current = (PVOID)iat[n];
                    DWORD oldp = 0;
                    if (current && current != (PVOID)bridge_Direct3DCreate9_vtable) {
                        g_d3d9_next_direct3dcreate9 = current;
                        if (pVirtualProtect(&iat[n], 4, PAGE_EXECUTE_READWRITE, &oldp)) {
                            iat[n] = (DWORD)bridge_Direct3DCreate9_vtable;
                            pVirtualProtect(&iat[n], 4, oldp, &oldp);
                            changed++;
                            dbg("[pop1_chs_compat] D3D9Fix=2 patched Direct3DCreate9 to vtable bridge\n");
                        }
                    }
                }
            }
            ++n;
        }
        desc += 20;
    }
    return changed;
}


/* 直接从 d3d9.dll 获取真正的 Direct3DCreate9 导出函数地址
   绕过所有可能的中间层包装 */
static PVOID get_real_d3d9_direct3dcreate9_export(void)
{
    PVOID d3d9;
    if (!pGetModuleHandleA || !pGetProcAddress) return 0;
    d3d9 = pGetModuleHandleA("d3d9.dll");
    if (!d3d9) d3d9 = pGetModuleHandleA("D3D9.DLL");
    if (!d3d9) return 0;
    return pGetProcAddress(d3d9, "Direct3DCreate9");
}

/* 判断节区是否适合扫描指针（要求节区可写，避免误修改代码段）
   dxwrapper 通常将解析好的真实 D3D9 函数指针存储在可写数据节中 */
static int section_is_reasonable_for_pointer_scan(DWORD chars)
{
    /* IMAGE_SCN_MEM_WRITE = 0x80000000.  dxwrapper usually stores resolved
       real-d3d9 function pointers in writable data.  Avoid patching code. */
    return (chars & 0x80000000) ? 1 : 0;
}

/* 扫描 dxwrapper 模块中所有缓存了 real D3D9 函数指针的位置并替换为 out.dll 钩子
   遍历所有可写节区，查找内容等于 real Direct3DCreate9 的 DWORD 指针位置。
   用于捕获 dxwrapper 内部缓存的 Direct3DCreate9 指针并重定向。 */
static int patch_cached_direct3dcreate9_pointers(PVOID module)
{
    BYTE* base = (BYTE*)module;
    BYTE* nt;
    BYTE* optional;
    BYTE* sec;
    WORD num_sections;
    WORD opt_size;
    PVOID real = get_real_d3d9_direct3dcreate9_export();
    PVOID hook = get_out_d3dcreate9_hook_if_ready();
    int patched = 0;
    WORD i;

    if (!module || !real || !hook || real == hook || !pVirtualProtect) return 0;
    if (*(WORD*)(base + 0x00) != 0x5A4D) return 0;
    nt = base + *(DWORD*)(base + 0x3C);
    if (*(DWORD*)nt != 0x00004550) return 0;
    num_sections = *(WORD*)(nt + 6);
    opt_size = *(WORD*)(nt + 20);
    optional = nt + 24;
    if (*(WORD*)optional != 0x10B) return 0;
    sec = optional + opt_size;

    /* Do not override out.dll's saved original.  The goal is specifically to
       catch dxwrapper's cached real-d3d9 pointer and redirect that one through
       out.dll, preserving dxwrapper as the outer windowed wrapper. */
    for (i = 0; i < num_sections; ++i) {
        BYTE* sh = sec + i * 40;
        DWORD vsize = *(DWORD*)(sh + 8);
        DWORD va    = *(DWORD*)(sh + 12);
        DWORD rsize = *(DWORD*)(sh + 16);
        DWORD chars = *(DWORD*)(sh + 36);
        DWORD size  = vsize > rsize ? vsize : rsize;
        DWORD off;
        if (!section_is_reasonable_for_pointer_scan(chars) || !size) continue;
        for (off = 0; off + 4 <= size; off += 4) {
            DWORD* cell = (DWORD*)(base + va + off);
            if (!readable_ptr(cell, 4)) continue;
            if ((PVOID)(*cell) == real) {
                DWORD oldp = 0;
                if (pVirtualProtect(cell, 4, PAGE_EXECUTE_READWRITE, &oldp)) {
                    *cell = (DWORD)hook;
                    pVirtualProtect(cell, 4, oldp, &oldp);
                    patched++;
                }
            }
        }
    }
    if (patched) dbg("[pop1_chs_compat] D3D9Fix=2 patched cached dxwrapper Direct3DCreate9 pointers\n");
    return patched;
}

/* 模式 4 的 D3D9 钩子（指针扫描模式）
   仅在模块为 dxwrapper 时执行 cached 指针替换 */
static int hook_d3d9_ptrscan_mode4(PVOID module)
{
    if (!module || !module_is_dxwrapper(module)) return 0;
    return patch_cached_direct3dcreate9_pointers(module);
}

/* D3D9 IAT 钩子的总入口（根据配置模式选择策略）
   0 = 关闭
   1 = 全屏模式：out.dll wrapper 作为外层 D3D9 链
   2 = 窗口化 vtable 桥接：dxwrapper 保持最外层，patch 返回的 IDirect3D9 对象 vtable */
static int hook_d3d9_iat_for_module(PVOID module)
{
    if (!g_cfg_enabled || !g_cfg_d3d9_fix || !module) return 0;

    /* 0 = off.
       1 = confirmed fullscreen mode: force out.dll wrapper as the outer D3D9 chain.
       2 = windowed vtable bridge: keep dxwrapper outermost, patch the returned
           IDirect3D9 object's CreateDevice, and let out.dll wrap the final device. */
    if (g_cfg_d3d9_fix == 1) return hook_d3d9_iat_for_module_mode1(module);
    if (g_cfg_d3d9_fix == 2) return hook_d3d9_iat_for_module_mode2(module);
    return 0;
}

/* 对所有相关的 PE 模块执行 IAT 钩子安装
   覆盖的模块列表包括：主 EXE、binkw32（过场动画）、out.dll（汉化核心）、
   dxwrapper.asi（窗口化）、pop1w.asi 和 dinput8.dll（输入处理）。
   每个模块都会安装文件系统钩子和 D3D9 钩子。 */
static void hook_all_relevant_iats(void)
{
    PVOID mods[12];
    int i;
    if (!pGetModuleHandleA) return;
    mods[0] = pGetModuleHandleA(0);
    mods[1] = pGetModuleHandleA("POP.EXE");
    mods[2] = pGetModuleHandleA("PrinceOfPersia.EXE");
    mods[3] = pGetModuleHandleA("BinkW32.DLL");
    mods[4] = pGetModuleHandleA("binkw32.dll");
    mods[5] = pGetModuleHandleA("BinkW32Hooked.DLL");
    mods[6] = pGetModuleHandleA("binkw32Hooked.dll");
    mods[7] = pGetModuleHandleA("out.dll");
    mods[8] = pGetModuleHandleA("OUT.DLL");
    mods[9] = pGetModuleHandleA("dxwrapper.asi");
    mods[10] = pGetModuleHandleA("pop1w.asi");
    mods[11] = pGetModuleHandleA("dinput8.dll");
    for (i = 0; i < 12; ++i) {
        if (mods[i]) {
            hook_file_iat_for_module(mods[i]);
            hook_d3d9_iat_for_module(mods[i]);
        }
    }
    hook_out_createfile_iat();
}

/* 加载 out.dll（汉化补丁的核心 DLL）
   搜索优先级：
   1. 已加载则直接返回
   2. 游戏根目录
   3. 当前目录
   4. .\ 前缀
   5. scripts\ 子目录 */
static PVOID load_out_dll(void)
{
    PVOID h = 0;
    char path[MAX_PATH_CHARS];
    resolve_kernel32_exports();
    init_game_dir();
    load_config_once();
    if (!g_cfg_enabled || !pLoadLibraryA) return 0;
    if (pGetModuleHandleA) { h = pGetModuleHandleA("out.dll"); if (h) return h; }
    if (build_game_path(path, MAX_PATH_CHARS, "out.dll")) h = pLoadLibraryA(path);
    if (!h) h = pLoadLibraryA("out.dll");
    if (!h) h = pLoadLibraryA(".\\out.dll");
    if (!h) h = pLoadLibraryA("scripts\\out.dll");
    if (h) { g_loaded = 1; dbg("[pop_out_loader] loaded out.dll\n"); }
    return h;
}

/* 后台修复线程
   循环 8000 次（约 8 秒），持续尝试：
   - 修复 out.dll 代码拷贝的 DEP/NX 保护
   - 安装文件系统和 D3D9 的 IAT 钩子
   - 应用字幕补丁
   这是为了应对 out.dll 延迟加载或动态解压的情况，
   确保在所有时机都能成功 hook */
static DWORD __stdcall fix_thread(PVOID param)
{
    DWORD i;
    (void)param;
    resolve_kernel32_exports();
    init_game_dir();
    load_config_once();
    load_out_dll();
    for (i = 0; i < 8000; ++i) {
        init_game_dir();
        protect_out_dll_copies();
        hook_all_relevant_iats();
        apply_subtitle_patches();
        if (pSleep) pSleep(1);
    }
    return 0;
}

/* ============================================================
   入口与初始化
   ============================================================ */

/* 主初始化函数：在 DllMain 或 InitializeASI 中调用
   执行顺序：
   1. 解析 kernel32 导出函数
   2. 确定游戏根目录
   3. 加载 INI 配置
   4. 加载 out.dll
   5. 修复 DEP/NX
   6. 安装文件系统和 D3D9 的 IAT 钩子
   7. 启动后台修复线程 */
static void start_loader_and_fix(void)
{
    DWORD tid = 0;
    resolve_kernel32_exports();
    init_game_dir();
    load_config_once();
    load_out_dll();
    protect_out_dll_copies();
    hook_all_relevant_iats();
    apply_subtitle_patches();
    if (!g_started && pCreateThread) {
        g_started = 1;
        pCreateThread(0, 0, fix_thread, 0, 0, &tid);
    }
}

/* Ultimate ASI Loader 标准入口（ASI 插件自动调用） */
void __stdcall InitializeASI(void) { start_loader_and_fix(); }

/* DLL 入口点（同时被 ASI Loader 和常规 LoadLibrary 触发） */
BOOL __stdcall DllMain(PVOID hinst, DWORD reason, PVOID reserved)
{
    (void)hinst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) start_loader_and_fix();
    return TRUE;
}
