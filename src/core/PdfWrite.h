#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cstring>

namespace td
{
/** 3.55.0 (asked for on 11 November 2026: "I DO want to be able to print the score to a new PDF"): a PDF written from nothing - each
    page one grey picture that fills it, with shapes drawn sharp on top (the takes' brackets and numbers on a score's pages).

    It is the plainest PDF there is (version 1.4: a catalog, the page list, and per page a picture, a drawing and the page itself; the
    picture and the drawing packed with Deflate), written straight to a stream page by page, so a long score is never all in memory.
    Nothing of an existing PDF is read or kept: the pages are pictures of it, which is why any PDF can be marked - a scan as well as a
    program's export - and why the music in the result is a picture (it prints the same; it cannot be searched as text).

    A header only; fermata's score window is the one user. */
namespace pdfwrite
{
/** The drawing of one page, in the page's own measure: x to the right and y DOWN from its top left corner, in points (1/72 inch) -
    as a window is drawn, not as a PDF counts. */
class Drawing
{
public:
    explicit Drawing (double pageHeightPoints) : pageH (pageHeightPoints) {}
    /** A filled shape: begin with its colour (red, green, blue 0..1; 'opacity' below 1 lets the page show through - a pale patch behind
        a number), give its outline (moveTo, lineTo, curveTo, close - several outlines make holes, as in an 8), then fill(). */
    void begin (float red, float green, float blue, float opacity = 1.0f)
    {
        const int alpha = juce::jlimit (1, 100, juce::roundToInt (opacity * 100.0f));
        if (std::find (alphas.begin(), alphas.end(), alpha) == alphas.end()) alphas.push_back (alpha);
        ops << "/A" << alpha << " gs " << num (red) << ' ' << num (green) << ' ' << num (blue) << " rg\n";
    }
    void moveTo (double x, double y) { ops << xy (x, y) << " m\n"; }
    void lineTo (double x, double y) { ops << xy (x, y) << " l\n"; }
    void curveTo (double x1, double y1, double x2, double y2, double x3, double y3) { ops << xy (x1, y1) << ' ' << xy (x2, y2) << ' ' << xy (x3, y3) << " c\n"; }
    void close() { ops << "h\n"; }
    void fill (bool evenOdd = false) { ops << (evenOdd ? "f*\n" : "f\n"); }
    void fillRect (double x, double y, double w, double h, float red, float green, float blue, float opacity = 1.0f)
    {
        begin (red, green, blue, opacity);
        moveTo (x, y); lineTo (x + w, y); lineTo (x + w, y + h); lineTo (x, y + h); close(); fill();
    }
    bool isEmpty() const noexcept { return ops.isEmpty(); }

private:
    friend class Writer;
    static juce::String num (double v) { juce::String s (v, 3); if (s.containsChar ('.')) { while (s.endsWithChar ('0')) s = s.dropLastCharacters (1); if (s.endsWithChar ('.')) s = s.dropLastCharacters (1); } return s == "-0" ? juce::String ("0") : s; }
    juce::String xy (double x, double y) const { return num (x) + ' ' + num (pageH - y); }
    double pageH;
    juce::String ops;
    std::vector<int> alphas;                                  // the opacities used, in hundredths (each needs a named state in the page)
};

class Writer
{
public:
    explicit Writer (juce::OutputStream& stream) : out (stream)
    {
        start = out.getPosition();
        put ("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");                 // (the second line: four bytes above 127, which tell a reader the file is not plain text)
        offsets.resize (3, 0);                                // objects 1 (the catalog) and 2 (the page list) are written last; 0 is never one
    }
    /** One page of w x h points: a grey picture (0 black ... 255 white, picW x picH dots, 'stride' bytes a row) filling it, and a drawing
        on top (may be null or empty). False when the stream would not take it. */
    bool addPage (double w, double h, const unsigned char* grey, int picW, int picH, int stride, const Drawing* drawing)
    {
        if (grey == nullptr || picW <= 0 || picH <= 0 || w <= 0.0 || h <= 0.0) return false;
        juce::MemoryOutputStream packed;
        {
            juce::GZIPCompressorOutputStream z (packed, 6);
            for (int y = 0; y < picH; ++y) z.write (grey + (size_t) y * (size_t) stride, (size_t) picW);
        }
        const int image = beginObject();
        put ("<< /Type /XObject /Subtype /Image /Width " + juce::String (picW) + " /Height " + juce::String (picH)
             + " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length " + juce::String ((juce::int64) packed.getDataSize()) + " >>\nstream\n");
        out.write (packed.getData(), packed.getDataSize());
        put ("\nendstream\nendobj\n");

        juce::String content = "q " + Drawing::num (w) + " 0 0 " + Drawing::num (h) + " 0 0 cm /Im0 Do Q\n";       // the picture, stretched over the page
        if (drawing != nullptr) content << drawing->ops;
        juce::MemoryOutputStream packedContent;
        { juce::GZIPCompressorOutputStream z (packedContent, 6); z.write (content.toRawUTF8(), content.getNumBytesAsUTF8()); }
        const int contents = beginObject();
        put ("<< /Filter /FlateDecode /Length " + juce::String ((juce::int64) packedContent.getDataSize()) + " >>\nstream\n");
        out.write (packedContent.getData(), packedContent.getDataSize());
        put ("\nendstream\nendobj\n");

        juce::String states;
        if (drawing != nullptr) for (int a : drawing->alphas) states << "/A" << a << " << /ca " << Drawing::num (a / 100.0) << " >> ";
        const int page = beginObject();
        put ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + Drawing::num (w) + ' ' + Drawing::num (h) + "] /Contents " + juce::String (contents) + " 0 R"
             " /Resources << /XObject << /Im0 " + juce::String (image) + " 0 R >>" + (states.isNotEmpty() ? " /ExtGState << " + states + ">>" : juce::String()) + " >> >>\nendobj\n");
        pages.push_back (page);
        return ! failed;
    }
    /** The page list, the catalog and the table of where everything is. Nothing may be added after it. True when all of it was written. */
    bool finish (const juce::String& title = {})
    {
        if (finished) return ! failed;
        finished = true;
        offsets[2] = out.getPosition() - start;
        juce::String kids; for (int p : pages) kids << p << " 0 R ";
        put ("2 0 obj\n<< /Type /Pages /Count " + juce::String ((int) pages.size()) + " /Kids [ " + kids + "] >>\nendobj\n");
        offsets[1] = out.getPosition() - start;
        put ("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
        int info = 0;
        if (title.isNotEmpty())
        {
            info = beginObject();
            juce::String safe; for (auto c : title) safe << ((c >= 32 && c < 127 && c != '(' && c != ')' && c != '\\') ? juce::String::charToString (c) : juce::String ("_"));
            put ("<< /Title (" + safe + ") /Producer (fermata) >>\nendobj\n");
        }
        const auto table = out.getPosition() - start;
        put ("xref\n0 " + juce::String ((int) offsets.size()) + "\n0000000000 65535 f \n");
        for (size_t i = 1; i < offsets.size(); ++i) put (juce::String (offsets[i]).paddedLeft ('0', 10) + " 00000 n \n");
        put ("trailer\n<< /Size " + juce::String ((int) offsets.size()) + " /Root 1 0 R" + (info > 0 ? " /Info " + juce::String (info) + " 0 R" : juce::String()) + " >>\nstartxref\n" + juce::String (table) + "\n%%EOF\n");
        out.flush();
        return ! failed;
    }
    int pageCount() const noexcept { return (int) pages.size(); }

private:
    void put (const juce::String& text) { if (! out.write (text.toRawUTF8(), text.getNumBytesAsUTF8())) failed = true; }
    void put (const char* raw) { if (! out.write (raw, std::strlen (raw))) failed = true; }
    int beginObject()
    {
        offsets.push_back (out.getPosition() - start);
        const int n = (int) offsets.size() - 1;
        put (juce::String (n) + " 0 obj\n");
        return n;
    }
    juce::OutputStream& out;
    juce::int64 start = 0;
    std::vector<juce::int64> offsets;                         // by object number: where it starts in the file
    std::vector<int> pages;
    bool failed = false, finished = false;
};
} // namespace pdfwrite
} // namespace td
