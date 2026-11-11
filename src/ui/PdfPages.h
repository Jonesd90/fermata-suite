#pragma once
#include <string>
#include <vector>

namespace td
{
/** 3.51.0: the pages of a PDF as pictures, for the score window (ScoreWindow.h).

    JUCE cannot draw a PDF, so the pages are drawn by Windows' own PDF reader (Windows.Data.Pdf, part of Windows 10 and 11: nothing is installed
    and nothing is added to the program). That is reached through C++/WinRT, whose headers neither JUCE nor the rest of fermata is built with -
    so this header is plain C++ (no JUCE, no Windows types) and has two bodies, of which the build picks one (CMakeLists.txt):
      - PdfPagesWin.cpp: the real one. Built as C++20, on its own, and only when it compiles and links on the machine that builds fermata;
      - PdfPagesNone.cpp: nothing behind it (available() is false, open() fails with a text that says so). The score window then says that this
        build cannot draw the pages and offers to open the PDF in the computer's own PDF program. This is what a build falls back to, so that
        the PDF reader can never stop fermata from building.

    Every function is called from ONE thread - the score window's own worker, never the message thread: opening a file and drawing a page both
    wait for Windows to finish. */
namespace pdfpages
{
struct Doc;                                                                // an open PDF

bool available();                                                          // false: this build has nothing that can draw a PDF's pages
/** 3.53.0: true once Windows' PDF reader has been used in this run (any thread). The program then ends itself the moment it has finished closing
    (Main.cpp): left to unload in the ordinary way, the reader failed in its own tidying-up and Windows reported that as a crash. */
bool everUsed();
/** Opens the file. Returns nullptr and an error text when it cannot be read (or is locked with a password). */
Doc* open (const std::wstring& path, std::string& error);
int  pageCount (Doc*);
/** A page's size (0 = the first), in Windows' own units of 1/96 inch: what matters is its shape, and the sizes of the pages against each other. */
bool pageSize (Doc*, int page, double& width, double& height);
/** Draws a page as a picture of exactly this many pixels, on white, and gives the picture as the bytes of a PNG file. */
bool render (Doc*, int page, int widthPx, int heightPx, std::vector<unsigned char>& png, std::string& error);
void close (Doc*);
} // namespace pdfpages
} // namespace td
