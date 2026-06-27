/*
 * skip_popup.c — 轻量 ASI 插件：拦截 MessageBox 弹窗 + 纯净日志
 *
 * 用途:
 *   屏蔽戦女神VERITA (Ikusa Megami VERITA) 的光盘校验弹窗。
 *   游戏每约 2.5 分钟弹出 "データが壊れています"（数据损坏）错误框，
 *   这是 ARCGameEngine v4 的复制保护 / 光盘检测机制。
 *   本插件自动点击"是"/"重试"，使游戏在免光盘或无原版启动器的
 *   环境下正常运行，无需外挂辅助进程。
 *
 * 原理:
 *   使用 5 字节 JMP 内联 Hook 拦截 user32.dll 的 MessageBoxA
 *   和 MessageBoxW 函数。Hook 函数检查弹窗标题/内容是否包含
 *   Shift-JIS 编码的日文错误关键词（"エラー" 或 "データ"），
 *   匹配则自动返回 IDYES 或 IDRETRY；不匹配则调用原始
 *   MessageBox 正常显示，不影响其他弹窗。
 *
 * Hook 架构 (每条 Hook 独立):
 *   原始函数入口 [5 字节] → 替换为 JMP Hook函数
 *   Trampoline [5 字节原指令 + JMP 回原始函数+5] → Hook 调原函数用
 *
 * 按钮处理策略:
 *   MB_YESNO / MB_YESNOCANCEL / MB_RETRYCANCEL  → IDYES   (点"是"/"重试")
 *   MB_ABORTRETRYIGNORE                          → IDRETRY (点"重试")
 *   核心思路：给游戏一个"继续运行"的返回值，避免因弹窗阻塞而卡死。
 *
 * 为什么 Hook 两个版本:
 *   - MessageBoxA: 日文老游戏绝大多数走 ANSI 路径，Shift-JIS 编码
 *   - MessageBoxW: 万一游戏走 Unicode 路径也能兜底（宽字符串直接匹配）
 *
 * 编译:
 *   cl /LD /O1 /GS- skip_popup.c /Fe:skip_popup.asi
 *
 * 依赖:
 *   编译时: windows.h
 *   运行时: kernel32.dll (VirtualAlloc, CreateFile, WriteFile 等)
 *           user32.dll  (MessageBoxA, MessageBoxW)
 *
 * 部署:
 *   将 skip_popup.asi 放入游戏目录（与 AGE.EXE 同级），
 *   依赖 ASI Loader（如本项目的 version.dll）来加载。
 *
 * 日志输出:
 *   游戏目录/skip_popup.log，ASCII 文本，每行格式:
 *   [HH:MM:SS.mmm] 消息内容
 *   每次 DLL 加载时覆盖旧日志 (CREATE_ALWAYS)。
 *
 * 相关文件:
 *   version_hook.c  — ASI Loader + 内置弹窗屏蔽（本文件的姊妹项目）
 *   skip_popup.c.bak — 废弃方案：Hook 注册表 RegQueryValueExA（无效）
 *
 * 作者: luminous
 * 许可: MIT
 * 日期: 2026-06
 */

#include <windows.h>

/* ============================================================
 * 日志系统
 * ============================================================ */

/* 日志文件句柄。INVALID_HANDLE_VALUE 表示未打开 */
static HANDLE g_log = INVALID_HANDLE_VALUE;

/* 临界区 — 保护日志写入的线程安全。
 * 多线程同时触发 MessageBox 时，不加锁会导致日志行交错乱码。 */
static CRITICAL_SECTION g_cs;

/*
 * log_open — 打开日志文件
 *
 * 路径构造过程:
 *   1. GetModuleFileNameA(NULL) 获取当前进程 EXE 的完整路径
 *   2. 找到路径中最后一个 '\' 或 '/'，截断为目录路径
 *   3. 拼接 "skip_popup.log"
 *   4. CreateFileA + CREATE_ALWAYS 创建/覆盖日志文件
 */
static void log_open(void) {
    InitializeCriticalSection(&g_cs);
    char b[MAX_PATH];
    GetModuleFileNameA(NULL, b, MAX_PATH);
    /* 找最后一个路径分隔符 */
    char *p = b;
    for (char *s = b; *s; s++)
        if (*s == '\\' || *s == '/') p = s;
    /* 截断为目录路径（保留最后的反斜杠） */
    p[1] = '\0';
    /* 手动拼接 "skip_popup.log" */
    char *e = b;
    while (*e) e++;
    *e++ = 's'; *e++ = 'k'; *e++ = 'i'; *e++ = 'p'; *e++ = '_';
    *e++ = 'p'; *e++ = 'o'; *e++ = 'p'; *e++ = 'u'; *e++ = 'p';
    *e++ = '.'; *e++ = 'l'; *e++ = 'o'; *e++ = 'g'; *e = '\0';
    g_log = CreateFileA(b, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/*
 * log_write — 写入一行带时间戳的日志
 *
 * 输出格式: [HH:MM:SS.mmm] <消息文本>\r\n
 *
 * 使用 CRITICAL_SECTION 加锁，保证多线程写日志时整行原子写入。
 * 日志文件由其他进程共享读取 (FILE_SHARE_READ)，方便实时 tail。
 */
static void log_write(const char *m) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char t[32];
    DWORD w;
    wsprintfA(t, "[%02d:%02d:%02d.%03d] ",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    WriteFile(g_log, t, lstrlenA(t), &w, NULL);
    int l = 0;
    while (m[l]) l++; /* 手动 strlen (免去 string.h 依赖) */
    WriteFile(g_log, m, l, &w, NULL);
    WriteFile(g_log, "\r\n", 2, &w, NULL); /* Windows 换行 */
    LeaveCriticalSection(&g_cs);
}

/*
 * log_close — 关闭日志文件并释放临界区资源
 */
static void log_close(void) {
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
    DeleteCriticalSection(&g_cs);
}

/* ============================================================
 * 5 字节内联 Hook 引擎
 * ============================================================
 *
 * 这是整个项目的核心技术原语。在 x86 架构上，用一个 5 字节的相对
 * JMP 指令 (E9 xx xx xx xx) 替换目标函数的前 5 字节，将执行流重定向
 * 到我们的钩子函数。
 *
 * Trampoline 内存布局 (VirtualAlloc 分配 32 字节, RWX):
 *
 *   偏移  内容                          说明
 *   ─────────────────────────────────────────────────
 *   [0]   原始函数的第 1 字节             保存的
 *   [1]   原始函数的第 2 字节             原指令
 *   [2]   原始函数的第 3 字节             （5 字节）
 *   [3]   原始函数的第 4 字节
 *   [4]   原始函数的第 5 字节
 *   [5]   0xE9                          JMP 操作码
 *   [6]   偏移量低字节                   跳转目标 =
 *   [7]   偏移量                          (tgt+5) - (tr+10)
 *   [8]   偏移量                          即跳回原函数的下一条指令
 *   [9]   偏移量高字节
 *   [10..31] 未使用（预留对齐）
 *
 * 目标函数入口修改后:
 *
 *   [0]   0xE9                          JMP 操作码
 *   [1]   偏移量低字节                   跳转目标 =
 *   [2]   偏移量                          hook - (tgt+5)
 *   [3]   偏移量                          即跳转到钩子函数
 *   [4]   偏移量高字节
 *
 * 调用流程:
 *   游戏调 MessageBoxA → [JMP Hook_MBA] → 判断是否为校验弹窗
 *     → 是: 直接返回 IDYES/IDRETRY
 *     → 否: 调 trampoline → [执行原 5 字节] → [JMP 回原函数+5] → 正常显示
 *
 * 参数:
 *   tgt  — 目标函数地址（被 Hook 的一方）
 *   hook — 钩子函数地址（替换函数）
 *   tr   — [输出] trampoline 地址指针。Hook 函数通过 ((原函数类型)*tr)(...)
 *           来调用原始函数。
 *
 * 返回值: TRUE 成功, FALSE（VirtualAlloc 失败）
 *
 * 关键假设:
 *   目标函数前 5 字节构成完整的、可独立执行的指令序列。
 *   Windows DLL 导出函数通常以 hot-patching 前缀开头:
 *     mov edi, edi    (2 字节)
 *     push ebp        (1 字节)
 *     mov ebp, esp    (2 字节)
 *   恰好 5 字节，这是微软为 API Hook 场景特意保留的标准前缀。
 *
 * 线程安全:
 *   本函数在 DllMain 的 DLL_PROCESS_ATTACH 中被调用。此时 Loader
 *   持有进程锁，尚未启动其他线程，因此对目标函数的修改是安全的。
 *   没有额外的同步保护。
 *
 * 魔法数字速查:
 *   5  — x86 near JMP 指令长度 (1 操作码 + 4 偏移)
 *   6  — trampoline 中 JMP 偏移字段的起始位置（= trampoline_base + 5 + 1）
 *   10 — trampoline 中 JMP 指令的下一条地址（= trampoline_base + 5 + 5）
 *   32 — 为 trampoline 分配的字节数（目前只用了 10 字节，余量充足）
 */
static BOOL hook5(PBYTE tgt, PBYTE hook, PBYTE *tr) {
    DWORD old_protect;

    /*
     * 第一步: 分配 trampoline
     * 32 字节可读可写可执行内存。虽然实际只用到 10 字节，
     * 但多分配一些留作 safety margin——万一需要保存超过 5 字节
     * 的原指令，不必改这个数字。
     */
    *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                       PAGE_EXECUTE_READWRITE);
    if (!*tr) return FALSE;

    /*
     * 第二步: 让目标函数前 5 字节变为可写
     * 正常代码段是 RX（不可写），直接写会触发访问违例。
     * VirtualProtect 改为 RWX 后写入，最后恢复原保护。
     */
    VirtualProtect(tgt, 5, PAGE_EXECUTE_READWRITE, &old_protect);

    /* ---- 构建 Trampoline ---- */

    /* 保存目标函数前 5 字节原指令 */
    for (int i = 0; i < 5; i++)
        (*tr)[i] = tgt[i];

    /* 在 trampoline 第 5 字节处写 JMP，跳回原函数 + 5 处
     * 相对偏移公式: offset = 目标地址 - (JMP指令地址 + 5)
     *              = (tgt+5) - ((*tr) + 10) */
    (*tr)[5] = 0xE9;
    *(DWORD*)((*tr) + 6) = (DWORD)((tgt + 5) - ((*tr) + 10));

    /* ---- 修改目标函数入口 ---- */

    /* 在目标函数入口写 JMP，跳到钩子函数
     * 相对偏移公式: offset = hook - (tgt + 5) */
    tgt[0] = 0xE9;
    *(DWORD*)(tgt + 1) = (DWORD)(hook - (tgt + 5));

    /* 恢复目标函数原始内存保护标志 */
    VirtualProtect(tgt, 5, old_protect, &old_protect);

    /*
     * 刷新 CPU 指令缓存。
     * x86 CPU 可能会缓存已解码的指令。如果不 flush，
     * 当程序执行到 tgt 时可能仍运行修改前的旧指令，
     * 导致 Hook 不生效——表现为间歇性漏拦截。
     */
    FlushInstructionCache(GetCurrentProcess(), tgt, 5);

    return TRUE;
}

/*
 * gf — Get Function，获取 DLL 导出函数地址
 *
 * 按需加载 DLL 并解析函数地址的便捷封装。
 *
 * 参数:
 *   d — DLL 模块名（如 "user32.dll"）
 *   n — 导出函数名（如 "MessageBoxA"）
 *
 * 返回: 函数地址 (PBYTE)，失败返回 NULL
 */
static PBYTE gf(const char *d, const char *n) {
    HMODULE m = GetModuleHandleA(d);  /* 先检查 DLL 是否已加载 */
    if (!m) m = LoadLibraryA(d);      /* 未加载则加载之 */
    return m ? (PBYTE)GetProcAddress(m, n) : NULL;
}

/* ============================================================
 * MessageBoxA Hook — 拦截 ANSI 版弹窗
 * ============================================================ */

/* MessageBox 函数指针类型，用于通过 trampoline 调用原始函数 */
typedef int (WINAPI *MB_t)(HWND, LPCSTR, LPCSTR, UINT);

/* g_mba — MessageBoxA 的 trampoline，存储原函数前 5 字节 + 回跳代码 */
/* g_mbw — MessageBoxW 的 trampoline */
static PBYTE g_mba = NULL, g_mbw = NULL;

/* ============================================================
 * Shift-JIS 字节匹配
 * ============================================================
 *
 * Shift-JIS (SJIS) 是日文 Windows 的传统编码，Eushully 的老游戏
 * 全部使用 SJIS 输出对话框文本。
 *
 * 为什么不能用 strstr():
 *   SJIS 是双字节编码。第 2 字节的范围是 0x40-0xFC，其中包含了
 *   0x5C ('\') 和 0x7C ('|') 等 ASCII 可打印字符。更重要的是，
 *   某些 SJIS 序列会产生值为 0x00 的字节——这对 C 的 strstr()
 *   来说就是字符串结束符,会导致匹配提前终止。
 *
 * 算法: 朴素的逐字节滑动窗口匹配，对 SJIS 完全透明。
 *
 * 参数:
 *   h      — 待搜索的 ANSI 字符串 (SJIS 编码)
 *   needle — 要匹配的 SJIS 字节序列
 *   nlen   — needle 的字节长度
 *
 * 返回: 1 = 找到, 0 = 未找到
 */
static int match_sjis(const char *h, const unsigned char *needle, int nlen) {
    if (!h || !needle || !nlen) return 0;
    while (*h) {
        int i;
        for (i = 0; i < nlen && h[i] && (unsigned char)h[i] == needle[i]; i++);
        if (i == nlen) return 1;  /* nlen 字节全部匹配成功 */
        h++;
    }
    return 0;
}

/*
 * Shift-JIS 字节序列常量
 *
 *  文字    Shift-JIS 字节             出现位置
 *  ───────────────────────────────────────────
 *  エラー  83 47 83 89 81 5B          弹窗标题栏
 *  データ  83 66 81 5B 83 5E          弹窗消息正文
 *
 * 游戏在校验失败时弹出标题为 "エラー" 的 MessageBox，
 * 有时内容包含 "データが壊れています"。
 */
static const unsigned char k_er_sj[] = {0x83, 0x47, 0x83, 0x89, 0x81, 0x5B};
static const unsigned char k_da_sj[] = {0x83, 0x66, 0x81, 0x5B, 0x83, 0x5E};

/* 累计拦截的弹窗数量（仅用于日志统计，不作控制流依据） */
static int g_suppressed = 0;

/*
 * Hook_MBA — MessageBoxA 的替换函数
 *
 * 每次游戏调用 MessageBoxA 时，执行流先进入此函数。
 *
 * 判断流程:
 *   1. 提取按钮类型 (u & 0x0F)，剥离图标标志位
 *   2. 检查标题(t)和内容(c)是否包含 SJIS 关键词
 *      → 标题/内容含 "エラー" → 是校验弹窗
 *      → 标题含 "データ"     → 是校验弹窗（内容可能太长不匹配）
 *   3. 是校验弹窗:
 *      - Yes/No 类按钮 → 返回 IDYES(6)
 *      - Abort/Retry/Ignore → 返回 IDRETRY(4)
 *   4. 不是校验弹窗:
 *      - 通过 trampoline (g_mba) 调用原始 MessageBoxA 正常显示
 *
 * 参数: 同标准 MessageBoxA(hWnd, lpText, lpCaption, uType)
 */
static int WINAPI Hook_MBA(HWND hw, LPCSTR t, LPCSTR c, UINT u) {
    /* 提取纯按钮类型: 低 4 位是按钮组合，高位是图标/默认按钮等 */
    UINT bt = u & 0x0F;

    /* 检测是否为校验弹窗
     * 同时检查标题(t)和内容(c)，覆盖不同游戏版本的弹窗格式 */
    int is_err = (c && match_sjis(c, k_er_sj, 6)) ||   /* 内容含"エラー" */
                 (t && match_sjis(t, k_er_sj, 6)) ||   /* 标题含"エラー" */
                 (t && match_sjis(t, k_da_sj, 6));     /* 标题含"データ" */

    /* 按钮类型: [是]/[否] 或 [重试]/[取消] → 自动点"是" */
    if (is_err && (bt == MB_YESNO || bt == MB_YESNOCANCEL || bt == MB_RETRYCANCEL)) {
        g_suppressed++;
        char m[100];
        wsprintfA(m, "[skip] verification popup #%d suppressed (btns=%d)",
                  g_suppressed, bt);
        log_write(m);
        return IDYES;
    }

    /* 按钮类型: [中止]/[重试]/[忽略] → 自动点"重试" */
    if (is_err && bt == MB_ABORTRETRYIGNORE) {
        g_suppressed++;
        char m[100];
        wsprintfA(m, "[skip] verification popup #%d suppressed (btns=%d)",
                  g_suppressed, bt);
        log_write(m);
        return IDRETRY;
    }

    /* 非校验弹窗 — 原样显示 */
    return ((MB_t)g_mba)(hw, t, c, u);
}

/* ============================================================
 * 宽字符串子串搜索
 * ============================================================
 *
 * wsstr (wide string substring) — 宽字符串版的 strstr。
 * 在 Haystack(h) 中查找 Needle(n) 的首次出现。
 *
 * 用于 MessageBoxW 钩子中匹配 Unicode 版 "エラー" (U+30A8 U+30E9 U+30FC)。
 *
 * 参数:
 *   h — 被搜索的宽字符串
 *   n — 要匹配的宽字符串子串
 * 返回: 首次匹配位置的指针，未找到返回 NULL
 */
static const WCHAR* wsstr(const WCHAR *h, const WCHAR *n) {
    if (!h || !n || !*n) return NULL;
    while (*h) {
        const WCHAR *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return h;  /* b 已走到末尾 → 全部匹配成功 */
        h++;
    }
    return NULL;
}

/* ============================================================
 * MessageBoxW Hook — 拦截 Unicode 版弹窗
 * ============================================================
 *
 * 与 Hook_MBA 逻辑相同，但处理 Unicode (UTF-16LE) 版本的 MessageBox。
 *
 * 关键差异:
 *   - 不使用 SJIS 字节匹配，直接用 C 宽字符字面量 L"エラー"
 *   - Windows 内部 MessageBoxW 的参数本身就是 UTF-16LE，日文字符
 *     可以直接用宽字符常量表达，无需编码转换。
 *   - 对比 Hook_MBA: ANSI 版的参数是 SJIS 字节流，必须用字节序列匹配。
 *
 * 注意: wsstr 只在内容(c)和标题(t)中搜索 "エラー"，不搜索 "データ"。
 *       对于 Unicode 弹窗，仅 "エラー" 标题就足够判定为校验弹窗。
 */
static int WINAPI Hook_MBW(HWND hw, LPCWSTR t, LPCWSTR c, UINT u) {
    UINT bt = u & 0x0F;

    /* Unicode 版直接用宽字符串匹配 "エラー" */
    int is_err = (c && wsstr(c, L"エラー")) ||
                 (t && wsstr(t, L"エラー"));

    if (is_err && (bt == MB_YESNO || bt == MB_YESNOCANCEL || bt == MB_RETRYCANCEL)) {
        g_suppressed++;
        char m[100];
        wsprintfA(m, "[skip] verification popup #%d suppressed (wide, btns=%d)",
                  g_suppressed, bt);
        log_write(m);
        return IDYES;
    }
    if (is_err && bt == MB_ABORTRETRYIGNORE) {
        g_suppressed++;
        char m[100];
        wsprintfA(m, "[skip] verification popup #%d suppressed (wide, btns=%d)",
                  g_suppressed, bt);
        log_write(m);
        return IDRETRY;
    }
    /* 非校验弹窗 — 原样显示 */
    return ((MB_t)g_mbw)(hw, t, c, u);
}

/* ============================================================
 * DllMain — 插件入口函数
 * ============================================================
 *
 * 当 ASI Loader 将此 DLL 加载到游戏进程时，Windows 自动调用此函数。
 *
 * DLL_PROCESS_ATTACH (r == 1):
 *   ① DisableThreadLibraryCalls — 禁止后续线程创建/销毁通知，减少开销
 *   ② 打开日志 (log_open)
 *   ③ Hook MessageBoxA → Hook_MBA
 *   ④ Hook MessageBoxW → Hook_MBW
 *
 * DLL_PROCESS_DETACH (r == 0):
 *   ① 记录累计拦截数量到日志
 *   ② 关闭日志 (log_close)
 *
 * 已知局限:
 *   - 卸载时没有 unhook（恢复目标函数的原始 5 字节）。
 *     游戏进程退出时 DLL 卸载，但进程也即将终止，故无实际影响。
 *     如果在进程运行中途卸载 DLL，后续调用 MessageBox 会跳转到
 *     已释放的代码区导致崩溃。
 *   - DllMain 中调用了 LoadLibraryA/CreateFileA 等函数。
 *     这在规范上属于 DllMain 最佳实践中警告避免的操作，但实际中
 *     kernel32 在 DllMain 之前已完成初始化，此处安全。
 */
BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID v) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        log_open();
        log_write("[skip_popup] plugin loaded");

        /* Hook MessageBoxA — 拦截 ANSI (SJIS) 弹窗 */
        PBYTE ma = gf("user32.dll", "MessageBoxA");
        if (ma && hook5(ma, (PBYTE)Hook_MBA, &g_mba)) {
            log_write("[skip_popup] MessageBoxA hooked");
        } else {
            log_write("[skip_popup] MessageBoxA hook FAILED");
        }

        /* Hook MessageBoxW — 拦截 Unicode 弹窗（兜底） */
        PBYTE mw = gf("user32.dll", "MessageBoxW");
        if (mw && hook5(mw, (PBYTE)Hook_MBW, &g_mbw)) {
            log_write("[skip_popup] MessageBoxW hooked");
        } else {
            log_write("[skip_popup] MessageBoxW hook FAILED");
        }

        char m[64];
        wsprintfA(m, "[skip_popup] intercept count: %d", g_suppressed);
        log_write(m);
    } else if (r == DLL_PROCESS_DETACH) {
        char m[64];
        wsprintfA(m, "[skip_popup] unloaded, total suppressed: %d", g_suppressed);
        log_write(m);
        log_close();
    }
    return TRUE;
}
