#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>

ID3D11DeviceContext *WrapContext(ID3D11DeviceContext *orig, ID3D11Device *wrappedDevice);
ID3D11DeviceContext1 *WrapContext1(ID3D11DeviceContext1 *orig, ID3D11Device *wrappedDevice);
