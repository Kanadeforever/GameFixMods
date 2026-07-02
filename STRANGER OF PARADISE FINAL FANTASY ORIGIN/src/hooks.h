#pragma once

#include <windows.h>
#include <d3d11.h>
#include <cstdint>

#include "db_log.h"

constexpr uint64_t TARGET_BLOOM_HASH = 0x775de01cde4f0102ULL;
constexpr LONG MAX_TRACKED_BLOOM_SHADERS = 16;

extern CRITICAL_SECTION g_cs;
extern ID3D11PixelShader *g_bloomShaders[MAX_TRACKED_BLOOM_SHADERS];
extern volatile LONG g_bloomShaderCount;

extern ID3D11Device *g_device;
extern ID3D11DeviceContext *g_context;
extern ID3D11PixelShader *g_currentPS;

extern bool g_bloomEnabled;     // false = bloom disabled
extern bool g_keyDown;          // Numpad0 edge state

uint64_t HashShaderBytecode(const void *bytecode, SIZE_T len);
bool IsTargetBloomHash(uint64_t hash);
void RememberPixelShader(ID3D11PixelShader *ps, uint64_t hash);
bool IsRememberedBloomShader(ID3D11PixelShader *ps);
void CheckToggleKey();
