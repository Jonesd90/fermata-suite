// Stand-in for <mfapi.h> (see windows.h in this folder).
#pragma once
#include <windows.h>
struct IMFAttributes : IUnknown
{
    virtual HRESULT GetUINT32 (REFGUID guidKey, UINT32* punValue) = 0;
    virtual HRESULT GetUINT64 (REFGUID guidKey, UINT64* punValue) = 0;
    virtual HRESULT GetGUID (REFGUID guidKey, GUID* pguidValue) = 0;
    virtual HRESULT GetBlob (REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) = 0;
    virtual HRESULT SetUINT32 (REFGUID guidKey, UINT32 unValue) = 0;
    virtual HRESULT SetUINT64 (REFGUID guidKey, UINT64 unValue) = 0;
    virtual HRESULT SetGUID (REFGUID guidKey, REFGUID guidValue) = 0;
    virtual HRESULT SetUnknown (REFGUID guidKey, IUnknown* pUnknown) = 0;
};
struct IMFMediaType : IMFAttributes {};
struct IMFMediaBuffer : IUnknown
{
    virtual HRESULT Lock (BYTE** ppbBuffer, DWORD* pcbMaxLength, DWORD* pcbCurrentLength) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetCurrentLength (DWORD* pcbCurrentLength) = 0;
};
struct IMF2DBuffer : IUnknown
{
    virtual HRESULT Lock2D (BYTE** ppbScanline0, LONG* plPitch) = 0;
    virtual HRESULT Unlock2D() = 0;
};
struct IMFSample : IMFAttributes
{
    virtual HRESULT GetSampleTime (LONGLONG* phnsSampleTime) = 0;
    virtual HRESULT GetSampleDuration (LONGLONG* phnsSampleDuration) = 0;
    virtual HRESULT GetBufferCount (DWORD* pdwBufferCount) = 0;
    virtual HRESULT GetBufferByIndex (DWORD dwIndex, IMFMediaBuffer** ppBuffer) = 0;
    virtual HRESULT ConvertToContiguousBuffer (IMFMediaBuffer** ppBuffer) = 0;
};
struct IMFDXGIDeviceManager : IUnknown { virtual HRESULT ResetDevice (IUnknown* pUnkDevice, UINT resetToken) = 0; };
struct MFOffset { WORD fract; short value; };
struct MFVideoArea { MFOffset OffsetX; MFOffset OffsetY; SIZE Area; };
enum MFVideoInterlaceMode { MFVideoInterlace_Unknown = 0, MFVideoInterlace_Progressive = 2 };
#define MF_VERSION 0x00020070
#define MFSTARTUP_FULL 0
STDAPI MFStartup (ULONG Version, DWORD dwFlags = MFSTARTUP_FULL);
STDAPI MFCreateAttributes (IMFAttributes** ppMFAttributes, UINT32 cInitialSize);
STDAPI MFCreateMediaType (IMFMediaType** ppMFType);
STDAPI MFGetStrideForBitmapInfoHeader (DWORD format, DWORD dwWidth, LONG* pStride);
STDAPI MFCreateDXGIDeviceManager (UINT* resetToken, IMFDXGIDeviceManager** ppDeviceManager);
inline HRESULT MFGetAttributeSize (IMFAttributes* pAttributes, REFGUID guidKey, UINT32* punWidth, UINT32* punHeight) { UINT64 v = 0; const HRESULT hr = pAttributes->GetUINT64 (guidKey, &v); if (SUCCEEDED (hr)) { *punWidth = (UINT32) (v >> 32); *punHeight = (UINT32) v; } return hr; }
inline HRESULT MFGetAttributeRatio (IMFAttributes* pAttributes, REFGUID guidKey, UINT32* punNumerator, UINT32* punDenominator) { return MFGetAttributeSize (pAttributes, guidKey, punNumerator, punDenominator); }
inline HRESULT MFSetAttributeSize (IMFAttributes* pAttributes, REFGUID guidKey, UINT32 unWidth, UINT32 unHeight) { return pAttributes->SetUINT64 (guidKey, ((UINT64) unWidth << 32) | unHeight); }
inline HRESULT MFSetAttributeRatio (IMFAttributes* pAttributes, REFGUID guidKey, UINT32 unNumerator, UINT32 unDenominator) { return MFSetAttributeSize (pAttributes, guidKey, unNumerator, unDenominator); }
extern const GUID MF_MT_MAJOR_TYPE, MF_MT_SUBTYPE, MF_MT_FRAME_SIZE, MF_MT_FRAME_RATE, MF_MT_PIXEL_ASPECT_RATIO, MF_MT_INTERLACE_MODE, MF_MT_DEFAULT_STRIDE,
                  MF_MT_MINIMUM_DISPLAY_APERTURE, MF_MT_VIDEO_ROTATION, MF_MT_AUDIO_NUM_CHANNELS, MF_MT_AUDIO_SAMPLES_PER_SEC, MF_MT_AUDIO_BITS_PER_SAMPLE,
                  MFMediaType_Video, MFMediaType_Audio, MFVideoFormat_RGB32, MFAudioFormat_Float, MFAudioFormat_PCM;
