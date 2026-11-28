#include "VideoReader.h"

// 3.68.0: Windows' own video system - Media Foundation, part of Windows 10 and 11 - behind VideoReader.h. fermata ships no codecs: what this
// computer's Windows can decode, fermata can show (MP4 / MOV with H.264 everywhere; H.265 with Microsoft's HEVC Video Extensions; WMV, AVI,
// MPEG-4 Part 2, MPEG-2). Written with Claude Fable (26 November 2026).
//
// THIS FILE COULD NOT BE COMPILED WHERE IT WAS WRITTEN (no Windows headers there; tools/check-windows-code.sh compiles it against stand-ins
// that only prove its own grammar). So CMakeLists.txt tries it on its own first - with the graphics card's decoder (FERMATA_VIDEO_D3D=1), then
// without (=0) - and builds VideoReaderNone.cpp if neither builds: this file can never stop fermata from building. The build's log says which.
//
// How it reads a file: Media Foundation's Source Reader, asked plainly for one frame after another (no callbacks, no threads of its own
// here). It is asked for frames as RGB32 at the size wanted, so Windows does the decoding, the colours and the resizing - on the graphics
// card when there is one (way 0), else in the processor (way 1), else with the old converter that cannot resize (way 2). The first frame
// is decoded as the file is opened: a way that cannot deliver a frame is given up for the next one there and then.
//
// Nothing of Media Foundation is linked into fermata.exe by name: mfplat.dll and mfreadwrite.dll are loaded when the first video is opened.
// On an "N" edition of Windows without Microsoft's Media Feature Pack they are not there, and fermata must still start: it then says what
// to install, in the picture window, instead of failing to start at all.

#ifndef FERMATA_VIDEO_D3D
 #define FERMATA_VIDEO_D3D 1
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#if FERMATA_VIDEO_D3D
 #include <d3d11.h>
 #include <d3d10.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#pragma comment (lib, "mfuuid.lib")         // (the names of Media Foundation's formats and settings: data, no DLL)
#pragma comment (lib, "ole32.lib")

namespace td::video::raw
{
namespace
{
/** A COM pointer that lets go of what it holds. */
template <typename T> class Com
{
public:
    Com() = default;
    Com (const Com& o) : p (o.p) { if (p != nullptr) p->AddRef(); }
    Com& operator= (const Com& o) { if (o.p != nullptr) o.p->AddRef(); reset(); p = o.p; return *this; }
    ~Com() { reset(); }
    void reset() { if (p != nullptr) { p->Release(); p = nullptr; } }
    T* get() const { return p; }
    T* operator->() const { return p; }
    T** put() { reset(); return &p; }
    explicit operator bool() const { return p != nullptr; }
private:
    T* p = nullptr;
};

std::string hexOf (HRESULT hr)
{
    char b[32];
    std::snprintf (b, sizeof (b), "0x%08lX", (unsigned long) hr);
    return b;
}

std::wstring widen (const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, utf8.c_str(), (int) utf8.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w ((size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, utf8.c_str(), (int) utf8.size(), &w[0], n);
    return w;
}

/** Media Foundation's own calls, found in its DLLs when first needed (see the top of this file), and Media Foundation started. */
struct Mf
{
    bool ok = false;
    std::string why;                                                    // (not ok: in plain words)
    decltype (&::MFStartup) startup = nullptr;
    decltype (&::MFCreateAttributes) createAttributes = nullptr;
    decltype (&::MFCreateMediaType) createMediaType = nullptr;
    decltype (&::MFGetStrideForBitmapInfoHeader) strideFor = nullptr;
    decltype (&::MFCreateSourceReaderFromURL) createReader = nullptr;
#if FERMATA_VIDEO_D3D
    decltype (&::MFCreateDXGIDeviceManager) createCardManager = nullptr;
    decltype (&::D3D11CreateDevice) createCard = nullptr;
#endif
};

const Mf& mf()
{
    static Mf m;
    static std::once_flag once;
    std::call_once (once, []
    {
        const HMODULE plat = LoadLibraryExW (L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const HMODULE rw = LoadLibraryExW (L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (plat == nullptr || rw == nullptr)
        {
            m.why = "Windows' own video system (Media Foundation) is not on this computer. On an \"N\" edition of Windows, install Microsoft's Media Feature Pack (Settings, Apps, Optional features)";
            return;
        }
        m.startup          = reinterpret_cast<decltype (m.startup)> (GetProcAddress (plat, "MFStartup"));
        m.createAttributes = reinterpret_cast<decltype (m.createAttributes)> (GetProcAddress (plat, "MFCreateAttributes"));
        m.createMediaType  = reinterpret_cast<decltype (m.createMediaType)> (GetProcAddress (plat, "MFCreateMediaType"));
        m.strideFor        = reinterpret_cast<decltype (m.strideFor)> (GetProcAddress (plat, "MFGetStrideForBitmapInfoHeader"));
        m.createReader     = reinterpret_cast<decltype (m.createReader)> (GetProcAddress (rw, "MFCreateSourceReaderFromURL"));
        if (m.startup == nullptr || m.createAttributes == nullptr || m.createMediaType == nullptr || m.strideFor == nullptr || m.createReader == nullptr)
        {
            m.why = "Windows' video system on this computer is older than fermata needs";
            return;
        }
#if FERMATA_VIDEO_D3D
        m.createCardManager = reinterpret_cast<decltype (m.createCardManager)> (GetProcAddress (plat, "MFCreateDXGIDeviceManager"));
        if (const HMODULE d3d = LoadLibraryExW (L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            m.createCard = reinterpret_cast<decltype (m.createCard)> (GetProcAddress (d3d, "D3D11CreateDevice"));
#endif
        const HRESULT hr = m.startup (MF_VERSION, MFSTARTUP_FULL);
        if (FAILED (hr)) { m.why = "Windows' video system could not be started (" + hexOf (hr) + ")"; return; }
        m.ok = true;                                                    // (never shut down again: it lasts as long as fermata runs)
    });
    return m;
}

#if FERMATA_VIDEO_D3D
/** The graphics card, for decoding: one device for every reader, made when first asked for and kept. Null when there is none to be had. */
IMFDXGIDeviceManager* cardManager()
{
    struct Card { Com<ID3D11Device> device; Com<IMFDXGIDeviceManager> manager; };
    static Card* card = nullptr;
    static std::once_flag once;
    std::call_once (once, []
    {
        const auto& m = mf();
        if (! m.ok || m.createCard == nullptr || m.createCardManager == nullptr) return;
        static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        const UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        auto* c = new Card();
        HRESULT hr = m.createCard (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4, D3D11_SDK_VERSION, c->device.put(), nullptr, nullptr);
        if (hr == E_INVALIDARG)                                         // (a Windows that does not know level 11.1)
            hr = m.createCard (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 3, D3D11_SDK_VERSION, c->device.put(), nullptr, nullptr);
        if (FAILED (hr) || ! c->device) { delete c; return; }
        {   // several readers, each on its own thread, use this one device
            Com<ID3D10Multithread> mt;
            if (SUCCEEDED (c->device->QueryInterface (__uuidof (ID3D10Multithread), reinterpret_cast<void**> (mt.put()))) && mt) mt->SetMultithreadProtected (TRUE);
        }
        UINT token = 0;
        if (FAILED (m.createCardManager (&token, c->manager.put())) || ! c->manager || FAILED (c->manager->ResetDevice (c->device.get(), token))) { delete c; return; }
        card = c;
    });
    return card != nullptr ? card->manager.get() : nullptr;
}
#endif

/** Why Windows could not open the file at all. */
std::string openWhy (HRESULT hr)
{
    if (hr == HRESULT_FROM_WIN32 (ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32 (ERROR_PATH_NOT_FOUND)) return "the file is not there";
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32 (ERROR_SHARING_VIOLATION)) return "Windows was not allowed to read the file (is another program writing it?)";
    if (hr == MF_E_UNSUPPORTED_BYTESTREAM_TYPE) return "Windows cannot read this kind of file on this computer";
    return "Windows could not open it (" + hexOf (hr) + ")";
}

/** The picture's kind, in words, from the four letters Windows knows it by. */
std::string codecName (const GUID& sub)
{
    const DWORD f = sub.Data1;
    if (f == 0xe06d8026) return "MPEG-2";                               // (the one common kind whose name is not four letters)
    std::string s;
    for (int i = 0; i < 4; ++i)
    {
        const char c = (char) ((f >> (8 * i)) & 0xff);
        if (c < 32 || c > 126) return "a kind without a name";
        s += (char) ((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    if (s == "H264" || s == "AVC1" || s == "X264") return "H.264";
    if (s == "HEVC" || s == "HVC1" || s == "HEV1" || s == "H265") return "H.265 (HEVC)";
    if (s == "WMV1" || s == "WMV2" || s == "WMV3" || s == "WVC1") return "Windows Media Video";
    if (s == "MP4V" || s == "M4S2" || s == "MP4S" || s == "XVID" || s == "DIVX" || s == "DX50") return "MPEG-4";
    if (s == "MJPG") return "Motion JPEG";
    if (s == "MPG1") return "MPEG-1";
    if (s == "AV01") return "AV1";
    if (s == "VP90") return "VP9";
    if (s == "VP80") return "VP8";
    if (s.compare (0, 2, "DV") == 0) return "DV";
    if (s.compare (0, 2, "AP") == 0) return "Apple ProRes";
    return s;
}

double secondsOf (LONGLONG hundredNanos) { return (double) hundredNanos / 10000000.0; }

/** The file's length (0: Windows does not know it). */
double lengthOf (IMFSourceReader& reader)
{
    PROPVARIANT var;
    PropVariantInit (&var);
    double seconds = 0.0;
    if (SUCCEEDED (reader.GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
        seconds = (double) var.uhVal.QuadPart / 10000000.0;
    PropVariantClear (&var);
    return seconds;
}

// ============================================================================================================== the picture
class WinPicture final : public PictureReader
{
public:
    /** Opens the file one way (see the top of this file). False with 'why'; 'hopeless' when no other way could do better (the file itself). */
    bool open (const std::wstring& path, const Options& o, int way, std::string& why, bool& hopeless)
    {
        hopeless = false;
        const auto& m = mf();
        Com<IMFAttributes> attr;
        HRESULT hr = m.createAttributes (attr.put(), 4);
        if (FAILED (hr) || ! attr) { why = "Windows' video system gave nothing to set it up with (" + hexOf (hr) + ")"; hopeless = true; return false; }
        bool onCard = false;
        if (way == 0)
        {
#if FERMATA_VIDEO_D3D
            IMFDXGIDeviceManager* const man = cardManager();
            if (man == nullptr) { why = "the graphics card cannot be used for video"; return false; }
            attr->SetUnknown (MF_SOURCE_READER_D3D_MANAGER, man);
            attr->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            onCard = true;
#else
            why = "this build does not use the graphics card for video";
            return false;
#endif
        }
        if (way <= 1) attr->SetUINT32 (MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        else          attr->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        hr = m.createReader (path.c_str(), attr.get(), reader.put());
        if (FAILED (hr) || ! reader) { why = openWhy (hr); hopeless = true; return false; }

        const DWORD video = (DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED (reader->SetStreamSelection (video, TRUE))) { why = "it has no picture"; hopeless = true; return false; }

        // ---- what the file holds
        Com<IMFMediaType> native;
        hr = reader->GetNativeMediaType (video, 0, native.put());
        if (FAILED (hr) || ! native) { why = "it has no picture Windows can describe (" + hexOf (hr) + ")"; hopeless = true; return false; }
        GUID sub = GUID_NULL;
        native->GetGUID (MF_MT_SUBTYPE, &sub);
        fmt.codec = codecName (sub);
        UINT32 w = 0, h = 0, parN = 1, parD = 1, fpsN = 0, fpsD = 0, turned = 0;
        MFGetAttributeSize (native.get(), MF_MT_FRAME_SIZE, &w, &h);
        if (FAILED (MFGetAttributeRatio (native.get(), MF_MT_PIXEL_ASPECT_RATIO, &parN, &parD)) || parN == 0 || parD == 0) { parN = 1; parD = 1; }
        if (SUCCEEDED (MFGetAttributeRatio (native.get(), MF_MT_FRAME_RATE, &fpsN, &fpsD)) && fpsN > 0 && fpsD > 0) fmt.fps = (double) fpsN / (double) fpsD;
        native->GetUINT32 (MF_MT_VIDEO_ROTATION, &turned);
        {   // (the part of the coded frame that is picture: 1080 lines of the 1088 an H.264 file may be coded as)
            MFVideoArea area;
            std::memset (&area, 0, sizeof (area));
            if (SUCCEEDED (native->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*> (&area), (UINT32) sizeof (area), nullptr))
                && area.Area.cx > 0 && area.Area.cy > 0 && (UINT32) area.Area.cx <= w && (UINT32) area.Area.cy <= h)
            { w = (UINT32) area.Area.cx; h = (UINT32) area.Area.cy; }
        }
        if (w < 2 || h < 2 || w > 32768 || h > 32768) { why = "its picture has no size Windows can tell"; hopeless = true; return false; }
        fmt.width = (int) std::max<long long> (2, std::llround ((double) w * (double) parN / (double) parD));
        fmt.height = (int) h;
        fmt.seconds = lengthOf (*reader.get());
        {   // its first sound track (not read here: only described)
            Com<IMFMediaType> sound;
            if (SUCCEEDED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, sound.put())) && sound)
            {
                UINT32 ch = 0, rate = 0;
                sound->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch); sound->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                if (ch > 0 && rate > 0) { fmt.soundChannels = (int) ch; fmt.soundRate = (double) rate; }
            }
        }

        // ---- the frames wanted: RGB32, no bigger than asked, in the picture's own shape
        double scale = 1.0;
        if (o.maxWidth > 0 && fmt.width > o.maxWidth) scale = std::min (scale, (double) o.maxWidth / (double) fmt.width);
        if (o.maxHeight > 0 && fmt.height > o.maxHeight) scale = std::min (scale, (double) o.maxHeight / (double) fmt.height);
        const UINT32 wantW = (UINT32) std::max<long long> (16, std::llround ((double) fmt.width * scale / 2.0) * 2);
        const UINT32 wantH = (UINT32) std::max<long long> (16, std::llround ((double) fmt.height * scale / 2.0) * 2);
        bool set = false;
        if (way <= 1) set = setOutput (true, wantW, wantH);              // (Windows' video processor resizes; the old converter cannot)
        if (! set) set = setOutput (false, 0, 0);
        if (! set || ! readOutput())
        {
            why = "this computer's Windows has no decoder for its picture (" + fmt.codec + ")";
            if (fmt.codec == "H.265 (HEVC)") why += " - Microsoft's \"HEVC Video Extensions\" from the Microsoft Store adds one";
            return false;
        }
        fmt.hardware = onCard;
        fmt.how = std::string ("Windows Media Foundation, ")
                  + (way == 0 ? "decoded on the graphics card" : way == 1 ? "decoded by the processor" : "decoded by the processor (the old converter: frames at the file's own size)")
                  + (turned != 0 ? ", the file says its picture is turned by " + std::to_string (turned) + " degrees (fermata shows it as it is stored)" : std::string());

        // ---- the first frame, now: a way that cannot deliver it is no way
        bool ended = false;
        if (! read (first, ended, why))
        {
            if (ended) { why = "it has no picture frames"; hopeless = true; }
            return false;
        }
        firstWaiting = true;
        return true;
    }

    Format format() const override { return fmt; }

    bool next (Frame& f, bool& ended, std::string& why) override
    {
        if (firstWaiting) { firstWaiting = false; f = first; return true; }
        return read (f, ended, why);
    }

    bool copyFrame (std::uint8_t* rows, int stride, int width, int height) override
    {
        if (! held || rows == nullptr) return false;
        Com<IMFMediaBuffer> buf;
        DWORD count = 0;
        if (SUCCEEDED (held->GetBufferCount (&count)) && count == 1) held->GetBufferByIndex (0, buf.put());
        if (! buf && FAILED (held->ConvertToContiguousBuffer (buf.put()))) return false;
        if (! buf) return false;
        const int w = std::min (width, outW), h = std::min (height, outH);
        if (w <= 0 || h <= 0) return false;
        // the frame's own rows (the top one first, whichever way up it lies in memory): through the buffer's two-dimensional side when it has
        // one - a frame on the graphics card is brought into memory by this - else through the plain one, with the stride the format gave
        BYTE* top = nullptr; LONG pitch = 0;
        Com<IMF2DBuffer> two;
        bool lockedTwo = false, lockedPlain = false;
        if (SUCCEEDED (buf->QueryInterface (__uuidof (IMF2DBuffer), reinterpret_cast<void**> (two.put()))) && two && SUCCEEDED (two->Lock2D (&top, &pitch)))
            lockedTwo = true;
        else
        {
            BYTE* data = nullptr; DWORD length = 0;
            if (FAILED (buf->Lock (&data, nullptr, &length)) || data == nullptr) return false;
            lockedPlain = true;
            const LONG rowBytes = defaultStride < 0 ? -defaultStride : defaultStride;
            if (rowBytes < bufW * 4 || (unsigned long long) length < (unsigned long long) rowBytes * (unsigned long long) bufH) { buf->Unlock(); return false; }
            pitch = defaultStride;
            top = defaultStride < 0 ? data + (size_t) rowBytes * (size_t) (bufH - 1) : data;
        }
        bool ok = top != nullptr && pitch != 0;
        if (ok)
            for (int y = 0; y < h; ++y)
            {
                const BYTE* src = top + (ptrdiff_t) pitch * (ptrdiff_t) (cropY + y) + (ptrdiff_t) cropX * 4;
                std::uint8_t* dst = rows + (ptrdiff_t) stride * (ptrdiff_t) y;
                std::memcpy (dst, src, (size_t) w * 4);
                for (int x = 0; x < w; ++x) dst[x * 4 + 3] = 255;                  // (RGB32's fourth byte means nothing: opaque)
            }
        if (lockedTwo) two->Unlock2D();
        if (lockedPlain) buf->Unlock();
        return ok;
    }

    bool seek (double t) override
    {
        held.reset(); firstWaiting = false;
        PROPVARIANT var;
        PropVariantInit (&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = (LONGLONG) std::llround (std::max (0.0, t) * 10000000.0);
        const HRESULT hr = reader->SetCurrentPosition (GUID_NULL, var);    // (lands on the key frame at or before it)
        PropVariantClear (&var);
        return SUCCEEDED (hr);
    }

private:
    bool setOutput (bool sized, UINT32 w, UINT32 h)
    {
        Com<IMFMediaType> want;
        if (FAILED (mf().createMediaType (want.put())) || ! want) return false;
        want->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        want->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (sized)
        {
            MFSetAttributeSize (want.get(), MF_MT_FRAME_SIZE, w, h);
            MFSetAttributeRatio (want.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            want->SetUINT32 (MF_MT_INTERLACE_MODE, (UINT32) MFVideoInterlace_Progressive);     // (an interlaced file is made whole frames)
        }
        return SUCCEEDED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, want.get()));
    }
    /** What the frames are really like (Windows may give another size than asked): the buffer, its stride, the part of it that is picture. */
    bool readOutput()
    {
        Com<IMFMediaType> cur;
        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, cur.put())) || ! cur) return false;
        UINT32 w = 0, h = 0;
        if (FAILED (MFGetAttributeSize (cur.get(), MF_MT_FRAME_SIZE, &w, &h)) || w < 2 || h < 2 || w > 32768 || h > 32768) return false;
        bufW = (int) w; bufH = (int) h;
        UINT32 st = 0; LONG calc = 0;
        if (SUCCEEDED (cur->GetUINT32 (MF_MT_DEFAULT_STRIDE, &st))) defaultStride = (LONG) (INT32) st;
        else if (SUCCEEDED (mf().strideFor (MFVideoFormat_RGB32.Data1, w, &calc)) && calc != 0) defaultStride = calc;
        else defaultStride = (LONG) w * 4;
        cropX = 0; cropY = 0; outW = bufW; outH = bufH;
        MFVideoArea area;
        std::memset (&area, 0, sizeof (area));
        if (SUCCEEDED (cur->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*> (&area), (UINT32) sizeof (area), nullptr))
            && area.Area.cx >= 2 && area.Area.cy >= 2 && area.OffsetX.value >= 0 && area.OffsetY.value >= 0
            && area.OffsetX.value + area.Area.cx <= bufW && area.OffsetY.value + area.Area.cy <= bufH)
        { cropX = area.OffsetX.value; cropY = area.OffsetY.value; outW = (int) area.Area.cx; outH = (int) area.Area.cy; }
        return true;
    }
    bool read (Frame& f, bool& ended, std::string& why)
    {
        for (int tries = 0; tries < 2000; ++tries)
        {
            DWORD stream = 0, flags = 0; LONGLONG when = 0;
            Com<IMFSample> sample;
            const HRESULT hr = reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &when, sample.put());
            if (FAILED (hr)) { why = "Windows stopped decoding it (" + hexOf (hr) + ")"; return false; }
            if ((flags & (DWORD) MF_SOURCE_READERF_ERROR) != 0) { why = "Windows reports an error in the file"; return false; }
            if ((flags & (DWORD) MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0 && ! readOutput()) { why = "the picture changed to a form fermata cannot show"; return false; }
            if ((flags & (DWORD) MF_SOURCE_READERF_ENDOFSTREAM) != 0) { ended = true; return false; }
            if (! sample) continue;                                                // (a gap in the file: no frame for this moment)
            LONGLONG length = 0;
            if (FAILED (sample->GetSampleDuration (&length)) || length <= 0) length = fmt.fps > 0.0 ? (LONGLONG) std::llround (10000000.0 / fmt.fps) : 400000;
            held = sample;
            f.time = secondsOf (when); f.length = secondsOf (length); f.width = outW; f.height = outH;
            return true;
        }
        why = "Windows gave no frames";
        return false;
    }

    Com<IMFSourceReader> reader;
    Com<IMFSample> held;                        // the frame next() last gave
    Format fmt;
    Frame first; bool firstWaiting = false;     // the frame decoded as the file was opened: the first one next() gives
    int bufW = 0, bufH = 0;                     // the frames' buffer
    LONG defaultStride = 0;                     // bytes from one row to the next in it (below 0: the bottom row first)
    int cropX = 0, cropY = 0, outW = 0, outH = 0;      // the part of the buffer that is picture
};

// ============================================================================================================== the sound
class WinSound final : public SoundReader
{
public:
    bool open (const std::wstring& path, std::string& why)
    {
        const auto& m = mf();
        HRESULT hr = m.createReader (path.c_str(), nullptr, reader.put());
        if (FAILED (hr) || ! reader) { why = openWhy (hr); return false; }
        const DWORD audio = (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED (reader->SetStreamSelection (audio, TRUE))) { why = "it has no sound"; return false; }
        if (! setOutput (MFAudioFormat_Float) && ! setOutput (MFAudioFormat_PCM)) { why = "this computer's Windows has no decoder for its sound"; return false; }
        if (! readType (why)) return false;
        fmt.seconds = lengthOf (*reader.get());
        // the first block, now: some sound only shows its true form (its rate, its channels) once the decoder has begun on it, and whoever
        // reads this is told the form before the first block
        bool ended = false;
        firstWaiting = read (firstTime, first, ended, why, true);
        if (! firstWaiting && ! ended) return false;
        endedAtOnce = ! firstWaiting;
        return true;
    }

    SoundFormat format() const override { return fmt; }

    bool next (double& time, std::vector<float>& out, bool& ended, std::string& why) override
    {
        if (firstWaiting) { firstWaiting = false; time = firstTime; out.swap (first); first.clear(); return true; }
        if (endedAtOnce) { ended = true; return false; }
        return read (time, out, ended, why, false);
    }

private:
    bool setOutput (const GUID& kind)
    {
        Com<IMFMediaType> want;
        if (FAILED (mf().createMediaType (want.put())) || ! want) return false;
        want->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID (MF_MT_SUBTYPE, kind);
        return SUCCEEDED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.get()));
    }
    /** The form the blocks come in: floats or whole numbers, how many bits, the channels and the rate. */
    bool readType (std::string& why)
    {
        Com<IMFMediaType> cur;
        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, cur.put())) || ! cur) { why = "Windows cannot describe its sound"; return false; }
        UINT32 ch = 0, rate = 0, bitsNow = 0;
        GUID sub = GUID_NULL;
        cur->GetGUID (MF_MT_SUBTYPE, &sub);
        cur->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch); cur->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate); cur->GetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsNow);
        floats = IsEqualGUID (sub, MFAudioFormat_Float) != 0;
        bits = (int) bitsNow;
        const bool known = floats ? (bits == 32 || bits == 64) : (bits == 8 || bits == 16 || bits == 24 || bits == 32);
        if (ch == 0 || rate == 0 || ! known) { why = "its sound is in a form fermata cannot read (" + std::to_string (ch) + " channels, " + std::to_string (bits) + " bits)"; return false; }
        fmt.channels = (int) ch; fmt.rate = (double) rate;
        return true;
    }
    bool read (double& time, std::vector<float>& out, bool& ended, std::string& why, bool isFirst)
    {
        for (int tries = 0; tries < 2000; ++tries)
        {
            DWORD stream = 0, flags = 0; LONGLONG when = 0;
            Com<IMFSample> sample;
            const HRESULT hr = reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream, &flags, &when, sample.put());
            if (FAILED (hr)) { why = "Windows stopped decoding its sound (" + hexOf (hr) + ")"; return false; }
            if ((flags & (DWORD) MF_SOURCE_READERF_ERROR) != 0) { why = "Windows reports an error in the file's sound"; return false; }
            if ((flags & (DWORD) MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
            {
                if (! isFirst) { why = "its sound changes form part of the way through"; return false; }
                if (! readType (why)) return false;
            }
            if ((flags & (DWORD) MF_SOURCE_READERF_ENDOFSTREAM) != 0) { ended = true; return false; }
            if (! sample) continue;
            Com<IMFMediaBuffer> buf;
            if (FAILED (sample->ConvertToContiguousBuffer (buf.put())) || ! buf) { why = "Windows gave its sound in a form that cannot be read out"; return false; }
            BYTE* data = nullptr; DWORD length = 0;
            if (FAILED (buf->Lock (&data, nullptr, &length)) || data == nullptr) { why = "Windows gave its sound in a form that cannot be read out"; return false; }
            const size_t each = (size_t) (bits / 8);
            const size_t n = each > 0 ? (size_t) length / each : 0;              // samples, all channels together
            out.resize (n);
            if (floats && bits == 32) { if (n > 0) std::memcpy (out.data(), data, n * sizeof (float)); }
            else if (floats)     { for (size_t i = 0; i < n; ++i) { double d; std::memcpy (&d, data + i * 8, 8); out[i] = (float) d; } }
            else if (bits == 16) { for (size_t i = 0; i < n; ++i) { std::int16_t v; std::memcpy (&v, data + i * 2, 2); out[i] = (float) v / 32768.0f; } }
            else if (bits == 24) { for (size_t i = 0; i < n; ++i) { const BYTE* p = data + i * 3; const std::int32_t v = (std::int32_t) (((std::uint32_t) p[0] << 8) | ((std::uint32_t) p[1] << 16) | ((std::uint32_t) p[2] << 24)); out[i] = (float) ((double) v / 2147483648.0); } }
            else if (bits == 32) { for (size_t i = 0; i < n; ++i) { std::int32_t v; std::memcpy (&v, data + i * 4, 4); out[i] = (float) ((double) v / 2147483648.0); } }
            else                 { for (size_t i = 0; i < n; ++i) out[i] = ((float) data[i] - 128.0f) / 128.0f; }
            buf->Unlock();
            out.resize (n - n % (size_t) fmt.channels);                           // (whole frames only)
            if (out.empty()) continue;
            time = secondsOf (when);
            return true;
        }
        why = "Windows gave no sound";
        return false;
    }
    Com<IMFSourceReader> reader;
    SoundFormat fmt;
    bool floats = true; int bits = 32;
    std::vector<float> first; double firstTime = 0.0;      // the block decoded as the file was opened: the first one next() gives
    bool firstWaiting = false, endedAtOnce = false;
};
} // namespace

std::unique_ptr<PictureReader> openPicture (const std::string& path, const Options& o, std::string& why)
{
    const auto& m = mf();
    if (! m.ok) { why = m.why; return nullptr; }
    const std::wstring wide = widen (path);
    if (wide.empty()) { why = "the file has no name"; return nullptr; }
    std::string firstWhy;
    for (int way = o.hardware ? 0 : 1; way <= 2; ++way)
    {
        auto r = std::make_unique<WinPicture>();
        std::string w; bool hopeless = false;
        if (r->open (wide, o, way, w, hopeless)) return r;
        if (hopeless) { why = w; return nullptr; }
        if (way >= 1 && firstWhy.empty()) firstWhy = w;                           // (what the processor's own way said: the plainest reason)
    }
    why = firstWhy.empty() ? std::string ("Windows could not decode it") : firstWhy;
    return nullptr;
}

std::unique_ptr<SoundReader> openSound (const std::string& path, std::string& why)
{
    const auto& m = mf();
    if (! m.ok) { why = m.why; return nullptr; }
    const std::wstring wide = widen (path);
    if (wide.empty()) { why = "the file has no name"; return nullptr; }
    auto r = std::make_unique<WinSound>();
    if (! r->open (wide, why)) return nullptr;
    return r;
}

const char* systemName() { return "Windows Media Foundation"; }

ThreadUse::ThreadUse() { mine = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED)); }      // (a thread that already has COM its own way keeps it)
ThreadUse::~ThreadUse() { if (mine) CoUninitialize(); }
} // namespace td::video::raw
