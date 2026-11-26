#pragma once
#include "AppContext.h"
#include "VideoSource.h"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>

namespace td::video
{
/** 3.68.0: video in the edit window (asked for on 24 and 25 November 2026) - fermata's side of it. The model is EditVideo (src/core/Edit.h),
    lining up is src/core/VideoSync.h, decoding is the computer's own (VideoSource.h). Here: the small pictures along the lane (Stills), the
    picture window (makePicture, key "video:<edit id>"), adding clips and lining them up (addVideos, lineUpAgain), and the clip's other menu
    jobs. The lane itself is drawn and handled by the edit window (EditTimeline, EditWindow.cpp), like the guide's. fermata only. */

inline juce::String windowKey (const juce::Uuid& editId) { return "video:" + editId.toString(); }
constexpr int kLaneH = 72;          // the lane: 2 + the pictures (kPicH) + 2 + the camera's sound (kWaveH) + 2
constexpr int kPicH = 48, kWaveH = 16;

/** The small pictures along one video file: one every 'step' seconds from 0 (an empty Image where one could not be read). */
struct Strip
{
    double step = 2.0, aspect = 16.0 / 9.0;
    std::vector<juce::Image> pics;
    bool done = false;
    juce::String why;               // done and empty: why there are none
};

/** The small pictures: made on a thread of their own (low priority, one file at a time, never while a take records or is being finished), kept in
    the project's Analysis folder ("Video Pictures") so each file is read once. Message thread except run(). */
class Stills : private juce::Thread
{
public:
    Stills();
    ~Stills() override;
    std::function<bool()> holdOff;
    std::atomic<int> generation { 0 };              // goes up when pictures come: the edit windows draw their lanes again
    void setFolder (const juce::File& analysisPictures);
    /** The pictures of this file so far (more come while it is read), or nullptr before any. 'length' in seconds (from the clip). */
    std::shared_ptr<const Strip> get (const juce::File& video, double length);
    void stop();
private:
    struct Entry { juce::File file; double length = 0.0; std::shared_ptr<const Strip> strip; bool queued = false; };
    void run() override;
    std::mutex lock;
    juce::File folder;
    std::map<juce::String, Entry> entries;
    std::deque<juce::String> queue;
    int epoch = 0;
};
/** The one set of small pictures (made when first needed, for the project open now). */
Stills& stills (AppContext&);
/** Closing fermata: the pictures' thread stops and the decoders' threads finish (AppContext's destructor, after the windows have gone). */
void shutdown();
/** The user's video settings that are not of one window (decoding on the graphics card) are read from the settings file. */
void syncSettings (AppContext&);

/** The picture window's contents: the frame of the edit's video at the edit's playhead. */
juce::Component* makePicture (AppContext&, const juce::Uuid& editId);
/** Opens the picture window, or closes it when it is open. */
void togglePicture (AppContext&, const juce::Uuid& editId);
bool pictureOpen (AppContext&, const juce::Uuid& editId);
bool editIsPlaying (AppContext&, const juce::Uuid& editId);

/** Asks for video files and adds them to the edit's video lane, each lined up (timecode, then sound, then 0:00) and locked. */
void addVideos (AppContext&, const juce::Uuid& editId, juce::Component* over);
/** Lines a clip up again, the same way (the camera's sound already read is used again). */
void lineUpAgain (AppContext&, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over);
/** The clip's file has gone (moved, renamed, a drive away): choose it again. */
void findFile (AppContext&, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over);
/** Type where the clip starts on the edit's timeline (an unlocked clip). */
void typeStart (AppContext&, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over);

/** How the clip was placed, in words ("by the timecode in the file (10:02:13:05)" ...). */
juce::String syncedText (const EditVideo&);
/** The camera's sound is heard (any clip of the edit with it switched on). */
bool soundOn (const EditDef&);
/** Switches the camera's sound on / off for every clip of the edit (a listening choice: not undone). */
void setSound (AppContext&, EditDef&, bool on);

/** A text button that also has a right-click menu (the edit window's Video key). */
class MenuButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { menuPress = true; if (onMenu) onMenu(); return; } menuPress = false; juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (menuPress) { menuPress = false; return; } juce::TextButton::mouseUp (e); }
private:
    bool menuPress = false;
};
} // namespace td::video
