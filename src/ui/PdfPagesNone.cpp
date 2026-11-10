// 3.51.0: the body of PdfPages.h for a build that has no PDF reader in it (see PdfPages.h): on Windows, when PdfPagesWin.cpp did not build on the
// machine that built fermata; on every other system, always.
#include "PdfPages.h"

namespace td
{
namespace pdfpages
{
struct Doc {};
bool available() { return false; }
Doc* open (const std::wstring&, std::string& error) { error = "This build of fermata has no PDF reader in it."; return nullptr; }
int  pageCount (Doc*) { return 0; }
bool pageSize (Doc*, int, double&, double&) { return false; }
bool render (Doc*, int, int, int, std::vector<unsigned char>&, std::string& error) { error = "This build of fermata has no PDF reader in it."; return false; }
void close (Doc*) {}
} // namespace pdfpages
} // namespace td
