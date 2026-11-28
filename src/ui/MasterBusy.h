#pragma once

namespace td::masterbusy
{
/** 3.74.0: ONE thing at a time renders the Mastering window's previews (MasterRender.h: each piece's render is written to the same temporary
    file, whoever makes it). The Mastering window holds one of these while it renders or exports, the Spectrogram Editing window while it renders
    the piece it measures; each looks at holds() - less its own - before it starts, and waits if another is at work. Message thread only. */
inline int& holds() { static int n = 0; return n; }
struct Hold
{
    Hold() { ++holds(); }
    ~Hold() { --holds(); }
    Hold (const Hold&) = delete; Hold& operator= (const Hold&) = delete;
};
} // namespace td::masterbusy
