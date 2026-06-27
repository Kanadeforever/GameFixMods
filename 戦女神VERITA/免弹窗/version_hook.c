/*
 * version_hook.c — 独立 version.dll 代理: 弹窗屏蔽 + API 转发 (无 windows.h)
 *
 * 这是一个自包含的双功能 DLL:
 *
 *   1. 代理 DLL (Proxy DLL):
 *      劫持 version.dll。游戏加载 version.dll 时实际上加载的是本 DLL。
 *      本 DLL 导出所有 18 个 version.dll API，但自己不实现它们，
 *      而是从 C:\Windows\System32\version.dll 中动态查找并转发调用。
 *      对上层调用者完全透明。
 *
 *   2. 弹窗屏蔽:
 *      拦截 MessageBoxA，自动消除光盘校验弹窗。
 *      逻辑与 skip_popup.c 一致——匹配 Shift-JIS "エラー" 后自动应答。
 *
 * 为什么不用 windows.h:
 *   为了最小化编译依赖。本文件手动声明了所有需要的 Win32 类型、
 *   常量和 API 函数原型。编译时不需要 Windows SDK，仅需 MSVC 命令行:
 *     cl /LD /O1 /GS- version_hook.c /Fe:version.dll /DEF:ver_proxy.def
 *
 *   代价: 代码中有 ~30 行手动类型/常量声明，但换来的是零外部依赖。
 *   在 CI 或自动化构建中无需配置 SDK 路径。
 *
 * 为什么 version.dll 可以做 ASI Loader:
 *   许多游戏的 ASI Loader 选 version.dll 作为劫持目标，因为:
 *     - version.dll 导出函数少（仅 18 个），代理实现简单
 *     - 游戏通常不直接依赖 version.dll，劫持不影响启动
 *     - 但 Windows 会从游戏目录优先搜索 DLL，天然适合 DLL 劫持
 *
 * 代理模式工作流程:
 *   游戏调 GetFileVersionInfoA()
 *     → 本 DLL 的 X 宏生成的导出函数
 *     → get("GetFileVersionInfoA")
 *     → load_real() 加载 System32 的真 version.dll
 *     → GetProcAddress(g_real, "GetFileVersionInfoA")
 *     → 调用真函数 → 返回给游戏
 *
 * 与 skip_popup.c 的区别:
 *   - 本文件只 Hook MessageBoxA（不 Hook W 版），对老游戏够用
 *   - 本文件无 CRITICAL_SECTION（未声明相关 API），简化了依赖
 *   - 本文件日志文件名为 vh_log.txt ("version_hook")
 *   - 本文件承担额外职责：ASI Loader（加载 mai.asi 等插件）
 *
 * 编译:
 *   cl /LD /O1 /GS- version_hook.c /Fe:version.dll /DEF:ver_proxy.def
 *
 * 依赖:
 *   编译时: 无（手动声明所有 API），仅需 stddef.h (NULL, size_t)
 *   运行时: kernel32.dll, user32.dll
 *
 * 部署:
 *   将 version.dll + ver_proxy.def 编译产物放入游戏目录。
 *   按需配合 version.ini (dontloadfromdllmain=0) 解决非标准 PE 兼容。
 *
 * 相关文件:
 *   ver_proxy.def   — 导出函数列表（链接器用）
 *   skip_popup.c    — 独立版弹窗屏蔽 ASI 插件（不含代理转发）
 *   skip_popup.c.bak — 废弃的注册表 Hook 方案
 *
 * 作者: luminous
 * 许可: MIT
 * 日期: 2026-06
 */

#include <stddef.h> /* NULL, size_t, offsetof */

/* ============================================================
 * Win32 类型手动声明 (替代 <windows.h>)
 * ============================================================
 *
 * 所有类型和常量都在这里手动定义，不依赖任何 Windows SDK 头文件。
 * 按 Win32 API 规范匹配宽度和调用约定。
 * ============================================================ */

/* --- 基本类型 --- */
typedef unsigned long  DWORD;     /* 32 位无符号 */
typedef unsigned short WORD;      /* 16 位无符号 */
typedef int            BOOL;      /* Windows BOOL */
typedef unsigned int   UINT;      /* 32 位无符号 */
typedef long           LONG_PTR;  /* 指针宽度有符号整数 */

/* --- 指针类型 --- */
typedef void       *PVOID;        /* 泛型指针 */
typedef const void *LPCVOID;      /* 常指针 */

/* --- 字符串类型 --- */
typedef char        CHAR;
typedef const CHAR *LPCSTR;       /* ANSI 常字符串 */
typedef wchar_t     WCHAR;
typedef const WCHAR*LPCWSTR;      /* Unicode 常字符串 */
typedef CHAR       *LPSTR;        /* ANSI 可变字符串 */
typedef WCHAR      *LPWSTR;       /* Unicode 可变字符串 */
typedef DWORD      *LPDWORD;      /* DWORD 指针 */
typedef WORD       *PUINT;        /* UINT 指针 (名称略有历史偏差) */
typedef PVOID      *LPVOID;       /* 泛型指针的指针 */

/* --- 句柄类型 (实质上都是指针/整数) --- */
typedef PVOID HMODULE;            /* DLL 模块句柄 */
typedef PVOID HANDLE;             /* 内核对象句柄 */
typedef PVOID FARPROC;            /* 远函数指针 (GetProcAddress 返回值) */
typedef PVOID HWND;               /* 窗口句柄 */
typedef PVOID LPSECURITY_ATTRIBUTES; /* 安全属性指针 */

/* --- 其他 --- */
typedef unsigned char *PBYTE;     /* 字节指针 */
typedef struct {                  /* SYSTEMTIME 结构 */
    WORD y, mo, dw, d, h, m, s, ms;
} SYSTEMTIME;

/* --- 调用约定 --- */
#define WINAPI __stdcall           /* Win32 标准调用约定 */

/* --- 布尔值 --- */
#define FALSE 0
#define TRUE  1

/* --- 路径 --- */
#define MAX_PATH 260               /* Windows 路径最大长度 */

/* --- VirtualAlloc 标志 --- */
#define MEM_COMMIT  0x1000         /* 提交物理内存 */
#define MEM_RESERVE 0x2000         /* 保留地址空间 */

/* --- 内存保护标志 --- */
#define PAGE_EXECUTE_READWRITE 0x40 /* 可读+可写+可执行 */

/* --- 文件访问 --- */
#define GENERIC_WRITE    0x40000000 /* 写访问 */
#define FILE_SHARE_READ  1          /* 允许其他进程读取 */
#define CREATE_ALWAYS    2          /* 总是创建新文件（覆盖） */
#define FILE_ATTRIBUTE_NORMAL 0x80  /* 普通文件属性 */
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1) /* 无效句柄 */

/* --- MessageBox 返回值 --- */
#define IDYES   6                   /* 用户点击 "是" */
#define IDRETRY 4                   /* 用户点击 "重试" */

/* --- MessageBox 按钮类型 (uType & 0x0F) --- */
#define MB_YESNO            4       /* [是] [否] */
#define MB_YESNOCANCEL      3       /* [是] [否] [取消] */
#define MB_RETRYCANCEL      5       /* [重试] [取消] */
#define MB_ABORTRETRYIGNORE 2       /* [中止] [重试] [忽略] */

/* ============================================================
 * kernel32.dll API 手动声明
 * ============================================================
 * 只声明本文件实际用到的函数。实际链接由编译器的 .def 或
 * 隐式导入处理（/DEF:ver_proxy.def 不会妨碍对 kernel32 的隐式链接）。
 * ============================================================ */
DWORD   WINAPI GetSystemDirectoryW(LPWSTR, DWORD);
HMODULE WINAPI LoadLibraryA(LPCSTR);
HMODULE WINAPI LoadLibraryW(LPCWSTR);
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
BOOL    WINAPI DisableThreadLibraryCalls(HMODULE);
PVOID   WINAPI VirtualAlloc(PVOID, DWORD, DWORD, DWORD);
BOOL    WINAPI VirtualProtect(PVOID, DWORD, DWORD, DWORD*);
BOOL    WINAPI FlushInstructionCache(HANDLE, PVOID, DWORD);
DWORD   WINAPI GetModuleFileNameA(HMODULE, LPSTR, DWORD);
HANDLE  WINAPI CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                           DWORD, DWORD, HANDLE);
BOOL    WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, PVOID);
BOOL    WINAPI CloseHandle(HANDLE);
void    WINAPI GetLocalTime(SYSTEMTIME*);
HMODULE WINAPI GetModuleHandleA(LPCSTR);
int     WINAPI lstrlenA(LPCSTR);
HANDLE  WINAPI GetCurrentProcess(void);

/* ============================================================
 * user32.dll API 手动声明
 * ============================================================ */
int WINAPI wsprintfA(LPSTR, LPCSTR, ...);
int WINAPI MessageBoxA(HWND, LPCSTR, LPCSTR, DWORD);

/* ============================================================
 * 日志系统
 * ============================================================
 *
 * 单线程模型（无 CRITICAL_SECTION 保护）。
 * MessageBox 本身是 UI 线程操作，在 DllMain 后不会并发触发，
 * 因此不加锁是安全的。
 * ============================================================ */

/* 日志文件句柄。INVALID_HANDLE_VALUE 表示文件未打开 */
static HANDLE g_log = INVALID_HANDLE_VALUE;

/*
 * log_write — 写入一行带时间戳的日志
 *
 * 输出格式: [HH:MM:SS.mmm] <消息文本>\r\n
 *
 * 注意: 和 skip_popup.c 的 log_write 不同，本版本没有
 *       CRITICAL_SECTION 保护，也没有 log_open/log_close。
 *       日志文件在 DllMain 中直接创建和关闭。
 */
static void log_write(const char *m) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    DWORD w;
    char t[32];
    wsprintfA(t, "[%02d:%02d:%02d.%03d] ",
              st.h, st.m, st.s, st.ms);
    WriteFile(g_log, t, lstrlenA(t), &w, NULL);
    WriteFile(g_log, m, lstrlenA(m), &w, NULL);
    WriteFile(g_log, "\r\n", 2, &w, NULL);
}

/* ============================================================
 * 5 字节内联 Hook 引擎
 * ============================================================
 *
 * 与 skip_popup.c 的 hook5 完全相同的实现。详见该文件的注释。
 *
 * 工作原理摘要:
 *   目标函数前 5 字节 → 保存到 trampoline [0..4]
 *   trampoline [5..9]  → JMP 回原函数+5
 *   目标函数 [0..4]    → JMP 到钩子函数
 *   VirtualProtect 临时改内存保护 → 写 JMP → 恢复保护 → Flush 缓存
 *
 * 魔法数字速查:
 *   5  — x86 near JMP 长度 (1+4)
 *   6  — trampoline 偏移字段起始位置
 *   10 — trampoline 中 JMP 的下一条地址
 *   32 — trampoline 分配大小 (只用前 10 字节)
 *
 * 参数:
 *   tgt  — 被 Hook 的函数地址
 *   hook — 替换函数地址
 *   tr   — [输出] trampoline 地址，Hook 中通过它调用原始函数
 */
static BOOL hook5(PBYTE tgt, PBYTE hook, PBYTE *tr) {
    DWORD old_protect;

    /* 分配 32 字节可执行 trampoline */
    *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                       PAGE_EXECUTE_READWRITE);
    if (!*tr) return FALSE;

    /* 临时给目标函数开写权限 */
    VirtualProtect(tgt, 5, PAGE_EXECUTE_READWRITE, &old_protect);

    /* --- Trampoline: 保存原 5 字节 + JMP 回原函数 --- */
    for (int i = 0; i < 5; i++)
        (*tr)[i] = tgt[i];                              /* 原指令 */
    (*tr)[5] = 0xE9;                                     /* JMP 操作码 */
    *(DWORD*)((*tr) + 6) = (DWORD)((tgt + 5) - ((*tr) + 10)); /* 跳回偏移 */

    /* --- 目标函数: JMP 到钩子 --- */
    tgt[0] = 0xE9;                                       /* JMP 操作码 */
    *(DWORD*)(tgt + 1) = (DWORD)(hook - (tgt + 5));     /* 跳转偏移 */

    /* 恢复保护并刷新指令缓存 */
    VirtualProtect(tgt, 5, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), tgt, 5);

    return TRUE;
}

/*
 * gfn — Get Function Name，获取 DLL 导出函数地址
 *
 * 先检查 DLL 是否已加载 (GetModuleHandleA)，
 * 未加载则加载之 (LoadLibraryA)，然后解析函数地址 (GetProcAddress)。
 *
 * 参数:
 *   d — DLL 名称 ("user32.dll")
 *   n — 导出函数名 ("MessageBoxA")
 * 返回: 函数地址，失败返回 NULL
 */
static PBYTE gfn(const char *d, const char *n) {
    HMODULE m = GetModuleHandleA(d);
    if (!m) m = LoadLibraryA(d);
    return m ? (PBYTE)GetProcAddress(m, n) : NULL;
}

/* ============================================================
 * Shift-JIS 字节匹配
 * ============================================================
 *
 * 逐字节匹配日文 SJIS 关键词（不能直接用 strstr——SJIS 可能含 \0）。
 *
 * 关键词:
 *   エラー (error)  = 83 47 83 89 81 5B   — 弹窗标题
 *   データ (data)   = 83 66 81 5B 83 5E   — 弹窗内容
 * ============================================================ */

/*
 * match_sjis — 在 ANSI 字符串中匹配 SJIS 字节序列
 * 参数: h=待搜索字符串, n=匹配字节序列, nl=序列长度
 * 返回: 1=找到, 0=未找到
 */
static int match_sjis(const char *h, const unsigned char *n, int nl) {
    if (!h || !n || !nl) return 0;
    while (*h) {
        int i;
        for (i = 0; i < nl && h[i] && (unsigned char)h[i] == n[i]; i++);
        if (i == nl) return 1;
        h++;
    }
    return 0;
}

/* SJIS 字节序列常量 */
static const unsigned char k_er[] = {0x83, 0x47, 0x83, 0x89, 0x81, 0x5B};
static const unsigned char k_da[] = {0x83, 0x66, 0x81, 0x5B, 0x83, 0x5E};

/* ============================================================
 * MessageBoxA 钩子
 * ============================================================
 *
 * 与 skip_popup.c 的 Hook_MBA 逻辑完全一致:
 *   - 提取按钮类型 (u & 0x0F)
 *   - 检查标题(t)/内容(c)是否含 SJIS "エラー" 或 "データ"
 *   - 是校验弹窗 → 自动返回 IDYES 或 IDRETRY
 *   - 不是 → 通过 trampoline (g_tmba) 调原始 MessageBoxA
 *
 * 仅 Hook ANSI 版 (不 Hook MessageBoxW)。日文老游戏走 ANSI 路径，
 * 一个 Hook 点已覆盖所有弹窗场景。
 * ============================================================ */

/* MessageBoxA 函数指针类型 */
typedef int (WINAPI *MB_t)(HWND, LPCSTR, LPCSTR, UINT);

/* g_tmba — MessageBoxA 的 trampoline */
static PBYTE g_tmba = NULL;
/* g_count — 累计拦截计数 */
static int g_count = 0;

/* MessageBoxA 的替换函数 */
static int WINAPI Hook_MBA(HWND hw, LPCSTR t, LPCSTR c, UINT u) {
    /* 剥离图标标志，只保留按钮类型 (低 4 位) */
    UINT bt = u & 0x0F;

    /* SJIS 关键词匹配 */
    int err = (c && match_sjis(c, k_er, 6)) ||  /* 内容含"エラー" */
              (t && match_sjis(t, k_er, 6)) ||  /* 标题含"エラー" */
              (t && match_sjis(t, k_da, 6));    /* 标题含"データ" */

    /* Yes/No 类按钮 → 自动 "是" */
    if (err && (bt == MB_YESNO || bt == MB_YESNOCANCEL || bt == MB_RETRYCANCEL)) {
        g_count++;
        char m[100];
        wsprintfA(m, "[version_hook] popup #%d suppressed", g_count);
        log_write(m);
        return IDYES;
    }

    /* Abort/Retry/Ignore → 自动 "重试" */
    if (err && bt == MB_ABORTRETRYIGNORE) {
        g_count++;
        char m[100];
        wsprintfA(m, "[version_hook] popup #%d suppressed", g_count);
        log_write(m);
        return IDRETRY;
    }

    /* 非校验弹窗 — 原样显示 */
    return ((MB_t)g_tmba)(hw, t, c, u);
}

/* ============================================================
 * version.dll API 代理转发
 * ============================================================
 *
 * 这是代理 DLL 模式的核心实现。
 *
 * 架构:
 *   load_real() — 从 System32 加载真正的 version.dll
 *   get()       — 从真 DLL 中按名查找函数
 *   X() 宏      — 为每个导出函数生成一个 __declspec(dllexport) 包装器
 *
 * X() 宏设计原理:
 *   手动写 18 个转发函数每个约 8 行 = 144 行重复代码。
 *   用 X() 宏将模式抽象为一行: X(返回类型, 函数名, 参数列表, 参数名, 失败返回值)
 *   展开后自动生成:
 *     - static 函数指针缓存（首次调用时解析，后续零开销）
 *     - __declspec(dllexport) 导出
 *     - 调用真函数或返回 fail 值
 *
 *   Token paste (##): get(#name) 的 #name 将宏参数 name 转为字符串字面量，
 *   如 X(..., GetFileVersionInfoA, ...) → get("GetFileVersionInfoA")
 * ============================================================ */

/* g_real — 真正的 System32\version.dll 的模块句柄 */
static HMODULE g_real = NULL;

/*
 * load_real — 加载系统目录下的真实 version.dll
 *
 * 路径构造:
 *   1. GetSystemDirectoryW() → 获取 System32 路径
 *      （通常是 C:\Windows\System32）
 *   2. 手动拼接 "\version.dll" 到路径末尾
 *   3. LoadLibraryW() 加载
 *   4. 如果宽字符加载失败，回退到 LoadLibraryA("version.dll")
 *      （理论上不会走到这步，但作为兜底）
 *
 * 只会执行一次: g_real 非 NULL 时直接返回（幂等）。
 */
static void load_real(void) {
    if (g_real) return; /* 已加载，跳过 */

    WCHAR b[MAX_PATH];
    DWORD len = GetSystemDirectoryW(b, MAX_PATH);
    if (len == 0) return; /* GetSystemDirectoryW 失败 */

    /* 在 System32 路径后拼接 "\version.dll" */
    WCHAR *p = b + len;
    *p++ = L'\\';
    *p++ = L'v'; *p++ = L'e'; *p++ = L'r'; *p++ = L's';
    *p++ = L'i'; *p++ = L'o'; *p++ = L'n'; *p++ = L'.';
    *p++ = L'd'; *p++ = L'l'; *p++ = L'l';
    *p = 0;

    g_real = LoadLibraryW(b); /* 加载 System32\version.dll */

    /* 兜底: 如果宽路径加载失败，尝试短名称加载 */
    if (!g_real) g_real = LoadLibraryA("version.dll");
}

/*
 * get — 从真实 version.dll 中查找导出函数
 *
 * 先确保真实 DLL 已加载 (load_real)，然后 GetProcAddress。
 *
 * 参数: n = 函数名 ("GetFileVersionInfoA" 等)
 * 返回: 函数地址，失败返回 NULL
 */
static FARPROC get(const char *n) {
    load_real();
    return g_real ? GetProcAddress(g_real, n) : NULL;
}

/*
 * X() — 代理转发函数生成宏
 *
 * 这是整个项目中最精巧的部分。展开一个宏调用就生成一个完整的
 * __declspec(dllexport) 导出函数。
 *
 * 宏参数:
 *   type     — 函数返回类型 (DWORD, BOOL 等)
 *   name     — 函数名 (同时也是导出符号名和查询真 DLL 用的字符串)
 *   argtypes — 参数类型列表，用括号包裹
 *              (LPCSTR a, DWORD b, DWORD c, PVOID d)
 *   argnames — 参数名列表（不含类型），用于实际调用
 *              (a, b, c, d)
 *   fail     — 当真 DLL 中找不到此函数时的返回值
 *              (0 或 FALSE)
 *
 * 展开示例 (VerQueryValueA):
 *
 *   X(DWORD, VerQueryValueA,
 *     (LPCVOID a, LPCSTR b, LPVOID*c, PUINT d),
 *     (a, b, c, d), 0)
 *
 *   展开为:
 *
 *     __declspec(dllexport) DWORD WINAPI VerQueryValueA(
 *         LPCVOID a, LPCSTR b, LPVOID *c, PUINT d)
 *     {
 *         typedef DWORD (WINAPI *F)(LPCVOID a, LPCSTR b, LPVOID *c, PUINT d);
 *         static F fn = NULL;
 *         if (!fn) fn = (F)get("VerQueryValueA");   // 首次调用时懒解析
 *         if (fn) return fn(a, b, c, d);             // 转发到真函数
 *         return 0;                                   // 真函数不存在 → fail
 *     }
 *
 * 性能:
 *   - 首次调用: get() → load_real() → LoadLibraryW + GetProcAddress
 *   - 后续调用: 直接通过缓存的 fn 指针，零额外开销
 *   - static 变量在 DLL 生命周期内有效
 *
 * 为什么每个函数一个 static fn:
 *   互不干扰。18 个函数的指针各存各的，不需要全局映射表。
 */
#define X(type, name, argtypes, argnames, fail)            \
    __declspec(dllexport) type WINAPI name argtypes {      \
        typedef type (WINAPI *F) argtypes;                 \
        static F fn = NULL;                                \
        if (!fn) fn = (F)get(#name);                       \
        if (fn) return fn argnames;                        \
        return fail;                                        \
    }

/* ============================================================
 * version.dll 导出函数列表（18 个，由 X() 宏生成）
 * ============================================================
 *
 * 覆盖 version.dll 的全部公共 API。
 * 每个 X(...) 展开为一个完整的 __declspec(dllexport) 转发函数。
 *
 * 格式: X(返回类型, 函数名, (参数类型 参数名, ...), (参数名, ...), 失败返回值)
 */

/* GetFileVersionInfo 系列 — 获取文件版本信息 */
X(DWORD,GetFileVersionInfoA,(LPCSTR a,DWORD b,DWORD c,PVOID d),(a,b,c,d),0)
X(DWORD,GetFileVersionInfoByHandle,(DWORD a,DWORD b,DWORD c,PVOID d),(a,b,c,d),0)
X(DWORD,GetFileVersionInfoExA,(DWORD a,LPCSTR b,DWORD c,DWORD d,PVOID e),(a,b,c,d,e),0)
X(DWORD,GetFileVersionInfoExW,(DWORD a,LPCWSTR b,DWORD c,DWORD d,PVOID e),(a,b,c,d,e),0)

/* GetFileVersionInfoSize 系列 — 查询版本信息缓冲区大小 */
X(BOOL,GetFileVersionInfoSizeA,(LPCSTR a,LPDWORD b),(a,b),FALSE)
X(BOOL,GetFileVersionInfoSizeExA,(DWORD a,LPCSTR b,LPDWORD c),(a,b,c),FALSE)
X(BOOL,GetFileVersionInfoSizeExW,(DWORD a,LPCWSTR b,LPDWORD c),(a,b,c),FALSE)
X(BOOL,GetFileVersionInfoSizeW,(LPCWSTR a,LPDWORD b),(a,b),FALSE)
X(BOOL,GetFileVersionInfoW,(LPCWSTR a,DWORD b,DWORD c,PVOID d),(a,b,c,d),FALSE)

/* VerFindFile — 在系统中定位文件版本 */
X(DWORD,VerFindFileA,(DWORD a,LPCSTR b,LPCSTR c,LPCSTR d,LPSTR e,PUINT f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h),0)
X(DWORD,VerFindFileW,(DWORD a,LPCWSTR b,LPCWSTR c,LPCWSTR d,LPWSTR e,PUINT f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h),0)

/* VerInstallFile — 安装文件并处理版本冲突 */
X(DWORD,VerInstallFileA,(DWORD a,LPCSTR b,LPCSTR c,LPCSTR d,LPCSTR e,LPCSTR f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h),0)
X(DWORD,VerInstallFileW,(DWORD a,LPCWSTR b,LPCWSTR c,LPCWSTR d,LPCWSTR e,LPCWSTR f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h),0)

/* VerLanguageName — 将语言 ID 转为可读名称 */
X(BOOL,VerLanguageNameA,(DWORD a,LPSTR b,DWORD c),(a,b,c),FALSE)
X(BOOL,VerLanguageNameW,(DWORD a,LPWSTR b,DWORD c),(a,b,c),FALSE)

/* VerQueryValue — 从版本信息块中提取指定字段 */
X(DWORD,VerQueryValueA,(LPCVOID a,LPCSTR b,LPVOID*c,PUINT d),(a,b,c,d),0)
X(DWORD,VerQueryValueW,(LPCVOID a,LPCWSTR b,LPVOID*c,PUINT d),(a,b,c,d),0)

/* ============================================================
 * DllMain — DLL 入口函数
 * ============================================================
 *
 * 当此代理 DLL 被游戏进程加载时，Windows 自动调用此函数。
 *
 * DLL_PROCESS_ATTACH (r == 1):
 *   ① DisableThreadLibraryCalls — 禁止后续线程通知，减少 DLL 开销
 *   ② 构造日志文件路径 → 打开 vh_log.txt
 *   ③ 加载真实 version.dll (load_real) → 验证转发链路可用
 *   ④ Hook MessageBoxA → 自动拦截校验弹窗
 *
 *   Step ③ 在 ④ 之前执行是必要的——如果真 version.dll 加载失败，
 *   游戏即使没有弹窗也无法正常运行，Hook 弹窗没有意义。
 *
 * DLL_PROCESS_DETACH (r == 0):
 *   ① 记录累计拦截数量
 *   ② 关闭日志文件
 *   ③ 注意: 不 unhook（不恢复原始函数）。理由同上——进程即将终止。
 *
 * 日志文件命名:
 *   vh_log.txt = "version_hook log"
 *   路径 = 本 DLL 所在目录 + "vh_log.txt"
 *
 * 注: (HINSTANCE)h 参数是 DllMain 的标准参数，这里用于
 *     GetModuleFileNameA 获取本 DLL 的路径，进而确定日志文件位置。
 */
BOOL WINAPI DllMain(HMODULE h, DWORD r, PVOID v) {
    if (r == 1) {  /* DLL_PROCESS_ATTACH (值为 1, 未 include windows.h) */
        DisableThreadLibraryCalls(h);

        /* ---- 构造日志文件路径: <DLL目录>\vh_log.txt ---- */
        char lp[MAX_PATH];
        GetModuleFileNameA(h, lp, MAX_PATH);
        /* 找到最后一个目录分隔符 */
        char *p = lp;
        for (char *s = lp; *s; s++)
            if (*s == '\\' || *s == '/') p = s;
        /* 截断为目录路径 */
        p[1] = '\0';
        /* 手动拼接 "vh_log.txt" */
        char *e = lp;
        while (*e) e++;
        *e++ = 'v'; *e++ = 'h'; *e++ = '_'; *e++ = 'l'; *e++ = 'o';
        *e++ = 'g'; *e++ = '.'; *e++ = 't'; *e++ = 'x'; *e++ = 't';
        *e = '\0';

        /* 创建/覆盖日志文件 */
        g_log = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        log_write("[version_hook] loaded");

        /* ---- 加载真实 version.dll，建立转发链路 ---- */
        load_real();
        log_write("[version_hook] real version.dll loaded");

        /* ---- Hook MessageBoxA，拦截校验弹窗 ---- */
        PBYTE ma = gfn("user32.dll", "MessageBoxA");
        if (ma && hook5(ma, (PBYTE)Hook_MBA, &g_tmba)) {
            log_write("[version_hook] MessageBoxA hooked");
        } else {
            log_write("[version_hook] MessageBoxA hook FAILED");
        }
    } else if (r == 0) {  /* DLL_PROCESS_DETACH (值为 0) */
        char m[100];
        wsprintfA(m, "[version_hook] unloaded, total: %d", g_count);
        log_write(m);
        if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    }
    return TRUE;
}
