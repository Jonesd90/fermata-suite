// 3.51.0: the body of PdfPages.h on Windows - the pages are drawn by Windows' own PDF reader (Windows.Data.Pdf), reached through C++/WinRT.
//
// This file is built on its own, as C++20, with none of fermata's or JUCE's headers (C++/WinRT's headers and theirs are best kept apart), and
// only when it compiles and links on the machine that builds fermata: CMakeLists.txt tries it first, and uses PdfPagesNone.cpp when it does not.
// IT COULD NOT BE COMPILED WHERE IT WAS WRITTEN (Linux, no Windows SDK): the first Windows build is its first compile.
//
// Every call is made from the score window's worker thread. The calls that end in .get() wait there for Windows to finish; C++/WinRT refuses
// to wait like that on a thread that owns windows, which is one more reason these are never called from the message thread.
#include "PdfPages.h"

#pragma comment(lib, "windowsapp")

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include <cstdint>
#include <memory>

namespace td
{
namespace pdfpages
{
namespace
{
/** The worker thread joins the multi-threaded apartment once (a thread that some other code already set up is left as it is). */
void joinApartment()
{
    thread_local bool done = false;
    if (done) return;
    done = true;
    try { winrt::init_apartment (winrt::apartment_type::multi_threaded); } catch (...) {}
}

std::string why (const winrt::hresult_error& e, const char* fallback)
{
    std::string t;
    try { t = winrt::to_string (e.message()); } catch (...) {}
    while (! t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ')) t.pop_back();
    return t.empty() ? std::string (fallback) : t;
}
} // namespace

struct Doc
{
    winrt::Windows::Data::Pdf::PdfDocument pdf { nullptr };
};

bool available() { return true; }

Doc* open (const std::wstring& path, std::string& error)
{
    try
    {
        joinApartment();
        const auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync (winrt::hstring (path)).get();
        auto d = std::make_unique<Doc>();
        d->pdf = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync (file).get();
        if (d->pdf == nullptr) { error = "The PDF could not be opened."; return nullptr; }
        return d.release();
    }
    catch (const winrt::hresult_error& e) { error = why (e, "The PDF could not be opened."); }
    catch (...) { error = "The PDF could not be opened."; }
    return nullptr;
}

int pageCount (Doc* d)
{
    if (d == nullptr) return 0;
    try { return (int) d->pdf.PageCount(); } catch (...) {}
    return 0;
}

bool pageSize (Doc* d, int page, double& width, double& height)
{
    if (d == nullptr || page < 0) return false;
    try
    {
        const auto p = d->pdf.GetPage ((std::uint32_t) page);
        const auto s = p.Size();
        width = (double) s.Width; height = (double) s.Height;
        return width > 0.0 && height > 0.0;
    }
    catch (...) {}
    return false;
}

bool render (Doc* d, int page, int widthPx, int heightPx, std::vector<unsigned char>& png, std::string& error)
{
    png.clear();
    if (d == nullptr || page < 0 || widthPx < 1 || heightPx < 1) { error = "There is no such page."; return false; }
    try
    {
        const auto p = d->pdf.GetPage ((std::uint32_t) page);
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;                // (the picture's format is PNG and its background white unless told otherwise)
        options.DestinationWidth ((std::uint32_t) widthPx);
        options.DestinationHeight ((std::uint32_t) heightPx);
        p.RenderToStreamAsync (stream, options).get();
        const auto size = stream.Size();
        if (size == 0 || size > 0x7fffffffull) { error = "The page could not be drawn."; return false; }
        winrt::Windows::Storage::Streams::DataReader reader (stream.GetInputStreamAt (0));
        reader.LoadAsync ((std::uint32_t) size).get();
        png.resize ((size_t) size);
        reader.ReadBytes (png);
        return true;
    }
    catch (const winrt::hresult_error& e) { error = why (e, "The page could not be drawn."); }
    catch (...) { error = "The page could not be drawn."; }
    png.clear();
    return false;
}

void close (Doc* d) { try { delete d; } catch (...) {} }
} // namespace pdfpages
} // namespace td
