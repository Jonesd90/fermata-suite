#include "Video.h"
#include "FixTools.h"
#include "SpectralWindow.h"
#include "WindowPlace.h"
#include "../core/VideoSync.h"
#include "../core/AudioFormats.h"

namespace td::video
{
using namespace td::fixtools;           // (the dialogs' helpers: JobDialogBase, launchDialog, say, formats ...)

// ======================================================================================================== the small pictures
Stills::Stills() : juce::Thread ("Video pictures") {}
Stills::~Stills() { stop(); }

void Stills::setFolder (const juce::File& f)
{
    const std::lock_guard<std::mutex> g (lock);
    if (f == folder) return;
    folder = f; entries.clear(); queue.clear(); ++epoch;
    ++generation;
}

std::shared_ptr<const Strip> Stills::get (const juce::File& video, double length)
{
    if (video == juce::File() || length <= 0.0) return nullptr;
    const auto key = video.getFullPathName();
    {
        const std::lock_guard<std::mutex> g (lock);
        auto it = entries.find (key);
        if (it != entries.end()) return it->second.strip;
        auto& en = entries[key]; en.file = video; en.length = length; en.queued = true;
        queue.push_back (key);
    }
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
    notify();
    return nullptr;
}

// (3.68.0, the pass with Fable: it may be inside the computer's decoder, reading a drive that has gone to sleep - better a long wait as fermata
//  closes than a thread killed in there, as for the playback reader. Was 6 s, then killed.)
void Stills::stop() { signalThreadShouldExit(); notify(); stopThreadOrLog (*this, 30000); }

namespace
{
/** The file the pictures of 'video' are kept in: its name, and a fingerprint of its path, size and date (a changed file is read again). */
juce::File stillsFile (const juce::File& folder, const juce::File& video)
{
    const auto key = video.getFullPathName() + "|" + juce::String (video.getSize()) + "|" + juce::String (video.getLastModificationTime().toMilliseconds());
    return folder.getChildFile (sanitiseForFile (video.getFileNameWithoutExtension()).substring (0, 60) + " " + juce::String::toHexString (key.hashCode64()) + ".fvpics");
}
constexpr juce::int32 kStillsMagic = 0x46565031;      // "FVP1"
bool saveStrip (const juce::File& f, const Strip& s)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    {
        juce::FileOutputStream out (tmp.getFile());
        if (out.failedToOpen()) return false;
        out.writeInt (kStillsMagic); out.writeDouble (s.step); out.writeDouble (s.aspect); out.writeInt ((int) s.pics.size());
        juce::JPEGImageFormat jpg; jpg.setQuality (0.7f);
        for (auto& p : s.pics)
        {
            juce::MemoryOutputStream m;
            if (p.isValid()) jpg.writeImageToStream (p, m);
            out.writeInt ((int) m.getDataSize()); out.write (m.getData(), m.getDataSize());
        }
        out.flush();
        if (out.getStatus().failed()) return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}
std::shared_ptr<Strip> loadStrip (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (in.failedToOpen() || in.readInt() != kStillsMagic) return nullptr;
    auto s = std::make_shared<Strip>();
    s->step = in.readDouble(); s->aspect = in.readDouble();
    const int n = in.readInt();
    if (! (s->step > 0.0 && s->step < 3600.0) || ! (s->aspect > 0.1 && s->aspect < 10.0) || n < 0 || n > 100000) return nullptr;
    juce::JPEGImageFormat jpg;
    for (int i = 0; i < n; ++i)
    {
        const int len = in.readInt();
        if (len < 0 || len > 4 * 1024 * 1024) return nullptr;
        if (len == 0) { s->pics.push_back ({}); continue; }
        juce::MemoryBlock mb ((size_t) len);
        if (in.read (mb.getData(), len) != len) return nullptr;
        juce::MemoryInputStream mi (mb, false);
        s->pics.push_back (jpg.decodeImage (mi));
    }
    s->done = true;
    return s;
}
} // namespace

void Stills::run()
{
    td::crashreport::nameThisThreadW (L"Video pictures");
    while (! threadShouldExit())
    {
        juce::String key; juce::File file, dir; double length = 0.0; int ep = 0;
        {
            const std::lock_guard<std::mutex> g (lock);
            if (! queue.empty()) { key = queue.front(); queue.pop_front(); }
            if (key.isNotEmpty()) { auto it = entries.find (key); if (it == entries.end()) key.clear(); else { file = it->second.file; length = it->second.length; it->second.queued = false; } }
            dir = folder; ep = epoch;
        }
        if (key.isEmpty()) { wait (-1); continue; }
        auto publish = [&] (std::shared_ptr<const Strip> s)
        {
            const std::lock_guard<std::mutex> g (lock);
            if (ep != epoch) return false;
            auto it = entries.find (key);
            if (it == entries.end()) return false;
            it->second.strip = std::move (s);
            ++generation;
            return true;
        };
        const auto cache = dir != juce::File() ? stillsFile (dir, file) : juce::File();
        if (cache.existsAsFile())
            if (auto s = loadStrip (cache)) { publish (s); continue; }
        while (holdOff && holdOff() && ! threadShouldExit()) wait (500);       // (never beside a recording: the disk and the processors are the take's)
        if (threadShouldExit()) return;
        juce::String why;
        auto src = video::open (file, why);
        auto strip = std::make_shared<Strip>();
        if (src == nullptr) { strip->done = true; strip->why = why; publish (strip); continue; }
        const auto inf = src->info();
        if (inf.width > 0 && inf.height > 0) strip->aspect = (double) inf.width / (double) inf.height;
        const double len = inf.seconds > 0.0 ? inf.seconds : length;
        strip->step = juce::jmax (2.0, len / 1200.0);                            // (at most 1200 pictures a file: an hour is one every 3 s)
        bool stopped = false;
        for (double t = 0.0; t < len; t += strip->step)
        {
            if (threadShouldExit()) return;
            while (holdOff && holdOff() && ! threadShouldExit()) wait (500);
            { const std::lock_guard<std::mutex> g (lock); if (ep != epoch || entries.find (key) == entries.end()) { stopped = true; break; } }
            strip->pics.push_back (src->still (t, 160));
            if (strip->pics.size() % 6 == 0) publish (std::make_shared<Strip> (*strip));        // (a copy: the pictures themselves are shared)
        }
        if (stopped) continue;
        strip->done = true;
        if (publish (std::make_shared<Strip> (*strip)) && cache != juce::File()) saveStrip (cache, *strip);
    }
}

namespace
{
std::unique_ptr<Stills>& stillsHolder() { static std::unique_ptr<Stills> s; return s; }
}

Stills& stills (AppContext& app)
{
    auto& s = stillsHolder();
    if (s == nullptr)
    {
        s = std::make_unique<Stills>();
        syncSettings (app);
        auto* a = &app;
        s->holdOff = [a] { return a->engine.isRecording() || a->takesBusy.load (std::memory_order_acquire); };
    }
    s->setFolder (app.project.projectFile == juce::File() ? juce::File() : app.project.projectFolder().getChildFile ("Analysis").getChildFile ("Video Pictures"));
    return *s;
}

void shutdown() { if (auto& s = stillsHolder()) { s->stop(); s.reset(); } finishDecoders(); }

void syncSettings (AppContext& app)
{
    if (auto* st = app.props.getUserSettings()) setHardwareDecoding (st->getBoolValue ("videoHardware", true));
}

// ======================================================================================================== the playhead
bool editIsPlaying (AppContext& app, const juce::Uuid& editId)
{
    return app.isPlaying() && app.playInfo.kind == AppContext::PlayInfo::Kind::Edit && app.playInfo.id == editId;
}

// ======================================================================================================== words
juce::String syncedText (const EditVideo& v)
{
    switch (v.synced)
    {
        case EditVideo::byFileTimecode:  return "lined up by the timecode in the video file";
        case EditVideo::bySoundTimecode: return "lined up by the timecode (LTC) on the camera's sound";
        case EditVideo::bySound:         return "lined up by the camera's sound";
        case EditVideo::afterPrevious:   return "placed straight after the clip before it (it could not be lined up)";
        case EditVideo::byHand:          return "placed by hand";
        default:                         return "placed at 0:00 (it could not be lined up)";
    }
}
bool soundOn (const EditDef& e) { for (auto& v : e.videos) if (v.soundOn && v.hasSound()) return true; return false; }
void setSound (AppContext& app, EditDef& e, bool on)
{
    for (auto& v : e.videos) v.soundOn = on && v.hasSound();
    app.project.markDirty();
    if (editIsPlaying (app, e.id)) app.engine.setPlaybackMonitorGain (on && soundOn (e) ? 1.0f : 0.0f);      // (heard at once: the playing session carries the camera's sound, silent until now)
}

// ======================================================================================================== the picture window
namespace
{
/** How long after fermata asks for a frame it is seen on an ordinary computer screen (the drawing, the wait for the screen's next refresh,
    the panel itself): about a frame and a half at 60 Hz. The picture is asked for that much ahead; the user's own figure (the picture's
    menu) is added for a screen that is later still - most televisions and projectors are. */
constexpr double kScreenMs = 25.0;

double pictureLeadMs (AppContext& app)
{
    if (auto* st = app.props.getUserSettings()) return juce::jlimit (-500.0, 500.0, st->getDoubleValue ("videoPictureMs", 0.0));
    return 0.0;
}

class Picture : public juce::Component, private juce::Timer
{
public:
    Picture (AppContext& a, const juce::Uuid& eid) : app (a), editId (eid)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setSize (800, 450);
        if (auto* st = app.props.getUserSettings()) showTime = st->getBoolValue ("videoShowTime", false);
        syncSettings (app);
        leadMs = pictureLeadMs (app);
        startTimerHz (60);
    }
    ~Picture() override
    {
        if (juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent()) juce::Desktop::getInstance().setKioskModeComponent (nullptr);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        const auto r = getLocalBounds();
        const double sinceChange = juce::Time::getMillisecondCounterHiRes() - changedMs;
        // (the frame of the clip before stays for a moment while the next clip's first frame comes: no black flash between a camera's files)
        const bool drawFrame = frame.isValid() && ! clipId.isNull() && (frameClip == clipId || sinceChange < 150.0);
        if (drawFrame)
        {
            auto box = frame.getBounds().toFloat();
            if (shape > 0.0) box.setWidth (box.getHeight() * (float) shape);            // (the picture's own shape: a frame's pixels need not be square)
            const auto dest = juce::RectanglePlacement (juce::RectanglePlacement::centred).appliedTo (box, r.toFloat());
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImage (frame, dest);
        }
        juce::String line;
        if (clipId.isNull()) line = noClipText;
        else if (problem.isNotEmpty()) line = problem;
        else if (frameClip != clipId && sinceChange > 600.0) line = "Reading the video...";
        if (line.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.65f)); g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText (line, r.reduced (24), juce::Justification::centred, 4);
        }
        if ((showTime || isMouseOver (true)) && ! clipId.isNull() && frameClip == clipId && frame.isValid())
        {
            const juce::String t = clipName + "   " + formatTimeShort (frameTime, 2);
            const juce::Font f (juce::FontOptions (12.0f));
            const int w = juce::GlyphArrangement::getStringWidthInt (f, t) + 14;
            g.setColour (juce::Colour (0xb0000000)); g.fillRoundedRectangle (8.0f, 8.0f, (float) w, 20.0f, 4.0f);
            g.setColour (juce::Colours::white); g.setFont (f); g.drawText (t, 15, 8, w - 10, 20, juce::Justification::centredLeft, false);
        }
    }
    void resized() override { sendSizes(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { toggleFullScreen(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        const bool full = isFullScreen();
        juce::PopupMenu m;
        m.addItem (1, full ? "Leave full screen  (Esc, or double-click)" : "Full screen on this screen  (double-click)");
        m.addItem (2, "Show the clip's name and time over the picture", true, showTime);
        m.addSeparator();
        m.addItem (4, "Picture earlier or later: " + juce::String (leadMs > 0.0 ? "+" : "") + juce::String (juce::roundToInt (leadMs)) + " ms  (for a screen that shows it late - type it...)");
        m.addItem (5, "Decode on the graphics card  (untick if the picture looks wrong)", true, hardwareDecoding());
        if (infoLine.isNotEmpty()) { m.addSeparator(); m.addItem (9, infoLine, false); }
        m.addSeparator();
        m.addItem (3, "Close the picture");
        juce::Component::SafePointer<Picture> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->toggleFullScreen();
            else if (r == 2)
            {
                safe->showTime = ! safe->showTime;
                if (auto* st = safe->app.props.getUserSettings()) { st->setValue ("videoShowTime", safe->showTime); safe->app.props.saveIfNeeded(); }
                safe->repaint();
            }
            else if (r == 3) { if (safe->app.closeWindowByKey) safe->app.closeWindowByKey (windowKey (safe->editId)); }
            else if (r == 4) safe->typeLead();
            else if (r == 5)
            {
                const bool on = ! hardwareDecoding();
                setHardwareDecoding (on);
                if (auto* st = safe->app.props.getUserSettings()) { st->setValue ("videoHardware", on); safe->app.props.saveIfNeeded(); }
                juce::Logger::writeToLog (juce::String ("Video: decoding on the graphics card switched ") + (on ? "on" : "off"));
            }
        });
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && isFullScreen()) { toggleFullScreen(); return true; }
        return false;                                                            // (the rest - Space and the transport keys - go on to the window)
    }
private:
    bool isFullScreen() const { return juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent(); }
    /** Full screen on the screen the window is on (Esc or a double-click to leave it). */
    void toggleFullScreen()
    {
        auto& d = juce::Desktop::getInstance();
        auto* top = getTopLevelComponent();
        if (d.getKioskModeComponent() == top) d.setKioskModeComponent (nullptr);
        else d.setKioskModeComponent (top, false);
        grabKeyboardFocus();
    }
    /** "Picture earlier or later": how many milliseconds earlier the picture is drawn, for this computer's screen (a user setting). */
    void typeLead()
    {
        auto* aw = new juce::AlertWindow ("Picture earlier or later",
                                          "fermata draws the picture for the sound you are hearing, and allows for a computer screen's own delay. A television or a projector shows "
                                          "the picture later than that (often 20 to 100 ms): type how many milliseconds EARLIER the picture should be drawn to make up for it. "
                                          "0 for a computer monitor; below 0 draws it later.\n\nTo find it: play a passage with a clear beat or a conductor's downbeat, and change this until picture and sound agree.",
                                          juce::MessageBoxIconType::NoIcon, place::anchor (this));
        aw->addTextEditor ("ms", juce::String (juce::roundToInt (leadMs)), "Milliseconds earlier");
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Picture> safe (this);
        auto* a = &app;
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([a, aw, safe] (int res)
        {
            if (res != 1) return;
            const auto t = aw->getTextEditorContents ("ms").trim().replaceCharacter (',', '.');
            if (t.isEmpty() || ! t.containsOnly ("+-.0123456789")) { a->setNotice ("That is not a number of milliseconds: the picture's timing was left as it was."); return; }
            const double ms = juce::jlimit (-500.0, 500.0, t.getDoubleValue());
            if (auto* st = a->props.getUserSettings()) { st->setValue ("videoPictureMs", ms); a->props.saveIfNeeded(); }
            if (safe != nullptr) safe->leadMs = ms;
            juce::Logger::writeToLog ("Video: the picture is drawn " + juce::String (ms, 0) + " ms earlier (the user's figure for this screen)");
        }), true);
    }
    /** The size the picture is drawn at, in the screen's own pixels: the frames come no bigger than that needs. */
    void sendSizes()
    {
        double scale = 1.0;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) scale = d->scale;
        pixelW = juce::roundToInt (getWidth() * scale); pixelH = juce::roundToInt (getHeight() * scale);
        for (auto& s : sources) s.second->wantSize (pixelW, pixelH);
    }
    std::shared_ptr<Source> sourceFor (const juce::File& f)
    {
        const auto key = f.getFullPathName();
        if (auto it = sources.find (key); it != sources.end()) return it->second;
        auto s = video::openForPicture (f);                                      // (comes back at once: the file is opened on the decoder's own thread)
        s->wantSize (pixelW, pixelH);
        sources[key] = s;
        return s;
    }
    void timerCallback() override
    {
        auto* e = app.project.findEdit (editId);
        if (e == nullptr) return;
        const bool playing = editIsPlaying (app, editId);
        // Which moment's picture. Standing: the playhead's. Playing: the sound being HEARD now is not the sound the playhead counts - that
        // is still on its way through the mixer's plug-ins, the driver and the converters (AudioEngine::heardDelaySeconds) - and a frame
        // asked for now is seen a moment later (kScreenMs, and the user's figure for a slow screen). So: the playhead, less the one, plus the other.
        const double head = playing ? app.playheadSecondsSmooth() : e->playheadSeconds;
        double t = playing ? head - app.engine.heardDelaySeconds (editId) + 0.001 * (kScreenMs + leadMs) : head;
        const auto* c = e->videoAt (t);
        if (c == nullptr && playing) { c = e->videoAt (head); if (c != nullptr) t = juce::jlimit (c->start, c->end() - 0.001, t); }     // (the few milliseconds either side of a clip's ends)
        const auto id = c != nullptr ? c->id : juce::Uuid::null();
        if (id != clipId || firstTick)
        {
            firstTick = false;
            clipId = id; problem.clear(); changedMs = juce::Time::getMillisecondCounterHiRes();
            clipName = c != nullptr ? c->file.getFileName() : juce::String();
            noClipText = e->videos.empty() ? "This edit has no video yet: the Video key in the edit window adds one."
                                           : "No video at this point of the edit.";
            repaint();
        }
        if (++tidyTicks >= 60)                                                   // (once a second: files no longer in the edit are let go of)
        {
            tidyTicks = 0;
            for (auto it = sources.begin(); it != sources.end();)
            {
                bool used = false;
                for (auto& v : e->videos) used = used || v.file.getFullPathName() == it->first;
                it = used ? std::next (it) : sources.erase (it);
            }
            sendSizes();                                                         // (the window may have been moved to a screen with other pixels)
            leadMs = pictureLeadMs (app);
        }
        if (c == nullptr) return;
        auto src = sourceFor (c->file);
        juce::String why;
        const int st = src->state (why);
        const juce::String prob = st < 0 ? "Cannot show " + c->file.getFileName() + ": " + why + "." : juce::String();
        if (prob != problem) { problem = prob; repaint(); }
        double ft = 0.0;
        const auto im = src->frameAt (t - c->start, playing, ft);
        if (im.isValid() && (im != frame || ft != frameTime || frameClip != clipId))
        {
            if (frameClip != clipId || shape <= 0.0)
            {
                const auto inf = src->info();
                shape = inf.width > 0 && inf.height > 0 ? (double) inf.width / (double) inf.height : 0.0;
                infoLine = inf.codec + (inf.width > 0 ? ", " + juce::String (inf.width) + " x " + juce::String (inf.height) : juce::String())
                           + (inf.fps > 0.0 ? ", " + juce::String (inf.fps, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " fps" : juce::String())
                           + (inf.hardware ? "  -  decoded on the graphics card" : "  -  decoded by the processor") + "  (" + systemName() + ")";
            }
            frame = im; frameTime = ft; frameClip = clipId; repaint();
        }
        else if (frameClip != clipId) repaint();                                 // (waiting for the clip's first frame: the last one fades to black, then the words)
        // while playing: the clip that comes next within two seconds is opened and its first frames decoded, so the change is not a gap
        if (playing)
            if (const auto* nx = e->videoAt (t + 2.0); nx != nullptr && nx != c)
            {
                double unused = 0.0;
                sourceFor (nx->file)->frameAt (juce::jmax (0.0, t - nx->start), false, unused);
            }
    }
    AppContext& app;
    juce::Uuid editId;
    juce::Uuid clipId = juce::Uuid::null(), frameClip = juce::Uuid::null();
    juce::String clipName, problem, noClipText, infoLine;
    juce::Image frame; double frameTime = 0.0, shape = 0.0, changedMs = 0.0, leadMs = 0.0;
    bool showTime = false, firstTick = true;
    int tidyTicks = 0, pixelW = 0, pixelH = 0;
    std::map<juce::String, std::shared_ptr<Source>> sources;
};
} // namespace

juce::Component* makePicture (AppContext& app, const juce::Uuid& editId) { return new Picture (app, editId); }
bool pictureOpen (AppContext& app, const juce::Uuid& editId) { return app.windowIsOpen && app.windowIsOpen (windowKey (editId)); }
void togglePicture (AppContext& app, const juce::Uuid& editId)
{
    if (pictureOpen (app, editId)) { if (app.closeWindowByKey) app.closeWindowByKey (windowKey (editId)); return; }
    if (app.showVideo) app.showVideo (editId);
}

// ======================================================================================================== adding clips and lining them up
namespace
{
/** What happened to one file. */
struct Added
{
    juce::File file;
    bool ok = false;
    juce::String error;                       // (not ok)
    Info info;
    juce::File sound; int soundChannels = 0; double soundRate = 0.0; int soundLtc = -1;
    bool placed = false; double start = 0.0; int synced = EditVideo::atZero;
    juce::String note;                        // how it was lined up (or why not), in words
    juce::String tcNote;                      // timecode that was found but could not be used
};

/** Adds the files (or lines one clip up again): reads each file's sound into the project, then lines it up - timecode, sound, 0:00. */
class AddDialog : public JobDialogBase
{
public:
    AddDialog (AppContext& a, const juce::Uuid& eid, juce::Array<juce::File> fs, const juce::Uuid& again) : app (a), editId (eid), redo (again)
    {
        setSize (660, 400);
        title.setColour (juce::Label::textColourId, theme::text); title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setText (redo.isNull() ? "Adding " + juce::String (fs.size()) + (fs.size() == 1 ? " video file" : " video files") + " to the edit" : "Lining the video up again",
                       juce::dontSendNotification);
        addAndMakeVisible (title);
        log.setMultiLine (true); log.setReadOnly (true); log.setCaretVisible (false); log.setScrollbarsShown (true);
        log.setColour (juce::TextEditor::backgroundColourId, theme::field); log.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (log);
        closeButton.onClick = [this] { if (running) job.requestCancel(); else closeDialogOf (this); };
        addAndMakeVisible (closeButton);
        status.setColour (juce::Label::textColourId, theme::text);
        status.setText ("Starting...", juce::dontSendNotification);
        start (std::move (fs));
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        auto bottom = r.removeFromBottom (30);
        closeButton.setBounds (bottom.removeFromRight (110));
        r.removeFromBottom (8);
        bar.setBounds (r.removeFromBottom (22)); r.removeFromBottom (4);
        status.setBounds (r.removeFromBottom (22)); r.removeFromBottom (6);
        log.setBounds (r);
    }
private:
    struct State
    {
        juce::Array<juce::File> files;
        juce::File soundFolder;
        juce::File reuseSound; int reuseChannels = 0; double reuseRate = 0.0;     // (lining up again: the camera's sound already read)
        std::vector<videosync::PieceCode> pieces;
        videosync::EditSound editSound;
        std::vector<Added> results;
        juce::CriticalSection textLock; juce::String text;
    };
    static void say (State& s, const juce::String& t) { const juce::ScopedLock sl (s.textLock); s.text = t; }
    void start (juce::Array<juce::File> fs)
    {
        auto* e = app.project.findEdit (editId);
        if (e == nullptr) return;
        syncSettings (app);
        auto st = std::make_shared<State>();
        st->files = std::move (fs);
        st->soundFolder = app.project.projectFolder().getChildFile ("Video Sound");
        if (! redo.isNull())
            if (auto* c = e->findVideo (redo); c != nullptr && c->hasSound() && c->sound.existsAsFile())
            { st->reuseSound = c->sound; st->reuseChannels = c->soundChannels; st->reuseRate = c->soundRate; }
        st->pieces = videosync::pieceCodesOf (app.project, *e);
        st->editSound = videosync::editSoundOf (app.project, *e);
        state = st;
        startTimerHz (10);
        runJob ([st] (AudioJob& j) -> juce::String { return work (*st, j); },          // (static: the window may be closed while it runs)
                [this, st] (const juce::String& err) { finished (*st, err); },
                [st] (const juce::String&) { for (auto& r : st->results) if (r.sound != juce::File() && r.sound != st->reuseSound) r.sound.deleteFile(); });   // (closed: nothing is added, the sound read is deleted)
    }
    /** The job (its own thread): each file in turn. Static: it touches nothing of the window. */
    static juce::String work (State& s, AudioJob& j)
    {
        auto& fm = formats();
        bool piecesRead = false, editRead = false; videosync::Energy editEnergy;
        const int n = s.files.size();
        for (int i = 0; i < n; ++i)
        {
            if (j.cancelled()) return "Cancelled.";
            const auto f = s.files[i];
            const float p0 = (float) i / (float) n, p1 = (float) (i + 1) / (float) n;
            Added a; a.file = f;
            say (s, f.getFileName() + ": opening it");
            juce::String why;
            auto src = video::open (f, why);
            if (src == nullptr) { a.error = why; s.results.push_back (a); continue; }
            a.info = src->info();
            if (! (a.info.seconds > 0.0)) { a.error = "its length could not be read"; s.results.push_back (a); continue; }
            // the camera's sound, read out of the video into the project (or the one read before, lining up again)
            if (s.reuseSound != juce::File()) { a.sound = s.reuseSound; a.soundChannels = s.reuseChannels; a.soundRate = s.reuseRate; }
            else if (a.info.soundChannels > 0)
            {
                say (s, f.getFileName() + ": reading the camera's sound");
                s.soundFolder.createDirectory();
                const auto wav = audioops::uniqueFile (s.soundFolder.getChildFile (sanitiseForFile (f.getFileNameWithoutExtension()) + " - camera sound.wav"));
                juce::String swhy;
                if (src->readSound (wav, [&] (float p) { j.setProgress (p0 + (p1 - p0) * 0.6f * juce::jlimit (0.0f, 1.0f, p)); return ! j.cancelled(); }, swhy))
                {
                    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
                    if (r != nullptr && r->numChannels > 0 && r->sampleRate > 0.0) { a.sound = wav; a.soundChannels = (int) r->numChannels; a.soundRate = r->sampleRate; }
                    else { wav.deleteFile(); a.note = "its sound could not be read back"; }
                }
                else { wav.deleteFile(); if (j.cancelled()) return "Cancelled."; a.note = "its sound could not be read (" + swhy + ")"; }
            }
            src.reset();
            j.setProgress (p0 + (p1 - p0) * 0.6f);
            // 1. timecode: the file's own track, a Sony XML file, LTC on the camera's sound
            say (s, f.getFileName() + ": looking for timecode");
            auto tc = videosync::containerTimecode (f);
            if (! tc.found) tc = videosync::sidecarTimecode (f);
            // LTC on a channel of the camera's sound: the timecode, when the file has none of its own - and, either way, a channel that is
            // not music: it is left out of the comparing by sound below, and is never heard (EditVideo::soundLtc)
            int ltcChannel = -1; bool codeFromSound = false;
            if (a.sound != juce::File())
            {
                const auto stc = videosync::soundTimecode (fm, a.sound, ltcChannel);
                if (! tc.found && stc.found) { tc = stc; codeFromSound = true; }
            }
            a.soundLtc = ltcChannel;
            if (tc.found)
            {
                if (! piecesRead) { say (s, "Reading the timecode of the edit's pieces"); videosync::readPieceCodes (s.pieces, fm); piecesRead = true; }
                const auto pl = videosync::placeByTimecode (tc, a.info.seconds, s.pieces);
                const auto tcText = timecodeText (tc.seconds, tc.fps > 0.0 ? tc.fps : 25.0, tc.drop);
                if (pl.ok)
                {
                    a.placed = true; a.start = pl.start;
                    a.synced = codeFromSound ? EditVideo::bySoundTimecode : EditVideo::byFileTimecode;
                    a.note = "by its timecode (" + tcText + ", from " + tc.from + "), against " + pl.piece + "'s";
                }
                else
                {
                    bool anyCode = false; for (auto& pc : s.pieces) anyCode = anyCode || pc.found;
                    a.tcNote = "Its timecode, " + tcText + " (from " + tc.from + "), "
                               + (s.pieces.empty() ? juce::String ("could not be used: the project has no LTC track to compare it with.")
                                  : ! anyCode ? juce::String ("could not be used: no timecode could be read from the LTC of the edit's pieces.")
                                  : juce::String ("matches none of the edit's pieces."));
                }
            }
            // 2. the camera's sound against the edit's
            if (! a.placed && a.sound != juce::File() && ! s.editSound.parts.empty())
            {
                if (! editRead)
                {
                    say (s, "Reading the edit's sound (" + s.editSound.tracks.joinIntoString (", ") + ")");
                    if (! videosync::energyOfEdit (fm, s.editSound, editEnergy, [&] (float) { return ! j.cancelled(); }))
                    { if (j.cancelled()) return "Cancelled."; }
                    editRead = true;
                }
                say (s, f.getFileName() + ": comparing the camera's sound with the edit's");
                videosync::Energy cam;
                if (videosync::energyOfFile (fm, a.sound, ltcChannel, cam, [&] (float) { return ! j.cancelled(); }) && ! editEnergy.ms.empty())
                {
                    const auto m = videosync::matchBySound (cam, editEnergy);
                    if (m.ok)
                    {
                        a.placed = true; a.start = m.start; a.synced = EditVideo::bySound;
                        a.note = "by the camera's sound (a clear match over " + juce::String ((int) m.overlap) + " s)";
                    }
                    else a.note = (a.note.isNotEmpty() ? a.note + "; " : juce::String()) + m.why;
                }
                else if (j.cancelled()) return "Cancelled.";
            }
            else if (! a.placed && a.sound == juce::File()) a.note = (a.note.isNotEmpty() ? a.note + "; " : juce::String()) + "it has no sound to compare";
            else if (! a.placed && s.editSound.parts.empty()) a.note = (a.note.isNotEmpty() ? a.note + "; " : juce::String()) + "the edit has no sound of its own to compare";
            a.ok = true;
            s.results.push_back (a);
            j.setProgress (p1);
        }
        return {};
    }
    void timerCallback() override
    {
        JobDialogBase::timerCallback();
        if (auto s = state.lock()) { const juce::ScopedLock sl (s->textLock); if (running && s->text.isNotEmpty()) status.setText (s->text, juce::dontSendNotification); }
    }
    /** Back on the message thread: the clips go into the edit (one undo step), and the window says how each was lined up. */
    void finished (State& s, const juce::String& err)
    {
        closeButton.setButtonText ("Close");
        auto* e = app.project.findEdit (editId);
        if (err.isNotEmpty() || e == nullptr)
        {
            for (auto& r : s.results) if (r.sound != juce::File() && r.sound != s.reuseSound) r.sound.deleteFile();
            status.setColour (juce::Label::textColourId, theme::warn);
            status.setText (e == nullptr ? juce::String ("The edit has gone: nothing was added.") : err + " Nothing was added.", juce::dontSendNotification);
            return;
        }
        juce::String text; int added = 0, failedN = 0;
        const EditVideo* prev = nullptr;                                         // (a file that could not be lined up goes after the one before it in this batch)
        double prevEnd = 0.0; bool prevPlaced = false;
        for (auto& r : s.results)
        {
            if (! r.ok)
            {
                ++failedN; text << r.file.getFileName() << ":  NOT ADDED - " << r.error << ".\n\n";
                if (systemName() == "none in this build" && failedN == 1)                 // 3.73.0: the reader was left out of this build - the report says why
                    text << "(Help > Save the video report makes a file that says why this build has no video reader: please send it.)\n\n";
                continue;
            }
            double startAt = r.start; int synced = r.synced;
            if (! r.placed)
            {
                if (prevPlaced && redo.isNull()) { startAt = prevEnd; synced = EditVideo::afterPrevious; }
                else { startAt = 0.0; synced = EditVideo::atZero; }
            }
            if (! redo.isNull())
            {
                auto* c = e->findVideo (redo);
                if (c == nullptr) { text << "The clip has gone from the edit meanwhile.\n"; continue; }
                if (r.placed) { c->start = startAt; c->synced = synced; c->locked = true; }
                c->length = r.info.seconds; if (r.info.fps > 0.0) c->fps = r.info.fps;
                if (r.info.width > 0) { c->width = r.info.width; c->height = r.info.height; }
                if (r.sound != juce::File()) { c->sound = r.sound; c->soundChannels = r.soundChannels; c->soundRate = r.soundRate; c->soundLtc = r.soundLtc; }
                text << r.file.getFileName() << ":  " << (r.placed ? "lined up " + r.note + ", at " + formatTimeShort (startAt, 3) + " on the edit's timeline, and locked."
                                                                   : "could not be lined up (" + r.note + "). It was left where it was.")
                     << (r.tcNote.isNotEmpty() ? "\n" + r.tcNote : juce::String()) << "\n";
                ++added;
                continue;
            }
            EditVideo v;
            v.file = r.file; v.start = startAt; v.length = r.info.seconds; v.fps = r.info.fps; v.width = r.info.width; v.height = r.info.height;
            v.locked = true; v.synced = synced;
            v.sound = r.sound; v.soundChannels = r.soundChannels; v.soundRate = r.soundRate; v.soundLtc = r.soundLtc;
            e->videos.push_back (v);
            prev = &e->videos.back(); prevEnd = v.end(); prevPlaced = true;
            ++added;
            text << r.file.getFileName() << "  (" << formatTimeShort (r.info.seconds, 0) << (r.info.codec.isNotEmpty() ? ", " + r.info.codec : juce::String())
                 << (r.info.width > 0 ? ", " + juce::String (r.info.width) + " x " + juce::String (r.info.height) : juce::String())
                 << (r.info.fps > 0.0 ? ", " + juce::String (r.info.fps, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " fps" : juce::String()) << "):\n   "
                 << (r.placed ? "Lined up " + r.note : syncedText (v).substring (0, 1).toUpperCase() + syncedText (v).substring (1) + (r.note.isNotEmpty() ? " - " + r.note : juce::String()))
                 << "." << (r.tcNote.isNotEmpty() ? "\n   " + r.tcNote : juce::String()) << "\n   It starts at " << (startAt < 0.0 ? "-" + formatTimeShort (-startAt, 3) + " (before 0:00)" : formatTimeShort (startAt, 3)) << " on the edit's timeline, locked.\n";
            if (synced == EditVideo::bySound)
                text << "   (Lined up by sound, the PICTURE can be a little late if the camera stood far from the stage: sound takes about 3 ms a metre. "
                        "Unlock it and nudge it earlier by a frame if it looks late.)\n";
            text << "\n";
        }
        juce::ignoreUnused (prev);
        if (added > 0)
        {
            e->showVideo = true;
            app.project.changed();
            app.project.structureChanged();
        }
        for (auto& r : s.results) juce::Logger::writeToLog ("Video: " + r.file.getFullPathName() + (r.ok ? " - " + (r.placed ? "lined up " + r.note + " at " + juce::String (r.start, 4) + " s" : juce::String ("not lined up: ") + r.note)
                                                                                                    + (r.tcNote.isNotEmpty() ? " (" + r.tcNote + ")" : juce::String())
                                                                                            : " - not added: " + r.error));
        log.setText (text.trimEnd(), false);
        status.setColour (juce::Label::textColourId, failedN > 0 ? theme::warn : theme::text);
        status.setText (redo.isNull() ? (juce::String (added) + (added == 1 ? " clip added" : " clips added") + (failedN > 0 ? ", " + juce::String (failedN) + " could not be opened" : juce::String())
                                         + (added == 1 ? ". Ctrl + Z takes it away again." : added > 1 ? ". Ctrl + Z takes them away again." : "."))
                                      : juce::String ("Done."), juce::dontSendNotification);
        if (added > 0) app.setNotice (redo.isNull() ? "Video added to the edit: see the video lane under the guide. The Video key opens the picture." : "Video lined up again.");
    }
    AppContext& app;
    juce::Uuid editId, redo;
    std::weak_ptr<State> state;
    juce::Label title;
    juce::TextEditor log;
    juce::TextButton closeButton { "Cancel" };
};

void launchAdd (AppContext& app, const juce::Uuid& editId, juce::Array<juce::File> files, const juce::Uuid& redo, juce::Component* over)
{
    {   // (heavy work asks first while Rec mode is on - as the fix tools do: AppContext::recModeHolds)
        juce::Component::SafePointer<juce::Component> sp (over);
        if (app.recModeHolds ("Adding video", [ap = &app, editId, files, redo, sp] { launchAdd (*ap, editId, files, redo, sp.getComponent()); })) return;
    }
    if (app.project.projectFile == juce::File()) { say ("Video", "Save the project first: the camera's sound is kept in the project's folder."); return; }
    launchDialog (std::make_unique<AddDialog> (app, editId, std::move (files), redo), redo.isNull() ? "Add video" : "Line the video up again", over);
}
} // namespace

void addVideos (AppContext& app, const juce::Uuid& editId, juce::Component* over)
{
    if (app.project.findEdit (editId) == nullptr) return;
    if (app.project.projectFile == juce::File()) { say ("Video", "Save the project first: the camera's sound is kept in the project's folder."); return; }
    auto* st = app.props.getUserSettings();
    juce::File startDir (st != nullptr ? st->getValue ("videoFolder") : juce::String());
    if (! startDir.isDirectory()) startDir = juce::File::getSpecialLocation (juce::File::userMoviesDirectory);
    if (! startDir.isDirectory()) startDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto chooser = std::make_shared<juce::FileChooser> ("Choose the video file(s) for this edit. Several at once: a camera's files that run on from each other.", startDir,
                                                        "*.mp4;*.mov;*.m4v;*.mts;*.m2ts;*.avi;*.wmv;*.mkv;*.mxf");
    auto* a = &app;
    juce::Component::SafePointer<juce::Component> safeOver (over);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [chooser, a, editId, safeOver] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        if (auto* s2 = a->props.getUserSettings()) { s2->setValue ("videoFolder", files[0].getParentDirectory().getFullPathName()); a->props.saveIfNeeded(); }
        files.sort();                                                            // (a camera numbers its split files in order: C0001, C0002 ...)
        launchAdd (*a, editId, files, juce::Uuid::null(), safeOver.getComponent());
    });
}

void lineUpAgain (AppContext& app, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over)
{
    auto* e = app.project.findEdit (editId);
    auto* c = e != nullptr ? e->findVideo (clipId) : nullptr;
    if (c == nullptr) return;
    if (! c->file.existsAsFile()) { say ("Video", "The video file is not there: " + c->file.getFullPathName() + "\n\nRight-click the clip: Find the video file."); return; }
    juce::Array<juce::File> one; one.add (c->file);
    launchAdd (app, editId, one, clipId, over);
}

void findFile (AppContext& app, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over)
{
    auto* e = app.project.findEdit (editId);
    auto* c = e != nullptr ? e->findVideo (clipId) : nullptr;
    if (c == nullptr) return;
    const auto old = c->file;
    auto chooser = std::make_shared<juce::FileChooser> ("Where is " + old.getFileName() + " now?", old.getParentDirectory().isDirectory() ? old.getParentDirectory() : juce::File(),
                                                        "*" + old.getFileExtension());
    auto* a = &app;
    juce::ignoreUnused (over);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [chooser, a, editId, clipId] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        auto* e2 = a->project.findEdit (editId);
        auto* c2 = e2 != nullptr ? e2->findVideo (clipId) : nullptr;
        if (c2 == nullptr) return;
        c2->file = f;
        a->project.changed();
        a->setNotice ("The clip now plays " + f.getFullPathName());
    });
}

void typeStart (AppContext& app, const juce::Uuid& editId, const juce::Uuid& clipId, juce::Component* over)
{
    auto* e = app.project.findEdit (editId);
    auto* c = e != nullptr ? e->findVideo (clipId) : nullptr;
    if (c == nullptr) return;
    auto* aw = new juce::AlertWindow ("Where the video starts",
                                      "Where its first frame is on the edit's timeline, in seconds (below 0: it began before 0:00). Now: " + juce::String (c->start, 3) + " s.",
                                      juce::MessageBoxIconType::NoIcon, place::anchor (over));
    aw->addTextEditor ("s", juce::String (c->start, 3), "Seconds");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* a = &app;
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([a, aw, editId, clipId] (int res)
    {
        if (res != 1) return;
        const auto t = aw->getTextEditorContents ("s").trim().replaceCharacter (',', '.');
        auto* e2 = a->project.findEdit (editId);
        auto* c2 = e2 != nullptr ? e2->findVideo (clipId) : nullptr;
        if (c2 == nullptr) return;
        if (t.isEmpty() || ! t.containsOnly ("+-.0123456789")) { a->setNotice ("That is not a number of seconds: the clip was left where it was."); return; }
        c2->start = juce::jlimit (-c2->length + 0.5, 86400.0 * 7.0, t.getDoubleValue());
        c2->synced = EditVideo::byHand;
        a->project.changed();
    }), true);
}
} // namespace td::video
