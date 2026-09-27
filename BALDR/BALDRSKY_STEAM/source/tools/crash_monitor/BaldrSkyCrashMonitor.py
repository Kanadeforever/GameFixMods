# -*- coding: utf-8 -*-
"""
BALDR SKY Steam 版崩溃监视器 v0.1

用途：
1. 等待 BaldrSky.exe 启动。
2. 使用 Windows 原生调试 API 附加到游戏进程。
3. 记录调试事件、DLL 加载地址以及异常现场。
4. 遇到异常时尝试使用系统 dbghelp.dll 主动生成 DMP。
5. 不修改游戏文件，不向游戏注入 DLL。

本文件故意写了非常详细的中文注释。
即使只学过最基础的 Python，也可以沿着注释理解每一步在做什么。
"""

import ctypes
from ctypes import wintypes
import datetime
import os
import struct
import subprocess
import sys
import time
import traceback

# ------------------------------------------------------------
# 第一部分：一些固定值
# ------------------------------------------------------------

TARGET_EXE = "BaldrSky.exe"

# Windows 调试 API 使用的事件编号。
EXCEPTION_DEBUG_EVENT = 1
CREATE_THREAD_DEBUG_EVENT = 2
CREATE_PROCESS_DEBUG_EVENT = 3
EXIT_THREAD_DEBUG_EVENT = 4
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
UNLOAD_DLL_DEBUG_EVENT = 7
OUTPUT_DEBUG_STRING_EVENT = 8
RIP_EVENT = 9

# ContinueDebugEvent() 需要告诉 Windows：
# “这个异常我有没有处理掉？”
#
# DBG_CONTINUE 表示调试器已经处理。
# DBG_EXCEPTION_NOT_HANDLED 表示没有处理，让游戏自己的异常处理器继续接手。
#
# 为了尽量不改变 BALDR SKY 原本的运行行为，
# 对游戏异常默认使用 NOT_HANDLED。
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001

# 常见异常代码。
EXCEPTION_ACCESS_VIOLATION = 0xC0000005
EXCEPTION_BREAKPOINT = 0x80000003
EXCEPTION_SINGLE_STEP = 0x80000004

# 打开进程时需要的权限。
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
PROCESS_DUP_HANDLE = 0x0040
PROCESS_ALL_FOR_DUMP = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE

# Toolhelp32 用于枚举系统进程。
TH32CS_SNAPPROCESS = 0x00000002

# MiniDumpWriteDump 的 dump 类型。
# 这里不用极端巨大的“完整物理内存”模式，
# 但会保存分析崩溃通常需要的用户态内存、线程和模块信息。
MiniDumpWithDataSegs = 0x00000001
MiniDumpWithHandleData = 0x00000004
MiniDumpWithUnloadedModules = 0x00000020
MiniDumpWithIndirectlyReferencedMemory = 0x00000040
MiniDumpWithThreadInfo = 0x00001000
DUMP_TYPE = (
    MiniDumpWithDataSegs
    | MiniDumpWithHandleData
    | MiniDumpWithUnloadedModules
    | MiniDumpWithIndirectlyReferencedMemory
    | MiniDumpWithThreadInfo
)

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)

# ------------------------------------------------------------
# 第二部分：定义 Windows API 会用到的数据结构
# ------------------------------------------------------------

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    ]

class EXCEPTION_RECORD(ctypes.Structure):
    pass

EXCEPTION_RECORD._fields_ = [
    ("ExceptionCode", wintypes.DWORD),
    ("ExceptionFlags", wintypes.DWORD),
    ("ExceptionRecord", ctypes.POINTER(EXCEPTION_RECORD)),
    ("ExceptionAddress", ctypes.c_void_p),
    ("NumberParameters", wintypes.DWORD),
    ("ExceptionInformation", ctypes.c_size_t * 15),
]

class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("ExceptionRecord", EXCEPTION_RECORD),
        ("dwFirstChance", wintypes.DWORD),
    ]

class CREATE_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hThread", wintypes.HANDLE),
        ("lpThreadLocalBase", ctypes.c_void_p),
        ("lpStartAddress", ctypes.c_void_p),
    ]

class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hFile", wintypes.HANDLE),
        ("hProcess", wintypes.HANDLE),
        ("hThread", wintypes.HANDLE),
        ("lpBaseOfImage", ctypes.c_void_p),
        ("dwDebugInfoFileOffset", wintypes.DWORD),
        ("nDebugInfoSize", wintypes.DWORD),
        ("lpThreadLocalBase", ctypes.c_void_p),
        ("lpStartAddress", ctypes.c_void_p),
        ("lpImageName", ctypes.c_void_p),
        ("fUnicode", wintypes.WORD),
    ]

class EXIT_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("dwExitCode", wintypes.DWORD)]

class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("dwExitCode", wintypes.DWORD)]

class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("hFile", wintypes.HANDLE),
        ("lpBaseOfDll", ctypes.c_void_p),
        ("dwDebugInfoFileOffset", wintypes.DWORD),
        ("nDebugInfoSize", wintypes.DWORD),
        ("lpImageName", ctypes.c_void_p),
        ("fUnicode", wintypes.WORD),
    ]

class UNLOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p)]

class OUTPUT_DEBUG_STRING_INFO(ctypes.Structure):
    _fields_ = [
        ("lpDebugStringData", ctypes.c_void_p),
        ("fUnicode", wintypes.WORD),
        ("nDebugStringLength", wintypes.WORD),
    ]

class RIP_INFO(ctypes.Structure):
    _fields_ = [("dwError", wintypes.DWORD), ("dwType", wintypes.DWORD)]

# DEBUG_EVENT 内部是一个 union。
# union 的意思是：同一块内存根据事件类型，可以被解释成不同的数据结构。
class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [
        ("Exception", EXCEPTION_DEBUG_INFO),
        ("CreateThread", CREATE_THREAD_DEBUG_INFO),
        ("CreateProcessInfo", CREATE_PROCESS_DEBUG_INFO),
        ("ExitThread", EXIT_THREAD_DEBUG_INFO),
        ("ExitProcess", EXIT_PROCESS_DEBUG_INFO),
        ("LoadDll", LOAD_DLL_DEBUG_INFO),
        ("UnloadDll", UNLOAD_DLL_DEBUG_INFO),
        ("DebugString", OUTPUT_DEBUG_STRING_INFO),
        ("RipInfo", RIP_INFO),
    ]

class DEBUG_EVENT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [
        ("dwDebugEventCode", wintypes.DWORD),
        ("dwProcessId", wintypes.DWORD),
        ("dwThreadId", wintypes.DWORD),
        ("u", DEBUG_EVENT_UNION),
    ]

# x86 CONTEXT。
# BALDR SKY 是 32 位程序。
# 如果 Python 也是 32 位，可以直接使用 GetThreadContext。
# 64 位 Python 调试 32 位程序时则应使用 Wow64GetThreadContext。
#
# 下面的结构按照 Windows 的 x86 CONTEXT 布局定义。
class FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [
        ("ControlWord", wintypes.DWORD),
        ("StatusWord", wintypes.DWORD),
        ("TagWord", wintypes.DWORD),
        ("ErrorOffset", wintypes.DWORD),
        ("ErrorSelector", wintypes.DWORD),
        ("DataOffset", wintypes.DWORD),
        ("DataSelector", wintypes.DWORD),
        ("RegisterArea", ctypes.c_ubyte * 80),
        ("Cr0NpxState", wintypes.DWORD),
    ]

class CONTEXT32(ctypes.Structure):
    _fields_ = [
        ("ContextFlags", wintypes.DWORD),
        ("Dr0", wintypes.DWORD),
        ("Dr1", wintypes.DWORD),
        ("Dr2", wintypes.DWORD),
        ("Dr3", wintypes.DWORD),
        ("Dr6", wintypes.DWORD),
        ("Dr7", wintypes.DWORD),
        ("FloatSave", FLOATING_SAVE_AREA),
        ("SegGs", wintypes.DWORD),
        ("SegFs", wintypes.DWORD),
        ("SegEs", wintypes.DWORD),
        ("SegDs", wintypes.DWORD),
        ("Edi", wintypes.DWORD),
        ("Esi", wintypes.DWORD),
        ("Ebx", wintypes.DWORD),
        ("Edx", wintypes.DWORD),
        ("Ecx", wintypes.DWORD),
        ("Eax", wintypes.DWORD),
        ("Ebp", wintypes.DWORD),
        ("Eip", wintypes.DWORD),
        ("SegCs", wintypes.DWORD),
        ("EFlags", wintypes.DWORD),
        ("Esp", wintypes.DWORD),
        ("SegSs", wintypes.DWORD),
        ("ExtendedRegisters", ctypes.c_ubyte * 512),
    ]

CONTEXT_i386 = 0x00010000
CONTEXT_CONTROL = CONTEXT_i386 | 0x00000001
CONTEXT_INTEGER = CONTEXT_i386 | 0x00000002
CONTEXT_SEGMENTS = CONTEXT_i386 | 0x00000004
CONTEXT_FULL = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS

# ------------------------------------------------------------
# 第三部分：告诉 ctypes 每个 Windows API 的参数类型
# ------------------------------------------------------------

kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE

kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32FirstW.restype = wintypes.BOOL

kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32NextW.restype = wintypes.BOOL

kernel32.DebugActiveProcess.argtypes = [wintypes.DWORD]
kernel32.DebugActiveProcess.restype = wintypes.BOOL

kernel32.DebugActiveProcessStop.argtypes = [wintypes.DWORD]
kernel32.DebugActiveProcessStop.restype = wintypes.BOOL

kernel32.DebugSetProcessKillOnExit.argtypes = [wintypes.BOOL]
kernel32.DebugSetProcessKillOnExit.restype = wintypes.BOOL

kernel32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wintypes.DWORD]
kernel32.WaitForDebugEvent.restype = wintypes.BOOL

kernel32.ContinueDebugEvent.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]
kernel32.ContinueDebugEvent.restype = wintypes.BOOL

kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE

kernel32.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenThread.restype = wintypes.HANDLE

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

kernel32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
]
kernel32.ReadProcessMemory.restype = wintypes.BOOL

# 线程权限：读取寄存器需要 GET_CONTEXT 和 QUERY_INFORMATION。
THREAD_GET_CONTEXT = 0x0008
THREAD_QUERY_INFORMATION = 0x0040

# ------------------------------------------------------------
# 第四部分：日志辅助函数
# ------------------------------------------------------------

base_dir = os.path.dirname(os.path.abspath(__file__))
stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
log_path = os.path.join(base_dir, "BaldrSky崩溃监视_%s.log" % stamp)

def log(message):
    """同时把一行信息显示到屏幕，并写进日志文件。"""
    now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = "[%s] %s" % (now, message)
    print(line, flush=True)
    with open(log_path, "a", encoding="utf-8-sig") as f:
        f.write(line + "\n")

def winerr(prefix):
    """取得 Windows API 最近一次失败的错误码，方便之后定位问题。"""
    err = ctypes.get_last_error()
    return "%s；Win32错误=%d" % (prefix, err)

# ------------------------------------------------------------
# 第五部分：寻找 BaldrSky.exe
# ------------------------------------------------------------

def find_process(name):
    """枚举当前系统进程，找到指定 exe 后返回 PID。"""
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == wintypes.HANDLE(-1).value:
        return None

    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)

        ok = kernel32.Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == name.lower():
                return int(entry.th32ProcessID)
            ok = kernel32.Process32NextW(snap, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snap)

    return None

# ------------------------------------------------------------
# 第六部分：读取异常线程寄存器
# ------------------------------------------------------------

def get_context(thread_id):
    """
    读取发生异常的线程寄存器。

    返回 CONTEXT32；失败则返回 None。
    """
    hthread = kernel32.OpenThread(
        THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
        False,
        thread_id
    )
    if not hthread:
        log(winerr("无法打开异常线程"))
        return None

    try:
        ctx = CONTEXT32()
        ctx.ContextFlags = CONTEXT_FULL

        # Python 自身如果是 64 位，而目标 BALDR SKY 是 32 位，
        # 应使用 Wow64GetThreadContext。
        if ctypes.sizeof(ctypes.c_void_p) == 8:
            fn = getattr(kernel32, "Wow64GetThreadContext", None)
            if fn is None:
                log("系统没有 Wow64GetThreadContext，无法读取 32 位寄存器。")
                return None
            fn.argtypes = [wintypes.HANDLE, ctypes.POINTER(CONTEXT32)]
            fn.restype = wintypes.BOOL
            ok = fn(hthread, ctypes.byref(ctx))
        else:
            kernel32.GetThreadContext.argtypes = [
                wintypes.HANDLE, ctypes.POINTER(CONTEXT32)
            ]
            kernel32.GetThreadContext.restype = wintypes.BOOL
            ok = kernel32.GetThreadContext(hthread, ctypes.byref(ctx))

        if not ok:
            log(winerr("读取异常线程寄存器失败"))
            return None
        return ctx
    finally:
        kernel32.CloseHandle(hthread)

# ------------------------------------------------------------
# 第七部分：读取目标进程的一小段内存
# ------------------------------------------------------------

def read_memory(hprocess, address, size):
    """
    从 BALDR SKY 的地址空间读取一小段数据。
    这里主要用于把 ESP 附近的栈保存下来。
    """
    if not address:
        return b""

    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(
        hprocess,
        ctypes.c_void_p(address),
        buf,
        size,
        ctypes.byref(got)
    )
    if not ok and got.value == 0:
        return b""
    return buf.raw[:got.value]

def format_stack(data, start_address):
    """把栈数据按每 4 字节一个 x86 地址的方式排版。"""
    lines = []
    usable = len(data) - (len(data) % 4)
    for offset in range(0, usable, 4):
        value = struct.unpack_from("<I", data, offset)[0]
        lines.append(
            "    [0x%08X] = 0x%08X" %
            ((start_address + offset) & 0xFFFFFFFF, value)
        )
    return "\n".join(lines)

# ------------------------------------------------------------
# 第八部分：主动写 DMP
# ------------------------------------------------------------

def write_dump(pid, hprocess, reason):
    """
    在异常仍然停住的时候直接调用 MiniDumpWriteDump。

    这和之前依赖 WER 的方案不同：
    WER 没来得及生成文件也没关系，
    因为现在是监视器自己主动保存。
    """
    safe_reason = reason.replace("0x", "").replace(" ", "_")
    dump_name = "BaldrSky_%s_%s.dmp" % (stamp, safe_reason)
    dump_path = os.path.join(base_dir, dump_name)

    # Windows 的 CreateFileW 用于创建 DMP 文件。
    GENERIC_WRITE = 0x40000000
    CREATE_ALWAYS = 2
    FILE_ATTRIBUTE_NORMAL = 0x80

    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE

    hfile = kernel32.CreateFileW(
        dump_path,
        GENERIC_WRITE,
        0,
        None,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        None
    )

    invalid = wintypes.HANDLE(-1).value
    if hfile == invalid:
        log(winerr("创建 DMP 文件失败"))
        return None

    try:
        dbghelp.MiniDumpWriteDump.argtypes = [
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.HANDLE,
            wintypes.DWORD,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        dbghelp.MiniDumpWriteDump.restype = wintypes.BOOL

        ok = dbghelp.MiniDumpWriteDump(
            hprocess,
            pid,
            hfile,
            DUMP_TYPE,
            None,
            None,
            None
        )
        if ok:
            log("DMP 已保存：%s" % dump_path)
            return dump_path
        else:
            log(winerr("MiniDumpWriteDump 失败"))
            return None
    finally:
        kernel32.CloseHandle(hfile)

# ------------------------------------------------------------
# 第九部分：主监视流程
# ------------------------------------------------------------

def main():
    log("BALDR SKY Steam 崩溃监视器 v0.1")
    log("Python 位数：%d 位" % (ctypes.sizeof(ctypes.c_void_p) * 8))
    log("正在等待 %s 启动……" % TARGET_EXE)
    log("现在请正常从 Steam 启动游戏。")

    pid = None
    while pid is None:
        pid = find_process(TARGET_EXE)
        if pid is None:
            time.sleep(0.25)

    log("发现 %s，PID=%d。" % (TARGET_EXE, pid))

    # 附加调试器。
    if not kernel32.DebugActiveProcess(pid):
        log(winerr("附加到游戏失败"))
        log("如果这里提示访问被拒绝，可尝试右键“以管理员身份运行”启动 BAT。")
        input("按 Enter 退出……")
        return

    log("已成功附加到游戏。")

    # 非常重要：
    # 默认情况下，调试器自身如果异常退出，Windows 可能把被调试程序一起杀掉。
    # 这里明确关闭这个行为，避免监视器自身的问题伤到游戏进程。
    kernel32.DebugSetProcessKillOnExit(False)

    hprocess = kernel32.OpenProcess(PROCESS_ALL_FOR_DUMP, False, pid)
    if not hprocess:
        log(winerr("OpenProcess 失败"))
    else:
        log("已取得进程读取/转储句柄。")

    dumped_codes = set()
    event = DEBUG_EVENT()

    try:
        while True:
            # 500ms 超时意味着即使暂时没有调试事件，
            # Python 也不会永久卡死在 API 内部。
            ok = kernel32.WaitForDebugEvent(ctypes.byref(event), 500)

            if not ok:
                # 121 = ERROR_SEM_TIMEOUT，这只是“这 500ms 没事件”，不是故障。
                err = ctypes.get_last_error()
                if err == 121:
                    continue

                # 如果游戏已经结束，也可能出现其他错误。
                log("WaitForDebugEvent 返回失败，Win32错误=%d。" % err)
                break

            code = int(event.dwDebugEventCode)
            tid = int(event.dwThreadId)
            continue_status = DBG_CONTINUE

            if code == CREATE_PROCESS_DEBUG_EVENT:
                base = int(event.CreateProcessInfo.lpBaseOfImage or 0)
                log("创建进程事件：ImageBase=0x%08X，线程=%d。" % (base, tid))

                # CREATE_PROCESS_DEBUG_INFO 中 Windows 会交给调试器一个文件句柄。
                # 用完必须关闭，否则会泄漏句柄。
                if event.CreateProcessInfo.hFile:
                    kernel32.CloseHandle(event.CreateProcessInfo.hFile)

            elif code == LOAD_DLL_DEBUG_EVENT:
                base = int(event.LoadDll.lpBaseOfDll or 0)
                log("加载 DLL：Base=0x%08X。" % base)
                if event.LoadDll.hFile:
                    kernel32.CloseHandle(event.LoadDll.hFile)

            elif code == UNLOAD_DLL_DEBUG_EVENT:
                base = int(event.UnloadDll.lpBaseOfDll or 0)
                log("卸载 DLL：Base=0x%08X。" % base)

            elif code == EXIT_PROCESS_DEBUG_EVENT:
                exit_code = int(event.ExitProcess.dwExitCode)
                log("游戏进程退出：ExitCode=0x%08X。" % exit_code)
                kernel32.ContinueDebugEvent(pid, tid, DBG_CONTINUE)
                break

            elif code == EXCEPTION_DEBUG_EVENT:
                info = event.Exception
                record = info.ExceptionRecord

                exc_code = int(record.ExceptionCode)
                exc_addr = int(record.ExceptionAddress or 0)
                first = bool(info.dwFirstChance)

                log(
                    "异常：Code=0x%08X，Address=0x%08X，Thread=%d，%s。"
                    % (
                        exc_code,
                        exc_addr,
                        tid,
                        "FirstChance" if first else "SecondChance"
                    )
                )

                # 调试器刚附加时 Windows 通常会故意制造一个断点异常，
                # 这是正常现象，不能把它误判成 BALDR SKY 崩溃。
                if exc_code in (EXCEPTION_BREAKPOINT, EXCEPTION_SINGLE_STEP):
                    continue_status = DBG_CONTINUE
                else:
                    # 对真正的游戏异常读取寄存器。
                    ctx = get_context(tid)
                    if ctx:
                        log(
                            "寄存器："
                            "EAX=%08X EBX=%08X ECX=%08X EDX=%08X "
                            "ESI=%08X EDI=%08X EBP=%08X ESP=%08X "
                            "EIP=%08X EFLAGS=%08X"
                            % (
                                ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx,
                                ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp,
                                ctx.Eip, ctx.EFlags
                            )
                        )

                        # 读取 ESP 开始的 256 字节。
                        # 这通常足够看到若干返回地址和函数参数。
                        if hprocess:
                            stack = read_memory(hprocess, ctx.Esp, 256)
                            if stack:
                                log(
                                    "ESP 附近栈内容（每行 4 字节）：\n"
                                    + format_stack(stack, ctx.Esp)
                                )
                            else:
                                log("ESP 附近内存读取失败。")

                    # Access Violation 有额外参数：
                    # 参数 0：0=读，1=写，8=执行。
                    # 参数 1：被访问的地址。
                    if exc_code == EXCEPTION_ACCESS_VIOLATION and record.NumberParameters >= 2:
                        access_kind = int(record.ExceptionInformation[0])
                        bad_addr = int(record.ExceptionInformation[1])
                        names = {0: "读取", 1: "写入", 8: "执行"}
                        log(
                            "访问冲突详情：动作=%s(%d)，目标地址=0x%08X。"
                            % (names.get(access_kind, "未知"), access_kind, bad_addr)
                        )

                    # 同一种异常代码只主动写一次 dump，
                    # 避免异常循环时疯狂生成大量文件。
                    key = (exc_code, exc_addr)
                    if hprocess and key not in dumped_codes:
                        dumped_codes.add(key)
                        write_dump(
                            pid,
                            hprocess,
                            "EXC_%08X_AT_%08X" % (exc_code, exc_addr)
                        )

                    # 关键点：
                    # 我们只是观察，不声称已经修复异常。
                    # 把异常继续交给游戏自己的 SEH / Windows。
                    continue_status = DBG_EXCEPTION_NOT_HANDLED

            # Windows 要求每个调试事件最终都必须 Continue，
            # 否则被调试程序会一直被冻结。
            if not kernel32.ContinueDebugEvent(pid, tid, continue_status):
                log(winerr("ContinueDebugEvent 失败"))
                break

    except KeyboardInterrupt:
        log("用户按 Ctrl+C 停止监视。")
    except Exception:
        log("监视器自身发生 Python 异常：")
        log(traceback.format_exc())
    finally:
        if hprocess:
            kernel32.CloseHandle(hprocess)

        # 尽量解除调试附加。
        # 如果游戏已经退出，这个 API 失败也没有关系。
        kernel32.DebugActiveProcessStop(pid)

    log("监视结束。")
    log("请把本目录新生成的 .log 和 .dmp 一起压缩发回来。")
    input("按 Enter 关闭窗口……")

if __name__ == "__main__":
    try:
        main()
    except Exception:
        # 即使初始化阶段出错，也尽量留下日志而不是窗口一闪而过。
        with open(log_path, "a", encoding="utf-8-sig") as f:
            f.write(traceback.format_exc())
        print(traceback.format_exc())
        input("监视器发生错误。按 Enter 退出……")
