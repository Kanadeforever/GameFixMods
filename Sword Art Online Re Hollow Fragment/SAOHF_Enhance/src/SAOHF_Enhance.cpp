// SAOHF_Enhance.cpp
// Sword Art Online Re: Hollow Fragment PC - enhancement runtime fix
// x64 ASI, no CRT import build variant for clang/lld-link. v1.0 uses virtual in-memory localize_msg.dat override and English-style OK/Cancel layout for non-Japanese branches.

extern "C" {

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long long SIZE_T;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef wchar_t WCHAR;
typedef const WCHAR* LPCWSTR;
typedef char CHAR;
typedef const CHAR* LPCSTR;

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY* Flink;
    struct _LIST_ENTRY* Blink;
} LIST_ENTRY;

typedef struct _UNICODE_STRING {
    u16 Length;
    u16 MaximumLength;
    WCHAR* Buffer;
} UNICODE_STRING;

typedef struct _PEB_LDR_DATA {
    u32 Length;
    u8 Initialized;
    u8 _pad1[3];
    void* SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    u32 SizeOfImage;
    u32 _pad1;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY;

typedef struct _PEB_FAKE {
    u8 Reserved1[2];
    u8 BeingDebugged;
    u8 Reserved2[1];
    void* Reserved3[2]; // [0]=Mutant, [1]=ImageBaseAddress
    PEB_LDR_DATA* Ldr;
} PEB_FAKE;

// Win32 constants
#define DLL_PROCESS_ATTACH 1
#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_READ  0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ  0x00000001UL
#define FILE_SHARE_WRITE 0x00000002UL
#define CREATE_ALWAYS 2UL
#define OPEN_EXISTING 3UL
#define FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFUL
#define MOVEFILE_REPLACE_EXISTING 0x00000001UL
#define MOVEFILE_WRITE_THROUGH    0x00000008UL
#define TRUE 1
#define FALSE 0
#define NULLPTR ((void*)0)
#define INVALID_HANDLE_VALUE ((HANDLE)(i64)-1)
#define FILE_BEGIN 0UL
#define FILE_CURRENT 1UL
#define FILE_END 2UL
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFUL

// Function typedefs.  On x64 Windows the calling convention keyword is ignored.
typedef DWORD (*PFN_GetModuleFileNameW)(HMODULE, WCHAR*, DWORD);
typedef HANDLE (*PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef HANDLE (*PFN_CreateFileA)(LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef BOOL (*PFN_ReadFile)(HANDLE, void*, DWORD, DWORD*, void*);
typedef BOOL (*PFN_WriteFile)(HANDLE, const void*, DWORD, DWORD*, void*);
typedef BOOL (*PFN_CloseHandle)(HANDLE);
typedef BOOL (*PFN_GetFileSizeEx)(HANDLE, i64*);
typedef BOOL (*PFN_GetFileInformationByHandleEx)(HANDLE, int, void*, DWORD);
typedef BOOL (*PFN_CopyFileW)(LPCWSTR, LPCWSTR, BOOL);
typedef BOOL (*PFN_MoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
typedef DWORD (*PFN_GetFileAttributesW)(LPCWSTR);
typedef DWORD (*PFN_GetFileSize)(HANDLE, DWORD*);
typedef DWORD (*PFN_SetFilePointer)(HANDLE, long, long*, DWORD);
typedef BOOL (*PFN_SetFilePointerEx)(HANDLE, i64, i64*, DWORD);
typedef DWORD (*PFN_GetLastError)();
typedef void* (*PFN_fopen)(const char*, const char*);
typedef int (*PFN_fopen_s)(void**, const char*, const char*);
typedef u64 (*PFN_fread)(void*, u64, u64, void*);
typedef int (*PFN_fseek)(void*, long, int);
typedef long (*PFN_ftell)(void*);
typedef int (*PFN_fseeki64)(void*, i64, int);
typedef i64 (*PFN_ftelli64)(void*);
typedef int (*PFN_fclose)(void*);
typedef int (*PFN_feof)(void*);
typedef int (*PFN_ferror)(void*);
typedef BOOL (*PFN_VirtualProtect)(void*, SIZE_T, DWORD, DWORD*);
typedef BOOL (*PFN_FlushInstructionCache)(HANDLE, const void*, SIZE_T);
typedef HANDLE (*PFN_CreateThread)(void*, SIZE_T, DWORD (*)(void*), void*, DWORD, DWORD*);
typedef void (*PFN_Sleep)(DWORD);
typedef UINT (*PFN_GetPrivateProfileIntW)(LPCWSTR, LPCWSTR, int, LPCWSTR);
typedef void* (*PFN_VirtualAlloc)(void*, SIZE_T, DWORD, DWORD);
typedef BOOL (*PFN_VirtualFree)(void*, SIZE_T, DWORD);

#define MEM_COMMIT 0x00001000UL
#define MEM_RESERVE 0x00002000UL
#define MEM_RELEASE 0x00008000UL
#define PAGE_READWRITE 0x04UL

struct Api {
    PFN_GetModuleFileNameW GetModuleFileNameW;
    PFN_CreateFileW CreateFileW;
    PFN_ReadFile ReadFile;
    PFN_WriteFile WriteFile;
    PFN_CloseHandle CloseHandle;
    PFN_GetFileSizeEx GetFileSizeEx;
    PFN_GetFileInformationByHandleEx GetFileInformationByHandleEx;
    PFN_CopyFileW CopyFileW;
    PFN_MoveFileExW MoveFileExW;
    PFN_GetFileAttributesW GetFileAttributesW;
    PFN_GetFileSize GetFileSize;
    PFN_SetFilePointer SetFilePointer;
    PFN_SetFilePointerEx SetFilePointerEx;
    PFN_GetLastError GetLastError;
    PFN_VirtualProtect VirtualProtect;
    PFN_FlushInstructionCache FlushInstructionCache;
    PFN_CreateThread CreateThread;
    PFN_Sleep Sleep;
    PFN_GetPrivateProfileIntW GetPrivateProfileIntW;
    PFN_VirtualAlloc VirtualAlloc;
    PFN_VirtualFree VirtualFree;
};

static Api g;
static void* g_image_base = NULLPTR;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static WCHAR g_dir[1024];
static WCHAR g_ini[1024];
static WCHAR g_log_path[1024];
static WCHAR g_cht_loc_path[1024];
static WCHAR g_usa_loc_path[1024];
static WCHAR g_data_loc_path[1024];
static int g_enable_log = 1;
static int g_enable_cdsp = 1;
static int g_enable_okcancel = 1;
static int g_patch_text = 1;
static int g_patch_cht_text = 1;
static int g_patch_usa_text = 1;
static int g_virtual_cht_ready = 0;
static int g_virtual_cht_checked = 0;
static u8* g_virtual_cht_data = 0;
static u32 g_virtual_cht_size = 0;
static int g_virtual_usa_ready = 0;
static int g_virtual_usa_checked = 0;
static u8* g_virtual_usa_data = 0;
static u32 g_virtual_usa_size = 0;

static PFN_CreateFileW s_real_CreateFileW = 0;
static PFN_CreateFileA s_real_CreateFileA = 0;
static PFN_ReadFile s_real_ReadFile = 0;
static PFN_CloseHandle s_real_CloseHandle = 0;
static PFN_GetFileSizeEx s_real_GetFileSizeEx = 0;
static PFN_GetFileInformationByHandleEx s_real_GetFileInformationByHandleEx = 0;
static PFN_GetFileSize s_real_GetFileSize = 0;
static PFN_SetFilePointer s_real_SetFilePointer = 0;
static PFN_SetFilePointerEx s_real_SetFilePointerEx = 0;
static PFN_fopen s_real_fopen = 0;
static PFN_fopen_s s_real_fopen_s = 0;
static PFN_fread s_real_fread = 0;
static PFN_fseek s_real_fseek = 0;
static PFN_ftell s_real_ftell = 0;
static PFN_fseeki64 s_real_fseeki64 = 0;
static PFN_ftelli64 s_real_ftelli64 = 0;
static PFN_fclose s_real_fclose = 0;
static PFN_feof s_real_feof = 0;
static PFN_ferror s_real_ferror = 0;

static PEB_FAKE* get_peb() {
#if defined(_M_X64) || defined(__x86_64__)
    void* p = 0;
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(p));
    return (PEB_FAKE*)p;
#else
    return (PEB_FAKE*)0;
#endif
}

static u32 str_len(const char* s) {
    u32 n = 0; if (!s) return 0; while (s[n]) ++n; return n;
}
static u32 w_len(const WCHAR* s) {
    u32 n = 0; if (!s) return 0; while (s[n]) ++n; return n;
}
static void w_copy(WCHAR* dst, const WCHAR* src) { while ((*dst++ = *src++) != 0) {} }
static void w_cat(WCHAR* dst, const WCHAR* src) { while (*dst) ++dst; w_copy(dst, src); }
static void copy_bytes(void* d, const void* s, u64 n) {
    u8* dst=(u8*)d; const u8* src=(const u8*)s; for (u64 i=0;i<n;i++) dst[i]=src[i];
}
static int mem_eq(const void* a, const void* b, u64 n) {
    const u8* x=(const u8*)a; const u8* y=(const u8*)b; for (u64 i=0;i<n;i++) if (x[i]!=y[i]) return 0; return 1;
}
static int ascii_eq(const char* a, const char* b) {
    u32 i=0; while (a[i] && b[i]) { if (a[i]!=b[i]) return 0; ++i; } return a[i]==0 && b[i]==0;
}
static char lower_ascii(char c) { return (c>='A'&&c<='Z') ? (char)(c+32) : c; }
static int ascii_ieq_n(const char* a, const char* b, u32 n) {
    for (u32 i=0;i<n;i++) if (lower_ascii(a[i]) != lower_ascii(b[i])) return 0; return 1;
}
static WCHAR lower_w(WCHAR c) { return (c>=L'A'&&c<=L'Z') ? (WCHAR)(c+32) : c; }
static int wide_ieq_lit(const WCHAR* a, u32 byte_len, const WCHAR* lit) {
    u32 len = byte_len / 2;
    u32 lit_len = w_len(lit);
    if (len != lit_len) return 0;
    for (u32 i=0;i<len;i++) if (lower_w(a[i]) != lower_w(lit[i])) return 0;
    return 1;
}

static void log_raw(const char* s) {
    if (!g_enable_log || g_log == INVALID_HANDLE_VALUE || !s) return;
    DWORD wr = 0;
    g.WriteFile(g_log, s, str_len(s), &wr, NULLPTR);
}
static void log_line(const char* s) { log_raw(s); log_raw("\r\n"); }

static void* find_module_w(const WCHAR* name) {
    PEB_FAKE* peb = get_peb();
    if (!peb || !peb->Ldr) return NULLPTR;
    LIST_ENTRY* head = &peb->Ldr->InLoadOrderModuleList;
    for (LIST_ENTRY* e = head->Flink; e && e != head; e = e->Flink) {
        LDR_DATA_TABLE_ENTRY* ent = (LDR_DATA_TABLE_ENTRY*)e;
        if (ent->BaseDllName.Buffer && wide_ieq_lit(ent->BaseDllName.Buffer, ent->BaseDllName.Length, name)) {
            return ent->DllBase;
        }
    }
    return NULLPTR;
}

static u16 rd16(const u8* p) { return (u16)(p[0] | (p[1]<<8)); }
static u32 rd32(const u8* p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static void wr32(u8* p, u32 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void wr64(u8* p, u64 v) { for (int i=0;i<8;i++) p[i]=(u8)(v>>(i*8)); }

static void* resolve_export_raw(void* module, const char* name);

static void* resolve_forwarded(const char* fwd) {
    // Expected common form: KERNELBASE.CreateFileW
    u32 dot = 0;
    while (fwd[dot] && fwd[dot] != '.') ++dot;
    if (!fwd[dot]) return NULLPTR;
    const char* fn = fwd + dot + 1;
    void* mod = NULLPTR;
    if ((dot == 10 && ascii_ieq_n(fwd, "KERNELBASE", 10)) || (dot == 14 && ascii_ieq_n(fwd, "api-ms-win", 10))) {
        mod = find_module_w(L"kernelbase.dll");
    } else if (dot == 8 && ascii_ieq_n(fwd, "KERNEL32", 8)) {
        mod = find_module_w(L"kernel32.dll");
    } else if (dot == 5 && ascii_ieq_n(fwd, "NTDLL", 5)) {
        mod = find_module_w(L"ntdll.dll");
    }
    if (!mod) return NULLPTR;
    return resolve_export_raw(mod, fn);
}

static void* resolve_export_raw(void* module, const char* name) {
    if (!module || !name) return NULLPTR;
    u8* base = (u8*)module;
    if (base[0] != 'M' || base[1] != 'Z') return NULLPTR;
    u32 pe = rd32(base + 0x3C);
    if (base[pe] != 'P' || base[pe+1] != 'E') return NULLPTR;
    u8* opt = base + pe + 0x18;
    u32 export_rva = rd32(opt + 0x70);
    u32 export_size = rd32(opt + 0x74);
    if (!export_rva) return NULLPTR;
    u8* exp = base + export_rva;
    u32 num_names = rd32(exp + 0x18);
    u32 funcs_rva = rd32(exp + 0x1C);
    u32 names_rva = rd32(exp + 0x20);
    u32 ords_rva  = rd32(exp + 0x24);
    u32* funcs = (u32*)(base + funcs_rva);
    u32* names = (u32*)(base + names_rva);
    u16* ords  = (u16*)(base + ords_rva);
    for (u32 i=0;i<num_names;i++) {
        const char* nm = (const char*)(base + names[i]);
        if (ascii_eq(nm, name)) {
            u32 frva = funcs[ords[i]];
            if (frva >= export_rva && frva < export_rva + export_size) {
                return resolve_forwarded((const char*)(base + frva));
            }
            return base + frva;
        }
    }
    return NULLPTR;
}

static void* resolve_export(const char* name) {
    void* k32 = find_module_w(L"kernel32.dll");
    void* p = resolve_export_raw(k32, name);
    if (p) return p;
    void* kb = find_module_w(L"kernelbase.dll");
    return resolve_export_raw(kb, name);
}

static int resolve_api() {
    g.GetModuleFileNameW = (PFN_GetModuleFileNameW)resolve_export("GetModuleFileNameW");
    g.CreateFileW = (PFN_CreateFileW)resolve_export("CreateFileW");
    g.ReadFile = (PFN_ReadFile)resolve_export("ReadFile");
    g.WriteFile = (PFN_WriteFile)resolve_export("WriteFile");
    g.CloseHandle = (PFN_CloseHandle)resolve_export("CloseHandle");
    g.GetFileSizeEx = (PFN_GetFileSizeEx)resolve_export("GetFileSizeEx");
    g.GetFileInformationByHandleEx = (PFN_GetFileInformationByHandleEx)resolve_export("GetFileInformationByHandleEx");
    g.CopyFileW = (PFN_CopyFileW)resolve_export("CopyFileW");
    g.MoveFileExW = (PFN_MoveFileExW)resolve_export("MoveFileExW");
    g.GetFileAttributesW = (PFN_GetFileAttributesW)resolve_export("GetFileAttributesW");
    g.GetFileSize = (PFN_GetFileSize)resolve_export("GetFileSize");
    g.SetFilePointer = (PFN_SetFilePointer)resolve_export("SetFilePointer");
    g.SetFilePointerEx = (PFN_SetFilePointerEx)resolve_export("SetFilePointerEx");
    g.GetLastError = (PFN_GetLastError)resolve_export("GetLastError");
    g.VirtualProtect = (PFN_VirtualProtect)resolve_export("VirtualProtect");
    g.FlushInstructionCache = (PFN_FlushInstructionCache)resolve_export("FlushInstructionCache");
    g.CreateThread = (PFN_CreateThread)resolve_export("CreateThread");
    g.Sleep = (PFN_Sleep)resolve_export("Sleep");
    g.GetPrivateProfileIntW = (PFN_GetPrivateProfileIntW)resolve_export("GetPrivateProfileIntW");
    g.VirtualAlloc = (PFN_VirtualAlloc)resolve_export("VirtualAlloc");
    g.VirtualFree = (PFN_VirtualFree)resolve_export("VirtualFree");
    if (!g.GetModuleFileNameW || !g.CreateFileW || !g.ReadFile || !g.WriteFile || !g.CloseHandle || !g.GetFileSizeEx || !g.GetFileAttributesW || !g.VirtualProtect || !g.FlushInstructionCache || !g.CreateThread || !g.VirtualAlloc || !g.VirtualFree) return 0;
    return 1;
}

static void init_paths() {
    WCHAR exe[1024];
    exe[0] = 0;
    g.GetModuleFileNameW((HMODULE)0, exe, 1023);
    exe[1023] = 0;
    u32 last = 0;
    for (u32 i=0; exe[i]; i++) if (exe[i] == L'\\' || exe[i] == L'/') last = i;
    for (u32 i=0; i<=last && i<1023; i++) g_dir[i] = exe[i];
    g_dir[last+1] = 0;

    w_copy(g_ini, g_dir); w_cat(g_ini, L"SAOHF_Enhance.ini");
    w_copy(g_log_path, g_dir); w_cat(g_log_path, L"SAOHF_Enhance.log");
    w_copy(g_cht_loc_path, g_dir); w_cat(g_cht_loc_path, L"cht\\common\\msg\\localize_msg.dat");
    w_copy(g_usa_loc_path, g_dir); w_cat(g_usa_loc_path, L"usa\\common\\msg\\localize_msg.dat");
    w_copy(g_data_loc_path, g_dir); w_cat(g_data_loc_path, L"data\\common\\msg\\localize_msg.dat");
}

static int cfg_int(const WCHAR* key, int defv) {
    if (!g.GetPrivateProfileIntW) return defv;
    return (int)g.GetPrivateProfileIntW(L"Main", key, defv, g_ini);
}

static void open_log() {
    if (!g_enable_log) return;
    g_log = g.CreateFileW(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULLPTR, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULLPTR);
}
static void close_log() {
    if (g_log != INVALID_HANDLE_VALUE) { g.CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
}

static int patch_bytes(u32 rva, const u8* original, const u8* target, u32 len, const char* name) {
    u8* addr = ((u8*)g_image_base) + rva;
    if (mem_eq(addr, target, len)) { log_raw("[内存] 已是目标状态："); log_line(name); return 0; }
    if (!mem_eq(addr, original, len)) { log_raw("[内存] 字节不匹配，跳过："); log_line(name); return 0; }
    DWORD oldProt = 0;
    if (!g.VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt)) { log_raw("[内存] VirtualProtect 失败："); log_line(name); return 0; }
    copy_bytes(addr, target, len);
    DWORD tmp = 0;
    g.VirtualProtect(addr, len, oldProt, &tmp);
    g.FlushInstructionCache((HANDLE)(i64)-1, addr, len);
    log_raw("[内存] 已修改："); log_line(name);
    return 1;
}
static int patch_byte_allowed(u32 rva, const u8* allowed, u32 allowed_count, u8 target, const char* name) {
    u8* addr = ((u8*)g_image_base) + rva;
    if (*addr == target) { log_raw("[内存] 已是目标状态："); log_line(name); return 0; }
    int ok=0; for (u32 i=0;i<allowed_count;i++) if (*addr == allowed[i]) ok=1;
    if (!ok) { log_raw("[内存] 字节不匹配，跳过："); log_line(name); return 0; }
    DWORD oldProt=0;
    if (!g.VirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &oldProt)) return 0;
    *addr = target;
    DWORD tmp=0; g.VirtualProtect(addr, 1, oldProt, &tmp);
    g.FlushInstructionCache((HANDLE)(i64)-1, addr, 1);
    log_raw("[内存] 已修改："); log_line(name);
    return 1;
}

static void apply_memory_patches() {
    PEB_FAKE* peb = get_peb();
    g_image_base = peb ? peb->Reserved3[1] : NULLPTR;
    if (!g_image_base) { log_line("[错误] 无法取得主程序基址。"); return; }

    if (g_enable_cdsp) {
        // v0.3：不再把日文限定改成“繁中限定”，而是直接放开 CD/SP 相关分支。
        // 这样英文、繁中、简中等非日文分支也能使用同一套 Play Style 机制。
        u8 jne_battle[2] = {0x75,0x11};
        u8 jne_value[2]  = {0x75,0x13};
        u8 jne_menu[2]   = {0x75,0x0D};
        u8 nop2[2] = {0x90,0x90};
        patch_bytes(0x00264044, jne_battle, nop2, 2, "CD/SP 战斗机制分支：允许所有语言进入");
        patch_bytes(0x0038EEBB, jne_value,  nop2, 2, "Play Style 当前值读取：允许所有语言进入");
        patch_bytes(0x0043F96E, jne_menu,   nop2, 2, "设置菜单入口：允许所有语言显示");

        u8 allowed[3] = {0x07, 0x08, 0x09};
        patch_byte_allowed(0x00C32F4E, allowed, 3, 0x08, "Gameplay 页项目数固定为 8");

        u8 inc[6] = {0xFE,0x05,0xD8,0x35,0x7F,0x00};
        u8 nop6[6] = {0x90,0x90,0x90,0x90,0x90,0x90};
        patch_bytes(0x0043F970, inc, nop6, 6, "禁用重复 inc，防止空白第 9 项");
    } else {
        log_line("[配置] CD/SP 恢复关闭。");
    }

    if (g_enable_okcancel) {
        // v0.3：还原旧 v0.2/测试脚本的两处手工互换，再改语言分支。
        // 原版逻辑中，日文和中文会先把 EBX/EDI 调成 East/B 确定、South/A 取消；
        // 英文不会进入这个交换分支。把 ja 改成 jmp 后，非日文全部走英文版布局。
        u8 old_ok_swapped[2] = {0x8B,0xD7};
        u8 old_ok_orig[2]    = {0x8B,0xD3};
        u8 old_ca_swapped[2] = {0x8B,0xD3};
        u8 old_ca_orig[2]    = {0x8B,0xD7};
        patch_bytes(0x004C7AC8, old_ok_swapped, old_ok_orig, 2, "还原旧 UI_Ok 手工互换点");
        patch_bytes(0x004C7B41, old_ca_swapped, old_ca_orig, 2, "还原旧 UI_Cancel 手工互换点");

        u8 ja_orig[1] = {0x77};
        u8 jmp_new[1] = {0xEB};
        patch_bytes(0x004C791A, ja_orig, jmp_new, 1, "中文使用英文版 UI 确定/取消布局");
    } else {
        log_line("[配置] UI 确定/取消修正关闭。");
    }
}

struct MsgPatch { u32 id; const char* text; u32 len; };

static MsgPatch g_msg_cht[4] = {
    {0x834, "CD制", 0},
    {0x835, "SP制", 0},
    {0x836, "技能消耗方式", 0},
    {0x837, "【CD制】使用劍技後會進入冷卻時間。　【SP制】使用劍技時會消耗SP。", 0},
};

static MsgPatch g_msg_usa[4] = {
    {0x834, "Berserk", 0},
    {0x835, "Saver", 0},
    {0x836, "Play Style", 0},
    {0x837, "[Berserk] Sword skills enter cooldown after use.  [Saver] Sword skills consume SP when used.", 0},
};

struct Entry { u32 id; const u8* str; u32 len; int owned; };

static MsgPatch* msg_table_for_type(int type) {
    if (type == 2) return g_msg_usa;
    return g_msg_cht;
}

static int find_patch_index(u32 id, MsgPatch* msgs) {
    for (int i=0;i<4;i++) if (msgs[i].id == id) return i;
    return -1;
}
static int str_bytes_eq(const u8* a, u32 alen, const char* b) {
    u32 blen = str_len(b);
    if (alen != blen) return 0;
    for (u32 i=0;i<alen;i++) if (a[i] != (u8)b[i]) return 0;
    return 1;
}
static u32 align4(u32 v) { return (v + 3) & ~3U; }

static HANDLE open_read(const WCHAR* path) {
    return g.CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULLPTR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULLPTR);
}
static int read_whole_file(const WCHAR* path, u8** out, u32* out_size) {
    *out = 0; *out_size = 0;
    HANDLE h = open_read(path);
    if (h == INVALID_HANDLE_VALUE) return 0;
    i64 sz64 = 0;
    if (!g.GetFileSizeEx(h, &sz64) || sz64 <= 0 || sz64 > 0x10000000LL) { g.CloseHandle(h); return 0; }
    u32 sz = (u32)sz64;
    u8* buf = (u8*)g.VirtualAlloc(NULLPTR, sz, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!buf) { g.CloseHandle(h); return 0; }
    DWORD got = 0;
    BOOL ok = g.ReadFile(h, buf, sz, &got, NULLPTR);
    g.CloseHandle(h);
    if (!ok || got != sz) { g.VirtualFree(buf, 0, MEM_RELEASE); return 0; }
    *out = buf; *out_size = sz;
    return 1;
}
static int write_whole_file(const WCHAR* path, const u8* data, u32 size) {
    HANDLE h = g.CreateFileW(path, GENERIC_WRITE, 0, NULLPTR, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULLPTR);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD wr = 0;
    BOOL ok = g.WriteFile(h, data, size, &wr, NULLPTR);
    g.CloseHandle(h);
    return ok && wr == size;
}

static int parse_group36(const u8* data, u32 size, const u8** out_block, u32* out_block_size) {
    if (size < 0x20 || !mem_eq(data, "OFS3", 4)) return 0;
    u32 outer_count = rd32(data + 0x10);
    if (outer_count <= 36) return 0;
    u32 table = 0x14 + 36*8;
    if (table + 8 > size) return 0;
    u32 rel = rd32(data + table);
    u32 bsz = rd32(data + table + 4);
    u32 off = 0x10 + rel;
    if (off + bsz > size || bsz < 0x20) return 0;
    const u8* block = data + off;
    if (!mem_eq(block, "OFS3", 4)) return 0;
    *out_block = block;
    *out_block_size = bsz;
    return 1;
}

static int needs_text_patch(const u8* block, u32 bsz, MsgPatch* msgs) {
    u32 count = rd32(block + 0x10);
    int ok_count = 0;
    for (u32 i=0;i<count;i++) {
        u32 ent = 0x14 + i*8;
        if (ent + 8 > bsz) return 1;
        u32 srel = rd32(block + ent);
        u32 id = rd32(block + ent + 4);
        int pi = find_patch_index(id, msgs);
        if (pi < 0) continue;
        u32 so = 0x10 + srel;
        if (so >= bsz) return 1;
        u32 end = so;
        while (end < bsz && block[end] != 0) ++end;
        if (end >= bsz) return 1;
        if (str_bytes_eq(block + so, end - so, msgs[pi].text)) ok_count++;
    }
    return ok_count != 4;
}

static void sort_entries(Entry* e, u32 count) {
    for (u32 i=1;i<count;i++) {
        Entry key = e[i];
        u32 j = i;
        while (j > 0 && e[j-1].id > key.id) { e[j] = e[j-1]; --j; }
        e[j] = key;
    }
}

static int build_group36(const u8* old_block, u32 old_bsz, MsgPatch* msgs, u8** out_block, u32* out_size) {
    *out_block = 0; *out_size = 0;
    if (old_bsz < 0x20 || !mem_eq(old_block, "OFS3", 4)) return 0;
    u32 old_count = rd32(old_block + 0x10);
    if (old_count > 100000) return 0;
    Entry* entries = (Entry*)g.VirtualAlloc(NULLPTR, (old_count + 4) * sizeof(Entry), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!entries) return 0;
    u32 ec = 0;
    int present[4] = {0,0,0,0};
    for (u32 i=0;i<old_count;i++) {
        u32 ent = 0x14 + i*8;
        if (ent + 8 > old_bsz) { g.VirtualFree(entries, 0, MEM_RELEASE); return 0; }
        u32 srel = rd32(old_block + ent);
        u32 id = rd32(old_block + ent + 4);
        u32 so = 0x10 + srel;
        if (so >= old_bsz) { g.VirtualFree(entries, 0, MEM_RELEASE); return 0; }
        u32 end = so;
        while (end < old_bsz && old_block[end] != 0) ++end;
        if (end >= old_bsz) { g.VirtualFree(entries, 0, MEM_RELEASE); return 0; }
        int pi = find_patch_index(id, msgs);
        entries[ec].id = id;
        if (pi >= 0) {
            msgs[pi].len = str_len(msgs[pi].text);
            entries[ec].str = (const u8*)msgs[pi].text;
            entries[ec].len = msgs[pi].len;
            present[pi] = 1;
        } else {
            entries[ec].str = old_block + so;
            entries[ec].len = end - so;
        }
        entries[ec].owned = 0;
        ec++;
    }
    for (int i=0;i<4;i++) if (!present[i]) {
        msgs[i].len = str_len(msgs[i].text);
        entries[ec].id = msgs[i].id;
        entries[ec].str = (const u8*)msgs[i].text;
        entries[ec].len = msgs[i].len;
        entries[ec].owned = 0;
        ec++;
    }
    sort_entries(entries, ec);
    u32 body_size = 4 + ec * 8;
    for (u32 i=0;i<ec;i++) body_size += entries[i].len + 1;
    body_size = align4(body_size);
    u32 total = 0x10 + body_size;
    u8* out = (u8*)g.VirtualAlloc(NULLPTR, total, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!out) { g.VirtualFree(entries,0,MEM_RELEASE); return 0; }
    copy_bytes(out, "OFS3", 4);
    wr32(out+4, 0x10); wr32(out+8, 0x01040000); wr32(out+12, body_size);
    wr32(out+0x10, ec);
    u32 cursor = 4 + ec * 8;
    for (u32 i=0;i<ec;i++) {
        wr32(out + 0x14 + i*8, cursor);
        wr32(out + 0x14 + i*8 + 4, entries[i].id);
        copy_bytes(out + 0x10 + cursor, entries[i].str, entries[i].len);
        out[0x10 + cursor + entries[i].len] = 0;
        cursor += entries[i].len + 1;
    }
    // VirtualAlloc returns zero-filled memory, so padding is already zero.
    g.VirtualFree(entries,0,MEM_RELEASE);
    *out_block = out; *out_size = total;
    return 1;
}

static int build_localize_with_new_group36(const u8* old_data, u32 old_size, const u8* new_group, u32 new_group_size, u8** out_data, u32* out_size) {
    *out_data = 0; *out_size = 0;
    u32 outer_count = rd32(old_data + 0x10);
    u32 table_size = 4 + outer_count * 8;
    u32 total_body = table_size;
    for (u32 i=0;i<outer_count;i++) {
        u32 ent = 0x14 + i*8;
        if (ent + 8 > old_size) return 0;
        u32 rel = rd32(old_data + ent);
        u32 bsz = rd32(old_data + ent + 4);
        u32 off = 0x10 + rel;
        if (off + bsz > old_size) return 0;
        total_body += (i == 36) ? new_group_size : bsz;
    }
    u32 total = 0x10 + total_body;
    u8* out = (u8*)g.VirtualAlloc(NULLPTR, total, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!out) return 0;
    copy_bytes(out, "OFS3", 4);
    wr32(out+4, 0x10); wr32(out+8, 0x00040000); wr32(out+12, total_body);
    wr32(out+0x10, outer_count);
    u32 cursor = table_size;
    for (u32 i=0;i<outer_count;i++) {
        u32 ent = 0x14 + i*8;
        u32 rel = rd32(old_data + ent);
        u32 bsz = rd32(old_data + ent + 4);
        u32 off = 0x10 + rel;
        const u8* src = old_data + off;
        u32 sz = bsz;
        if (i == 36) { src = new_group; sz = new_group_size; }
        wr32(out + 0x14 + i*8, cursor);
        wr32(out + 0x14 + i*8 + 4, sz);
        copy_bytes(out + 0x10 + cursor, src, sz);
        cursor += sz;
    }
    *out_data = out; *out_size = total;
    return 1;
}

struct FakeFileSlot {
    int used;
    const u8* data;
    u32 size;
    u64 pos;
};

#define MAX_FAKE_FILE_SLOTS 8
#define FAKE_HANDLE_BASE 0x53414846494C0000ULL
#define FAKE_FILE_BASE   0x5341484649460000ULL
static FakeFileSlot g_fake_handles[MAX_FAKE_FILE_SLOTS];
static FakeFileSlot g_fake_files[MAX_FAKE_FILE_SLOTS];

static int path_matches_suffix_w(const WCHAR* p, const WCHAR* suf) {
    u32 plen = w_len(p);
    u32 slen = w_len(suf);
    if (plen < slen) return 0;
    const WCHAR* q = p + plen - slen;
    for (u32 i=0;i<slen;i++) {
        WCHAR a = q[i];
        WCHAR b = suf[i];
        if (a == L'\\') a = L'/';
        if (b == L'\\') b = L'/';
        if (lower_w(a) != lower_w(b)) return 0;
    }
    return 1;
}
static int path_matches_suffix_a(const char* p, const char* suf) {
    u32 plen = str_len(p);
    u32 slen = str_len(suf);
    if (plen < slen) return 0;
    const char* q = p + plen - slen;
    for (u32 i=0;i<slen;i++) {
        char a = q[i];
        char b = suf[i];
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (lower_ascii(a) != lower_ascii(b)) return 0;
    }
    return 1;
}
static int localize_path_type_w(const WCHAR* p) {
    if (!p) return 0;
    if (path_matches_suffix_w(p, L"cht/common/msg/localize_msg.dat")) return 1;
    if (path_matches_suffix_w(p, L"usa/common/msg/localize_msg.dat")) return 2;
    if (path_matches_suffix_w(p, L"data/common/msg/localize_msg.dat")) return 2;
    return 0;
}
static int localize_path_type_a(const char* p) {
    if (!p) return 0;
    if (path_matches_suffix_a(p, "cht/common/msg/localize_msg.dat")) return 1;
    if (path_matches_suffix_a(p, "usa/common/msg/localize_msg.dat")) return 2;
    if (path_matches_suffix_a(p, "data/common/msg/localize_msg.dat")) return 2;
    return 0;
}

static int ensure_virtual_localize_patch(int type) {
    if (!g_patch_text) return 0;
    if (type == 1 && !g_patch_cht_text) return 0;
    if (type == 2 && !g_patch_usa_text) return 0;

    int* checked = (type == 2) ? &g_virtual_usa_checked : &g_virtual_cht_checked;
    int* ready   = (type == 2) ? &g_virtual_usa_ready   : &g_virtual_cht_ready;
    u8** vdata   = (type == 2) ? &g_virtual_usa_data    : &g_virtual_cht_data;
    u32* vsize   = (type == 2) ? &g_virtual_usa_size    : &g_virtual_cht_size;
    WCHAR* path  = (type == 2) ? g_usa_loc_path         : g_cht_loc_path;
    MsgPatch* msgs = msg_table_for_type(type);

    if (*checked) return *ready;
    *checked = 1;

    if (type == 2 && g.GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        // 英文版常见情况是没有顶层 usa 目录，直接回退读取 data/common/msg/localize_msg.dat。
        path = g_data_loc_path;
    }
    if (g.GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        if (type == 2) log_line("[文本] 找不到 usa 或 data 的 localize_msg.dat，英文虚拟补丁跳过。");
        else log_line("[文本] 找不到 cht/common/msg/localize_msg.dat，繁中虚拟补丁跳过。");
        return 0;
    }
    u8* old_data = 0; u32 old_size = 0;
    if (!read_whole_file(path, &old_data, &old_size)) {
        if (type == 2) log_line("[文本] 读取英文 localize_msg.dat 失败，虚拟补丁跳过。");
        else log_line("[文本] 读取繁中 localize_msg.dat 失败，虚拟补丁跳过。");
        return 0;
    }
    const u8* group = 0; u32 group_size = 0;
    if (!parse_group36(old_data, old_size, &group, &group_size)) {
        log_line("[文本] localize_msg.dat 格式不符合预期，虚拟补丁跳过。");
        g.VirtualFree(old_data,0,MEM_RELEASE);
        return 0;
    }
    if (!needs_text_patch(group, group_size, msgs)) {
        if (type == 2) log_line("[文本] 英文 CD/SP 文本已经存在，不需要虚拟覆盖。");
        else log_line("[文本] 繁中 CD/SP 文本已经存在，不需要虚拟覆盖。");
        g.VirtualFree(old_data,0,MEM_RELEASE);
        return 0;
    }
    u8* new_group = 0; u32 new_group_size = 0;
    if (!build_group36(group, group_size, msgs, &new_group, &new_group_size)) {
        log_line("[文本] 重建虚拟 group 36 失败。");
        g.VirtualFree(old_data,0,MEM_RELEASE);
        return 0;
    }
    u8* new_data = 0; u32 new_size = 0;
    if (!build_localize_with_new_group36(old_data, old_size, new_group, new_group_size, &new_data, &new_size)) {
        log_line("[文本] 重建虚拟 localize_msg.dat 失败。");
        g.VirtualFree(new_group,0,MEM_RELEASE);
        g.VirtualFree(old_data,0,MEM_RELEASE);
        return 0;
    }
    *vdata = new_data;
    *vsize = new_size;
    *ready = 1;
    if (type == 2) log_line("[文本] 已在内存中生成英文虚拟 localize_msg.dat；不会修改原文件。");
    else log_line("[文本] 已在内存中生成繁中虚拟 localize_msg.dat；不会修改原文件。");
    g.VirtualFree(new_group,0,MEM_RELEASE);
    g.VirtualFree(old_data,0,MEM_RELEASE);
    return 1;
}

static HANDLE make_fake_handle_slot(FakeFileSlot* slots, u64 base, int idx) { return (HANDLE)(base | (u64)(idx + 1)); }
static int fake_slot_index(FakeFileSlot* slots, u64 base, void* h) {
    u64 v = (u64)h;
    if ((v & 0xFFFFFFFFFFFF0000ULL) != base) return -1;
    int idx = (int)(v & 0xFFFFULL) - 1;
    if (idx < 0 || idx >= MAX_FAKE_FILE_SLOTS) return -1;
    if (!slots[idx].used) return -1;
    return idx;
}
static int open_fake_slot(FakeFileSlot* slots, u64 base, int type, void** out) {
    if (!ensure_virtual_localize_patch(type)) return 0;
    const u8* data = (type == 2) ? g_virtual_usa_data : g_virtual_cht_data;
    u32 size = (type == 2) ? g_virtual_usa_size : g_virtual_cht_size;
    for (int i=0;i<MAX_FAKE_FILE_SLOTS;i++) {
        if (!slots[i].used) {
            slots[i].used = 1;
            slots[i].data = data;
            slots[i].size = size;
            slots[i].pos = 0;
            *out = make_fake_handle_slot(slots, base, i);
            return 1;
        }
    }
    return 0;
}
static u32 fake_read(FakeFileSlot* s, void* buffer, u32 want) {
    if (!s || !buffer || want == 0) return 0;
    if (s->pos >= s->size) return 0;
    u64 remain = (u64)s->size - s->pos;
    u32 n = (want < remain) ? want : (u32)remain;
    copy_bytes(buffer, s->data + s->pos, n);
    s->pos += n;
    return n;
}
static int fake_seek(FakeFileSlot* s, i64 move, DWORD method, u64* new_pos) {
    if (!s) return 0;
    i64 base = 0;
    if (method == FILE_BEGIN) base = 0;
    else if (method == FILE_CURRENT) base = (i64)s->pos;
    else if (method == FILE_END) base = (i64)s->size;
    else return 0;
    i64 np = base + move;
    if (np < 0) return 0;
    s->pos = (u64)np;
    if (new_pos) *new_pos = s->pos;
    return 1;
}

static HANDLE hook_CreateFileW(LPCWSTR path, DWORD access, DWORD share, void* sec, DWORD creation, DWORD flags, HANDLE tmpl) {
    (void)access; (void)creation;
    int ltype = path ? localize_path_type_w(path) : 0;
    if (ltype && g_patch_text) {
        void* h = 0;
        if (open_fake_slot(g_fake_handles, FAKE_HANDLE_BASE, ltype, &h)) return (HANDLE)h;
    }
    return s_real_CreateFileW ? s_real_CreateFileW(path, access, share, sec, creation, flags, tmpl) : g.CreateFileW(path, access, share, sec, creation, flags, tmpl);
}
static HANDLE hook_CreateFileA(LPCSTR path, DWORD access, DWORD share, void* sec, DWORD creation, DWORD flags, HANDLE tmpl) {
    int ltype = path ? localize_path_type_a(path) : 0;
    if (ltype && g_patch_text) {
        void* h = 0;
        if (open_fake_slot(g_fake_handles, FAKE_HANDLE_BASE, ltype, &h)) return (HANDLE)h;
    }
    return s_real_CreateFileA ? s_real_CreateFileA(path, access, share, sec, creation, flags, tmpl) : INVALID_HANDLE_VALUE;
}
static BOOL hook_ReadFile(HANDLE h, void* buffer, DWORD bytes, DWORD* read, void* overlapped) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) {
        if (overlapped) return FALSE;
        u32 got = fake_read(&g_fake_handles[idx], buffer, bytes);
        if (read) *read = got;
        return TRUE;
    }
    return s_real_ReadFile ? s_real_ReadFile(h, buffer, bytes, read, overlapped) : g.ReadFile(h, buffer, bytes, read, overlapped);
}
static BOOL hook_CloseHandle(HANDLE h) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) { g_fake_handles[idx].used = 0; return TRUE; }
    return s_real_CloseHandle ? s_real_CloseHandle(h) : g.CloseHandle(h);
}
static BOOL hook_GetFileSizeEx(HANDLE h, i64* size) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) { if (size) *size = (i64)g_fake_handles[idx].size; return TRUE; }
    return s_real_GetFileSizeEx ? s_real_GetFileSizeEx(h, size) : g.GetFileSizeEx(h, size);
}
static BOOL hook_GetFileInformationByHandleEx(HANDLE h, int info_class, void* info, DWORD size) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) {
        // FileStandardInfo = 1.  The game reads EndOfFile at offset +8.
        if (info_class == 1 && info && size >= 24) {
            u8* p = (u8*)info;
            for (u32 i=0;i<size;i++) p[i] = 0;
            wr64(p + 0, (u64)g_fake_handles[idx].size);
            wr64(p + 8, (u64)g_fake_handles[idx].size);
            wr32(p + 16, 1);
            return TRUE;
        }
        return FALSE;
    }
    return s_real_GetFileInformationByHandleEx ? s_real_GetFileInformationByHandleEx(h, info_class, info, size) : FALSE;
}
static DWORD hook_GetFileSize(HANDLE h, DWORD* high) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) { if (high) *high = 0; return g_fake_handles[idx].size; }
    return s_real_GetFileSize ? s_real_GetFileSize(h, high) : INVALID_SET_FILE_POINTER;
}
static DWORD hook_SetFilePointer(HANDLE h, long low, long* high, DWORD method) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) {
        i64 move = low;
        if (high) move |= ((i64)(*high) << 32);
        u64 np = 0;
        if (!fake_seek(&g_fake_handles[idx], move, method, &np)) return INVALID_SET_FILE_POINTER;
        if (high) *high = (long)(np >> 32);
        return (DWORD)(np & 0xFFFFFFFFULL);
    }
    return s_real_SetFilePointer ? s_real_SetFilePointer(h, low, high, method) : INVALID_SET_FILE_POINTER;
}
static BOOL hook_SetFilePointerEx(HANDLE h, i64 move, i64* newpos, DWORD method) {
    int idx = fake_slot_index(g_fake_handles, FAKE_HANDLE_BASE, h);
    if (idx >= 0) {
        u64 np = 0;
        if (!fake_seek(&g_fake_handles[idx], move, method, &np)) return FALSE;
        if (newpos) *newpos = (i64)np;
        return TRUE;
    }
    return s_real_SetFilePointerEx ? s_real_SetFilePointerEx(h, move, newpos, method) : FALSE;
}

static void* hook_fopen(const char* path, const char* mode) {
    (void)mode;
    int ltype = path ? localize_path_type_a(path) : 0;
    if (ltype && g_patch_text) {
        void* f = 0;
        if (open_fake_slot(g_fake_files, FAKE_FILE_BASE, ltype, &f)) return f;
    }
    return s_real_fopen ? s_real_fopen(path, mode) : 0;
}
static int hook_fopen_s(void** out, const char* path, const char* mode) {
    if (out) *out = 0;
    int ltype = path ? localize_path_type_a(path) : 0;
    if (ltype && g_patch_text) {
        void* f = 0;
        if (open_fake_slot(g_fake_files, FAKE_FILE_BASE, ltype, &f)) { if (out) *out = f; return 0; }
    }
    return s_real_fopen_s ? s_real_fopen_s(out, path, mode) : 1;
}
static u64 hook_fread(void* buffer, u64 size, u64 count, void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) {
        if (size == 0 || count == 0) return 0;
        u64 total64 = size * count;
        if (total64 > 0xFFFFFFFFULL) total64 = 0xFFFFFFFFULL;
        u32 got = fake_read(&g_fake_files[idx], buffer, (u32)total64);
        return got / size;
    }
    return s_real_fread ? s_real_fread(buffer, size, count, stream) : 0;
}
static int hook_fseek(void* stream, long offset, int origin) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return fake_seek(&g_fake_files[idx], offset, (DWORD)origin, 0) ? 0 : -1;
    return s_real_fseek ? s_real_fseek(stream, offset, origin) : -1;
}
static long hook_ftell(void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return (long)g_fake_files[idx].pos;
    return s_real_ftell ? s_real_ftell(stream) : -1;
}
static int hook_fseeki64(void* stream, i64 offset, int origin) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return fake_seek(&g_fake_files[idx], offset, (DWORD)origin, 0) ? 0 : -1;
    return s_real_fseeki64 ? s_real_fseeki64(stream, offset, origin) : -1;
}
static i64 hook_ftelli64(void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return (i64)g_fake_files[idx].pos;
    return s_real_ftelli64 ? s_real_ftelli64(stream) : -1;
}
static int hook_fclose(void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) { g_fake_files[idx].used = 0; return 0; }
    return s_real_fclose ? s_real_fclose(stream) : -1;
}
static int hook_feof(void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return g_fake_files[idx].pos >= g_fake_files[idx].size ? 1 : 0;
    return s_real_feof ? s_real_feof(stream) : 0;
}
static int hook_ferror(void* stream) {
    int idx = fake_slot_index(g_fake_files, FAKE_FILE_BASE, stream);
    if (idx >= 0) return 0;
    return s_real_ferror ? s_real_ferror(stream) : 0;
}

static int patch_import_by_name(const char* import_name, void* replacement, void** original) {
    if (!g_image_base || !import_name || !replacement) return 0;
    u8* base = (u8*)g_image_base;
    if (base[0] != 'M' || base[1] != 'Z') return 0;
    u32 pe = rd32(base + 0x3C);
    if (base[pe] != 'P' || base[pe+1] != 'E') return 0;
    u8* opt = base + pe + 0x18;
    u32 import_rva = rd32(opt + 0x78);
    if (!import_rva) return 0;
    u8* desc = base + import_rva;
    int patched = 0;
    for (;;) {
        u32 oft = rd32(desc + 0x00);
        u32 name_rva = rd32(desc + 0x0C);
        u32 ft = rd32(desc + 0x10);
        if (!oft && !name_rva && !ft) break;
        u64* name_thunk = (u64*)(base + (oft ? oft : ft));
        u64* addr_thunk = (u64*)(base + ft);
        for (u32 i=0; name_thunk[i]; i++) {
            u64 ent = name_thunk[i];
            if (ent & 0x8000000000000000ULL) continue;
            const char* nm = (const char*)(base + (u32)ent + 2);
            if (!ascii_eq(nm, import_name)) continue;
            void** iat = (void**)(&addr_thunk[i]);
            if (*iat == replacement) { patched++; continue; }
            if (original && !*original) *original = *iat;
            DWORD oldProt = 0;
            if (g.VirtualProtect(iat, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
                *iat = replacement;
                DWORD tmp = 0;
                g.VirtualProtect(iat, sizeof(void*), oldProt, &tmp);
                g.FlushInstructionCache((HANDLE)(i64)-1, iat, sizeof(void*));
                patched++;
            }
        }
        desc += 20;
    }
    return patched;
}

static void install_virtual_text_hooks() {
    if (!g_patch_text) { log_line("[配置] 内存文本补全关闭。"); return; }
    PEB_FAKE* peb = get_peb();
    g_image_base = peb ? peb->Reserved3[1] : NULLPTR;
    if (!g_image_base) { log_line("[文本] 无法取得主程序基址，无法安装虚拟文件钩子。"); return; }
    int n = 0;
    n += patch_import_by_name("CreateFileW", (void*)hook_CreateFileW, (void**)&s_real_CreateFileW);
    n += patch_import_by_name("CreateFileA", (void*)hook_CreateFileA, (void**)&s_real_CreateFileA);
    n += patch_import_by_name("ReadFile", (void*)hook_ReadFile, (void**)&s_real_ReadFile);
    n += patch_import_by_name("CloseHandle", (void*)hook_CloseHandle, (void**)&s_real_CloseHandle);
    n += patch_import_by_name("GetFileSizeEx", (void*)hook_GetFileSizeEx, (void**)&s_real_GetFileSizeEx);
    n += patch_import_by_name("GetFileInformationByHandleEx", (void*)hook_GetFileInformationByHandleEx, (void**)&s_real_GetFileInformationByHandleEx);
    n += patch_import_by_name("GetFileSize", (void*)hook_GetFileSize, (void**)&s_real_GetFileSize);
    n += patch_import_by_name("SetFilePointer", (void*)hook_SetFilePointer, (void**)&s_real_SetFilePointer);
    n += patch_import_by_name("SetFilePointerEx", (void*)hook_SetFilePointerEx, (void**)&s_real_SetFilePointerEx);
    n += patch_import_by_name("fopen", (void*)hook_fopen, (void**)&s_real_fopen);
    n += patch_import_by_name("fopen_s", (void*)hook_fopen_s, (void**)&s_real_fopen_s);
    n += patch_import_by_name("fread", (void*)hook_fread, (void**)&s_real_fread);
    n += patch_import_by_name("fseek", (void*)hook_fseek, (void**)&s_real_fseek);
    n += patch_import_by_name("ftell", (void*)hook_ftell, (void**)&s_real_ftell);
    n += patch_import_by_name("_fseeki64", (void*)hook_fseeki64, (void**)&s_real_fseeki64);
    n += patch_import_by_name("_ftelli64", (void*)hook_ftelli64, (void**)&s_real_ftelli64);
    n += patch_import_by_name("fclose", (void*)hook_fclose, (void**)&s_real_fclose);
    n += patch_import_by_name("feof", (void*)hook_feof, (void**)&s_real_feof);
    n += patch_import_by_name("ferror", (void*)hook_ferror, (void**)&s_real_ferror);
    if (n > 0) log_line("[文本] 已安装 localize_msg.dat 虚拟文件钩子。");
    else log_line("[文本] 未找到可安装的虚拟文件钩子导入项。");
}

static DWORD worker(void*) {
    if (!resolve_api()) return 0;
    init_paths();
    g_enable_log = cfg_int(L"EnableLog", 1) ? 1 : 0;
    g_enable_cdsp = cfg_int(L"EnableCdSpSwitch", 1) ? 1 : 0;
    g_enable_okcancel = cfg_int(L"UseEnglishOkCancelLayout", cfg_int(L"EnableOkCancelSwap", 1)) ? 1 : 0;
    g_patch_text = cfg_int(L"EnableVirtualTextPatch", 1) ? 1 : 0;
    g_patch_cht_text = cfg_int(L"PatchChtText", 1) ? 1 : 0;
    g_patch_usa_text = cfg_int(L"PatchUsaText", 1) ? 1 : 0;
    open_log();
    log_line("[启动] SAOHF_Enhance v1.0 已加载。");
    install_virtual_text_hooks();
    apply_memory_patches();
    log_line("[完成] 处理结束。Steam 语言可使用繁体中文/中文或英文。");
    close_log();
    return 0;
}

__declspec(dllexport) BOOL DllMainCRTStartup(void* hinst, DWORD reason, void*) {
    (void)hinst;
    if (reason == DLL_PROCESS_ATTACH) {
        if (resolve_api()) {
            DWORD tid = 0;
            HANDLE th = g.CreateThread(NULLPTR, 0, worker, NULLPTR, 0, &tid);
            if (th && th != INVALID_HANDLE_VALUE && g.CloseHandle) g.CloseHandle(th);
        }
    }
    return TRUE;
}

} // extern "C"
