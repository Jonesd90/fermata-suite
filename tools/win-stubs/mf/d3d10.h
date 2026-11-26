// Stand-in for <d3d10.h> (see windows.h in this folder).
#pragma once
#include <windows.h>
struct ID3D10Multithread : IUnknown { virtual void Enter() = 0; virtual void Leave() = 0; virtual BOOL SetMultithreadProtected (BOOL bMTProtect) = 0; virtual BOOL GetMultithreadProtected() = 0; };
