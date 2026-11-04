// fermata:Live 0.3.0: the Primary's take window - fermata's take window, reduced: the piece, Next Take, REC, the take's length, the H:M:S.MS
// display, the transport, ARM per track with Arm all / Arm none, markers (M), flags (Enter pins one and opens its note; Enter again saves it),
// the Fixer's replies, and the takes filling live on the timeline. Kept out: bars, matching, import, pitch, ReTouch, export, search, take sheet.
#include "LiveMainWindow.h"
#include "LiveTakeView.h"
#include "../ui/TimeReadout.h"
#include <set>

namespace td::live
{
static juce::String hms (double seconds, bool ms = true)
{
    if (seconds < 0.0) seconds = 0.0;
    const auto total = (juce::int64) std::floor (seconds);
    const int h = (int) (total / 3600), m = (int) ((total / 60) % 60), s = (int) (total % 60);
    juce::String t = juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s).paddedLeft ('0', 2);
    if (ms) t << "." << juce::String ((int) std::floor ((seconds - (double) total) * 1000.0)).paddedLeft ('0', 3);
    return t;
}

/** The big black display: a caption over the time. */
class TimeBox final : public juce::Component
{
public:
    explicit TimeBox (juce::String cap, float size = 34.0f) : caption (std::move (cap)), fontSize (size) {}
    void set (const juce::String& t, juce::Colour c = juce::Colours::white) { if (t != text || c != colour) { text = t; colour = c; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0e13)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::border); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (caption, getLocalBounds().reduced (8, 3).removeFromTop (12), juce::Justification::centredLeft, false);
        g.setColour (colour); g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), fontSize, juce::Font::bold)));
        g.drawFittedText (text, getLocalBounds().reduced (8, 2).withTrimmedTop (12), juce::Justification::centred, 1, 0.6f);
    }
private:
    juce::String caption, text; juce::Colour colour { juce::Colours::white }; float fontSize;
};

/** The flags, newest first, with their notes and the Fixer's replies (the Primary reads the replies here): the session's, and those the project
    already has from earlier sessions (its take window's flag markers). */
class FlagList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FlagList (PrimaryApp& a) : app (a)
    {
        list.setModel (this); list.setRowHeight (44); list.setColour (juce::ListBox::backgroundColourId, theme::field);
        addAndMakeVisible (list);
        caption.setText ("FLAGS AND REPLIES", juce::dontSendNotification); caption.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        caption.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (caption);
    }
    std::function<void (const juce::String& id, double windowSeconds)> onPick;      // (seconds < 0: not on this window's timeline)
    void refresh()
    {
        std::vector<Shown> now;
        const auto L = app.layout();
        std::set<juce::String> have;
        for (auto& f : app.model.flags)
        {
            have.insert (f.id);
            if (! f.removed) now.push_back ({ f, L != nullptr ? L->windowSecondsOf (f.sample) : -1.0 });
        }
        if (auto* w = app.pieceWindow())
            for (auto& m : w->markers)
                if (m.flag && have.count (m.id.toString()) == 0)
                {
                    Shown x; x.f.id = m.id.toString(); x.f.note = m.note; x.f.pinnedAt = m.pinnedAt; x.ws = m.seconds;
                    for (auto& r : m.replies) x.f.replies.push_back ({ r.text, r.by, r.at });
                    now.push_back (x);
                }
        std::sort (now.begin(), now.end(), [] (const Shown& x, const Shown& y) { return x.f.pinnedAt > y.f.pinnedAt; });
        bool same = now.size() == shown.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].f.id == shown[i].f.id && now[i].f.note == shown[i].f.note && now[i].f.replies.size() == shown[i].f.replies.size() && std::abs (now[i].ws - shown[i].ws) < 1.0e-6;
        if (same) return;
        shown = now; list.updateContent(); list.repaint();
    }
    void resized() override { auto r = getLocalBounds(); caption.setBounds (r.removeFromTop (18)); list.setBounds (r); }
private:
    struct Shown { FlagRec f; double ws = -1.0; };
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& f = shown[(size_t) row].f;
        const double ws = shown[(size_t) row].ws;
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (juce::Colour (0xffff5a3c)); g.fillRect (0, 0, 3, h);
        const double rate = app.model.sessionRate > 0 ? app.model.sessionRate : 48000.0;
        juce::String head = f.pinnedAt.formatted ("%H:%M:%S");
        if (ws >= 0.0) head << "  (" << hms (ws, false) << ")"; else head << "  (" << hms ((double) f.sample / rate, false) << " of the session)";
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f));
        g.drawText (head, 8, 2, w - 10, 13, juce::Justification::centredLeft, true);
        g.setColour (theme::text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (f.note.isNotEmpty() ? f.note : juce::String ("(no note)"), 8, 14, w - 10, 15, juce::Justification::centredLeft, true);
        if (! f.replies.empty())
        {
            g.setColour (juce::Colour (0xff7ee08a)); g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb3 ")) + f.replies.back().by + ": " + f.replies.back().text, 8, 29, w - 10, 14, juce::Justification::centredLeft, true);
        }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) shown.size()) && onPick) onPick (shown[(size_t) row].f.id, shown[(size_t) row].ws); }
    juce::String getTooltipForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return {};
        const auto& f = shown[(size_t) row].f;
        juce::String t = "Flag pinned at " + f.pinnedAt.formatted ("%H:%M:%S") + ": " + (f.note.isNotEmpty() ? f.note : juce::String ("(no note)"));
        for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
        return t;
    }
    PrimaryApp& app;
    juce::ListBox list;
    juce::Label caption;
    std::vector<Shown> shown;
};

class PrimaryTakeComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    PrimaryTakeComponent (PrimaryApp& a, std::function<void (const juce::String&)> open) : app (a), openWindow (std::move (open)), flags (a)
    {
        setWantsKeyboardFocus (true);
        auto caption = [this] (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification); l.setFont (juce::FontOptions (12.0f)); l.setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (l);
        };
        caption (pieceCaption, "Piece / Take Window Name:");
        caption (labelCaption, "Next take's label (empty: the piece):");
        piece.setText (app.pieceName(), false);
        piece.setTooltip ("The piece (or the half of the concert): the take window the takes go into. It is fixed while the session runs.");
        piece.onReturnKey = [this] { app.setPieceName (piece.getText()); grabKeyboardFocus(); };
        piece.onFocusLost = [this] { app.setPieceName (piece.getText()); };
        label.setTooltip ("The next take's label, as fermata shows it (\"005 - <label>\"); empty = the piece's name");
        label.onTextChange = [this] { app.takeLabel = label.getText(); };
        label.onReturnKey = [this] { grabKeyboardFocus(); };
        for (auto* c : std::initializer_list<juce::Component*> { &piece, &label, &nextTake, &recButton, &takeLength, &bigTime, &playheadLine, &flags, &view, &noteBox, &noteCaption, &hint })
            addAndMakeVisible (c);
        noteBox.setVisible (false); noteCaption.setVisible (false);
        recButton.setIcon (Icon::Record, "Record");
        recButton.setBoxLook (true);
        recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc62828));
        recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc62828));
        recButton.onClick = [this] { if (app.isRecording()) confirmStop(); else app.record(); };
        recButton.setWantsKeyboardFocus (false);                   // (a button with the keyboard presses itself on Enter: Enter must always pin a flag)
        loopBtn.setWantsKeyboardFocus (false);
        recButton.setTooltip ("Record [R]: a take starts - up to the pre-take before now. The session (everything written to the drives) starts with it if it is not on yet. While a take records this is STOP TAKE: click it to end the take (the R key never stops one).");
        auto tool = [this] (IconButton& b, Icon ic, const juce::String& name, const juce::String& tip, std::function<void()> f)
        {
            b.setIcon (ic, name); b.setTooltip (tip); b.onClick = std::move (f); b.setWantsKeyboardFocus (false); addAndMakeVisible (b);
        };
        tool (startBtn, Icon::Start, "To the start", "To the start of the piece [Home]", [this] { seek (0.0); });
        tool (rewBtn, Icon::Rewind, "Back", "Back 5 seconds", [this] { seek (app.playheadSeconds() - 5.0); });
        tool (playBtn, Icon::Play, "Play / Stop", "Play / Stop [Space]: plays the takes through the processing mixer (the live monitoring rests meanwhile)", [this] { togglePlay(); });
        tool (fwdBtn, Icon::Forward, "Forward", "Forward 5 seconds", [this] { seek (app.playheadSeconds() + 5.0); });
        tool (endBtn, Icon::End, "To the end", "To the end of the last take [End]", [this] { if (auto L = app.layout()) seek (L->endSeconds()); });
        loopBtn.setTooltip ("Loop: playback goes round the part marked by dragging on the timeline");
        loopBtn.onClick = [this] { loopOn = loopBtn.getToggleState(); applyLoop(); };
        addAndMakeVisible (loopBtn);
        for (auto* b : { &flagBtn, &markerBtn, &armAllBtn, &armNoneBtn, &fitBtn, &zoomIn, &zoomOut, &mixerBtn, &followBtn })
        { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
        flagBtn.setTooltip ("Pin a flag [Enter]: at the moment of the music now, sent to the Fixer at once. Type the note and press Enter again.");
        flagBtn.onClick = [this] { pinFlag(); };
        markerBtn.setTooltip ("A marker [M] at the playhead (or at the live end while recording). Markers 1 and 2 mark what Bounce out bounces.");
        markerBtn.onClick = [this] { addMarker(); };
        armAllBtn.onClick = [this] { app.armAll (true); };
        armNoneBtn.onClick = [this] { app.armAll (false); };
        armAllBtn.setTooltip ("Arm every track (the session records the armed tracks; chosen when it starts)");
        armNoneBtn.setTooltip ("Disarm every track");
        fitBtn.onClick = [this] { view.zoomToFit(); };
        zoomIn.onClick = [this] { view.zoomBy (1.5, app.playheadSeconds()); };
        zoomOut.onClick = [this] { view.zoomBy (1.0 / 1.5, app.playheadSeconds()); };
        mixerBtn.onClick = [this] { if (openWindow) openWindow ("mixer"); };
        mixerBtn.setTooltip ("The processing mixer: what you hear (live, and when playing back)");
        followBtn.setClickingTogglesState (true); followBtn.setToggleState (true, juce::dontSendNotification);
        followBtn.setTooltip ("Follow: the timeline keeps the live end (or the playhead) in view");
        noteCaption.setText ("Flag note (Enter saves, Esc leaves it empty):", juce::dontSendNotification);
        noteCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold)); noteCaption.setColour (juce::Label::textColourId, juce::Colour (0xffff8f78));
        noteBox.setFont (juce::FontOptions (16.0f));
        noteBox.onReturnKey = [this] { saveNote(); };
        noteBox.onEscapeKey = [this] { closeNote(); };
        hint.setFont (juce::FontOptions (12.5f)); hint.setColour (juce::Label::textColourId, theme::dimText); hint.setMinimumHorizontalScale (0.7f);
        flags.onPick = [this] (const juce::String& id, double s) { if (s >= 0.0) { view.keepInView (s, false); view.selectedFlag = id; seek (s); } };
        // the timeline
        view.model = &app.model; view.project = &app.project;
        view.markerList = [this]() -> std::vector<Marker>* { auto* w = app.pieceWindow(); return w != nullptr ? &w->markers : nullptr; };
        view.isArmed = [this] (const juce::Uuid& id) { return app.isArmed (id); };
        view.toggleArm = [this] (const juce::Uuid& id) { app.setArmed (id, ! app.isArmed (id)); };
        view.isMuteSolo = [this] (const juce::Uuid& id, bool solo) { auto* m = app.monitorMixer(); auto* s = m != nullptr ? m->stripFor (id) : nullptr; return s != nullptr && (solo ? s->solo.get() : s->mute.get()); };
        view.toggleMuteSolo = [this] (const juce::Uuid& id, bool solo)
        {
            auto* m = app.monitorMixer(); auto* s = m != nullptr ? m->stripFor (id) : nullptr;
            if (s == nullptr) return;
            if (solo) s->solo.set (! s->solo.get()); else s->mute.set (! s->mute.get());
            app.markDirty(); app.mixChanged();
        };
        view.colourOf = [this] (const juce::Uuid& id) { return chan::of (app.project, id); };
        view.canSeek = [] { return true; };
        view.onSeek = [this] (double s) { seek (s); };
        view.onSelect = [this] (double a, double b, const std::vector<juce::Uuid>&) { selA = a; selB = b; applyLoop(); };
        view.onFlagClick = [this] (const juce::String& id, bool dbl)
        {
            view.selectedFlag = id;
            if (dbl) openNote (id, true);
            if (auto* f = app.model.findFlag (id)) if (auto L = app.layout()) { const double s = L->windowSecondsOf (f->sample); if (s >= 0.0) seek (s); }
            view.repaint();
        };
        view.onMarkersChanged = [this] { app.markDirty(); };
        app.addChangeListener (this);
        refresh();
        startTimerHz (30);
    }
    ~PrimaryTakeComponent() override { app.removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (112);
        {
            auto left = top.removeFromLeft (230);
            pieceCaption.setBounds (left.removeFromTop (18)); piece.setBounds (left.removeFromTop (26));
            left.removeFromTop (6);
            labelCaption.setBounds (left.removeFromTop (18)); label.setBounds (left.removeFromTop (26));
        }
        top.removeFromLeft (10);
        nextTake.setBounds (top.removeFromLeft (84).withHeight (60));
        top.removeFromLeft (8);
        recButton.setBounds (top.removeFromLeft (96).withHeight (60));
        top.removeFromLeft (8);
        {
            auto col = top.removeFromLeft (180);
            takeLength.setBounds (col.removeFromTop (60));
        }
        top.removeFromLeft (8);
        {
            auto col = top.removeFromLeft (260);
            bigTime.setBounds (col.removeFromTop (60));
            col.removeFromTop (4);
            playheadLine.setBounds (col.removeFromTop (20));
        }
        top.removeFromLeft (12);
        {   // the transport and the tools, two rows
            auto row1 = top.removeFromTop (34), row2 = top.withHeight (34).translated (0, 6);
            for (auto* b : std::initializer_list<juce::Component*> { &startBtn, &rewBtn, &playBtn, &fwdBtn, &endBtn, &loopBtn }) { b->setBounds (row1.removeFromLeft (40)); row1.removeFromLeft (3); }
            row1.removeFromLeft (10);
            flagBtn.setBounds (row1.removeFromLeft (84)); row1.removeFromLeft (4);
            markerBtn.setBounds (row1.removeFromLeft (84)); row1.removeFromLeft (4);
            mixerBtn.setBounds (row1.removeFromLeft (70));
            armAllBtn.setBounds (row2.removeFromLeft (76)); row2.removeFromLeft (3);
            armNoneBtn.setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (10);
            fitBtn.setBounds (row2.removeFromLeft (52)); row2.removeFromLeft (3);
            zoomOut.setBounds (row2.removeFromLeft (30)); row2.removeFromLeft (3);
            zoomIn.setBounds (row2.removeFromLeft (30)); row2.removeFromLeft (10);
            followBtn.setBounds (row2.removeFromLeft (70));
        }
        auto noteRow = r.removeFromTop (30);
        if (noteBox.isVisible()) { noteCaption.setBounds (noteRow.removeFromLeft (330)); noteBox.setBounds (noteRow.reduced (0, 2)); hint.setBounds ({}); }
        else hint.setBounds (noteRow);
        auto right = r.removeFromRight (juce::jmin (300, r.getWidth() / 4));
        flags.setBounds (right.withTrimmedLeft (6));
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (noteBox.hasKeyboardFocus (false) || piece.hasKeyboardFocus (false) || label.hasKeyboardFocus (false)) return false;
        const auto code = k.getKeyCode();
        if (code == juce::KeyPress::returnKey) { pinFlag(); return true; }
        if (k == juce::KeyPress ('r') || k == juce::KeyPress ('R'))
        {
            if (app.isRecording()) setHint ("A take is recording: STOP TAKE (the red button) ends it - the R key never does.");
            else app.record();
            return true;
        }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (k == juce::KeyPress ('m') || k == juce::KeyPress ('M')) { addMarker(); return true; }
        if (code == juce::KeyPress::homeKey) { seek (0.0); return true; }
        if (code == juce::KeyPress::endKey) { if (auto L = app.layout()) seek (L->endSeconds()); return true; }
        if (code == juce::KeyPress::leftKey) { view.zoomBy (1.0 / 1.4, app.playheadSeconds()); return true; }
        if (code == juce::KeyPress::rightKey) { view.zoomBy (1.4, app.playheadSeconds()); return true; }
        if (code == juce::KeyPress::upKey) { view.laneH = juce::jmin (200, view.laneH + 8); view.repaint(); return true; }
        if (code == juce::KeyPress::downKey) { view.laneH = juce::jmax (22, view.laneH - 8); view.repaint(); return true; }
        if (k == juce::KeyPress ('+') || k == juce::KeyPress ('=')) { view.vzoom = juce::jmin (16.0f, view.vzoom * 1.4f); view.repaint(); return true; }
        if (k == juce::KeyPress ('-')) { view.vzoom = juce::jmax (0.25f, view.vzoom / 1.4f); view.repaint(); return true; }
        if (code == juce::KeyPress::numberPadAdd) { app.setTalkback (! app.talkbackOpen()); return true; }
        if (k == juce::KeyPress ('1') || k == juce::KeyPress ('2')) { setMark (k.getKeyCode() == '1'); return true; }
        return false;
    }
    /** Keys 1 and 2: the marks Bounce out can bounce between (fermata's own 1 / 2 marks of the take window: editTake, editIn, editOut). */
    void setMark (bool first)
    {
        auto* w = app.pieceWindow();
        const auto L = app.layout();
        if (w == nullptr || L == nullptr) return;
        const double ph = app.playheadSeconds();
        const auto* t = L->takeAt (ph);
        if (t == nullptr || ! t->inProject) { setHint ("Marks 1 and 2 go in a take that has been merged (the playhead is not in one)."); return; }
        const double at = juce::jlimit (0.0, t->lengthSeconds(), ph - t->start);
        if (w->editTake != t->groupId) { w->editTake = t->groupId; w->editIn = -1.0; w->editOut = -1.0; }
        if (first) { w->editIn = at; if (w->editOut >= 0.0 && w->editOut <= at) w->editOut = -1.0; }
        else       { w->editOut = at; if (w->editIn >= 0.0 && w->editIn >= at) w->editIn = -1.0; }
        app.markDirty();
        setHint (juce::String ("Mark ") + (first ? "1" : "2") + " set in take " + juce::String (t->number) + " (Bounce out can bounce between marks 1 and 2).");
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh()
    {
        if (! piece.hasKeyboardFocus (false) && piece.getText() != app.pieceName()) piece.setText (app.pieceName(), false);
        piece.setReadOnly (app.model.sessionOn);
        const bool locked = app.armingLocked();
        armAllBtn.setEnabled (! locked); armNoneBtn.setEnabled (! locked);
        flags.refresh();
        if (auto* w = app.pieceWindow())
        {
            view.projectFlags.clear();
            for (auto& m : w->markers) if (m.flag) view.projectFlags.push_back (m);
        }
        view.layout = app.layout();
        view.repaint();
    }
    void timerCallback() override
    {
        const auto& m = app.model;
        const auto L = app.layout();
        view.layout = L;
        nextTake.set (m.recordingTake != 0, m.recordingTake != 0 ? m.recordingTake : app.nextTakeNumber());
        recButton.setButtonText (app.isRecording() ? "STOP TAKE" : "REC [R]");
        recButton.setToggleState (app.isRecording(), juce::dontSendNotification);
        playBtn.setIcon (app.isPlaying() ? Icon::Stop : Icon::Play, "Play / Stop");
        playBtn.setEnabled (! app.isRecording() || app.isPlaying());
        double live = -1.0;
        const ShownTake* rec = nullptr;
        if (L != nullptr) for (auto& t : L->takes) if (t.recording) rec = &t;
        if (rec != nullptr)
        {
            live = rec->start + (double) (m.positionNow() - rec->sessionStart) / juce::jmax (1.0, rec->rate);
            const double fromRecord = (double) (m.positionNow() - rec->recordPoint) / juce::jmax (1.0, rec->rate);
            takeLength.set (hms (live - rec->start, false), juce::Colour (0xffff6b5a));
            bigTime.set (hms (fromRecord), juce::Colour (0xffff6b5a));
            const int pre = (int) std::lround ((double) (rec->recordPoint - rec->sessionStart) / juce::jmax (1.0, rec->rate));
            playheadLine.setText ("Take " + juce::String (rec->number) + " recording - " + hms (fromRecord, false) + " since Record "
                                  + (pre > 0 ? "(with " + juce::String (pre) + " s of pre-take before it)" : juce::String ("(no pre-take: the session started with it)")), juce::dontSendNotification);
        }
        else
        {
            const double ph = app.playheadSeconds();
            const auto* t = L != nullptr ? L->takeAt (ph) : nullptr;
            takeLength.set (t != nullptr ? hms (t->lengthSeconds(), false) : juce::String ("--:--:--"));
            bigTime.set (hms (ph));
            playheadLine.setText (t != nullptr ? "Playhead in take " + juce::String (t->number) + " at " + hms (ph - t->start) : juce::String ("Playhead: ") + hms (ph) + " (between takes)", juce::dontSendNotification);
        }
        view.liveEnd = live;
        view.playhead = app.playheadSeconds();
        view.mark1 = view.mark2 = -1.0;
        if (auto* w = app.pieceWindow(); w != nullptr && L != nullptr)
            for (auto& t : L->takes)
                if (t.inProject && t.groupId == w->editTake)
                {
                    if (w->editIn >= 0.0) view.mark1 = t.start + w->editIn;
                    if (w->editOut >= 0.0) view.mark2 = t.start + w->editOut;
                }
        if (followBtn.getToggleState())
        {
            if (live >= 0.0) view.keepInView (live, true);
            else if (app.isPlaying()) view.keepInView (view.playhead, true);
        }
        // the line under the tools: what matters now
        juce::String h;
        const auto now = juce::Time::getMillisecondCounter();
        const bool noAudio = m.connected && m.audioMissing > 0.0;      // (0.3.0: ahead of everything, in red, while it lasts)
        if (noAudio)
            h = m.audioMissingInSession ? "NO AUDIO FOR " + juce::String (juce::roundToInt (m.audioMissing)) + " s (" + m.audioWhy + "): NOTHING IS BEING RECORDED. "
                                          "What was recorded is kept; the log (Help) says what to do if it does not come back."
                                        : "The audio device gives no audio (" + m.audioWhy + ").";
        else if (hintText.isNotEmpty() && now - hintMs < 6000) h = hintText;
        else if (! m.connected) h = "The recorder is starting...";
        else if (! m.deviceRunning) h = "No audio device: open Audio settings.";
        else if (app.isPlaying()) h = "PLAYING BACK through the processing mixer - the live inputs are not heard until you stop.";
        else if (! m.sessionOn) h = "Ready. REC starts the session and the take (or click Pre-take in the main window to start writing first). " + juce::String (app.preTakeSeconds()) + " s pre-take.";
        else if (app.isRecording()) h = "Recording. Enter pins a flag (the Fixer sees it at once). STOP TAKE ends the take; the drives keep writing (the pre-take for the next one).";
        else h = "The session is on: the drives are writing (the pre-take). REC starts a take " + juce::String (app.preTakeSeconds()) + " s back.";
        if (! noAudio && ! m.drives.empty()) for (auto& d : m.drives) if (d.dropped) h = "A DRIVE WAS DROPPED (" + d.why + "): the recording goes on on the others. " + h;
        hint.setText (h, juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, noAudio ? juce::Colour (0xffff7a68) : theme::dimText);
        view.repaintIfChanged();
    }
    void setHint (const juce::String& t) { hintText = t; hintMs = juce::Time::getMillisecondCounter(); }
    void seek (double s)
    {
        s = juce::jmax (0.0, s);
        app.cursorSeconds = s;
        if (app.isPlaying()) app.play (s);
        view.playhead = s;
        view.keepInView (s, false);
        view.repaint();
    }
    void togglePlay()
    {
        if (app.isPlaying()) { app.stopPlayback(); return; }
        if (app.isRecording()) { setHint ("A take is recording: playback waits until it stops."); return; }
        applyLoop();
        app.play (app.cursorSeconds);
    }
    void applyLoop() { if (app.player != nullptr) app.player->setLoop (loopOn && selB > selA, selA, selB); }
    void confirmStop()
    {
        juce::Component::SafePointer<PrimaryTakeComponent> safe (this);
        confirmAsync ("Stop the take", "End take " + juce::String (app.model.recordingTake) + " now? (The drives keep writing: the next take can start with its pre-take.)", "Stop the take",
                      [safe] { if (safe != nullptr) safe->app.stopTake(); }, "Keep recording");
    }
    void pinFlag()
    {
        if (noteBox.isVisible()) { saveNote(); return; }
        const auto id = app.pinFlag();
        if (id.isEmpty()) { setHint ("A flag marks the recording: start the session (REC, or Pre-take) first."); return; }
        openNote (id, false);
    }
    void openNote (const juce::String& id, bool editing)
    {
        noteFor = id;
        juce::String current;
        if (editing)
        {
            if (auto* f = app.model.findFlag (id)) current = f->note;
            else if (auto* w = app.pieceWindow()) if (auto* m = findMarker (w->markers, juce::Uuid (id)); m != nullptr && m->flag) current = m->note;   // (a flag already in the project)
        }
        noteBox.setText (current, false);
        noteBox.setVisible (true); noteCaption.setVisible (true);
        resized();
        noteBox.grabKeyboardFocus();
        view.selectedFlag = id;
    }
    void saveNote()
    {
        if (noteFor.isNotEmpty())
        {
            if (app.model.findFlag (noteFor) != nullptr) app.setFlagNote (noteFor, noteBox.getText());       // (the session's: through the recorder)
            else if (auto* w = app.pieceWindow())
                if (auto* m = findMarker (w->markers, juce::Uuid (noteFor)); m != nullptr && m->flag)         // (one the project already has: here)
                { m->note = noteBox.getText().trim(); m->name = flagName (m->note, m->replies); app.markDirty(); }
        }
        closeNote();
    }
    void closeNote()
    {
        noteFor.clear();
        noteBox.setVisible (false); noteCaption.setVisible (false);
        resized();
        grabKeyboardFocus();
    }
    void addMarker()
    {
        auto* w = app.pieceWindow();
        if (w == nullptr) return;
        const double at = view.liveEnd >= 0.0 ? view.liveEnd : app.playheadSeconds();
        addMarker (w->markers, at);
        app.markDirty();
        view.repaint();
    }
    static void addMarker (std::vector<Marker>& list, double at) { td::addMarker (list, at); }

    PrimaryApp& app;
    std::function<void (const juce::String&)> openWindow;
    juce::Label pieceCaption, labelCaption, playheadLine, noteCaption, hint;
    juce::TextEditor piece, label, noteBox;
    NextTakeBox nextTake;
    IconButton recButton { "REC [R]" };
    TimeBox takeLength { "TAKE LENGTH", 26.0f }, bigTime { "H : M : S . MS", 34.0f };
    IconButton startBtn, rewBtn, playBtn, fwdBtn, endBtn;
    LoopButton loopBtn;
    juce::TextButton flagBtn { "Flag [Enter]" }, markerBtn { "Marker [M]" }, armAllBtn { "Arm all" }, armNoneBtn { "Arm none" }, fitBtn { "Fit" }, zoomIn { "+" }, zoomOut { "-" },
                     mixerBtn { "Mixer" }, followBtn { "Follow" };
    FlagList flags;
    LiveTakeView view;
    juce::String noteFor, hintText;
    juce::uint32 hintMs = 0;
    bool loopOn = false; double selA = -1.0, selB = -1.0;
};

std::unique_ptr<juce::Component> makePrimaryTakeWindow (PrimaryApp& app, std::function<void (const juce::String&)> openWindow)
{
    return std::make_unique<PrimaryTakeComponent> (app, std::move (openWindow));
}
} // namespace td::live
