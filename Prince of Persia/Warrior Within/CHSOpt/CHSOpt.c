/* ═══════════════════════════════════════════════════════════════
 *          CHSOpt.asi — 波斯王子2 汉化优化插件
 * ═══════════════════════════════════════════════════════════════
 *
 * ▎目标
 *   让 POP2 汉化补丁（out.dll / pop2.dll / local.dll）在
 *   原版 EAX.DLL（无汉化组修改）上也能正常工作。
 *
 * ▎原理
 *   汉化组修改过的 EAX.DLL 在 0x1001F000 处追加了一页导入表
 *   (.imptab 节)，out.dll 在 DllMain 中通过 CALL [0x1001F00F]
 *   调用字体初始化函数 font()。
 *   本插件在运行时在 0x1001F000 伪造该页，并将 4 处间接 CALL
 *   改写为直接相对 CALL out.dll!font。
 *
 * ▎功能
 *   1. 主动加载 out.dll，不依赖汉化版 EAX.DLL
 *   2. 伪造 EAX IAT 页，解决 out.dll 读取 0x1001F00F 崩溃
 *   3. 将 out.dll 内 4 处 CALL [0x1001F00F] → CALL out.dll!font
 *   4. 抑制 MSVC Debug CRT 的 INT3 断言崩溃 (ESC/菜单键)
 *   5. 可配置字体缩放
 *   6. 可屏蔽汉化组信息弹窗
 *   7. 修复进程退出后不驻留
 *   8. POP2.EXE 宽字符边界保护 (ESC 菜单 0xFFxx 越界)
 *
 * ▎构建
 *   不依赖 windows.h / import lib。
 *   通过 PEB 手动解析 kernel32/kernelbase 导出。
 *
 * ▎文件
 *   scripts/CHSOpt.asi  — 本插件
 *   scripts/CHSOpt.ini  — 配置文件
 *   scripts/CHSOpt.log  — 诊断日志（Log=1 时生成）
 * ═══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
 * 类型定义 — 仅用原生 C 类型，避免依赖 windows.h
 * ═══════════════════════════════════════════════════════════════ */
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned int        UINT;
typedef unsigned long       DWORD;
typedef long                LONG;
typedef int                 BOOL;
typedef void*               PVOID;
typedef void*               HANDLE;
typedef void*               HMODULE;
typedef const char*         LPCSTR;
typedef char*               LPSTR;
typedef unsigned long       SIZE_T;
typedef unsigned int        size_t;

/* MSVC /O1 优化会将简单循环替换为 CRT memcpy/memset 调用。
   我们无 CRT 依赖，用 #pragma function 禁止内置 intrinsic 优化。 */
#pragma function(memset, memcpy)
void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* dd = (unsigned char*)d;
    const unsigned char* ss = (const unsigned char*)s;
    while (n--) *dd++ = *ss++;
    return d;
}

/* ═══════════════════════════════════════════════════════════════
 * 函数指针类型 — 需要手动解析的 kernel32 API
 * ═══════════════════════════════════════════════════════════════ */
typedef DWORD (__stdcall *PFN_ThreadProc)(PVOID);
typedef HMODULE (__stdcall *PFN_LoadLibraryA)(LPCSTR);
typedef BOOL   (__stdcall *PFN_VirtualProtect)(PVOID, SIZE_T, DWORD, DWORD*);
typedef PVOID  (__stdcall *PFN_VirtualAlloc)(PVOID, SIZE_T, DWORD, DWORD);
typedef HANDLE (__stdcall *PFN_CreateThread)(PVOID, SIZE_T, PFN_ThreadProc, PVOID, DWORD, DWORD*);
typedef BOOL   (__stdcall *PFN_CloseHandle)(HANDLE);
typedef void   (__stdcall *PFN_Sleep)(DWORD);
typedef UINT   (__stdcall *PFN_GetPrivateProfileIntA)(LPCSTR, LPCSTR, int, LPCSTR);
typedef UINT   (__stdcall *PFN_GetPrivateProfileStringA)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
typedef BOOL   (__stdcall *PFN_FlushInstructionCache)(HANDLE, PVOID, SIZE_T);
typedef HANDLE (__stdcall *PFN_GetCurrentProcess)(void);
typedef HANDLE (__stdcall *PFN_CreateFileA)(LPCSTR, DWORD, DWORD, PVOID, DWORD, DWORD, HANDLE);
typedef BOOL   (__stdcall *PFN_WriteFile)(HANDLE, const void*, DWORD, DWORD*, PVOID);
typedef DWORD  (__stdcall *PFN_SetFilePointer)(HANDLE, LONG, LONG*, DWORD);
typedef void   (__stdcall *PFN_OutputDebugStringA)(LPCSTR);
typedef BOOL   (__stdcall *PFN_DisableThreadLibraryCalls)(HMODULE);

/* ═══════════════════════════════════════════════════════════════
 * 常量
 * ═══════════════════════════════════════════════════════════════ */
#define TRUE                    1
#define FALSE                   0
#define DLL_PROCESS_ATTACH      1
#define PAGE_EXECUTE_READWRITE  0x40
#define MEM_COMMIT              0x1000
#define MEM_RESERVE             0x2000
#define GENERIC_WRITE           0x40000000UL
#define FILE_SHARE_READ         0x00000001UL
#define OPEN_ALWAYS             4
#define FILE_ATTRIBUTE_NORMAL   0x80
#define FILE_END                2
#define INVALID_HANDLE_VALUE    ((HANDLE)(LONG)-1)

/* ── 伪造 EAX IAT 页地址 ─────────────────────────────────────
 * 汉化版 EAX.DLL 在 0x1001F000 有一页自定义导入表 (.imptab)，
 * out.dll 通过 CALL DS:[0x1001F00F] 调用 font()。
 * 此处定义我们将在进程内存中伪造的页地址。 */
#define FAKE_EAX_IAT_PAGE   ((BYTE*)0x1001F000)
#define FAKE_EAX_FONT_IAT   ((void**)0x1001F00F)

/* ═══════════════════════════════════════════════════════════════
 * 全局状态
 * ═══════════════════════════════════════════════════════════════ */
static int    g_FontScale100  = 200;   /* 字体缩放倍率 ×100，默认 2.0 */
static int    g_ShowInfoPopup = 1;     /* 1=显示汉化组弹窗 0=屏蔽 */
static int    g_LogEnabled    = 1;     /* 1=写 CHSOpt.log 0=关闭 */
static DWORD  g_PatchCount    = 0;     /* 成功应用的补丁计数 */

/* ── API 函数表 ──────────────────────────────────────────────
 * 所有 kernel32/kernelbase API 通过 PEB 遍历 + 导出表解析获得，
 * 不依赖 PE 导入表，因此本 ASI 没有任何 import library 依赖。 */
struct API {
    PFN_LoadLibraryA            LoadLibraryA;
    PFN_VirtualProtect          VirtualProtect;
    PFN_VirtualAlloc            VirtualAlloc;
    PFN_CreateThread            CreateThread;
    PFN_CloseHandle             CloseHandle;
    PFN_Sleep                   Sleep;
    PFN_GetPrivateProfileIntA   GetPrivateProfileIntA;
    PFN_GetPrivateProfileStringA GetPrivateProfileStringA;
    PFN_FlushInstructionCache   FlushInstructionCache;
    PFN_GetCurrentProcess       GetCurrentProcess;
    PFN_CreateFileA             CreateFileA;
    PFN_WriteFile               WriteFile;
    PFN_SetFilePointer          SetFilePointer;
    PFN_OutputDebugStringA      OutputDebugStringA;
    PFN_DisableThreadLibraryCalls DisableThreadLibraryCalls;
};
static struct API k;

/* ═══════════════════════════════════════════════════════════════
 * 工具函数 — 微型 libc 替代
 * ═══════════════════════════════════════════════════════════════ */

/* 不区分大小写的 ASCII 字符串比较 */
static int streqi_ascii(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

/* 自制 strlen */
static DWORD cstrlen(const char* s) {
    DWORD n = 0;
    while (s && s[n]) ++n;
    return n;
}

/* 自制 memcpy（用简单循环避免 CRT 调用） */
static void memcopy(void* d, const void* s, DWORD n) {
    BYTE* dd = (BYTE*)d;
    const BYTE* ss = (const BYTE*)s;
    while (n--) *dd++ = *ss++;
}

/* 将 DWORD 格式化为 8 位十六进制字符串 */
static void write_hex8(char* out, DWORD v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; ++i)
        out[i] = h[(v >> (28 - i * 4)) & 0xF];
    out[8] = 0;
}

/* ── 日志 ──────────────────────────────────────────────
 * 写入 scripts/CHSOpt.log，同时输出到调试器 (OutputDebugStringA)。
 * 受 g_LogEnabled 控制，设为 0 则完全跳过。 */
static void log_line(const char* s) {
    DWORD w;
    HANDLE f;
    if (!g_LogEnabled) return;
    if (k.OutputDebugStringA) k.OutputDebugStringA(s);
    if (!k.CreateFileA || !k.WriteFile || !k.CloseHandle) return;
    f = k.CreateFileA("scripts\\CHSOpt.log", GENERIC_WRITE, FILE_SHARE_READ, 0,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    if (k.SetFilePointer) k.SetFilePointer(f, 0, 0, FILE_END);
    k.WriteFile(f, s, cstrlen(s), &w, 0);
    k.WriteFile(f, "\r\n", 2, &w, 0);
    k.CloseHandle(f);
}

/* 带十六进制前缀的日志输出 */
static void log_hex(const char* prefix, DWORD value) {
    char buf[128], hx[9];
    DWORD i = 0, j = 0;
    write_hex8(hx, value);
    while (prefix && prefix[j] && i < sizeof(buf) - 1)
        buf[i++] = prefix[j++];
    j = 0;
    while (hx[j] && i < sizeof(buf) - 1)
        buf[i++] = hx[j++];
    buf[i] = 0;
    log_line(buf);
}

/* ═══════════════════════════════════════════════════════════════
 * PEB (进程环境块) 遍历 — 绕过导入表定位模块
 * ═══════════════════════════════════════════════════════════════
 *
 * 原理：
 *   Windows 线程信息块 (TIB) 的 fs:[0x30] 指向 PEB，
 *   PEB+0x0C → LDR (Loader Data)，
 *   LDR+0x0C → InLoadOrderModuleList (双向链表)。
 *   遍历该链表即可获取所有已加载模块的基址。
 *
 * 这样就不需要调用 GetModuleHandleA (避免导入表依赖)。
 */

/* 通过 FS 段寄存器读取 PEB 地址（x86 内联汇编） */
static void* get_peb(void) {
    void* p;
    __asm { mov eax, fs:[0x30] }
    __asm { mov p, eax }
    return p;
}

/* 判断 UNICODE_STRING 是否和 ASCII 字符串相等（不区分大小写） */
static int unicode_basename_eq(void* us, const char* ascii) {
    WORD  len  = *(WORD*)((BYTE*)us + 0);      /* UNICODE_STRING.Length */
    WORD* buf  = *(WORD**)((BYTE*)us + 4);     /* UNICODE_STRING.Buffer */
    DWORD ai = 0, wi, chars = len / 2;
    if (!buf) return 0;
    for (wi = 0; wi < chars && ascii[ai]; ++wi, ++ai) {
        char ca = ascii[ai];
        WORD cw = buf[wi];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cw >= 'A' && cw <= 'Z') cw = (WORD)(cw + 32);
        if ((WORD)(unsigned char)ca != cw) return 0;
    }
    return wi == chars && ascii[ai] == 0;
}

/* 根据 DLL 基名遍历 PEB 找到模块基址 */
static HMODULE peb_get_module(const char* baseName) {
    BYTE* peb   = (BYTE*)get_peb();
    BYTE* ldr   = *(BYTE**)(peb + 0x0C);        /* PEB.Ldr */
    BYTE* head  = ldr + 0x0C;                    /* InLoadOrderModuleList head */
    BYTE* cur   = *(BYTE**)head;                 /* 第一个节点 */
    int guard   = 0;
    while (cur && cur != head && guard++ < 256) {
        HMODULE dllBase    = *(HMODULE*)(cur + 0x18);  /* LDR_DATA_TABLE_ENTRY.DllBase */
        void*   baseDllName = cur + 0x2C;              /* LDR_DATA_TABLE_ENTRY.BaseDllName */
        if (unicode_basename_eq(baseDllName, baseName))
            return dllBase;
        cur = *(BYTE**)cur;                            /* Flink → 下一节点 */
    }
    return 0;
}

/* 将模块名转为小写 dll 文件名（如 "kernel32" → "kernel32.dll"） */
static void make_module_dll_name(const char* module, char* out) {
    DWORD i = 0, dot = 0;
    while (module[i] && module[i] != '.' && i < 60) {
        char c = module[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        out[i] = c;
        ++i;
    }
    if (module[i] == '.') dot = 1;
    if (!dot) {
        out[i++] = '.'; out[i++] = 'd'; out[i++] = 'l'; out[i++] = 'l';
    }
    out[i] = 0;
}

/* 前向声明 */
static void* resolve_export(HMODULE mod, const char* name, int depth);

/* ── 解析转发导出 (forwarder) ─────────────────────────────
 * 某些 API 是转发导出（如 kernel32.VirtualAlloc → kernelbase.VirtualAlloc），
 * 需要递归解析。 */
static void* resolve_forwarder(const char* fwd, int depth) {
    char modName[80], funcName[96];
    DWORD i = 0, j = 0;
    HMODULE m;
    if (depth > 4) return 0;          /* 防止循环转发 */
    /* 解析 "MODULE.function" 格式 */
    while (fwd[i] && fwd[i] != '.' && i < sizeof(modName) - 5) {
        modName[i] = fwd[i];
        ++i;
    }
    if (fwd[i] != '.') return 0;
    modName[i] = 0;
    make_module_dll_name(modName, modName);
    ++i;
    while (fwd[i] && j < sizeof(funcName) - 1)
        funcName[j++] = fwd[i++];
    funcName[j] = 0;

    m = peb_get_module(modName);
    if (!m && k.LoadLibraryA) m = k.LoadLibraryA(modName);
    if (!m || funcName[0] == '#') return 0;  /* 不支持序号导出 */
    return resolve_export(m, funcName, depth + 1);
}

/* ── 从 DLL 的导出表中按名称解析函数地址 ────────────
 * 遍历 PE 导出目录 (DataDirectory[0]) 的 AddressOfNames /
 * AddressOfFunctions / AddressOfNameOrdinals 三表。 */
static void* resolve_export(HMODULE mod, const char* name, int depth) {
    BYTE* base = (BYTE*)mod;
    DWORD e, opt, er, es, nn, fr, nr, orv, i;
    BYTE* exp;
    DWORD* funcs;
    DWORD* names;
    WORD*  ords;

    if (!base) return 0;
    if (*(WORD*)base != 0x5A4D) return 0;           /* "MZ" 签名 */
    e = *(DWORD*)(base + 0x3C);                      /* e_lfanew */
    if (*(DWORD*)(base + e) != 0x00004550) return 0; /* "PE" 签名 */
    opt = e + 0x18;                                  /* OptionalHeader 起始 */
    er  = *(DWORD*)(base + opt + 0x60);              /* 导出表 RVA */
    es  = *(DWORD*)(base + opt + 0x64);              /* 导出表大小 */
    if (!er) return 0;

    exp = base + er;
    nn   = *(DWORD*)(exp + 0x18);   /* NumberOfNames */
    fr   = *(DWORD*)(exp + 0x1C);   /* AddressOfFunctions RVA */
    nr   = *(DWORD*)(exp + 0x20);   /* AddressOfNames RVA */
    orv  = *(DWORD*)(exp + 0x24);   /* AddressOfNameOrdinals RVA */

    funcs = (DWORD*)(base + fr);
    names = (DWORD*)(base + nr);
    ords  = (WORD*)(base + orv);

    for (i = 0; i < nn; ++i) {
        const char* n = (const char*)(base + names[i]);
        if (streqi_ascii(n, name)) {
            DWORD r = funcs[ords[i]];
            /* 如果函数地址落在导出表范围内，说明是转发导出 */
            if (r >= er && r < er + es)
                return resolve_forwarder((const char*)(base + r), depth + 1);
            return (void*)(base + r);
        }
    }
    return 0;
}

/* ── 批量解析 kernel32/kernelbase API ────────────────
 * 优先找 kernel32.dll，找不到再找 kernelbase.dll。
 * 只要求 LoadLibraryA + VirtualProtect + VirtualAlloc 三个
 * 核心 API 解析成功，其余可选。 */
static int resolve_kernel_apis(void) {
    HMODULE k32 = peb_get_module("kernel32.dll");
    HMODULE kb  = peb_get_module("kernelbase.dll");
    if (!k32 && !kb) return 0;

#define R(name) \
    k.name = (PFN_##name)(k32 ? resolve_export(k32, #name, 0) : 0); \
    if (!k.name && kb) k.name = (PFN_##name)resolve_export(kb, #name, 0)

    R(LoadLibraryA);
    R(VirtualProtect);
    R(VirtualAlloc);
    R(CreateThread);
    R(CloseHandle);
    R(Sleep);
    R(GetPrivateProfileIntA);
    R(GetPrivateProfileStringA);
    R(FlushInstructionCache);
    R(GetCurrentProcess);
    R(CreateFileA);
    R(WriteFile);
    R(SetFilePointer);
    R(OutputDebugStringA);
    R(DisableThreadLibraryCalls);
#undef R

    return k.LoadLibraryA && k.VirtualProtect && k.VirtualAlloc;
}

/* ═══════════════════════════════════════════════════════════════
 * 内存补丁工具函数
 * ═══════════════════════════════════════════════════════════════ */

/* 在绝对地址写入补丁字节 */
static BOOL patch_abs_bytes(BYTE* addr, const BYTE* bytes, DWORD len) {
    DWORD oldp = 0;
    if (!k.VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldp))
        return FALSE;
    memcopy(addr, bytes, len);
    k.VirtualProtect(addr, len, oldp, &oldp);
    if (k.FlushInstructionCache && k.GetCurrentProcess)
        k.FlushInstructionCache(k.GetCurrentProcess(), addr, len);
    return TRUE;
}

/* 在模块基址 + RVA 处写入补丁 */
static BOOL patch_bytes(HMODULE h, DWORD rva, const BYTE* bytes, DWORD len) {
    return patch_abs_bytes((BYTE*)h + rva, bytes, len);
}

/* 写入 1 字节 */
static BOOL patch_byte(HMODULE h, DWORD rva, BYTE val) {
    return patch_bytes(h, rva, &val, 1);
}

/* 比例缩放 imm8 立即数 */
static BOOL patch_imm8_scaled(HMODULE h, DWORD rva, BYTE orig, int scale100) {
    int v = ((int)orig * scale100 + 50) / 100;  /* 四舍五入 */
    if (v < 2)   v = 2;
    if (v > 255) v = 255;
    return patch_byte(h, rva, (BYTE)v);
}

/* 写入相对偏移 (rel32) 到指定地址 */
static void put_rel32(BYTE* at, BYTE* target) {
    LONG rel = (LONG)(target - (at + 4));
    at[0] = (BYTE)(rel & 0xFF);
    at[1] = (BYTE)((rel >> 8) & 0xFF);
    at[2] = (BYTE)((rel >> 16) & 0xFF);
    at[3] = (BYTE)((rel >> 24) & 0xFF);
}

/* 绝对地址跳转补丁：E9 rel32 + NOP 填充 */
static BOOL patch_abs_jmp(BYTE* src, BYTE* dst, DWORD overwrite_len) {
    BYTE  b[16];
    DWORD i;
    LONG  rel;
    if (overwrite_len < 5 || overwrite_len > sizeof(b)) return FALSE;
    b[0] = 0xE9;                                       /* JMP 指令 */
    rel  = (LONG)(dst - (src + 5));
    b[1] = (BYTE)(rel & 0xFF);
    b[2] = (BYTE)((rel >> 8) & 0xFF);
    b[3] = (BYTE)((rel >> 16) & 0xFF);
    b[4] = (BYTE)((rel >> 24) & 0xFF);
    for (i = 5; i < overwrite_len; ++i) b[i] = 0x90;   /* NOP */
    return patch_abs_bytes(src, b, overwrite_len);
}

/* 在当前地址生成 JMP 跳板 */
static BYTE* emit_jmp(BYTE* p, BYTE* target) {
    p[0] = 0xE9;
    put_rel32(p + 1, target);
    return p + 5;
}

/* 间接 CALL 转直接 CALL：将 `CALL [XXX]` 改写为 `CALL target; NOP; NOP`
 *
 * 原指令格式 (7 字节):
 *   3E FF 15 0F F0 01 10   → CALL DS:[0x1001F00F]
 * 改写后:
 *   E8 xx xx xx xx 90 90   → CALL target; NOP; NOP */
static BOOL patch_direct_call(HMODULE h, DWORD rva, void* target) {
    BYTE  b[7];
    BYTE* src = (BYTE*)h + rva;
    LONG  rel = (LONG)((BYTE*)target - (src + 5));
    b[0] = 0xE8;                                            /* CALL */
    b[1] = (BYTE)(rel & 0xFF);
    b[2] = (BYTE)((rel >> 8) & 0xFF);
    b[3] = (BYTE)((rel >> 16) & 0xFF);
    b[4] = (BYTE)((rel >> 24) & 0xFF);
    b[5] = 0x90; b[6] = 0x90;                              /* NOP NOP */
    return patch_bytes(h, rva, b, 7);
}

/* ═══════════════════════════════════════════════════════════════
 * 配置读取 — 解析 scripts/CHSOpt.ini
 * ═══════════════════════════════════════════════════════════════ */

/* 解析 "1.5" 格式的字符串为 ×100 整数（返回 150） */
static int parse_scale100(const char* s) {
    int whole = 0, frac = 0, fd = 0;
    while (*s == ' ' || *s == '\t') ++s;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s - '0'); ++s; }
    if (*s == '.') {
        ++s;
        while (*s >= '0' && *s <= '9' && fd < 2) {
            frac = frac * 10 + (*s - '0'); ++s; ++fd;
        }
    }
    if (fd == 1) frac *= 10;   /* 如 ".5" → 50 */
    return whole * 100 + frac;
}

/* 读取全部配置项 */
static void read_config(void) {
    char buf[32];
    int v;

    /* 默认值 2.0 */
    buf[0] = '2'; buf[1] = '.'; buf[2] = '0'; buf[3] = 0;

    /* [Font] Scale — 字体缩放 */
    if (k.GetPrivateProfileStringA) {
        k.GetPrivateProfileStringA("Font", "Scale", "2.0", buf, sizeof(buf),
                                   "scripts\\CHSOpt.ini");
        g_FontScale100 = parse_scale100(buf);
    } else if (k.GetPrivateProfileIntA) {
        g_FontScale100 = (int)k.GetPrivateProfileIntA("Font", "Scale", 2,
                                                      "scripts\\CHSOpt.ini") * 100;
    }
    if (g_FontScale100 < 100) g_FontScale100 = 200;
    if (g_FontScale100 > 800) g_FontScale100 = 800;

    /* [General] ShowInfoPopup — 汉化组弹窗 */
    v = 1;
    if (k.GetPrivateProfileIntA)
        v = (int)k.GetPrivateProfileIntA("General", "ShowInfoPopup", 1,
                                         "scripts\\CHSOpt.ini");
    g_ShowInfoPopup = (v != 0);

    /* [General] Log — 日志开关 */
    v = 1;
    if (k.GetPrivateProfileIntA)
        v = (int)k.GetPrivateProfileIntA("General", "Log", 1,
                                         "scripts\\CHSOpt.ini");
    g_LogEnabled = (v != 0);
}

/* ═══════════════════════════════════════════════════════════════
 * 伪造 EAX IAT 页
 * ═══════════════════════════════════════════════════════════════
 *
 * 汉化版 EAX.DLL 在 0x1001F000 有一页包含 font 函数指针的导入表。
 * 原版 EAX.DLL 没有这页，我们需要在进程内存中补上。
 *
 * 流程：
 *   1. VirtualAlloc(0x1001F000, …) 在目标地址申请内存
 *   2. 将 out.dll!font 的地址写入 0x1001F00F
 *   3. 之后 out.dll 的 CALL [0x1001F00F] 就能找到 font()
 *
 * 即使 VirtualAlloc 失败（已有映射），也尝试直接写。
 */

/* 临时占位函数 — 防止 IAT 为空时误调用返回垃圾 */
static int __cdecl dummy_font(void) {
    return 0;
}

/* 将值写入 IAT 槽 */
static BOOL write_fake_iat(void* value) {
    DWORD oldp = 0;
    if (!k.VirtualProtect(FAKE_EAX_FONT_IAT, 4, PAGE_EXECUTE_READWRITE, &oldp))
        return FALSE;
    *FAKE_EAX_FONT_IAT = value;
    k.VirtualProtect(FAKE_EAX_FONT_IAT, 4, oldp, &oldp);
    return TRUE;
}

/* 确保 IAT 页存在并写入 font 地址 */
static BOOL ensure_fake_eax_iat_page(void) {
    DWORD oldp = 0;
    void* page = k.VirtualAlloc(FAKE_EAX_IAT_PAGE, 0x1000,
                                MEM_RESERVE | MEM_COMMIT,
                                PAGE_EXECUTE_READWRITE);
    if (!page) {
        /* 页已存在（被汉化 EAX 或其他模块映射），尝试直接改保护 */
        if (!k.VirtualProtect(FAKE_EAX_FONT_IAT, 4, PAGE_EXECUTE_READWRITE, &oldp))
            return FALSE;
        k.VirtualProtect(FAKE_EAX_FONT_IAT, 4, oldp, &oldp);
    }
    /* 先放 dummy_font 占位，后续 patch_out_dll 会用真实地址覆盖 */
    return write_fake_iat((void*)dummy_font);
}

/* ═══════════════════════════════════════════════════════════════
 * POP2.EXE 补丁 — 宽字符边界保护
 * ═══════════════════════════════════════════════════════════════
 *
 * 问题：
 *   游戏在 ESC 菜单中渲染文本时，遇到中文字符 (0xFFxx) 会进入
 *   glyph metrics 查表函数产生越界读，导致崩溃。
 *
 * 修复（POP2.EXE + 0x42A2C0）：
 *   在原 cmp ax,20 / jb skip 基础上，增加 cmp ax,0100 / jae skip，
 *   让宽字符和原版控制字符走同一个跳过路径。
 */

/* 获取主 EXE 基址（POP2.EXE 或 PrinceOfPersia.exe） */
static HMODULE get_main_exe_module(void) {
    HMODULE h = peb_get_module("POP2.EXE");
    if (!h) h = peb_get_module("PrinceOfPersia.exe");
    if (!h) h = (HMODULE)0x00400000;  /* 默认 PE 基址 */
    return h;
}

static BOOL patch_pop2_width_guard(void) {
    HMODULE hExe = get_main_exe_module();
    BYTE*   base = (BYTE*)hExe;
    BYTE*   site = base + 0x42A2C0;   /* width/metrics 查表前 */
    BYTE*   stub;
    BYTE*   p;
    DWORD   ok = 0;

    log_hex("CHSOpt: POP2.EXE base=0x", (DWORD)base);

    /* 验证目标地址特征 */
    if (!(site[0] == 0x66 && site[1] == 0x3D && site[2] == 0x20 &&
          site[3] == 0x00 && site[4] == 0x72 && site[5] == 0x45))
        log_line("CHSOpt: WARNING width-only guard signature mismatch");

    /* 在进程内存中分配跳板 */
    stub = (BYTE*)k.VirtualAlloc(0, 0x100, MEM_RESERVE | MEM_COMMIT,
                                 PAGE_EXECUTE_READWRITE);
    if (!stub) {
        log_line("CHSOpt: ERROR cannot allocate POP2 width guard stub");
        return FALSE;
    }

    /* 构造跳板代码：
     *   cmp ax, 20        ; 原逻辑：小于 0x20 的控制字符跳过
     *   jb  skip
     *   cmp ax, 0100      ; 新增：大于等于 0x100 的中文字符也跳过
     *   jae skip
     *   jmp continue      ; 正常走查表逻辑 */
    p = stub;
    p[0] = 0x66; p[1] = 0x83; p[2] = 0xF8; p[3] = 0x20; p += 4;       /* cmp ax, 20 */
    p[0] = 0x0F; p[1] = 0x82; put_rel32(p + 2, base + 0x42A30B); p += 6; /* jb skip */
    p[0] = 0x66; p[1] = 0x3D; p[2] = 0x00; p[3] = 0x01; p += 4;       /* cmp ax, 0100 */
    p[0] = 0x0F; p[1] = 0x83; put_rel32(p + 2, base + 0x42A30B); p += 6; /* jae skip */
    p = emit_jmp(p, base + 0x42A2C6);                                   /* jmp continue */

    ok += patch_abs_jmp(site, stub, 6);
    if (k.FlushInstructionCache && k.GetCurrentProcess)
        k.FlushInstructionCache(k.GetCurrentProcess(), stub, (SIZE_T)(p - stub));
    log_hex("CHSOpt: POP2 width-only guard patches=0x", ok);
    return ok == 1;
}

/* ═══════════════════════════════════════════════════════════════
 * Debug CRT INT3 自动扫描修复
 * ═══════════════════════════════════════════════════════════════
 *
 * 扫描 out.dll 的 .text 段，查找 MSVC Debug 断言模式并 NOP 掉 INT3。
 *
 * 匹配模式：
 *   1. 83 F8 01 75 01 CC   → cmp eax,1 / jne +1 / int3
 *   2. 75 01 CC            → jne +1 / int3 (更宽松匹配)
 */
static DWORD nop_debug_int3_patterns(HMODULE hOut) {
    BYTE*  text = (BYTE*)hOut + 0x1000;
    DWORD  size = 0x30800;
    DWORD  i, n = 0;

    for (i = 0; i + 6 <= size; ++i) {
        /* 完整模式：cmp eax, 1 / jne +1 / int3 */
        if (text[i] == 0x83 && text[i+1] == 0xF8 &&
            text[i+2] == 0x01 && text[i+3] == 0x75 &&
            text[i+4] == 0x01 && text[i+5] == 0xCC) {
            if (patch_byte(hOut, 0x1000 + i + 5, 0x90)) ++n;
        }
        /* 宽松模式：任意 jne +1 / int3 */
        else if (text[i] == 0x75 && text[i+1] == 0x01 && text[i+2] == 0xCC) {
            if (patch_byte(hOut, 0x1000 + i + 2, 0x90)) ++n;
        }
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════
 * MessageBoxA 临时抑制
 * ═══════════════════════════════════════════════════════════════
 *
 * 在加载 out.dll 时临时 hook user32!MessageBoxA，
 * 防止汉化组信息弹窗弹出。加载完成后恢复原函数。
 *
 * 抑制补丁 (8 字节)：
 *   33 C0          xor eax, eax    ; 返回 IDOK（0）
 *   C2 10 00       ret 16          ; 清理 4 个参数
 *   90 90 90       nop nop nop     ; 填充 */
static BYTE  g_msgbox_orig[8];      /* 保存的原始指令 */
static BYTE* g_msgbox_addr  = 0;
static int   g_msgbox_patched = 0;

static BOOL patch_user32_messagebox(BOOL suppress) {
    HMODULE u;
    BYTE stub[8] = {0x33, 0xC0, 0xC2, 0x10, 0x00, 0x90, 0x90, 0x90};

    if (suppress) {
        /* ── 启用抑制 ── */
        if (g_msgbox_patched) return TRUE;
        u = peb_get_module("user32.dll");
        if (!u && k.LoadLibraryA) u = k.LoadLibraryA("user32.dll");
        if (!u) return FALSE;
        g_msgbox_addr = (BYTE*)resolve_export(u, "MessageBoxA", 0);
        if (!g_msgbox_addr) return FALSE;
        memcopy(g_msgbox_orig, g_msgbox_addr, 8);   /* 保存原始字节 */
        if (!patch_abs_bytes(g_msgbox_addr, stub, 8)) return FALSE;
        g_msgbox_patched = 1;
        log_line("CHSOpt: temporary MessageBoxA suppress enabled");
        return TRUE;
    } else {
        /* ── 恢复 ── */
        if (g_msgbox_patched && g_msgbox_addr) {
            patch_abs_bytes(g_msgbox_addr, g_msgbox_orig, 8);
            log_line("CHSOpt: temporary MessageBoxA suppress restored");
        }
        g_msgbox_patched = 0;
        return TRUE;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * out.dll 补丁 — 核心补丁
 * ═══════════════════════════════════════════════════════════════
 *
 * 对已加载的 out.dll 应用全部运行时补丁。
 *
 * 补丁清单：
 *   ┌──────┬────────┬────────────────────────────────┐
 *   │ Part │  RVA   │ 作用                          │
 *   ├──────┼────────┼────────────────────────────────┤
 *   │ A    │ 0x111A │ CALL [0x1001F00F] → CALL font │
 *   │ A    │ 0x1150 │ CALL [0x1001F00F] → CALL font │
 *   │ A    │ 0x1175 │ CALL [0x1001F00F] → CALL font │
 *   │ A    │ 0x11AB │ CALL [0x1001F00F] → CALL font │
 *   ├──────┼────────┼────────────────────────────────┤
 *   │ B    │ 0x70C3 │ INT3 (ESC 菜单) → NOP         │
 *   │ B    │ 0x7155 │ INT3 (手柄菜单) → NOP         │
 *   │ B    │ 0x717F │ INT3 (循环后断言) → NOP       │
 *   │ B    │ 0x7FC0 │ _CrtDbgReport → xor eax,ret   │
 *   ├──────┼────────┼────────────────────────────────┤
 *   │ C    │ 0x22DC │ push 0x10 → push (16×scale)  │
 *   │ C    │ 0x2323 │ push 0x20 → push (32×scale)  │
 *   ├──────┼────────┼────────────────────────────────┤
 *   │ D    │ 0x74B2 │ DETACH 跳过 → EB 46          │
 *   │ D    │ 0x121F │ 超时计数器 → 自旋计数        │
 *   │ D    │ 0x128C1│ 自旋等待 → EB (恒跳过)       │
 *   ├──────┼────────┼────────────────────────────────┤
 *   │ E    │ 0x4586 │ 汉化组弹窗 → xor eax,ret      │
 *   └──────┴────────┴────────────────────────────────┘
 */
static void patch_out_dll(HMODULE hOut) {
    void* pFont;
    BYTE dbgReportStub[5] = {0x33, 0xC0, 0xC3, 0x90, 0x90};  /* xor eax,eax; ret; nop; nop */
    BYTE stay1[2]   = {0xEB, 0x46};                            /* JMP +0x46 (跳过 DETACH) */
    BYTE stay2[13]  = {0xFF, 0x45, 0xE8, 0x83, 0x7D, 0xE8,    /* inc [ebp-0x18]; cmp [ebp-0x18],0x1E */
                       0x1E, 0x0F, 0x8D, 0xDA, 0x01, 0x00, 0x00}; /* jge 超时退出 */
    BYTE popupRet[3] = {0x33, 0xC0, 0xC3};                     /* xor eax,eax; ret (跳过弹窗) */
    DWORD n;

    if (!hOut) return;

    /* 解析 out.dll 导出的 font() 函数地址 */
    pFont = resolve_export(hOut, "font", 0);
    if (!pFont) {
        log_line("CHSOpt: ERROR cannot resolve out.dll!font");
        return;
    }

    /* 使用真实 font 地址覆盖 IAT 槽 */
    write_fake_iat(pFont);
    log_hex("CHSOpt: out.dll base=0x", (DWORD)hOut);
    log_hex("CHSOpt: out.dll!font=0x", (DWORD)pFont);

    /* ═══ Part A: 4 处间接 CALL → 直接 CALL ═══ */
    g_PatchCount += patch_direct_call(hOut, 0x111A, pFont);
    g_PatchCount += patch_direct_call(hOut, 0x1150, pFont);
    g_PatchCount += patch_direct_call(hOut, 0x1175, pFont);
    g_PatchCount += patch_direct_call(hOut, 0x11AB, pFont);

    /* ═══ Part B: Debug CRT 崩溃修复 ═══ */
    g_PatchCount += patch_byte(hOut, 0x70C3, 0x90);            /* 原 INT3 */
    g_PatchCount += patch_byte(hOut, 0x7155, 0x90);            /* 原 INT3 */
    g_PatchCount += patch_byte(hOut, 0x717F, 0x90);            /* 原 INT3 */
    g_PatchCount += patch_bytes(hOut, 0x7FC0, dbgReportStub, sizeof(dbgReportStub)); /* _CrtDbgReport 钩子 */
    n = nop_debug_int3_patterns(hOut);                          /* 扫描 NOP 其余断言 */
    g_PatchCount += n;
    log_hex("CHSOpt: debug INT3 patterns nopped=0x", n);

    /* ═══ Part C: 字体缩放 ═══ */
    g_PatchCount += patch_imm8_scaled(hOut, 0x22DC, 0x10, g_FontScale100);
    g_PatchCount += patch_imm8_scaled(hOut, 0x2323, 0x20, g_FontScale100);

    /* ═══ Part D: 进程驻留修复 ═══ */
    g_PatchCount += patch_bytes(hOut, 0x74B2, stay1, sizeof(stay1));
    g_PatchCount += patch_bytes(hOut, 0x121F, stay2, sizeof(stay2));
    g_PatchCount += patch_byte(hOut, 0x128C1, 0xEB);

    /* ═══ Part E: 汉化组信息弹窗屏蔽 ═══ */
    if (!g_ShowInfoPopup) {
        if (patch_bytes(hOut, 0x4586, popupRet, sizeof(popupRet))) {
            g_PatchCount++;
            log_line("CHSOpt: out.dll translation info popup disabled");
        } else {
            log_line("CHSOpt: WARNING popup patch failed");
        }
    } else {
        log_line("CHSOpt: out.dll translation info popup enabled");
    }

    log_hex("CHSOpt: patch count=0x", g_PatchCount);
}

/* ═══════════════════════════════════════════════════════════════
 * 工作线程
 * ═══════════════════════════════════════════════════════════════
 *
 * 在 DllMain 中不能安全地调用 LoadLibraryA（可能导致死锁），
 * 因此将加载和补丁操作移到独立线程执行。
 *
 * 流程：
 *   1. Sleep(200) — 给系统一点时间加载其他模块
 *   2. 读取 CHSOpt.ini 配置
 *   3. 安装 POP2.EXE 宽度守卫
 *   4. 伪造 EAX IAT 页
 *   5. 查找 out.dll，若未加载则自行 LoadLibraryA
 *   6. 对 out.dll 应用全部补丁
 */
static DWORD __stdcall worker_thread(PVOID unused) {
    HMODULE hOut;
    (void)unused;

    /* 等待其他模块加载完毕 */
    if (k.Sleep) k.Sleep(200);

    read_config();
    log_line("CHSOpt: CHSOpt No-EAX worker started");
    log_hex("CHSOpt: Font Scale x100=0x", (DWORD)g_FontScale100);
    log_hex("CHSOpt: ShowInfoPopup=0x",   (DWORD)g_ShowInfoPopup);
    log_hex("CHSOpt: Log=0x",             (DWORD)g_LogEnabled);

    /* POP2.EXE 宽度守卫 */
    if (patch_pop2_width_guard())
        log_line("CHSOpt: POP2 width-only guard ready");
    else
        log_line("CHSOpt: WARNING POP2 width-only guard not applied");

    /* 伪造 EAX IAT 页 */
    if (!ensure_fake_eax_iat_page())
        log_line("CHSOpt: WARNING fake EAX IAT page unavailable; trying anyway");
    else
        log_line("CHSOpt: fake EAX IAT page ready");

    /* 定位 out.dll —— 优先找已加载的，否则自己加载 */
    hOut = peb_get_module("out.dll");
    if (!hOut && k.LoadLibraryA) {
        /* 如果要屏蔽弹窗，在加载 out.dll 前 hook MessageBoxA */
        if (!g_ShowInfoPopup) patch_user32_messagebox(TRUE);

        log_line("CHSOpt: loading out.dll");
        hOut = k.LoadLibraryA("out.dll");

        /* 加载完成后恢复 MessageBoxA */
        if (!g_ShowInfoPopup) patch_user32_messagebox(FALSE);
    }
    if (!hOut) {
        log_line("CHSOpt: ERROR out.dll not loaded");
        return 0;
    }

    /* 应用全部补丁 */
    patch_out_dll(hOut);
    log_line("CHSOpt: done");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * DllMain — ASI 入口
 * ═══════════════════════════════════════════════════════════════
 *
 * 由 Ultimate ASI Loader (dinput8.dll) 在进程启动时调用。
 *
 * 不能在此函数中执行复杂操作（如 LoadLibraryA），
 * 因此通过 CreateThread 委托给 worker_thread。
 * 如果 CreateThread 失败则降级为同步执行。
 */
BOOL __stdcall DllMain(HMODULE hModule, DWORD reason, PVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DWORD tid = 0;
        HANDLE th;

        /* 1. 解析 kernel32 API */
        if (!resolve_kernel_apis()) return TRUE;

        /* 2. 禁用 DLL_THREAD_ATTACH/DETACH 通知 */
        if (k.DisableThreadLibraryCalls) k.DisableThreadLibraryCalls(hModule);

        /* 3. 创建工作线程进行实际补丁操作 */
        th = k.CreateThread ? k.CreateThread(0, 0, worker_thread, 0, 0, &tid) : 0;
        if (th && k.CloseHandle) k.CloseHandle(th);

        /* 4. 若无法创建线程，直接运行（有风险但比不运行好） */
        if (!th) worker_thread(0);
    }
    return TRUE;
}
