#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <psapi.h>
#include "hooks.h"
#include "d3d11_proxy.h"
#include "d3d11_context_proxy.h"

typedef HRESULT(WINAPI *CreateDeviceAndSwapChain_t)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef HRESULT(WINAPI *CreateDevice_t)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static CreateDeviceAndSwapChain_t Real_D3D11CreateDeviceAndSwapChain = nullptr;
static CreateDevice_t Real_D3D11CreateDevice = nullptr;

static HMODULE FindRealD3D11()
{
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    _strlwr_s(sysPath);

    HMODULE hMods[1024];
    DWORD needed;
    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
            char path[MAX_PATH];
            if (!GetModuleFileNameExA(GetCurrentProcess(), hMods[i], path, MAX_PATH)) continue;
            _strlwr_s(path);
            if (strstr(path, "d3d11.dll") && strstr(path, sysPath)) return hMods[i];
        }
    }

    char dllPath[MAX_PATH];
    GetSystemDirectoryA(dllPath, MAX_PATH);
    strcat_s(dllPath, "\\d3d11.dll");
    return LoadLibraryA(dllPath);
}

static WrappedD3D11Device *WrapDevice(ID3D11Device **ppDev)
{
    if (!ppDev || !*ppDev) return nullptr;
    if (*ppDev == g_device) {
        WrappedD3D11Device *existing = reinterpret_cast<WrappedD3D11Device*>(g_device);
        DB_LOGF("[DBloom] device already wrapped: %p", existing);
        return existing;
    }
    WrappedD3D11Device *wrapped = new WrappedD3D11Device(*ppDev);
    *ppDev = wrapped;
    g_device = wrapped;
    return wrapped;
}

HRESULT WINAPI HookRealCreateDeviceAndSwapChain(
    IDXGIAdapter *a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT flags,
    const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk,
    const DXGI_SWAP_CHAIN_DESC *scd, IDXGISwapChain **ppSC,
    ID3D11Device **ppDev, D3D_FEATURE_LEVEL *pfl, ID3D11DeviceContext **ppCtx)
{
    DB_LOGF("[DBloom] Hook D3D11CreateDeviceAndSwapChain entry");
    HRESULT hr = Real_D3D11CreateDeviceAndSwapChain(a, dt, sw, flags, lv, nlv, sdk, scd, ppSC, ppDev, pfl, ppCtx);
    DB_LOGF("[DBloom] Hook D3D11CreateDeviceAndSwapChain real returned hr=0x%08lx sc=%p dev=%p ctx=%p",
            (unsigned long)hr,
            (ppSC && *ppSC) ? *ppSC : nullptr,
            (ppDev && *ppDev) ? *ppDev : nullptr,
            (ppCtx && *ppCtx) ? *ppCtx : nullptr);
    if (SUCCEEDED(hr)) {
        WrappedD3D11Device *wrappedDev = WrapDevice(ppDev);
        if (ppCtx && *ppCtx && wrappedDev) {
            *ppCtx = WrapContext(*ppCtx, static_cast<ID3D11Device*>(wrappedDev));
            g_context = *ppCtx;
        } else if (ppCtx && *ppCtx) {
            g_context = *ppCtx;
        }
        if (ppSC && *ppSC && wrappedDev) {
            *ppSC = new WrappedDXGISwapChain(*ppSC, wrappedDev);
        }
        DB_LOGF("[DBloom] D3D11CreateDeviceAndSwapChain wrapped hr=0x%08lx", (unsigned long)hr);
    }
    return hr;
}

HRESULT WINAPI HookRealCreateDevice(
    IDXGIAdapter *a, D3D_DRIVER_TYPE dt, HMODULE sw, UINT flags,
    const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk,
    ID3D11Device **ppDev, D3D_FEATURE_LEVEL *pfl, ID3D11DeviceContext **ppCtx)
{
    DB_LOGF("[DBloom] Hook D3D11CreateDevice entry");
    HRESULT hr = Real_D3D11CreateDevice(a, dt, sw, flags, lv, nlv, sdk, ppDev, pfl, ppCtx);
    DB_LOGF("[DBloom] Hook D3D11CreateDevice real returned hr=0x%08lx dev=%p ctx=%p",
            (unsigned long)hr,
            (ppDev && *ppDev) ? *ppDev : nullptr,
            (ppCtx && *ppCtx) ? *ppCtx : nullptr);
    if (SUCCEEDED(hr)) {
        WrappedD3D11Device *wrappedDev = WrapDevice(ppDev);
        if (ppCtx && *ppCtx && wrappedDev) {
            *ppCtx = WrapContext(*ppCtx, static_cast<ID3D11Device*>(wrappedDev));
            g_context = *ppCtx;
        } else if (ppCtx && *ppCtx) {
            g_context = *ppCtx;
        }
        DB_LOGF("[DBloom] D3D11CreateDevice wrapped hr=0x%08lx", (unsigned long)hr);
    }
    return hr;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hMod);
        SetProcessDPIAware();
        InitializeCriticalSection(&g_cs);
        DB_LOGF("[DBloom] === ASI (COM wrapper) ===");

        if (MH_Initialize() != MH_OK) break;

        HMODULE hReal = FindRealD3D11();
        if (!hReal) {
            DB_LOGF("[DBloom] real d3d11.dll not found");
            break;
        }

        FARPROC p1 = GetProcAddress(hReal, "D3D11CreateDeviceAndSwapChain");
        FARPROC p2 = GetProcAddress(hReal, "D3D11CreateDevice");
        if (!p1) break;

        if (p1 && MH_CreateHook(p1, HookRealCreateDeviceAndSwapChain, (void**)&Real_D3D11CreateDeviceAndSwapChain) == MH_OK) {
            MH_EnableHook(p1);
            DB_LOGF("[DBloom] Export hook OK: D3D11CreateDeviceAndSwapChain");
        }
        if (p2) {
            DB_LOGF("[DBloom] Export hook skipped: D3D11CreateDevice (stability)");
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        MH_Uninitialize();
        DeleteCriticalSection(&g_cs);
        break;
    }
    return TRUE;
}
