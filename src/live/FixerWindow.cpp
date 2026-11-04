#include "FixerWindow.h"
#include "LiveTakeView.h"
#include "LiveDesk.h"
#include "../core/AudioOps.h"
#include <set>

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static juce::String hmsF (double seconds, bool ms = true)
{
    if (seconds < 0.0) seconds = 0.0;
    const auto total = (juce::int64) std::floor (seconds);
    juce::String t = juce::String ((int) (total / 3600)).paddedLeft ('0', 2) + ":" + juce::String ((int) ((total / 60) % 60)).paddedLeft ('0', 2) + ":" + juce::String ((int) (total % 60)).paddedLeft ('0', 2);
    if (ms) t << "." << juce::String ((int) std::floor ((seconds - (double) total) * 1000.0)).paddedLeft ('0', 3);
    return t;
}

/** A black display with a caption over a time (as the Primary's take window). */
class FixerTimeBox final : public juce::Component
{
public:
    explicit FixerTimeBox (juce::String cap, float size) : caption (std::move (cap)), fontSize (size) {}
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

/** What one ReTouch try needs, gathered on the message thread; the work reads the audio itself (its own readers) on the job's thread. */
struct FixTry
{
    struct Track { juce::Uuid id; juce::String name; int channels = 1; int sessionIndex = -1; juce::File file; };
    int takeNumber = 0; juce::int64 from = 0, to = 0, takeLength = 0, sessionStart = 0; double rate = 48000.0;
    bool session = false; juce::File sessionFolder; juce::int64 writtenTo = 0;
    std::vector<Track> tracks;
    FixSpec spec; juce::String what;
    // the result
    std::map<juce::Uuid, std::vector<std::vector<float>>> fixed;
};

static juce::String runFix (FixTry& t, AudioJob& job)
{
    const juce::int64 ctx = (juce::int64) std::llround (1.0 * t.rate);
    const juce::int64 c0 = juce::jmax ((juce::int64) 0, t.from - ctx), c1 = juce::jmin (t.takeLength > 0 ? t.takeLength : t.to + ctx, t.to + ctx);
    if (c1 <= c0 || t.to <= t.from) return "Nothing to fix there.";
    ChunkTimeline timeline;
    if (t.session) { timeline.setSession (t.sessionFolder); timeline.setWrittenTo (t.writtenTo); }
    const int n = (int) (c1 - c0);
    int done = 0;
    for (auto& tr : t.tracks)
    {
        if (job.cancelled()) return "Cancelled.";
        std::vector<std::vector<float>> buf ((size_t) tr.channels, std::vector<float> ((size_t) n, 0.0f));
        std::vector<float*> dest; for (auto& b : buf) dest.push_back (b.data());
        bool got = false;
        if (t.session && tr.sessionIndex >= 0 && timeline.valid()) got = timeline.read (tr.sessionIndex, t.sessionStart + c0, n, dest.data());
        if (! got && tr.file.existsAsFile())
        {
            RawWavReader r;
            if (r.open (tr.file)) got = r.read (c0, n, dest.data(), tr.channels) > 0;
        }
        if (! got) return "The audio of " + tr.name + " could not be read there (is it on the disk yet?).";
        auto spec = t.spec;
        if (spec.kind == FixSpec::Kind::Denoise)                     // the room tone learned from the quietest moments around the box, per channel
        {
            auto profiles = std::make_shared<std::vector<denoise::Profile>>();
            auto p = spec.dn; p.quietest = true;
            for (auto& ch : buf) profiles->push_back (denoise::learnProfile ({ ch }, t.rate, p));
            spec.dnProfiles = profiles;
        }
        audioops::applyFix (buf, t.rate, (long) (t.from - c0), (long) (t.to - c0), spec, (long) std::lround (0.004 * t.rate));
        std::vector<std::vector<float>> span;
        for (auto& ch : buf) span.emplace_back (ch.begin() + (std::ptrdiff_t) (t.from - c0), ch.begin() + (std::ptrdiff_t) (t.to - c0));
        t.fixed[tr.id] = std::move (span);
        job.setProgress ((float) ++done / (float) juce::jmax ((size_t) 1, t.tracks.size()));
    }
    return {};
}

/** The flags with their notes and replies, newest first - the session's, with a reply box for the one chosen, and those the project already has
    from earlier sessions (its take window's flag markers: shown, and gone to, but not replied to - a reply goes through the session). */
class FixerFlags final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FixerFlags (FixerApp& a) : app (a)
    {
        caption.setText ("THE PRIMARY'S FLAGS", juce::dontSendNotification); caption.setFont (juce::FontOptions (11.0f, juce::Font::bold)); caption.setColour (juce::Label::textColourId, theme::dimText);
        list.setModel (this); list.setRowHeight (46); list.setColour (juce::ListBox::backgroundColourId, theme::field);
        detail.setMultiLine (true); detail.setReadOnly (true); detail.setCaretVisible (false); detail.setFont (juce::FontOptions (12.5f));
        replyBox.setTextToShowWhenEmpty ("Type a reply for the Primary, then Enter", theme::dimText.withAlpha (0.6f));
        replyBox.onReturnKey = [this] { sendReply(); };
        send.onClick = [this] { sendReply(); };
        goThere.onClick = [this] { if (onGo != nullptr) if (auto* x = find (chosen)) onGo (chosen, x->ws); };
        goThere.setTooltip ("To the flag's place (Follow is unticked: the playhead is yours)");
        for (auto* c : std::initializer_list<juce::Component*> { &caption, &list, &detail, &replyBox, &send, &goThere }) addAndMakeVisible (c);
        send.setWantsKeyboardFocus (false); goThere.setWantsKeyboardFocus (false);
    }
    std::function<void (const juce::String& flagId, double windowSeconds)> onGo;      // (seconds < 0: not on this window's timeline)
    std::function<void (const juce::String& flagId)> onChoose;
    juce::String chosen;
    void choose (const juce::String& id)
    {
        chosen = id;
        for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i].f.id == id) list.selectRow (i, false, true);
        showDetail();
        if (onChoose) onChoose (id);
    }
    void focusReply() { replyBox.grabKeyboardFocus(); }
    void refresh()
    {
        std::vector<Shown> now;
        const auto L = app.layout();
        std::set<juce::String> have;
        for (auto& f : app.model.flags)
        {
            have.insert (f.id);
            if (! f.removed) now.push_back ({ f, L != nullptr ? L->windowSecondsOf (f.sample) : -1.0, false });
        }
        if (auto* w = app.pieceWindow())
            for (auto& m : w->markers)
                if (m.flag && have.count (m.id.toString()) == 0)
                {
                    Shown x; x.f.id = m.id.toString(); x.f.note = m.note; x.f.pinnedAt = m.pinnedAt; x.f.by = "Primary"; x.ws = m.seconds; x.fromProject = true;
                    for (auto& r : m.replies) x.f.replies.push_back ({ r.text, r.by, r.at });
                    now.push_back (x);
                }
        std::sort (now.begin(), now.end(), [] (const Shown& x, const Shown& y) { return x.f.pinnedAt > y.f.pinnedAt; });
        bool same = now.size() == shown.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].f.id == shown[i].f.id && now[i].f.note == shown[i].f.note && now[i].f.replies.size() == shown[i].f.replies.size() && std::abs (now[i].ws - shown[i].ws) < 1.0e-6;
        if (same) return;
        shown = now; list.updateContent(); list.repaint();
        for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i].f.id == chosen) list.selectRow (i, false, true);
        showDetail();
    }
    /** The chosen flag is the session's (so a reply, or a fix version made for it, reaches the Primary). */
    bool chosenIsLive() const { auto* x = find (chosen); return x != nullptr && ! x->fromProject; }
    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromTop (18));
        auto bottom = r.removeFromBottom (30);
        send.setBounds (bottom.removeFromRight (60)); bottom.removeFromRight (4);
        replyBox.setBounds (bottom.reduced (0, 2));
        r.removeFromBottom (4);
        auto mid = r.removeFromBottom (110);
        goThere.setBounds (mid.removeFromBottom (26).removeFromLeft (110));
        mid.removeFromBottom (4);
        detail.setBounds (mid);
        r.removeFromBottom (4);
        list.setBounds (r);
    }
private:
    struct Shown { FlagRec f; double ws = -1.0; bool fromProject = false; };
    const Shown* find (const juce::String& id) const { for (auto& x : shown) if (x.f.id == id) return &x; return nullptr; }
    void sendReply()
    {
        if (chosen.isEmpty()) { app.say ("Choose a flag first (click it in the list or on the timeline)"); return; }
        if (! chosenIsLive()) { app.say ("That flag is from an earlier session (it is in the project): a reply goes to a flag of the session that is running"); return; }
        app.reply (chosen, replyBox.getText());
        replyBox.clear();
    }
    void showDetail()
    {
        const auto* x = find (chosen);
        if (x == nullptr) { detail.setText (shown.empty() ? "No flags yet. The Primary pins one with Enter; it shows here at once." : "Click a flag to reply to it."); return; }
        const auto& f = x->f;
        juce::String t = "Pinned " + f.pinnedAt.formatted ("%H:%M:%S") + " by " + f.by + ":\n" + (f.note.isNotEmpty() ? f.note : juce::String ("(no note yet)"));
        for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
        if (x->fromProject) t << "\n\n(From an earlier session: it is in the project. Replies go to the flags of the session that is running.)";
        detail.setText (t);
    }
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& f = shown[(size_t) row].f;
        const double ws = shown[(size_t) row].ws;
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (shown[(size_t) row].fromProject ? juce::Colour (0xffb06a5c) : juce::Colour (0xffff5a3c)); g.fillRect (0, 0, 3, h);
        const double we = app.writtenEndSeconds();
        const bool onDisk = shown[(size_t) row].fromProject || (ws >= 0.0 && (we < 0.0 || ws < we));
        const double rate = app.model.sessionRate > 0.0 ? app.model.sessionRate : 48000.0;
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f));
        const juce::String where = ws < 0.0 ? "  - between takes (" + hmsF ((double) f.sample / rate, false) + " of the session)"
                                 : "  at " + hmsF (ws, false) + (onDisk ? juce::String() : juce::String ("  - its audio is on its way to the disk"));
        g.drawText (f.pinnedAt.formatted ("%H:%M:%S") + where, 8, 2, w - 10, 13, juce::Justification::centredLeft, true);
        g.setColour (theme::text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (f.note.isNotEmpty() ? f.note : juce::String ("(no note yet)"), 8, 15, w - 10, 15, juce::Justification::centredLeft, true);
        if (! f.replies.empty())
        {
            g.setColour (juce::Colour (0xff7ee08a)); g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb3 ")) + f.replies.back().by + ": " + f.replies.back().text, 8, 30, w - 10, 14, juce::Justification::centredLeft, true);
        }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) shown.size())) choose (shown[(size_t) row].f.id); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) shown.size()) && onGo) onGo (shown[(size_t) row].f.id, shown[(size_t) row].ws); }
    FixerApp& app;
    juce::Label caption;
    juce::ListBox list;
    juce::TextEditor detail, replyBox;
    juce::TextButton send { "Send" }, goThere { "Go to the flag" };
    std::vector<Shown> shown;
};

class FixerComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener, private juce::ListBoxModel
{
public:
    explicit FixerComponent (FixerApp& a) : app (a), flags (a)
    {
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &badge, &pieceLabel, &followBtn, &delayLabel, &delayBox, &backBtn, &playBtn, &fwdBtn, &playhead, &behind, &mixerBtn,
                                                                 &status, &flags, &view, &fixTitle, &selLabel, &fixKind, &amount, &amountLabel, &tryBtn, &abBtn, &saveBtn, &fixNote, &tellPrimary,
                                                                 &versions, &progressLabel })
            addAndMakeVisible (c);
        pieceLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold)); pieceLabel.setColour (juce::Label::textColourId, desk::pal().cream);
        followBtn.setToggleState (app.follow, juce::dontSendNotification);
        followBtn.setTooltip ("Follow Primary: the playhead stays a few seconds behind the audio on the disk and cannot be moved. Untick it to move about.");
        followBtn.onClick = [this] { app.setFollow (followBtn.getToggleState()); };
        delayLabel.setText ("seconds behind the disk:", juce::dontSendNotification); delayLabel.setFont (juce::FontOptions (12.0f)); delayLabel.setColour (juce::Label::textColourId, theme::dimText);
        delayBox.setText (juce::String (app.delaySeconds, 1), false); delayBox.setInputRestrictions (4, "0123456789.");
        delayBox.onReturnKey = [this] { app.delaySeconds = juce::jlimit (1.0, 30.0, delayBox.getText().getDoubleValue()); delayBox.setText (juce::String (app.delaySeconds, 1), false); grabKeyboardFocus(); };
        auto tool = [] (IconButton& b, Icon ic, const juce::String& name, const juce::String& tip) { b.setIcon (ic, name); b.setTooltip (tip); b.setWantsKeyboardFocus (false); };
        tool (backBtn, Icon::Rewind, "Back", "Back 5 seconds (Follow unticked)");
        tool (playBtn, Icon::Play, "Play / Stop", "Play / Stop [Space] through the Fixer's own mixer");
        tool (fwdBtn, Icon::Forward, "Forward", "Forward 5 seconds (Follow unticked)");
        backBtn.onClick = [this] { seek (app.playheadSeconds() - 5.0); };
        fwdBtn.onClick = [this] { seek (app.playheadSeconds() + 5.0); };
        playBtn.onClick = [this] { togglePlay(); };
        mixerBtn.onClick = [this] { openMixer(); };
        mixerBtn.setTooltip ("The Fixer's own mixer and the outputs it plays to");
        status.setFont (juce::FontOptions (13.0f)); status.setColour (juce::Label::textColourId, desk::pal().cream); status.setMinimumHorizontalScale (0.7f);
        // the timeline
        view.model = &app.model; view.project = &app.project; view.showArm = false; view.editableMarkers = false;
        view.canSeek = [this] { return ! app.follow; };
        view.onSeek = [this] (double s) { seek (s); };
        view.onSelect = [this] (double a, double b, const std::vector<juce::Uuid>& tr) { selA = a; selB = b; selTracks = tr; lastTry.reset(); app.setAudition (nullptr); refreshFixControls(); };
        view.onFlagClick = [this] (const juce::String& id, bool) { flags.choose (id); };
        view.colourOf = [this] (const juce::Uuid& id) { return app.hasProject() ? chan::of (app.project, id) : juce::Colour (0xff3f7fc4); };
        view.isMuteSolo = [this] (const juce::Uuid& id, bool solo) { auto it = app.mix.find (id.toString()); return it != app.mix.end() && (solo ? it->second.solo : it->second.mute); };
        view.toggleMuteSolo = [this] (const juce::Uuid& id, bool solo) { auto& m = app.mix[id.toString()]; if (solo) m.solo = ! m.solo; else m.mute = ! m.mute; app.mixChanged(); };
        flags.onGo = [this] (const juce::String& id, double seconds) { goToFlag (id, seconds); };
        flags.onChoose = [this] (const juce::String& id) { view.selectedFlag = id; view.repaint(); };
        // ReTouch
        fixTitle.setText ("RETOUCH (the fix is a new version: the recording is never changed)", juce::dontSendNotification);
        fixTitle.setFont (juce::FontOptions (11.0f, juce::Font::bold)); fixTitle.setColour (juce::Label::textColourId, theme::dimText);
        selLabel.setFont (juce::FontOptions (12.5f)); selLabel.setColour (juce::Label::textColourId, theme::text);
        fixKind.addItem ("De-click", 1); fixKind.addItem ("Repair: pull down what sticks out (a cough, a pencil, a click)", 2); fixKind.addItem ("Repair: rebuild from the sound around it", 3);
        fixKind.addItem ("Denoise (the room tone of the quietest moments around it)", 4); fixKind.addItem ("Gain", 5);
        fixKind.setSelectedId (2, juce::dontSendNotification);
        fixKind.onChange = [this] { lastTry.reset(); app.setAudition (nullptr); refreshFixControls(); };
        amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
        amount.onValueChange = [this] { lastTry.reset(); app.setAudition (nullptr); };
        amountLabel.setFont (juce::FontOptions (12.0f)); amountLabel.setColour (juce::Label::textColourId, theme::dimText);
        tryBtn.onClick = [this] { tryFix(); };
        tryBtn.setTooltip ("Works the fix out and plays the part with it (from a second before). Nothing is saved yet.");
        abBtn.setClickingTogglesState (true); abBtn.setToggleState (true, juce::dontSendNotification);
        abBtn.onClick = [this] { app.setAudition (abBtn.getToggleState() ? lastTry : nullptr); abBtn.setButtonText (abBtn.getToggleState() ? "Hearing: FIXED" : "Hearing: ORIGINAL"); };
        abBtn.setTooltip ("Switch between the fixed sound and the original while it plays");
        saveBtn.onClick = [this] { saveVersion(); };
        saveBtn.setTooltip ("Keeps the fix as a new version (its own files; the recording is not changed). fermata's review takes it from there.");
        fixNote.setTextToShowWhenEmpty ("a note for the review (optional)", theme::dimText.withAlpha (0.6f));
        tellPrimary.setToggleState (true, juce::dontSendNotification);
        tellPrimary.setTooltip ("Also tell the Primary (a reply to the chosen flag)");
        versions.setModel (this); versions.setRowHeight (20); versions.setColour (juce::ListBox::backgroundColourId, theme::field);
        progressLabel.setFont (juce::FontOptions (12.0f)); progressLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffd070));
        for (juce::Button* b : std::initializer_list<juce::Button*> { &followBtn, &tellPrimary, &mixerBtn, &tryBtn, &abBtn, &saveBtn })
            b->setWantsKeyboardFocus (false);                         // (a button with the keyboard presses itself on Enter or Space)
        refreshFixControls();
        app.addChangeListener (this);
        startTimerHz (30);
    }
    ~FixerComponent() override
    {
        app.removeChangeListener (this);
        if (job != nullptr) job->release();
        mixerWindow.reset();
    }
    void flagArrived (const FlagRec& f)
    {
        flags.refresh();
        // a flag just pinned lights the window and is chosen - once (its note and the replies that follow are the same flag), and not while a fix is
        // being tried or worked out: the version kept then, and the reply it sends, belong to the flag that was chosen for it
        const bool fresh = ! f.removed && seenFlags.insert (f.id).second && (juce::Time::getCurrentTime() - f.pinnedAt).inSeconds() < 15.0;
        if (! fresh) return;
        flashUntil = juce::Time::getMillisecondCounter() + 2500;
        const bool fixing = lastTry != nullptr || (job != nullptr && job->isRunning());
        if (! fixing) flags.choose (f.id);
        else app.say ("FLAG from the Primary" + (f.note.isNotEmpty() ? ": " + f.note : juce::String()) + " - not chosen, as a fix is being tried (click it in the list to answer it)");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (64);
        {
            auto left = top.removeFromLeft (260);
            badge.setBounds (left.removeFromTop (28));
            pieceLabel.setBounds (left.removeFromTop (26));
        }
        top.removeFromLeft (10);
        {
            auto col = top.removeFromLeft (250);
            followBtn.setBounds (col.removeFromTop (28));
            auto x = col.removeFromTop (26); delayLabel.setBounds (x.removeFromLeft (160)); delayBox.setBounds (x.removeFromLeft (50).reduced (0, 2));
        }
        top.removeFromLeft (10);
        { auto x = top.removeFromLeft (130).withSizeKeepingCentre (130, 36); backBtn.setBounds (x.removeFromLeft (40)); x.removeFromLeft (4); playBtn.setBounds (x.removeFromLeft (40)); x.removeFromLeft (4); fwdBtn.setBounds (x.removeFromLeft (40)); }
        top.removeFromLeft (10);
        playhead.setBounds (top.removeFromLeft (230).withHeight (58));
        top.removeFromLeft (8);
        behind.setBounds (top.removeFromLeft (170).withHeight (58));
        top.removeFromLeft (10);
        mixerBtn.setBounds (top.removeFromLeft (90).withSizeKeepingCentre (90, 30));
        r.removeFromTop (4);
        status.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto fix = r.removeFromBottom (150);
        {
            fixTitle.setBounds (fix.removeFromTop (18));
            auto right = fix.removeFromRight (juce::jmin (430, fix.getWidth() / 2));
            versions.setBounds (right.withTrimmedLeft (8));
            selLabel.setBounds (fix.removeFromTop (22));
            fixKind.setBounds (fix.removeFromTop (26).reduced (0, 1));
            { auto x = fix.removeFromTop (26); amountLabel.setBounds (x.removeFromLeft (150)); amount.setBounds (x); }
            fix.removeFromTop (4);
            { auto x = fix.removeFromTop (28); tryBtn.setBounds (x.removeFromLeft (110)); x.removeFromLeft (6); abBtn.setBounds (x.removeFromLeft (150)); x.removeFromLeft (6); saveBtn.setBounds (x.removeFromLeft (190)); x.removeFromLeft (6); progressLabel.setBounds (x); }
            fix.removeFromTop (4);
            { auto x = fix.removeFromTop (26); tellPrimary.setBounds (x.removeFromRight (150)); fixNote.setBounds (x.reduced (0, 1)); }
        }
        r.removeFromBottom (6);
        flags.setBounds (r.removeFromRight (juce::jmin (320, r.getWidth() / 4)).withTrimmedLeft (6));
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        if (juce::Time::getMillisecondCounter() < flashUntil)                  // a new flag: the window's edge lights for a moment
        {
            g.setColour (juce::Colour (0xffff5a3c).withAlpha (0.8f)); g.drawRect (getLocalBounds(), 4);
        }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (delayBox.hasKeyboardFocus (false) || fixNote.hasKeyboardFocus (false)) return false;
        if (k.getKeyCode() == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (k.getKeyCode() == juce::KeyPress::leftKey) { view.zoomBy (1.0 / 1.4, app.playheadSeconds()); return true; }
        if (k.getKeyCode() == juce::KeyPress::rightKey) { view.zoomBy (1.4, app.playheadSeconds()); return true; }
        if (k.getKeyCode() == juce::KeyPress::upKey) { view.laneH = juce::jmin (200, view.laneH + 8); view.repaint(); return true; }
        if (k.getKeyCode() == juce::KeyPress::downKey) { view.laneH = juce::jmax (22, view.laneH - 8); view.repaint(); return true; }
        if (k == juce::KeyPress ('f') || k == juce::KeyPress ('F')) { followBtn.setToggleState (! followBtn.getToggleState(), juce::sendNotificationSync); return true; }
        if (k.getKeyCode() == juce::KeyPress::returnKey) { flags.focusReply(); return true; }
        return false;
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        flags.refresh();
        versions.updateContent(); versions.repaint();
        if (followBtn.getToggleState() != app.follow) followBtn.setToggleState (app.follow, juce::dontSendNotification);
    }
    void timerCallback() override
    {
        const auto L = app.layout();
        view.layout = L;
        if (auto* w = app.pieceWindow()) { view.projectFlags.clear(); for (auto& m : w->markers) if (m.flag) view.projectFlags.push_back (m); }
        pieceLabel.setText (app.hasProject() ? app.project.name + "  -  " + app.pieceName() : juce::String ("No project yet"), juce::dontSendNotification);
        const double we = app.writtenEndSeconds(), live = app.liveEndSeconds();
        view.writtenEnd = we; view.liveEnd = live;
        view.playhead = app.playheadSeconds();
        playhead.set (hmsF (view.playhead));
        if (live >= 0.0) behind.set (juce::String (live - view.playhead, 1) + " s", (live - view.playhead) > app.delaySeconds + 5.0 ? juce::Colour (0xffffb050) : juce::Colours::white);
        else behind.set ("-");
        playBtn.setIcon (app.isPlaying() ? Icon::Stop : Icon::Play, "Play / Stop");
        for (auto* b : { &backBtn, &fwdBtn }) b->setEnabled (! app.follow);
        if (app.follow) { if (app.isPlaying()) view.keepInView (view.playhead, true); else if (we >= 0.0) view.keepInView (we, true); }
        else if (app.isPlaying()) view.keepInView (view.playhead, true);
        juce::String s;
        if (! app.model.connected) s = "Waiting for the recorder: start fermata:Live (the Primary) on this PC.";
        else if (! app.model.sessionOn) s = "No session running: the takes of the project are shown; the live recording appears when the Primary starts the session.";
        else if (app.model.recordingTake != 0) s = "Take " + juce::String (app.model.recordingTake) + " recording. " + (app.follow ? "Following " + juce::String (app.delaySeconds, 1) + " s behind the disk." : juce::String ("Not following (F follows again)."));
        else s = "The session is on, no take recording.";
        if (app.lastMessage.isNotEmpty()) s << "   |   " << app.lastMessage;
        status.setText (s, juce::dontSendNotification);
        if (job != nullptr && job->isRunning()) progressLabel.setText ("Working it out... " + juce::String ((int) std::lround (job->progress() * 100.0f)) + " %", juce::dontSendNotification);
        if (juce::Time::getMillisecondCounter() < flashUntil + 100) repaint();
        view.repaintIfChanged();
    }
    void seek (double s)
    {
        if (app.follow) { app.say ("Following the Primary: untick Follow to move the playhead"); return; }
        s = juce::jmax (0.0, s);
        app.cursorSeconds = s;
        if (app.isPlaying()) app.play (s);
    }
    void togglePlay()
    {
        if (app.isPlaying()) { app.stop(); if (app.follow) { app.setFollow (false); followBtn.setToggleState (false, juce::dontSendNotification); } return; }
        app.play (app.follow ? juce::jmax (0.0, app.writtenEndSeconds() - app.delaySeconds) : app.cursorSeconds);
    }
    void goToFlag (const juce::String& id, double s)
    {
        if (s < 0.0) { app.say ("That flag is not on this piece's timeline (it was pinned between takes)"); return; }
        app.setFollow (false); followBtn.setToggleState (false, juce::dontSendNotification);
        app.cursorSeconds = juce::jmax (0.0, s - 2.0);
        if (app.isPlaying()) app.stop();
        view.keepInView (s, false);
        flags.choose (id);
    }
    void openMixer()
    {
        if (mixerWindow != nullptr) { mixerWindow->toFront (true); return; }
        mixModel = std::make_unique<FixerMixModel> (app);
        juce::Component::SafePointer<FixerComponent> safe (this);
        mixerWindow = std::make_unique<ToolWindow> ("Mixer - the Fixer's own", [safe] (ToolWindow*) { if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->mixerWindow.reset(); }); });
        auto* v = new LiveMixerView (*mixModel);
        v->setSize (1000, 600);
        mixerWindow->setContentOwned (v, true);
        mixerWindow->forwardKeys = true;
        mixerWindow->centreWithSize (mixerWindow->getWidth(), mixerWindow->getHeight());
        mixerWindow->setVisible (true);
        if (auto* top = getTopLevelComponent()) makeOwnedBy (*mixerWindow, *top);
    }

    // ---- ReTouch
    const ShownTake* selectedTake() const
    {
        const auto L = app.layout();
        if (L == nullptr || selB <= selA) return nullptr;
        const auto* t = L->takeAt (selA + 1.0e-6);
        if (t == nullptr || selB > t->end() + 1.0e-6) return nullptr;
        return t;
    }
    void refreshFixControls()
    {
        const int k = fixKind.getSelectedId();
        if (k == 1) { amountLabel.setText ("Sensitivity (1 - 10)", juce::dontSendNotification); amount.setRange (1.0, 10.0, 0.5); amount.setValue (6.0, juce::dontSendNotification); }
        else if (k == 2 || k == 3) { amountLabel.setText ("Strength (%)", juce::dontSendNotification); amount.setRange (10.0, 100.0, 1.0); amount.setValue (100.0, juce::dontSendNotification); }
        else if (k == 4) { amountLabel.setText ("Amount (dB more / less)", juce::dontSendNotification); amount.setRange (-6.0, 6.0, 0.5); amount.setValue (0.0, juce::dontSendNotification); }
        else { amountLabel.setText ("Gain (dB)", juce::dontSendNotification); amount.setRange (-30.0, 12.0, 0.5); amount.setValue (-6.0, juce::dontSendNotification); }
        const auto* t = selectedTake();
        if (selB <= selA) selLabel.setText ("Drag over the tracks on the timeline to choose what to fix (Follow can stay on while you choose).", juce::dontSendNotification);
        else if (t == nullptr) selLabel.setText ("The part chosen must lie inside one take.", juce::dontSendNotification);
        else selLabel.setText ("Take " + juce::String (t->number) + ", " + hmsF (selA - t->start) + " - " + hmsF (selB - t->start) + " (" + juce::String (selB - selA, 2) + " s) on " + juce::String ((int) selTracks.size()) + " track(s)", juce::dontSendNotification);
        saveBtn.setEnabled (lastTry != nullptr);
        abBtn.setEnabled (lastTry != nullptr);
        if (lastTry == nullptr) { abBtn.setToggleState (false, juce::dontSendNotification); abBtn.setButtonText ("Fixed / original"); }   // (lit only while a try can be heard)
    }
    void tryFix()
    {
        const auto* t = selectedTake();
        const auto L = app.layout();
        if (t == nullptr || L == nullptr || selTracks.empty()) { app.say ("Choose a part of one take first (drag over the tracks)"); return; }
        const double we = app.writtenEndSeconds();
        if (t->recording && we >= 0.0 && selB > we) { app.say ("That part is not on the disk yet: wait a moment"); return; }
        auto fx = std::make_shared<FixTry>();
        fx->takeNumber = t->number; fx->rate = t->rate; fx->session = t->session; fx->sessionStart = t->sessionStart;
        fx->sessionFolder = L->sessionFolder; fx->writtenTo = app.model.writtenTo();
        fx->from = (juce::int64) std::llround ((selA - t->start) * t->rate); fx->to = (juce::int64) std::llround ((selB - t->start) * t->rate);
        fx->takeLength = t->recording ? (juce::int64) std::llround (((we >= 0.0 ? we : t->end()) - t->start) * t->rate) : t->lengthSamples;
        for (auto& id : selTracks)
            if (auto* lt = L->findTrack (id))
            {
                FixTry::Track tr; tr.id = id; tr.name = lt->name; tr.channels = lt->channels; tr.sessionIndex = lt->sessionIndex;
                for (auto& f : t->files) if (f.trackId == id) tr.file = f.file;
                fx->tracks.push_back (tr);
            }
        auto& spec = fx->spec;
        const double v = amount.getValue();
        switch (fixKind.getSelectedId())
        {
            case 1: spec.kind = FixSpec::Kind::Declick; spec.sensitivity = v; fx->what = "De-click (sensitivity " + juce::String (v, 1) + ")"; break;
            case 2: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Attenuate; spec.repair.strength = v / 100.0;
                    fx->what = "Repair, pulling down what sticks out (" + juce::String ((int) v) + " %)"; break;
            case 3: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Replace; spec.repair.strength = v / 100.0;
                    fx->what = "Repair, rebuilt from the sound around it (" + juce::String ((int) v) + " %)"; break;
            case 4: spec.kind = FixSpec::Kind::Denoise; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.dn.amountDb = v;
                    fx->what = "Denoise (" + juce::String (v, 1) + " dB)"; break;
            default: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Gain; spec.repair.gainDb = v;
                    fx->what = "Gain " + juce::String (v, 1) + " dB"; break;
        }
        if (job == nullptr) job = new AudioJob();
        tryBtn.setEnabled (false);
        juce::Component::SafePointer<FixerComponent> safe (this);
        const double from = selA;
        job->start ([fx] (AudioJob& j) { return runFix (*fx, j); }, [safe, fx, from] (const juce::String& err)
        {
            if (safe == nullptr) return;
            safe->tryBtn.setEnabled (true);
            safe->progressLabel.setText ({}, juce::dontSendNotification);
            if (err.isNotEmpty()) { safe->app.say ("ReTouch: " + err); return; }
            auto a = std::make_shared<Player::Audition>();
            a->takeNumber = fx->takeNumber; a->from = fx->from; a->tracks = fx->fixed;
            safe->lastTry = a; safe->lastFix = fx;
            safe->abBtn.setToggleState (true, juce::dontSendNotification); safe->abBtn.setButtonText ("Hearing: FIXED");
            safe->app.setAudition (a);
            if (safe->app.follow) { safe->app.setFollow (false); safe->followBtn.setToggleState (false, juce::dontSendNotification); }
            safe->app.play (juce::jmax (0.0, from - 1.0));
            safe->app.say ("Trying: " + fx->what + " - playing from a second before. Save it as a version, or change it and try again.");
            safe->refreshFixControls();
        });
    }
    void saveVersion()
    {
        if (lastFix == nullptr || lastTry == nullptr) return;
        auto* w = app.pieceWindow();
        if (w == nullptr || ! app.hasProject()) { app.say ("No project to keep the version in"); return; }
        // the session keeps the fix versions (every drive's index) and the Primary puts them into the project: with no session there is nowhere
        // safe for one - nothing is written, and the try stays to be kept once the Primary has started the session (Pre-take is enough)
        if (! app.model.sessionOn) { app.say ("A fix version is kept by the session: the Primary must start it first (Pre-take is enough). Nothing was saved - keep it again then."); return; }
        const auto& fx = *lastFix;
        // the attempt number: one more than the versions already made over this place of this take
        int n = 0; juce::String replaces;
        for (auto& v : versionRows())                                  // (the session's and the project's)
            if (v.take == fx.takeNumber && v.from < fx.to && v.to > fx.from && v.version > n) { n = v.version; replaces = v.id; }
        const auto folder = app.project.takeFolder (*w).getChildFile ("Fixes");
        folder.createDirectory();
        juce::Array<juce::var> files;
        for (auto& tr : fx.tracks)
        {
            auto it = fx.fixed.find (tr.id);
            if (it == fx.fixed.end()) continue;
            auto f = audioops::uniqueFile (folder.getChildFile (juce::String (fx.takeNumber).paddedLeft ('0', 3) + " fix " + juce::String (n + 1) + " at " + hmsF ((double) fx.from / fx.rate).replaceCharacter (':', '-') + " - " + sanitiseForFile (tr.name) + ".wav"));
            if (! audioops::writeWav (f, it->second, fx.rate)) { app.say ("The fix could not be written to " + f.getFullPathName()); return; }
            auto x = obj(); put (x, "track", tr.id.toString()); put (x, "trackName", tr.name); put (x, "file", f.getFullPathName()); put (x, "channels", tr.channels);
            files.add (x);
        }
        auto v = obj();
        put (v, "id", juce::Uuid().toString()); put (v, "takeNumber", fx.takeNumber); put (v, "from", fx.from); put (v, "to", fx.to); put (v, "rate", fx.rate);
        put (v, "files", files); put (v, "what", fx.what); put (v, "note", fixNote.getText().trim()); put (v, "by", "Fixer"); put (v, "madeAt", juce::Time::getCurrentTime().toMilliseconds());
        put (v, "version", n + 1); put (v, "window", w->name);
        if (replaces.isNotEmpty()) put (v, "replaces", replaces);
        if (flags.chosen.isNotEmpty()) put (v, "flag", flags.chosen);   // (a flag already in the project too: the review shows what it was for)
        app.announceFix (v);
        if (tellPrimary.getToggleState() && flags.chosenIsLive())
            app.reply (flags.chosen, "Fix version " + juce::String (n + 1) + " saved (take " + juce::String (fx.takeNumber) + ", " + hmsF ((double) fx.from / fx.rate, false) + "): " + fx.what
                                     + (fixNote.getText().trim().isNotEmpty() ? " - " + fixNote.getText().trim() : juce::String()));
        app.say ("Kept as fix version " + juce::String (n + 1) + " of take " + juce::String (fx.takeNumber) + " (" + juce::String (files.size()) + " file(s) in " + folder.getFileName() + "). The recording is unchanged.");
        fixNote.clear();
        lastTry.reset(); lastFix.reset(); app.setAudition (nullptr);
        refreshFixControls();
    }
    // the versions list: the session's fix versions, and those the project already has (made in an earlier session; fermata's review state)
    struct VersionRow { juce::String id; int take = 0; juce::int64 from = 0, to = 0; double rate = 48000.0; int version = 1; juce::String what; int state = -1; };
    std::vector<VersionRow> versionRows() const
    {
        std::vector<VersionRow> out;
        std::set<juce::String> have;
        for (auto& v : app.model.fixes)
        {
            VersionRow r; r.id = v["id"].toString(); r.take = (int) v["takeNumber"]; r.from = (juce::int64) v["from"]; r.to = (juce::int64) v["to"];
            r.rate = (double) v["rate"] > 0 ? (double) v["rate"] : 48000.0; r.version = juce::jmax (1, (int) v["version"]); r.what = v["what"].toString();
            have.insert (r.id); out.push_back (r);
        }
        if (auto* w = const_cast<FixerApp&> (app).pieceWindow())
            for (auto& v : w->fixVersions)
                if (have.count (v.id.toString()) == 0)
                {
                    VersionRow r; r.id = v.id.toString(); r.take = v.takeNumber; r.from = v.startSample; r.to = v.endSample; r.rate = v.sampleRate > 0 ? v.sampleRate : 48000.0;
                    r.version = v.versionNumber; r.what = v.what; r.state = v.state;
                    out.push_back (r);
                }
        std::sort (out.begin(), out.end(), [] (const VersionRow& a, const VersionRow& b) { return a.take != b.take ? a.take < b.take : (a.from != b.from ? a.from < b.from : a.version < b.version); });
        return out;
    }
    int getNumRows() override { return (int) versionRows().size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        const auto rows = versionRows();
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        const auto& v = rows[(size_t) row];
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (theme::text); g.setFont (juce::FontOptions (12.0f));
        const juce::String state = v.state == 1 ? "  (approved in the review)" : v.state == 2 ? "  (not used in the review)" : juce::String();
        g.drawText ("Take " + juce::String (v.take) + "  " + hmsF ((double) v.from / v.rate, false) + "  v" + juce::String (v.version) + "  " + v.what + state, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    FixerApp& app;
    LiveBadge badge;
    juce::Label pieceLabel, delayLabel, status, fixTitle, selLabel, amountLabel, progressLabel;
    juce::ToggleButton followBtn { "Follow Primary" }, tellPrimary { "Tell the Primary" };
    juce::TextEditor delayBox, fixNote;
    IconButton backBtn, playBtn, fwdBtn;
    FixerTimeBox playhead { "PLAYHEAD  H : M : S . MS", 30.0f }, behind { "BEHIND LIVE", 26.0f };
    juce::TextButton mixerBtn { "Mixer" }, tryBtn { "Try it" }, abBtn { "Hearing: FIXED" }, saveBtn { "Keep as a fix version" };
    juce::ComboBox fixKind;
    juce::Slider amount;
    FixerFlags flags;
    LiveTakeView view;
    juce::ListBox versions;
    double selA = -1.0, selB = -1.0;
    std::vector<juce::Uuid> selTracks;
    AudioJob* job = nullptr;
    std::shared_ptr<const Player::Audition> lastTry;
    std::shared_ptr<FixTry> lastFix;
    std::unique_ptr<FixerMixModel> mixModel;
    std::unique_ptr<ToolWindow> mixerWindow;
    juce::uint32 flashUntil = 0;
    std::set<juce::String> seenFlags;
};

FixerWindow::FixerWindow (FixerApp& app) : juce::DocumentWindow ("fermata:Live " + app.version + " - Fixer", theme::window, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    content = new FixerComponent (app);
    setContentOwned (content, true);
    setResizeLimits (900, 560, 6000, 4000);
    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    centreWithSize (juce::jmin (1500, area.getWidth() - 40), juce::jmin (900, area.getHeight() - 40));
    setVisible (true);
}
FixerWindow::~FixerWindow() = default;
void FixerWindow::flagArrived (const FlagRec& f) { if (content != nullptr) content->flagArrived (f); }
} // namespace td::live
