#include "hooks.h"
#include "hash.h"

CRITICAL_SECTION g_cs;
ID3D11PixelShader *g_bloomShaders[MAX_TRACKED_BLOOM_SHADERS] = {};
volatile LONG g_bloomShaderCount = 0;
ID3D11Device *g_device = nullptr;
ID3D11DeviceContext *g_context = nullptr;
ID3D11PixelShader *g_currentPS = nullptr;
bool g_bloomEnabled = false;
bool g_keyDown      = false;

void CheckToggleKey()
{
    static DWORD lastCheckTick = 0;
    DWORD now = GetTickCount();
    if (lastCheckTick && now - lastCheckTick < 16) return;
    lastCheckTick = now;

    bool curr = (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) != 0;
    if (curr && !g_keyDown) {
        g_bloomEnabled = !g_bloomEnabled;
        DB_LOGF("[DBloom] TOGGLE Bloom=%s", g_bloomEnabled ? "ON" : "OFF");
    }
    g_keyDown = curr;
}

uint64_t HashShaderBytecode(const void *bytecode, SIZE_T len)
{
    if (!bytecode || len == 0) return 0;
    return fnv_64_buf(bytecode, len);
}

bool IsTargetBloomHash(uint64_t hash)
{
    return hash == TARGET_BLOOM_HASH;
}

void RememberPixelShader(ID3D11PixelShader *ps, uint64_t hash)
{
    if (!ps || !IsTargetBloomHash(hash)) return;
    EnterCriticalSection(&g_cs);
    LONG count = g_bloomShaderCount;
    for (LONG i = 0; i < count; ++i) {
        if (g_bloomShaders[i] == ps) {
            LeaveCriticalSection(&g_cs);
            return;
        }
    }
    if (count < MAX_TRACKED_BLOOM_SHADERS) {
        g_bloomShaders[count] = ps;
        MemoryBarrier();
        InterlockedExchange(&g_bloomShaderCount, count + 1);
    } else {
        DB_LOGF("[DBloom] bloom shader tracking list full, ps=%p", ps);
    }
    LeaveCriticalSection(&g_cs);
}

bool IsRememberedBloomShader(ID3D11PixelShader *ps)
{
    if (!ps) return false;
    LONG count = g_bloomShaderCount;
    if (count > MAX_TRACKED_BLOOM_SHADERS) count = MAX_TRACKED_BLOOM_SHADERS;
    for (LONG i = 0; i < count; ++i) {
        if (g_bloomShaders[i] == ps) return true;
    }
    return false;
}
