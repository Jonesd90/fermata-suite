// Stand-in for <windows.h> and the COM basics, only for tools/check-windows-code.sh's check of src/ui/VideoReaderWin.cpp (fermata 3.68.0): just
// what that file uses, with the shapes of the real Windows SDK as its writer knew them. It proves the file's own grammar and that it uses
// these calls consistently - NOT that the real headers agree (CMakeLists.txt finds that out on Windows, and falls back if they do not).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
typedef long HRESULT; typedef unsigned long ULONG; typedef unsigned long DWORD; typedef int BOOL; typedef long LONG; typedef long long LONGLONG; typedef unsigned long long ULONGLONG;
typedef unsigned short WORD; typedef unsigned char BYTE; typedef unsigned int UINT; typedef std::uint32_t UINT32; typedef std::int32_t INT32; typedef std::uint64_t UINT64; typedef std::uint8_t UINT8;
typedef void* HANDLE; typedef void* HMODULE; typedef void* LPVOID; typedef const wchar_t* LPCWSTR; typedef wchar_t* LPWSTR; typedef const char* LPCSTR; typedef unsigned short VARTYPE;
typedef long long INT_PTR;
#define WINAPI
#define STDAPI HRESULT
#define TRUE 1
#define FALSE 0
#define near
#define far
#define pascal
#define small char
#define hyper long long
#define interface struct
#ifndef NOMINMAX
 #define min(a, b) (((a) < (b)) ? (a) : (b))
 #define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
struct GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; };
typedef const GUID& REFGUID; typedef const GUID& REFIID;
extern const GUID GUID_NULL;
inline int IsEqualGUID (REFGUID a, REFGUID b) { return std::memcmp (&a, &b, sizeof (GUID)) == 0; }
template <typename T> const GUID& stub_uuidof();
#define __uuidof(T) stub_uuidof<T>()
#define SUCCEEDED(hr) (((HRESULT) (hr)) >= 0)
#define FAILED(hr) (((HRESULT) (hr)) < 0)
#define E_INVALIDARG ((HRESULT) 0x80070057L)
#define E_ACCESSDENIED ((HRESULT) 0x80070005L)
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_SHARING_VIOLATION 32L
inline HRESULT HRESULT_FROM_WIN32 (unsigned long x) { return (HRESULT) x <= 0 ? (HRESULT) x : (HRESULT) ((x & 0x0000FFFF) | (7 << 16) | 0x80000000); }
#define CP_UTF8 65001
int MultiByteToWideChar (UINT, DWORD, LPCSTR, int, LPWSTR, int);
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
typedef INT_PTR (WINAPI* FARPROC)();
HMODULE LoadLibraryExW (LPCWSTR, HANDLE, DWORD);
FARPROC GetProcAddress (HMODULE, LPCSTR);
struct IUnknown { virtual HRESULT QueryInterface (REFIID riid, void** ppvObject) = 0; virtual ULONG AddRef() = 0; virtual ULONG Release() = 0; };
struct SIZE { LONG cx, cy; };
union LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; };
union ULARGE_INTEGER { struct { DWORD LowPart; DWORD HighPart; } u; ULONGLONG QuadPart; };
enum VARENUM { VT_EMPTY = 0, VT_I8 = 20, VT_UI8 = 21 };
struct PROPVARIANT { VARTYPE vt; WORD wReserved1, wReserved2, wReserved3; union { LARGE_INTEGER hVal; ULARGE_INTEGER uhVal; }; };
typedef const PROPVARIANT& REFPROPVARIANT;
#define PropVariantInit(pvar) memset ((pvar), 0, sizeof (PROPVARIANT))
HRESULT PropVariantClear (PROPVARIANT*);
enum COINIT { COINIT_APARTMENTTHREADED = 0x2, COINIT_MULTITHREADED = 0x0 };
HRESULT CoInitializeEx (LPVOID, DWORD);
void CoUninitialize();
