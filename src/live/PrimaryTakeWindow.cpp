// fermata:Live 0.3.0: the Primary's take window - fermata's take window, reduced: the piece, Next Take, REC, the take's length, the H:M:S.MS
// display, the transport, ARM per track with Arm all / Arm none, flags (Enter pins one and opens its note; Enter again saves it), the Fixer's
// replies, and the takes filling live on the timeline. Kept out: bars, matching, import, pitch, ReTouch, export, search, take sheet.
// 0.5.0: notes (N) instead of markers, the time of day, the flag point, fermata's zoom and keys (P, 5, C, L, . and ,), the scroll bars, the
// coloured waveforms, M for the mixer, the station's name, and the recording's report.
#include "LiveMainWindow.h"
#include "LiveTakeView.h"
#include "NotePanel.h"
#include "StationNotes.h"
#include "../ui/TimeReadout.h"
#include <set>

namespace td::live
{
static juce::String hms (double seconds, bool ms = true) { return TakeLayout::hmsText (seconds, ms); }

/** The big black display: a caption over the time. */
class TimeBox final : public juce::Component
{
public:
    explicit TimeBox (juce::String cap, float size = 34.0f) : caption (std::move (cap)), fontSize (size) {}
    void set (const juce::String& t, juce::Colour c = juce::Colours::white) { if (t != text || c != colour) { text = t; colour = c; repaint(); } }
    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0e13)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::border); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (caption, getLocalBounds().reduced (8, 3).removeFromTop (12), juce::Justification::centredLeft, false);
        g.setColour (colour); g.setFont (juce::Font (theme::figureFont (fontSize)));
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
            if (f.isNote) continue;
            have.insert (f.id);
            if (! f.removed) now.push_back ({ f, L != nullptr ? L->windowSecondsOf (f.sample) : -1.0 });
        }
        if (auto* w = app.pieceWindow())
            for (auto& m : w->markers)
                if (m.flag && have.count (m.id.toString()) == 0)
                {
                    Shown x; x.f.id = m.id.toString(); x.f.note = m.note; x.f.pinnedAt = m.pinnedAt; x.f.by = m.by; x.ws = L != nullptr ? L->shownOfWindow (m.seconds) : m.seconds;
                    for (auto& r : m.replies) x.f.replies.push_back ({ r.text, r.by, r.at });
                    now.push_back (x);
                }
        std::sort (now.begin(), now.end(), [] (const Shown& x, const Shown& y) { return x.f.pinnedAt > y.f.pinnedAt; });
        bool same = now.size() == shown.size() && (L == nullptr || L->timeOfDay == shownToD);
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].f.id == shown[i].f.id && now[i].f.note == shown[i].f.note && now[i].f.replies.size() == shown[i].f.replies.size() && std::abs (now[i].ws - shown[i].ws) < 1.0e-6;
        if (same) return;
        shown = now; shownToD = L != nullptr && L->timeOfDay; list.updateContent(); list.repaint();
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
        const auto L = app.layout();
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (juce::Colour (0xffff5a3c)); g.fillRect (0, 0, 3, h);
        const double rate = app.model.sessionRate > 0 ? app.model.sessionRate : 48000.0;
        juce::String head = f.pinnedAt.formatted ("%H:%M:%S");
        if (ws >= 0.0)
        {
            if (L != nullptr && L->timeOfDay) { if (auto* t = L->takeAt (ws)) head << "  (take " << t->number << " at " << hms (ws - t->start, false) << ")"; }
            else head << "  (" << hms (ws, false) << ")";
        }
        else head << "  (" << hms ((double) f.sample / rate, false) << " of the session)";
        if (f.by.isNotEmpty()) head << "  by " << f.by;
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
        juce::String t = "Flag pinned at " + f.pinnedAt.formatted ("%H:%M:%S") + (f.by.isNotEmpty() ? " by " + f.by : juce::String()) + ": " + (f.note.isNotEmpty() ? f.note : juce::String ("(no note)"));
        for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
        return t;
    }
    PrimaryApp& app;
    juce::ListBox list;
    juce::Label caption;
    std::vector<Shown> shown;
    bool shownToD = false;
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
        for (auto* c : std::initializer_list<juce::Component*> { &piece, &label, &nextTake, &recButton, &takeLength, &bigTime, &playheadLine, &flags, &view, &noteBox, &noteCaption, &removeFlagBtn, &hint })
            addAndMakeVisible (c);
        addChildComponent (notePanel);
        noteBox.setVisible (false); noteCaption.setVisible (false); removeFlagBtn.setVisible (false);
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
        tool (startBtn, Icon::Start, "To the start", "To the start of the piece [Home]", [this] { seek (firstSeconds()); });
        tool (rewBtn, Icon::Rewind, "Back", "Back 5 seconds", [this] { seek (app.playheadSeconds() - 5.0); });
        tool (playBtn, Icon::Play, "Play / Stop", "Play / Stop [Space]: plays the takes through the processing mixer (the live monitoring rests meanwhile)", [this] { togglePlay(); });
        tool (fwdBtn, Icon::Forward, "Forward", "Forward 5 seconds", [this] { seek (app.playheadSeconds() + 5.0); });
        tool (endBtn, Icon::End, "To the end", "To the end of the last take [End]", [this] { if (auto L = app.layout()) seek (L->endSeconds()); });
        loopBtn.setTooltip ("Loop [L]: playback goes round the part marked by dragging on the timeline");
        loopBtn.onClick = [this] { loopOn = loopBtn.getToggleState(); applyLoop(); };
        addAndMakeVisible (loopBtn);
        for (auto* b : { &flagBtn, &noteBtn, &armAllBtn, &armNoneBtn, &fitBtn, &zoomIn, &zoomOut, &mixerBtn, &followBtn, &todBtn, &colourBtn, &reportBtn })
        { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
        flagBtn.setTooltip ("Pin a flag [Enter]: at the moment of the music now (or at the flag point, if you clicked one in the flag band), sent to the Fixer at once. Type the note and press Enter again.");
        flagBtn.onClick = [this] { pinFlag(); };
        noteBtn.setTooltip ("A note [N]: your own, at the moment now (or at the flag point, or at the playhead when nothing records). A box opens at the side to write in; the Fixer does not see it. The report lists every note.");
        noteBtn.onClick = [this] { pinNote(); };
        armAllBtn.onClick = [this] { app.armAll (true); };
        armNoneBtn.onClick = [this] { app.armAll (false); };
        armAllBtn.setTooltip ("Arm every track (the session records the armed tracks; chosen when it starts)");
        armNoneBtn.setTooltip ("Disarm every track");
        fitBtn.onClick = [this] { view.zoomToFit(); };
        fitBtn.setTooltip ("Everything on the screen");
        zoomIn.onClick = [this] { view.zoomCentred (1.5, zoomAnchor()); };
        zoomOut.onClick = [this] { view.zoomCentred (1.0 / 1.5, zoomAnchor()); };
        zoomIn.setTooltip ("Zoom in [Right], round the playhead (the flag point; now, while recording), which goes to the middle");
        zoomOut.setTooltip ("Zoom out [Left]");
        mixerBtn.onClick = [this] { if (openWindow) openWindow ("mixer-toggle"); };
        mixerBtn.setTooltip ("The processing mixer [M]: what you hear (live, and when playing back). M again closes it.");
        followBtn.setClickingTogglesState (true); followBtn.setToggleState (true, juce::dontSendNotification);
        followBtn.setTooltip ("Follow: the timeline keeps the live end (or the playhead) in view. Scrolling away switches it off; a click on the tracks while recording goes back to now and switches it on.");
        todBtn.setClickingTogglesState (true); todBtn.setToggleState (app.timeOfDay(), juce::dontSendNotification);
        todBtn.setTooltip ("Time of day: the timeline is the clock - every take at the time it was recorded, the gaps between them too, on past midnight (the date at each midnight). "
                           "Off: fermata's timeline, the takes one after another from 00:00:00.000.");
        todBtn.onClick = [this] { switchTimeOfDay (todBtn.getToggleState()); };
        colourBtn.setClickingTogglesState (true); colourBtn.setToggleState (app.colourWaves(), juce::dontSendNotification);
        colourBtn.setTooltip ("Coloured waveforms (fermata's WaveColour): the colour says how high the sound is (red low, green middle, blue high), how vivid how tonal it is; "
                              "a thump below 100 Hz (a kicked stand, a door) is black. Worked out as the take records, from the drive.");
        colourBtn.onClick = [this] { app.setColourWaves (colourBtn.getToggleState()); applyColours(); view.repaint(); };
        reportBtn.setTooltip ("The recording's report: a PDF of the piece's takes with every note (yours and the Fixer's), flag, reply, gap and fix version at its place, and who wrote it");
        reportBtn.onClick = [this] { makeReport(); };
        noteCaption.setText ("Flag note (Enter saves, Esc leaves it empty):", juce::dontSendNotification);
        noteCaption.setFont (juce::FontOptions (12.0f, juce::Font::bold)); noteCaption.setColour (juce::Label::textColourId, juce::Colour (0xffff8f78));
        noteBox.setFont (juce::FontOptions (16.0f));
        noteBox.onReturnKey = [this] { saveNote(); };
        noteBox.onEscapeKey = [this] { closeNote(); };
        // 0.8.0 (asked for on 10 November 2026): a flag can be taken away - one pinned by mistake stayed for good, with the Fixer told of it
        removeFlagBtn.setTooltip ("Take this flag away (one pinned by mistake): it goes from the Fixer's list and from the project too");
        removeFlagBtn.setWantsKeyboardFocus (false);
        removeFlagBtn.onClick = [this] { removeFlag(); };
        hint.setFont (juce::FontOptions (12.5f)); hint.setColour (juce::Label::textColourId, theme::dimText); hint.setMinimumHorizontalScale (0.7f);
        flags.onPick = [this] (const juce::String& id, double s) { if (s >= 0.0) { view.keepInView (s, false); view.selectedFlag = id; if (! app.isRecording()) seek (s); } };
        // the note panel (0.5.0)
        notePanel.onSave = [this] (const juce::String& id, const juce::String& text) { app.setNoteText (id, text); };
        notePanel.onDelete = [this] (const juce::String& id) { app.removeNote (id); };
        notePanel.onClosed = [this] { view.selectedNote.clear(); resized(); grabKeyboardFocus(); view.repaint(); };
        // the timeline
        view.model = &app.model; view.project = &app.project;
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
        view.canSeek = [this] { return ! app.isRecording(); };
        view.onSeek = [this] (double s) { seek (s); };
        view.onLaneClick = [this] (double)
        {   // a click on the tracks: the flag point goes back to NOW (while recording, the view follows now again)
            const bool hadPoint = view.flagPoint >= 0.0;
            view.flagPoint = -1.0;
            if (app.isRecording()) { followBtn.setToggleState (true, juce::dontSendNotification); if (hadPoint) setHint ("Back to NOW: Enter and N pin where the music is now."); }
        };
        view.onFlagBandClick = [this] (double s)
        {
            view.flagPoint = s;
            setHint ("Flag point at " + where (s) + ": Enter pins a flag there, N a note (zooming centres on it). A click on the tracks goes back to NOW.");
            view.repaint();
        };
        view.onTakeClick = [this] (int number) { chosenTake = number; if (number != 0) setHint ("Take " + juce::String (number) + " chosen: P puts marks 1 and 2 round all of it (for Bounce out)."); };
        view.onNoteClick = [this] (const juce::String& id) { openNotePanel (id); };
        view.onUserScrolled = [this] { if (followBtn.getToggleState() && (app.isRecording() || app.isPlaying())) { followBtn.setToggleState (false, juce::dontSendNotification); setHint ("Follow is off while you look about: the Follow button (or a click on the tracks while recording) brings it back."); } };
        view.onSelect = [this] (double a, double b, const std::vector<juce::Uuid>&) { selA = a; selB = b; applyLoop(); };
        view.onFlagClick = [this] (const juce::String& id, bool dbl)
        {
            view.selectedFlag = id;
            if (dbl) openNote (id, true);
            if (! app.isRecording())
                if (auto* f = app.model.findFlag (id)) if (auto L = app.layout()) { const double s = L->windowSecondsOf (f->sample); if (s >= 0.0) seek (s); }
            view.repaint();
        };
        fileColours.folder = [this] { return app.hasProject() ? app.project.projectFolder().getChildFile ("Waveforms") : juce::File(); };
        fileColours.onReady = [this] { view.repaint(); };
        applyColours();
        app.addChangeListener (this);
        refresh();
        startTimerHz (30);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PrimaryTakeComponent> (this)]
        {
            if (safe == nullptr) return;
            if (const auto L = safe->app.layout(); L != nullptr && L->timeOfDay && safe->app.cursorSeconds < L->firstSeconds()) safe->app.cursorSeconds = L->firstSeconds();
            safe->view.playhead = safe->app.cursorSeconds;
            safe->view.zoomToFit();
        });
    }
    ~PrimaryTakeComponent() override
    {
        app.removeChangeListener (this);
        notePanel.onClosed = nullptr;
        if (notePanel.isOpen()) notePanel.close();
        liveColours.stop();
        fileColours.stop();
    }

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
            auto col = top.removeFromLeft (160);
            takeLength.setBounds (col.removeFromTop (60));
        }
        top.removeFromLeft (8);
        {
            auto col = top.removeFromLeft (250);
            bigTime.setBounds (col.removeFromTop (60));
            col.removeFromTop (4);
            playheadLine.setBounds (col.removeFromTop (40));
        }
        top.removeFromLeft (12);
        {   // the transport and the tools, three rows
            auto row1 = top.removeFromTop (32); top.removeFromTop (6);
            auto row2 = top.removeFromTop (32); top.removeFromTop (6);
            auto row3 = top.removeFromTop (32);
            for (auto* b : std::initializer_list<juce::Component*> { &startBtn, &rewBtn, &playBtn, &fwdBtn, &endBtn, &loopBtn }) { b->setBounds (row1.removeFromLeft (38)); row1.removeFromLeft (3); }
            row1.removeFromLeft (8);
            flagBtn.setBounds (row1.removeFromLeft (88)); row1.removeFromLeft (4);
            noteBtn.setBounds (row1.removeFromLeft (76));
            armAllBtn.setBounds (row2.removeFromLeft (70)); row2.removeFromLeft (3);
            armNoneBtn.setBounds (row2.removeFromLeft (78)); row2.removeFromLeft (10);
            fitBtn.setBounds (row2.removeFromLeft (44)); row2.removeFromLeft (3);
            zoomOut.setBounds (row2.removeFromLeft (28)); row2.removeFromLeft (3);
            zoomIn.setBounds (row2.removeFromLeft (28)); row2.removeFromLeft (10);
            followBtn.setBounds (row2.removeFromLeft (66));
            todBtn.setBounds (row3.removeFromLeft (96)); row3.removeFromLeft (4);
            colourBtn.setBounds (row3.removeFromLeft (76)); row3.removeFromLeft (4);
            mixerBtn.setBounds (row3.removeFromLeft (84)); row3.removeFromLeft (4);
            reportBtn.setBounds (row3.removeFromLeft (80));
        }
        auto noteRow = r.removeFromTop (30);
        if (noteBox.isVisible())
        {
            noteCaption.setBounds (noteRow.removeFromLeft (330));
            removeFlagBtn.setBounds (noteRow.removeFromRight (110).reduced (0, 2)); noteRow.removeFromRight (6);
            noteBox.setBounds (noteRow.reduced (0, 2)); hint.setBounds ({});
        }
        else hint.setBounds (noteRow);
        // the side column: the note panel (when a note is open) over the flags and replies
        const int sideW = notePanel.isOpen() ? juce::jmin (420, r.getWidth() / 3) : juce::jmin (300, r.getWidth() / 4);
        auto right = r.removeFromRight (sideW).withTrimmedLeft (6);
        if (notePanel.isOpen()) { notePanel.setBounds (right.removeFromTop (juce::jmax (180, right.getHeight() / 2))); right.removeFromTop (6); }
        flags.setBounds (right);
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (noteBox.hasKeyboardFocus (false) || piece.hasKeyboardFocus (false) || label.hasKeyboardFocus (false) || notePanel.typing()) return false;
        const auto code = k.getKeyCode();
        const auto mods = k.getModifiers();
        const bool plain = ! mods.isCommandDown() && ! mods.isAltDown();
        if (code == juce::KeyPress::returnKey) { pinFlag(); return true; }
        if (k == juce::KeyPress ('r') || k == juce::KeyPress ('R'))
        {
            if (app.isRecording()) setHint ("A take is recording: STOP TAKE (the red button) ends it - the R key never does.");
            else app.record();
            return true;
        }
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (plain && (code == 'N' || code == 'n')) { pinNote(); return true; }
        if (plain && (code == 'M' || code == 'm')) { if (openWindow) openWindow ("mixer-toggle"); return true; }
        if (plain && (code == 'P' || code == 'p')) { marksRound(); return true; }
        if (plain && (code == 'C' || code == 'c')) { view.centreOn (zoomAnchor()); return true; }
        if (plain && (code == 'L' || code == 'l')) { loopBtn.setToggleState (! loopBtn.getToggleState(), juce::dontSendNotification); loopOn = loopBtn.getToggleState(); applyLoop(); return true; }
        if (plain && code == '5') { clearMarks(); return true; }
        if (code == juce::KeyPress::homeKey) { seek (firstSeconds()); return true; }
        if (code == juce::KeyPress::endKey) { if (auto L = app.layout()) seek (L->endSeconds()); return true; }
        if (mods.isShiftDown() && code == juce::KeyPress::leftKey)  { if (! app.isRecording()) seek (app.playheadSeconds() - 0.01); return true; }
        if (mods.isShiftDown() && code == juce::KeyPress::rightKey) { if (! app.isRecording()) seek (app.playheadSeconds() + 0.01); return true; }
        if (code == juce::KeyPress::leftKey)  { view.zoomCentred (1.0 / 1.4, zoomAnchor()); return true; }     // (fermata's arrows: left / right zoom,
        if (code == juce::KeyPress::rightKey) { view.zoomCentred (1.4, zoomAnchor()); return true; }           //  round the playhead, put in the middle;
        if (code == juce::KeyPress::upKey)    { view.changeLaneHeight (-1); return true; }                    //  up / down: every track smaller / bigger)
        if (code == juce::KeyPress::downKey)  { view.changeLaneHeight (+1); return true; }
        if (k == juce::KeyPress ('+') || k == juce::KeyPress ('=') || k == juce::KeyPress ('.')) { view.vzoom = juce::jmin (16.0f, view.vzoom * 1.4f); view.repaint(); return true; }
        if (k == juce::KeyPress ('-') || k == juce::KeyPress (',')) { view.vzoom = juce::jmax (0.25f, view.vzoom / 1.4f); view.repaint(); return true; }
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
        const auto L = app.layout();
        auto* w = app.pieceWindow();
        view.projectFlags.clear();
        if (w != nullptr) for (auto& m : w->markers) if (m.flag) view.projectFlags.push_back (m);
        const auto list = notes::collect (app.model, w, L.get(), "Primary");
        view.notes = notes::forView (list, w, L.get());
        if (notePanel.isOpen())
        {
            if (auto* x = notes::find (list, notePanel.noteId())) { notePanel.setHeading (notes::heading (*x, L.get())); notePanel.refreshText (x->text); }
        }
        view.layout = L;
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
        const bool tod = L != nullptr && L->timeOfDay;
        {   // (0.5.0: the first layout, or a new day on the clock - a project opened, the switch: the view starts on the takes)
            const juce::int64 key = L == nullptr ? -1 : (tod ? L->originMs : 1);
            if (key != shownLayoutKey)
            {
                shownLayoutKey = key;
                if (L != nullptr)
                {
                    if (tod && app.cursorSeconds < L->firstSeconds()) app.cursorSeconds = L->firstSeconds();
                    view.layout = L; view.playhead = app.cursorSeconds;
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PrimaryTakeComponent> (this)] { if (safe != nullptr) safe->view.zoomToFit(); });
                }
            }
        }
        double live = -1.0;
        const ShownTake* rec = nullptr;
        if (L != nullptr) for (auto& t : L->takes) if (t.recording) rec = &t;
        if (rec != nullptr)
        {
            live = rec->start + (double) (m.positionNow() - rec->sessionStart) / juce::jmax (1.0, rec->rate);
            const double fromRecord = (double) (m.positionNow() - rec->recordPoint) / juce::jmax (1.0, rec->rate);
            takeLength.set (hms (live - rec->start, false), juce::Colour (0xffff6b5a));
            bigTime.setCaption ("SINCE RECORD  H : M : S . MS");
            bigTime.set (hms (fromRecord), juce::Colour (0xffff6b5a));
            const int pre = (int) std::lround ((double) (rec->recordPoint - rec->sessionStart) / juce::jmax (1.0, rec->rate));
            juce::String line = "Take " + juce::String (rec->number) + " recording - " + hms (fromRecord, false) + " since Record "
                                + (pre > 0 ? "(+ " + juce::String (pre) + " s pre-take)" : juce::String ("(no pre-take)"));
            if (tod) line << "\nNow " << juce::Time::getCurrentTime().formatted ("%H:%M:%S");
            if (view.flagPoint >= 0.0) line << (tod ? "   " : "\n") << "Flag point: " << where (view.flagPoint);
            playheadLine.setText (line, juce::dontSendNotification);
        }
        else
        {
            const double ph = app.playheadSeconds();
            const auto* t = L != nullptr ? L->takeAt (ph) : nullptr;
            takeLength.set (t != nullptr ? hms (t->lengthSeconds(), false) : juce::String ("--:--:--"));
            bigTime.setCaption (tod ? "PLAYHEAD - TIME OF DAY" : "PLAYHEAD  H : M : S . MS");
            bigTime.set (L != nullptr ? L->timeText (ph) : hms (ph));
            juce::String line = t != nullptr ? "Playhead in take " + juce::String (t->number) + " at " + hms (ph - t->start) : juce::String ("Playhead between takes");
            if (tod) line << "\nNow " << juce::Time::getCurrentTime().formatted ("%H:%M:%S") << " (" << juce::Time::getCurrentTime().formatted ("%d/%m/%Y") << ")";
            if (view.flagPoint >= 0.0) line << (tod ? "   " : "\n") << "Flag point: " << where (view.flagPoint);
            playheadLine.setText (line, juce::dontSendNotification);
        }
        view.liveEnd = live;
        view.playhead = app.isRecording() ? -1.0 : app.playheadSeconds();      // (0.5.0: while a take records the playhead is not used - the flag point is)
        view.nowLine = tod ? (double) (juce::Time::currentTimeMillis() - L->originMs) / 1000.0 : -1.0;
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
        if (view.colourWaves) feedColours();
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
        else if (app.isRecording()) h = "Recording. Enter pins a flag (the Fixer sees it at once), N a note of your own; click the flag band first for a moment that has passed. STOP TAKE ends the take.";
        else h = "The session is on: the drives are writing (the pre-take). REC starts a take " + juce::String (app.preTakeSeconds()) + " s back.";
        if (! noAudio && ! m.drives.empty()) for (auto& d : m.drives) if (d.dropped) h = "A DRIVE WAS DROPPED (" + d.why + "): the recording goes on on the others. " + h;
        hint.setText (h, juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, noAudio ? juce::Colour (0xffff7a68) : theme::dimText);
        view.repaintIfChanged();
    }
    void setHint (const juce::String& t) { hintText = t; hintMs = juce::Time::getMillisecondCounter(); }
    /** A place on the timeline as words: "take 5 at 00:12:34.500 (19:44:44)". */
    juce::String where (double s) const
    {
        const auto L = app.layout();
        if (L == nullptr) return hms (s);
        juce::String t;
        if (auto* k = L->takeAt (s)) t << "take " << k->number << " at " << hms (s - k->start);
        else t << (L->timeOfDay ? "" : hms (s) + " ") << "(between takes)";
        if (const auto c = L->clockMsAt (s); c != 0) t << " (" << juce::Time (c).formatted ("%H:%M:%S") << ")";
        return t;
    }
    double firstSeconds() const { const auto L = app.layout(); return L != nullptr && L->timeOfDay ? L->firstSeconds() : 0.0; }
    /** What zooming centres on: the flag point; while recording, now (the live end); else the playhead. */
    double zoomAnchor() const
    {
        if (view.flagPoint >= 0.0) return view.flagPoint;
        if (view.liveEnd >= 0.0) return view.liveEnd;
        return app.playheadSeconds();
    }
    void seek (double s)
    {
        if (app.isRecording()) return;                                   // (0.5.0: while a take records the playhead stands; the flag point is clicked in the flag band)
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
    /** Where Enter and N pin: the flag point; while playing back, the playhead; in the session, now; else the playhead. */
    void target (double& s, bool& now) const
    {
        now = false;
        if (view.flagPoint >= 0.0) { s = view.flagPoint; return; }
        if (app.isPlaying()) { s = app.playheadSeconds(); return; }
        if (app.model.sessionOn) { now = true; s = view.liveEnd; return; }
        s = app.playheadSeconds();
    }
    void pinFlag()
    {
        if (noteBox.isVisible()) { saveNote(); return; }
        double s = 0.0; bool now = false;
        target (s, now);
        juce::int64 sample = -1;
        if (! now)
        {
            const auto L = app.layout();
            sample = L != nullptr && app.model.sessionOn ? L->sessionSampleAt (s) : -1;
            if (sample < 0)
            {
                setHint (app.model.sessionOn ? "A flag reaches the Fixer through the session: put the flag point on a take of this session (or click the tracks to go back to now)."
                                             : "A flag marks the recording: start the session (REC, or Pre-take) first. N pins a note of your own anywhere on a take.");
                return;
            }
        }
        const auto id = app.pinFlag (sample);
        if (id.isEmpty()) { setHint ("A flag marks the recording: start the session (REC, or Pre-take) first."); return; }
        view.flagPoint = -1.0;                                           // (pinned: Enter is back to now)
        openNote (id, false);
    }
    void pinNote()
    {
        double s = 0.0; bool now = false;
        target (s, now);
        if (now && ! app.isRecording()) { now = false; s = app.playheadSeconds(); }   // (a note between takes goes at the playhead: the take you are looking at)
        juce::String why;
        const auto id = app.pinNote (s, now, why);
        if (id.isEmpty()) { setHint (why); return; }
        view.flagPoint = -1.0;
        openNotePanel (id, true);
    }
    void openNotePanel (const juce::String& id, bool fresh = false)
    {
        const auto L = app.layout();
        const auto list = notes::collect (app.model, app.pieceWindow(), L.get(), "Primary");
        notes::Info x;
        if (auto* f = notes::find (list, id)) x = *f;
        else if (! fresh) return;
        else { x.id = id; x.by = app.stationName(); }
        view.selectedNote = id;
        notePanel.open (id, notes::heading (x, L.get()), x.text);
        resized();
        view.repaint();
    }
    void openNote (const juce::String& id, bool editing)
    {
        noteFor = id; noteFresh = ! editing;
        juce::String current;
        if (editing)
        {
            if (auto* f = app.model.findFlag (id)) current = f->note;
            else if (auto* w = app.pieceWindow()) if (auto* m = findMarker (w->markers, juce::Uuid (id)); m != nullptr && m->flag) current = m->note;   // (a flag already in the project)
        }
        noteBox.setText (current, false);
        noteBox.setVisible (true); noteCaption.setVisible (true); removeFlagBtn.setVisible (true);
        resized();
        noteBox.grabKeyboardFocus();
        view.selectedFlag = id;
    }
    void saveNote()
    {
        if (noteFor.isNotEmpty())
        {
            if (app.model.findFlag (noteFor) != nullptr || noteFresh) app.setFlagNote (noteFor, noteBox.getText());   // (the session's, or just pinned: through the recorder)
            else if (auto* w = app.pieceWindow())
                if (auto* m = findMarker (w->markers, juce::Uuid (noteFor)); m != nullptr && m->flag)         // (one the project already has: here)
                { m->note = noteBox.getText().trim(); m->name = flagName (m->note, m->replies); app.markDirty(); }
        }
        closeNote();
    }
    /** The flag whose note row is open is taken away: the session's through the recorder (which tells the Fixer, and the project loses its marker
        at the next look), one the project has from an earlier session straight out of the project. */
    void removeFlag()
    {
        if (noteFor.isEmpty()) return;
        const auto id = noteFor;
        if (app.model.findFlag (id) != nullptr || noteFresh) app.removeFlag (id);
        else if (auto* w = app.pieceWindow())
            if (auto* m = findMarker (w->markers, juce::Uuid (id)); m != nullptr && m->flag) { removeMarker (w->markers, juce::Uuid (id)); app.markDirty(); }
        if (view.selectedFlag == id) view.selectedFlag.clear();
        closeNote();
        setHint ("Flag taken away.");
        view.repaint();
    }
    void closeNote()
    {
        noteFor.clear();
        noteBox.setVisible (false); noteCaption.setVisible (false); removeFlagBtn.setVisible (false);
        resized();
        grabKeyboardFocus();
    }
    // ---- marks 1 and 2 (Bounce out): P round the selection, or round the take chosen by its header; 5 takes them away
    void marksRound()
    {
        auto* w = app.pieceWindow();
        const auto L = app.layout();
        if (w == nullptr || L == nullptr) return;
        const ShownTake* t = nullptr;
        double a = 0.0, b = 0.0;
        if (selB > selA)
        {
            t = L->takeAt (selA + 1.0e-6);
            if (t == nullptr) { setHint ("The part dragged over does not start in a take."); return; }
            a = selA - t->start; b = selB - t->start;
        }
        else if (chosenTake != 0 && (t = L->findTake (chosenTake)) != nullptr) { a = 0.0; b = t->lengthSeconds(); }
        else { setHint ("P puts marks 1 and 2 round a part you drag over, or round a whole take: click its header (the strip with its number) first."); return; }
        if (! t->inProject) { setHint ("Marks 1 and 2 go in a take that has been merged: take " + juce::String (t->number) + " is not in the project yet."); return; }
        const double len = t->lengthSeconds();
        const bool cut = b > len + 1.0e-6;
        a = juce::jlimit (0.0, len, a); b = juce::jlimit (0.0, len, b);
        if (b - a < 0.01) { setHint ("That part is too short for marks 1 and 2."); return; }
        w->editTake = t->groupId; w->editIn = a; w->editOut = b;
        app.markDirty();
        setHint ("Marks 1 and 2 round " + (a <= 0.0 && b >= len ? "all of take " + juce::String (t->number) : "take " + juce::String (t->number) + ", " + hms (a, false) + " - " + hms (b, false))
                 + (cut ? " (cut at the end of the take: marks go in one take)" : juce::String()) + ". Bounce out can bounce them.");
    }
    void clearMarks()
    {
        if (auto* w = app.pieceWindow()) { w->editIn = -1.0; w->editOut = -1.0; app.markDirty(); setHint ("Marks 1 and 2 taken away."); }
    }
    // ---- the time of day (0.5.0)
    void switchTimeOfDay (bool on)
    {
        if (app.isPlaying()) app.stopPlayback();
        const auto old = app.layout();
        const double ws = old != nullptr ? old->windowOfShown (app.cursorSeconds) : app.cursorSeconds;
        app.setTimeOfDay (on);
        const auto L = app.layout();
        if (L != nullptr) app.cursorSeconds = ws >= 0.0 ? L->shownOfWindow (ws) : L->firstSeconds();
        view.flagPoint = -1.0; selA = selB = -1.0; view.selA = view.selB = -1.0; view.selTracks.clear(); applyLoop();
        view.layout = L;
        view.playhead = app.cursorSeconds;
        refresh();
        timerCallback();                                                 // (the live end, now and the playhead on the new timeline before fitting)
        view.zoomToFit();
        setHint (on ? "The timeline is the time of day: every take at the time it was recorded (the date at each midnight)." : "fermata's timeline: the takes one after another from 00:00:00.000.");
    }
    // ---- the coloured waveforms (0.5.0)
    void applyColours()
    {
        view.colourWaves = app.colourWaves();
        view.fileColours = &fileColours;
        view.sessionColours = &liveColours;
        if (! view.colourWaves) liveColours.want ({}, 0, {});
    }
    void feedColours()
    {
        const auto& m = app.model;
        if (! m.sessionOn || m.windowName != app.pieceName()) { liveColours.want ({}, 0, {}); return; }
        std::vector<std::pair<juce::int64, juce::int64>> spans;
        for (auto& t : m.takes) spans.push_back ({ t.start, t.end });
        liveColours.want (juce::File (m.sessionFolder), m.writtenTo(), std::move (spans));
    }
    // ---- the report (0.5.0)
    void makeReport()
    {
        juce::File f; juce::String err;
        if (! app.writeReport (f, err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Report", err); return; }
        setHint ("The report is in " + f.getFullPathName());
        confirmAsync ("Report", "The report of " + app.pieceName() + " is written:\n" + f.getFullPathName(), "Open it", [f] { f.startAsProcess(); }, "Close");
    }

    PrimaryApp& app;
    std::function<void (const juce::String&)> openWindow;
    juce::Label pieceCaption, labelCaption, playheadLine, noteCaption, hint;
    juce::TextEditor piece, label, noteBox;
    juce::TextButton removeFlagBtn { "Remove flag" };
    NextTakeBox nextTake;
    IconButton recButton { "REC [R]" };
    TimeBox takeLength { "TAKE LENGTH", 26.0f }, bigTime { "H : M : S . MS", 32.0f };
    IconButton startBtn, rewBtn, playBtn, fwdBtn, endBtn;
    LoopButton loopBtn;
    juce::TextButton flagBtn { "Flag [Enter]" }, noteBtn { "Note [N]" }, armAllBtn { "Arm all" }, armNoneBtn { "Arm none" }, fitBtn { "Fit" }, zoomIn { "+" }, zoomOut { "-" },
                     mixerBtn { "Mixer [M]" }, followBtn { "Follow" }, todBtn { "Time of day" }, colourBtn { "Colours" }, reportBtn { "Report..." };
    FlagList flags;
    NotePanel notePanel;
    LiveTakeView view;
    WaveColorCache fileColours;
    LiveColours liveColours;
    juce::String noteFor, hintText;
    juce::uint32 hintMs = 0;
    int chosenTake = 0;
    juce::int64 shownLayoutKey = -2;
    bool noteFresh = false;
    bool loopOn = false; double selA = -1.0, selB = -1.0;
};

std::unique_ptr<juce::Component> makePrimaryTakeWindow (PrimaryApp& app, std::function<void (const juce::String&)> openWindow)
{
    return std::make_unique<PrimaryTakeComponent> (app, std::move (openWindow));
}
} // namespace td::live
