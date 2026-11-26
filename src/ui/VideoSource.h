#pragma once
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <memory>

namespace td::video
{
/** 3.68.0: a video file, opened with the computer's OWN video system - fermata ships no codecs ("as light weight as possible ... not having to
    install extra codecs but just rely on what a PC may already have installed as default ... make a Mac version ... at a later date", 25
    November 2026). Everything of fermata's video (the edit's video lane, the picture window, the small pictures along the lane, lining a
    clip up) uses only this. It is in two layers:

      - VideoSource.cpp (this header's other half; every system): the thread that decodes ahead of the playhead, the frames it keeps ready,
        the frame the picture window is given without ever waiting, the small pictures, the sound written out as a WAV.
      - VideoReader.h (one small file per system): the computer's own decoder, handing out frames and sound one after another. Windows:
        Media Foundation (Windows 10 / 11's own: MP4 / MOV with H.264, also H.265 when Microsoft's HEVC extension is installed, WMV, AVI,
        MPEG-4 Part 2, MPEG-2), VideoReaderWin.cpp. A Mac later: AVFoundation, a VideoReaderMac.mm. Else VideoReaderNone.cpp.
    (Both written with Claude Fable, 26 November 2026: the part asked for on Fable - the decoding, its threads, keeping the picture in step.)

    THE CONTRACT (what fermata relies on):
      - open() may take a moment (the file is opened and its first frame decoded): never on the message thread. For the small pictures and
        the sound, each on a thread of its own. openForPicture() is the picture window's: it comes back at once and the file is opened on
        the Source's own thread (state() says how that went).
      - One Source belongs to one user: its calls come from one thread at a time.
      - frameAt() is the picture window's, on the message thread, up to 60 times a second: it NEVER waits for the disk or the decoder. It gives
        the newest frame it has whose time is at or before 't' (and 'frameTime', that frame's time), or the last one it gave while the right
        one is not ready yet, or an empty Image before it has any. 'playing' says the playhead is moving forwards at normal speed (frames are
        decoded ahead of it); false: it was moved, or stands still (the frame at 't' is what is wanted - after a jump, a scrub, a click - and
        the frames just before it are kept, so a step back is at once). Times are seconds from the video's first frame.
      - wantSize() says how big the picture is drawn (pixels): the frames come no bigger than that needs (and never above 1080 lines).
      - still() is the lane's: a picture of the key frame at or just before 't', at most 'maxWidth' pixels wide, keeping its shape. It may
        wait. Never on the message thread.
      - readSound() writes the video's sound into a WAV (its own rate and channels; 24 bit), so that the WAV's sample 0 is the video's first
        frame (silence put in where the sound starts later than the picture; sound before the first frame left out). It may take a while
        (progress 0..1; false from it stops). Never on the message thread. False, with 'why', when there is no sound or it cannot be read. */
struct Info
{
    double seconds = 0.0;                // length
    double fps = 0.0;                    // frames a second (0: not known)
    int width = 0, height = 0;           // the picture's size, as it is shown (square pixels)
    juce::String codec;                  // "H.264", "H.265 (HEVC)" ... (for the clip's menu and the log)
    int soundChannels = 0;               // 0: no sound
    double soundRate = 0.0;
    bool hardware = false;               // decoded on the graphics card (as last opened)
};

class Source
{
public:
    virtual ~Source() = default;
    /** What is known of the file (all 0 while openForPicture's file is still being opened). */
    virtual Info info() const = 0;
    virtual juce::Image frameAt (double t, bool playing, double& frameTime) = 0;
    virtual void wantSize (int width, int height) = 0;
    /** 1: open. 0: being opened (openForPicture). -1: it cannot be shown, and 'why' says so in plain words. */
    virtual int state (juce::String& why) const = 0;
    virtual juce::Image still (double t, int maxWidth) = 0;
    virtual bool readSound (const juce::File& wav, const std::function<bool (float)>& progress, juce::String& why) = 0;
};

/** Opens a video file (not on the message thread). Null, with 'why' in plain words, when this computer's video system cannot (no decoder for
    it, not a video, missing). */
std::shared_ptr<Source> open (const juce::File& file, juce::String& why);
/** The picture window's: comes back at once, never null; the file is opened on the Source's own thread (Source::state). */
std::shared_ptr<Source> openForPicture (const juce::File& file);
/** What decodes the video on this computer, for the log ("Windows Media Foundation", "none in this build"). */
juce::String systemName();
/** Whether the graphics card may be used to decode (the user's choice, in the picture's menu: on unless a card's driver shows the picture
    wrongly). Any thread; open Sources follow a change. */
void setHardwareDecoding (bool allowed);
bool hardwareDecoding();
/** Closing fermata, once every Source has gone: waits for the decoders' threads that are still finishing their last frame. */
void finishDecoders();
} // namespace td::video
