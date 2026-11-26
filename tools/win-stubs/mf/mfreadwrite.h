// Stand-in for <mfreadwrite.h> (see windows.h in this folder).
#pragma once
#include <mfidl.h>
enum { MF_SOURCE_READER_INVALID_STREAM_INDEX = 0xffffffff, MF_SOURCE_READER_ALL_STREAMS = 0xfffffffe, MF_SOURCE_READER_ANY_STREAM = 0xfffffffe,
       MF_SOURCE_READER_FIRST_AUDIO_STREAM = 0xfffffffd, MF_SOURCE_READER_FIRST_VIDEO_STREAM = 0xfffffffc, MF_SOURCE_READER_MEDIASOURCE = 0xffffffff };
enum MF_SOURCE_READER_FLAG { MF_SOURCE_READERF_ERROR = 0x1, MF_SOURCE_READERF_ENDOFSTREAM = 0x2, MF_SOURCE_READERF_NEWSTREAM = 0x4, MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED = 0x10,
                             MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED = 0x20, MF_SOURCE_READERF_STREAMTICK = 0x100 };
struct IMFSourceReader : IUnknown
{
    virtual HRESULT GetStreamSelection (DWORD dwStreamIndex, BOOL* pfSelected) = 0;
    virtual HRESULT SetStreamSelection (DWORD dwStreamIndex, BOOL fSelected) = 0;
    virtual HRESULT GetNativeMediaType (DWORD dwStreamIndex, DWORD dwMediaTypeIndex, IMFMediaType** ppMediaType) = 0;
    virtual HRESULT GetCurrentMediaType (DWORD dwStreamIndex, IMFMediaType** ppMediaType) = 0;
    virtual HRESULT SetCurrentMediaType (DWORD dwStreamIndex, DWORD* pdwReserved, IMFMediaType* pMediaType) = 0;
    virtual HRESULT SetCurrentPosition (REFGUID guidTimeFormat, REFPROPVARIANT varPosition) = 0;
    virtual HRESULT ReadSample (DWORD dwStreamIndex, DWORD dwControlFlags, DWORD* pdwActualStreamIndex, DWORD* pdwStreamFlags, LONGLONG* pllTimestamp, IMFSample** ppSample) = 0;
    virtual HRESULT Flush (DWORD dwStreamIndex) = 0;
    virtual HRESULT GetServiceForStream (DWORD dwStreamIndex, REFGUID guidService, REFIID riid, LPVOID* ppvObject) = 0;
    virtual HRESULT GetPresentationAttribute (DWORD dwStreamIndex, REFGUID guidAttribute, PROPVARIANT* pvarAttribute) = 0;
};
STDAPI MFCreateSourceReaderFromURL (LPCWSTR pwszURL, IMFAttributes* pAttributes, IMFSourceReader** ppSourceReader);
extern const GUID MF_SOURCE_READER_D3D_MANAGER, MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING;
