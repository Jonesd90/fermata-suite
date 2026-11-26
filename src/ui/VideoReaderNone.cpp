#include "VideoReader.h"

// 3.68.0: no video system (the Linux build the checks use; and Windows, if VideoReaderWin.cpp did not build - CMakeLists.txt says so in the
// build's log): no file can be opened. See VideoReader.h.
namespace td::video::raw
{
std::unique_ptr<PictureReader> openPicture (const std::string&, const Options&, std::string& why)
{
    why = "this build of fermata cannot read video on this computer's system";
    return nullptr;
}
std::unique_ptr<SoundReader> openSound (const std::string&, std::string& why)
{
    why = "this build of fermata cannot read video on this computer's system";
    return nullptr;
}
const char* systemName() { return "none in this build"; }
ThreadUse::ThreadUse() {}
ThreadUse::~ThreadUse() { (void) mine; }
} // namespace td::video::raw
