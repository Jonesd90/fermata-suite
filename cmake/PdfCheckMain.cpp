// 3.51.0: with src/ui/PdfPagesWin.cpp this makes the small program CMakeLists.txt builds to find out whether the PDF reader's code compiles and
// links on this machine (see PdfPages.h). It is never run.
#include "../src/ui/PdfPages.h"

int main()
{
    std::string error;
    std::vector<unsigned char> png;
    td::pdfpages::Doc* d = td::pdfpages::open (L"", error);
    double w = 0.0, h = 0.0;
    td::pdfpages::pageSize (d, 0, w, h);
    td::pdfpages::render (d, 0, 1, 1, png, error);
    const int n = td::pdfpages::pageCount (d);
    td::pdfpages::close (d);
    return td::pdfpages::available() && n >= 0 ? 0 : 1;
}
