// 3.68.0: with src/ui/VideoReaderWin.cpp this makes the small program CMakeLists.txt builds to find out whether the video reader's code compiles
// and links on this machine (see src/ui/VideoReader.h). It is never run.
#include "../src/ui/VideoReader.h"

int main()
{
    using namespace td::video::raw;
    const ThreadUse use;
    std::string why;
    Options o;
    auto picture = openPicture (std::string(), o, why);
    auto sound = openSound (std::string(), why);
    Frame f; bool ended = false; std::vector<float> samples; double t = 0.0; std::uint8_t px[4] = {};
    if (picture != nullptr) { picture->seek (0.0); picture->next (f, ended, why); picture->copyFrame (px, 4, 1, 1); }
    if (sound != nullptr) sound->next (t, samples, ended, why);
    return systemName() != nullptr && picture == nullptr && sound == nullptr ? 0 : 1;
}
