#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace td::video::raw
{
/** 3.68.0: the ONE place where fermata meets a computer's own video system. Plain C++, nothing of JUCE: a reader hands out a file's frames one
    after another, and its sound block after block; it has no threads and keeps nothing. Everything else of the video - the thread that
    decodes ahead of the playhead, the frames kept ready, the picture never waiting, the small pictures, the sound written out as a WAV - is
    VideoSource.cpp, which is the same on every system and is tested where it was written. So a new system needs only this:

      - Windows: VideoReaderWin.cpp (Media Foundation's Source Reader). It could not be compiled where it was written (no Windows headers
        there), so CMakeLists.txt tries it first and, if it does not build, builds VideoReaderNone.cpp instead and says so in the build's log.
      - Mac (later): a VideoReaderMac.mm with AVAssetReader. Nothing else changes.
      - Anything else: VideoReaderNone.cpp - no file can be opened.

    Threads: a reader is used by one thread at a time (it may be made on one thread and used on another). Each thread holds a ThreadUse
    while it makes, uses or frees one. Times are seconds on the file's own clock (its first frame is usually, but not always, at 0: the caller
    takes the first frame's time as the start). */
struct Options
{
    int maxWidth = 0, maxHeight = 0;     // frames no bigger than this are wanted (0: the picture's own size); the reader gets as near as it can
    bool hardware = true;                // it may decode on the graphics card
};

struct Format
{
    double seconds = 0.0;                // length
    double fps = 0.0;                    // frames a second (0: not known)
    int width = 0, height = 0;           // the picture as it is shown (square pixels)
    std::string codec;                   // "H.264" ...
    int soundChannels = 0;               // the file's first sound track (0: none)
    double soundRate = 0.0;
    bool hardware = false;               // decoding on the graphics card
    std::string how;                     // for the log: how it is being read
};

/** A frame that has been decoded and is held by the reader until the next one. */
struct Frame
{
    double time = 0.0, length = 0.0;     // when it is shown and for how long (seconds, the file's clock)
    int width = 0, height = 0;           // its pixels, as copyFrame() gives them (the picture's shape is Format's width : height)
};

class PictureReader
{
public:
    virtual ~PictureReader() = default;
    virtual Format format() const = 0;
    /** Decodes the next frame and holds it. False at the end of the file ('ended') or when it cannot go on ('why'). */
    virtual bool next (Frame&, bool& ended, std::string& why) = 0;
    /** Copies the held frame: rows from the top, 4 bytes a pixel in the order blue, green, red, 255; 'stride' bytes from one row to the next;
        'width' x 'height' pixels of it (the Frame's own size, or less). False when it could not be read out. */
    virtual bool copyFrame (std::uint8_t* rows, int stride, int width, int height) = 0;
    /** The next frame next() gives is the key frame at or before 't' - or a later one up to the frame at 't' itself; never one after it
        (the caller steps on to the frame it wants). */
    virtual bool seek (double t) = 0;
};

struct SoundFormat { int channels = 0; double rate = 0.0; double seconds = 0.0; };

class SoundReader
{
public:
    virtual ~SoundReader() = default;
    virtual SoundFormat format() const = 0;
    /** The next block of the sound: when its first sample belongs (seconds, the same clock as the frames'), and its samples (floats, the
        channels interleaved). False at the end ('ended') or when it cannot go on ('why'). */
    virtual bool next (double& time, std::vector<float>& interleaved, bool& ended, std::string& why) = 0;
};

/** Opens the picture of a video file (its path as UTF-8). Null, with 'why' in plain words, when this computer's video system cannot. */
std::unique_ptr<PictureReader> openPicture (const std::string& path, const Options&, std::string& why);
/** Opens the file's first sound track. Null with 'why' ("it has no sound" ...). */
std::unique_ptr<SoundReader> openSound (const std::string& path, std::string& why);
/** What reads the video on this computer ("Windows Media Foundation", "none in this build"). */
const char* systemName();

/** Held by a thread while it makes, uses or frees a reader (Windows: COM for that thread). */
class ThreadUse
{
public:
    ThreadUse();
    ~ThreadUse();
    ThreadUse (const ThreadUse&) = delete;
    ThreadUse& operator= (const ThreadUse&) = delete;
private:
    bool mine = false;
};
} // namespace td::video::raw
