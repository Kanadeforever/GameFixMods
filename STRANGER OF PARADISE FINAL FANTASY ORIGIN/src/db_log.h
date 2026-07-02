#pragma once

#include <windows.h>
#include <cstdio>
#include <cstdarg>

#ifdef DBLOOM_NO_FILE_LOG

inline void DB_Log(const char *)
{
}

inline void DB_LOGF(const char *, ...)
{
}

#else

inline void DB_Log(const char *msg)
{
    static char logPath[MAX_PATH] = {};
    if (!logPath[0]) {
        HMODULE hMod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DB_Log), &hMod);
        GetModuleFileNameA(hMod, logPath, MAX_PATH);
        char *dot = strrchr(logPath, '.');
        if (dot) *dot = '\0';
        strcat_s(logPath, ".log");
    }

    FILE *f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

inline void DB_LOGF(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    DB_Log(buf);
}

#endif
