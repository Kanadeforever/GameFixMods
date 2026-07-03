/**
 * nfsuc_countclamp.asi — Clamp callback array entry count to max 8
 *
 * Hooks at 0x008000DF (mov ebx, eax) and clamps EBX to 8 max.
 * This limits the callback array loop to 8 entries, preventing
 * write overflow past EDI+0x7C0 into uninitialized stack memory.
 *
 * Build (VS2022 x86):
 *   cl /nologo /O2 /LD nfsuc_countclamp.cpp /Fe:nfsuc_countclamp.asi /link kernel32.lib
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

static uintptr_t g_hookAddr = 0;      // 0x008000DF
static uintptr_t g_retAddr = 0;       // 0x008000E6
static uint32_t  g_funcAddr = 0;      // 0x009ED060 (call target)

__declspec(naked) void EntryCountClamp() {
    __asm {
        // Original instruction: mov ebx, eax
        mov  ebx, eax

        // Clamp EBX to max 8
        cmp  ebx, 8
        jle  DO_CALL
        mov  ebx, 8

    DO_CALL:
        // Execute the original call that was partially overwritten
        // call 0x009ED060  → return to 0x008000E6
        push dword ptr [g_retAddr]
        push dword ptr [g_funcAddr]
        ret
    }
}

void InstallClamp() {
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!base) return;

    g_hookAddr = base + 0x4000DF;     // 0x008000DF
    g_retAddr  = base + 0x4000E6;     // 0x008000E6
    g_funcAddr = (uint32_t)(base + 0x5ED060); // 0x009ED060

    int32_t relJmp = (int32_t)((uintptr_t)EntryCountClamp - g_hookAddr - 5);

    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)g_hookAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return;

    uint8_t* p = (uint8_t*)g_hookAddr;
    p[0] = 0xE9;  // JMP rel32
    *(int32_t*)(p + 1) = relJmp;

    VirtualProtect((LPVOID)g_hookAddr, 5, oldProtect, &oldProtect);
}

extern "C" __declspec(dllexport) void InitializeASI() {
    InstallClamp();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InstallClamp();
    }
    return TRUE;
}
