// Stand-in for <d3d11.h> (see windows.h in this folder).
#pragma once
#include <windows.h>
struct IDXGIAdapter; struct ID3D11DeviceContext;
struct ID3D11Device : IUnknown {};
enum D3D_DRIVER_TYPE { D3D_DRIVER_TYPE_UNKNOWN = 0, D3D_DRIVER_TYPE_HARDWARE = 1 };
enum D3D_FEATURE_LEVEL { D3D_FEATURE_LEVEL_10_0 = 0xa000, D3D_FEATURE_LEVEL_10_1 = 0xa100, D3D_FEATURE_LEVEL_11_0 = 0xb000, D3D_FEATURE_LEVEL_11_1 = 0xb100 };
enum D3D11_CREATE_DEVICE_FLAG { D3D11_CREATE_DEVICE_BGRA_SUPPORT = 0x20, D3D11_CREATE_DEVICE_VIDEO_SUPPORT = 0x800 };
#define D3D11_SDK_VERSION 7
HRESULT WINAPI D3D11CreateDevice (IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
                                  ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);
