#pragma once
#include "AppContext.h"
#include "../core/VolumeAreas.h"
#include "../core/Feedback.h"
#include <limits>

namespace td
{
/** 3.43.0: the level fader of the "Change volume" box: -inf at the bottom to +36 dB (kVolumeChangeMaxDb) at the top. The lower four-fifths of its travel
    feel exactly as the whole fader did before (-inf to +12 dB, -20 dB half way up them); the top fifth goes on from +12 to +36 dB. */
inline juce::NormalisableRange<double> volumeFaderRange()
{
    constexpr double lo = -100.0, knee = 12.0, hi = (double) kVolumeChangeMaxDb, kneeAt = 0.8;
    const double skew = std::log (0.5) / std::log ((-20.0 - lo) / (knee - lo));
    auto from01 = [=] (double, double, double p)
    {
        p = juce::jlimit (0.0, 1.0, p);
        if (p >= kneeAt) return knee + (p - kneeAt) / (1.0 - kneeAt) * (hi - knee);
        const double q = p / kneeAt;
        return q <= 0.0 ? lo : lo + (knee - lo) * std::exp (std::log (q) / skew);
    };
    auto to01 = [=] (double, double, double v)
    {
        v = juce::jlimit (lo, hi, v);
        if (v >= knee) return kneeAt + (v - knee) / (hi - knee) * (1.0 - kneeAt);
        const double q = (v - lo) / (knee - lo);
        return q <= 0.0 ? 0.0 : kneeAt * std::pow (q, skew);
    };
    auto snap = [=] (double, double, double v) { return juce::jlimit (lo, hi, std::round (v * 10.0) / 10.0); };
    return juce::NormalisableRange<double> (lo, hi, from01, to01, snap);
}

/** The little mixer for changing the volume inside a grouped piece (like Pyramix's and SADiE's clip volume): one fader per file of the piece.
    The change starts at the point you right-clicked (or at the start of the piece), and can glide over 0.1 - 1.0 s instead of jumping. */
class VolumeChangePanel : public juce::Component
{
public:
    VolumeChangePanel (AppContext& a, const juce::Uuid& edit, const juce::Uuid& region, juce::int64 relSample, std::function<void()> close)
        : app (a), editId (edit), regionId (region), rel (relSample), closeFn (std::move (close))
    {
        auto* e = app.project.findEdit (editId);
        auto* r = e ? e->findAny (regionId) : nullptr;
        if (r == nullptr) { setSize (10, 10); return; }
        const double rate = juce::jmax (1.0, r->sampleRate);
        existing = false;
        const GainChange* ex = nullptr;
        for (auto& g : r->gains) if (std::abs (g.at - rel) <= (juce::int64) (0.001 * rate)) { existing = true; ex = &g; rel = g.at; initialRamp = g.ramp; }

        const int n = (int) r->files.size();
        for (int i = 0; i < n; ++i)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
            setupFader (*s);
            s->setValue (juce::jlimit (-100.0f, kVolumeChangeMaxDb, ex != nullptr && (size_t) i < ex->db.size() ? ex->db[(size_t) i] : r->levelDbAt ((size_t) i, rel)), juce::dontSendNotification);
            addAndMakeVisible (*s);
            auto l = std::make_unique<juce::Label>();
            l->setText (r->files[(size_t) i].trackName, juce::dontSendNotification);
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, chan::of (app.project, r->files[(size_t) i].trackId));
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            addAndMakeVisible (*l);
            faders.push_back (std::move (s)); labels.push_back (std::move (l));
        }
        allFader.reset (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
        setupFader (*allFader);
        allFader->setRange (-(double) kVolumeChangeMaxDb, (double) kVolumeChangeMaxDb, 0.1); allFader->setSkewFactor (1.0);     // (3.43.0: +/-36 dB, was 24)
        allFader->setValue (0.0, juce::dontSendNotification);
        allFader->setTooltip ("Moves every fader up or down together, keeping the balance between them (up to +36 dB)");
        allFader->onValueChange = [this]
        {
            const double d = allFader->getValue() - lastAll; lastAll = allFader->getValue();
            for (auto& f : faders) f->setValue (juce::jlimit (-100.0, (double) kVolumeChangeMaxDb, f->getValue() + d), juce::dontSendNotification);
        };
        addAndMakeVisible (*allFader);
        allLabel.setText ("All", juce::dontSendNotification); allLabel.setJustificationType (juce::Justification::centred);
        allLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold)); addAndMakeVisible (allLabel);

        title.setText ("Volume adjustment at " + formatTimeShort ((double) (r->startSample + rel) / rate, 2) + "  -  " + r->takeName, juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold)); addAndMakeVisible (title);

        fadeBox.setButtonText ("Fade the change");
        fadeBox.setTooltip ("Off: the level jumps at once. On: it glides over the time on the right (a short fade sounds more natural)");
        fadeBox.setToggleState (existing ? initialRamp > 0.0 : true, juce::dontSendNotification);
        addAndMakeVisible (fadeBox);
        for (int i = 1; i <= 10; ++i) fadeLen.addItem (juce::String (i / 10.0, 1) + " s", i);
        fadeLen.setSelectedId (juce::jlimit (1, 10, (int) std::lround ((existing && initialRamp > 0.0 ? initialRamp : 0.3) * 10.0)), juce::dontSendNotification);
        addAndMakeVisible (fadeLen);
        wholeBox.setButtonText ("Between fades: the whole take (from its start)");
        wholeBox.setTooltip ("Set the level for the whole piece, instead of starting the change at the point you clicked");
        wholeBox.setToggleState (false, juce::dontSendNotification);
        wholeBox.onClick = [this] { fadeBox.setEnabled (! wholeBox.getToggleState()); fadeLen.setEnabled (! wholeBox.getToggleState()); };
        if (rel <= 0) { wholeBox.setToggleState (true, juce::dontSendNotification); wholeBox.onClick(); }
        addAndMakeVisible (wholeBox);

        apply.setButtonText ("Apply"); cancel.setButtonText ("Cancel"); remove.setButtonText ("Remove this adjustment");
        remove.setEnabled (existing);
        apply.onClick  = [this] { doApply(); };
        cancel.onClick = [this] { if (closeFn) closeFn(); };
        remove.onClick = [this] { doRemove(); };
        for (auto* b : { &apply, &cancel, &remove }) addAndMakeVisible (b);
        apply.setColour (juce::TextButton::buttonColourId, theme::accent);

        setSize (juce::jmax (400, 70 * (n + 1) + 30), 420);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (20));
        auto bottom = r.removeFromBottom (32);
        const int bw = bottom.getWidth() / 3;
        apply.setBounds (bottom.removeFromLeft (bw).reduced (2)); cancel.setBounds (bottom.removeFromLeft (bw).reduced (2)); remove.setBounds (bottom.reduced (2));
        r.removeFromBottom (6);
        auto opts = r.removeFromBottom (54);
        auto row1 = opts.removeFromTop (26);
        fadeLen.setBounds (row1.removeFromRight (80).reduced (0, 2));
        fadeBox.setBounds (row1);
        wholeBox.setBounds (opts);
        r.removeFromBottom (4);
        const int n = (int) faders.size() + 1;
        const int w = r.getWidth() / juce::jmax (1, n);
        for (size_t i = 0; i < faders.size(); ++i)
        {
            auto col = r.removeFromLeft (w);
            labels[i]->setBounds (col.removeFromTop (18)); faders[i]->setBounds (col.reduced (6, 0));
        }
        allLabel.setBounds (r.removeFromTop (18)); allFader->setBounds (r.reduced (6, 0));
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

private:
    static void setupFader (juce::Slider& s)
    {
        s.setNormalisableRange (volumeFaderRange());              // 3.43.0: -inf to +36 dB (was +12), the part below +12 as before
        s.setTooltip ("This track's level from here: 0 = untouched, up to +36 dB (the top fifth of the fader is +12 to +36). Double-click: 0 dB. You can type a number in the box under it.");
        s.setDoubleClickReturnValue (true, 0.0);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 20);
        s.setTextValueSuffix (" dB");
        s.textFromValueFunction = [] (double v) { return v <= -99.9 ? juce::String ("-inf") : juce::String (v, 1); };
        s.valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -100.0 : t.getDoubleValue(); };
        s.setNumDecimalPlacesToDisplay (1);
        s.updateText();                                           // (3.43.0: the range has no step of its own now, so the box is written again with the function above)
    }

    void doApply()
    {
        auto* e = app.project.findEdit (editId);
        auto* r = e ? e->findAny (regionId) : nullptr;
        if (r == nullptr) { if (closeFn) closeFn(); return; }
        std::vector<float> db;
        for (auto& f : faders) db.push_back ((float) f->getValue());
        const bool whole = wholeBox.getToggleState();
        if (whole)                                                     // the whole piece: one level from its start, earlier changes in it are dropped
        {
            r->gains.clear();
            r->setGainChange (0, 0.0, db);
        }
        else r->setGainChange (rel, fadeBox.getToggleState() ? fadeLen.getSelectedId() / 10.0 : 0.0, db);
        app.project.changed();
        if (closeFn) closeFn();
    }

    void doRemove()
    {
        auto* e = app.project.findEdit (editId);
        auto* r = e ? e->findAny (regionId) : nullptr;
        if (r != nullptr && r->removeGainChangeAt (rel)) app.project.changed();
        if (closeFn) closeFn();
    }

    AppContext& app; juce::Uuid editId, regionId; juce::int64 rel; std::function<void()> closeFn;
    bool existing = false; double initialRamp = 0.0, lastAll = 0.0;
    std::vector<std::unique_ptr<juce::Slider>> faders; std::vector<std::unique_ptr<juce::Label>> labels;
    std::unique_ptr<juce::Slider> allFader; juce::Label allLabel, title;
    juce::ToggleButton fadeBox, wholeBox; juce::ComboBox fadeLen;
    juce::TextButton apply, cancel, remove;
};

/** Opens the volume mixer for a piece in its own small window. */
inline void showVolumeChange (AppContext& app, const juce::Uuid& editId, const juce::Uuid& regionId, juce::int64 relSample)
{
    struct Holder { juce::DialogWindow* w = nullptr; };
    auto holder = std::make_shared<Holder>();
    auto panel = std::make_unique<VolumeChangePanel> (app, editId, regionId, relSample, [holder]
    {
        if (holder->w) { holder->w->exitModalState (0); }
    });
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Change volume";
    o.content.setOwned (panel.release());
    o.componentToCentreAround = place::anchor ();
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false; o.useBottomRightCornerResizer = false;
    holder->w = o.launchAsync();
}

/** 3.50.0 (asked for on 10 November 2026): the one box for a volume change - a volume AREA of the edit (VolumeArea, Edit.h): so many dB for each
    track it is on, from a start to an end, with a fade in and a fade out. It says what the change does, and everything about it can be changed
    here: the amounts (a fader for each track: 0 = that track is left alone), where it starts and ends, and both fades. It is heard as it is
    changed (Play plays it, from 2 s before to 2 s after). While the box is open the undo history waits: Done is one undo step, Cancel leaves no
    trace.

    It is opened three ways (Start): for a new area ("from here to next edit", "between the 1/2 marks"); for an area that is there (one click on
    its teal triangle); and for a volume change made before 3.50.0, which was written into the pieces - that one is read as the area it amounts
    to (core/VolumeAreas.h), its old points are taken out of the pieces, and with Done it is an area from then on (Cancel puts it back as it was).

    It replaces the two boxes there were: the little mixer of "from here to next edit" (one glide, no end of its own, stopped at the join) and the
    box of "between the 1/2 marks" (two fades, but neither its start nor its end could be changed). */
class VolumeAreaPanel : public juce::Component, private juce::Timer
{
public:
    struct Start
    {
        juce::int64 from = 0, to = 0;            // a new area: where
        bool markedOnly = false;                 //   only the tracks marked with Ctrl + Alt + drag (between the 1/2 marks)
        juce::Uuid areaId = juce::Uuid::null();  // an area that is there
        bool old = false;                        // a change from before 3.50.0: by its group, or the piece and the place of a change on its own
        juce::String oldGroup; juce::Uuid oldRegion = juce::Uuid::null(); juce::int64 oldAt = 0;
    };

    VolumeAreaPanel (AppContext& a, const juce::Uuid& edit, const Start& st, std::function<void()> close) : app (a), editId (edit), closeFn (std::move (close))
    {
        auto* e = app.project.findEdit (editId);
        if (e == nullptr) { setSize (10, 10); return; }
        rate = juce::jmax (1.0, e->sampleRate);
        beforeRegions = e->regions; beforeAreas = e->areas;       // (Cancel puts these back)
        app.undoHold += 1;                                         // (one undo step for whatever is done in the box)
        begun = true;
        juce::String note;
        if (st.old)
        {
            volumeareas::LoneFound found;
            const bool ok = st.oldGroup.isNotEmpty() ? volumeareas::takeOutGroup (*e, st.oldGroup, area) : volumeareas::takeOutLoneChange (*e, st.oldRegion, st.oldAt, area, &found);
            if (ok && ! area.isEmpty())
            {
                e->areas.push_back (area); existing = true; app.project.changed();
                note = "This change was made before volume areas: it was written into the pieces. It is shown here as the area it amounts to";
                if (found.carriedInto > 0) note << " - the " << (found.carriedInto == 1 ? juce::String ("next piece") : "next " + juce::String (found.carriedInto) + " pieces") << " started at the same raise, so it runs on through " << (found.carriedInto == 1 ? "it" : "them");
                if (found.broughtBack) note << (found.carriedInto > 0 ? ", " : " - ") << "and a later change brought the level back: that is its fade out";
                note << ". Done keeps it as an area (its level now stays on top of each piece's own level, whatever that does); Cancel leaves the old change as it was.";
            }
            else area = VolumeArea();
        }
        else if (const auto* live = e->findArea (st.areaId)) { area = *live; existing = true; }
        else { area.id = juce::Uuid(); area.from = juce::jmax ((juce::int64) 0, st.from); area.to = juce::jmax (area.from, st.to); area.fadeIn = 0.3; area.fadeOut = st.markedOnly ? 0.3 : 0.0; }
        if (area.id.isNull()) area.id = juce::Uuid();
        // the tracks offered: every track with audio under the area (a new area between the marks: of those, the marked ones), and any the area is on already
        for (auto& t : app.project.tracks)
        {
            if (t.isLtc) continue;
            bool has = area.touches (t.id);
            if (! has && (! st.markedOnly || e->fixUses (t.id)))
                for (auto* rs : { &e->regions, &e->overdubs }) for (auto& r : *rs) if (r.endSample() > area.from && r.startSample < area.to) for (auto& f : r.files) if (f.trackId == t.id) has = true;
            if (has) tracks.push_back (t.id);
        }
        build (note);
    }

    ~VolumeAreaPanel() override
    {
        stopTimer();
        if (begun)
        {
            if (! done)                                            // Cancel, Escape or the window's close button: as it was before the box opened
                if (auto* e = app.project.findEdit (editId)) { e->regions = beforeRegions; e->areas = beforeAreas; app.project.changed(); }
            if (playing) app.stopPlayback();
            app.undoHold -= 1;
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (20));
        summary.setBounds (r.removeFromTop (36));
        if (info.getText().isNotEmpty()) info.setBounds (r.removeFromTop (58));
        auto bottom = r.removeFromBottom (32);                    // Done first, then Cancel (the "yes" on the left); Play and Remove at the other end
        done_.setBounds (bottom.removeFromLeft (100).reduced (2)); cancel.setBounds (bottom.removeFromLeft (100).reduced (2));
        play.setBounds (bottom.removeFromRight (90).reduced (2));
        if (remove.isVisible()) remove.setBounds (bottom.removeFromRight (160).reduced (2));
        r.removeFromBottom (6);
        auto times = r.removeFromBottom (26);
        auto field = [&times] (juce::Label& cap, int capW, juce::TextEditor& ed, int edW) { cap.setBounds (times.removeFromLeft (capW)); ed.setBounds (times.removeFromLeft (edW).reduced (0, 2)); times.removeFromLeft (12); };
        field (fromCap, 40, fromEd, 86); field (toCap, 24, toEd, 86); field (fadeInCap, 56, fadeInEd, 54); field (fadeOutCap, 62, fadeOutEd, 54);
        r.removeFromBottom (6);
        auto allCol = r.removeFromRight (kColW);
        allLabel.setBounds (allCol.removeFromTop (18)); allFader->setBounds (allCol.reduced (6, 0).withTrimmedBottom (14));
        viewport.setBounds (r);
        const int h = juce::jmax (100, viewport.getMaximumVisibleHeight());
        content.setSize (juce::jmax (r.getWidth(), (int) faders.size() * kColW), h);
        for (size_t i = 0; i < faders.size(); ++i)
        {
            auto col = juce::Rectangle<int> ((int) i * kColW, 0, kColW, h);
            labels[i]->setBounds (col.removeFromTop (18)); faders[i]->setBounds (col.reduced (6, 0));
        }
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

    /** Seconds as the box writes and reads them: "1:33.27", "93.27", "0:01:33.27". */
    static bool parseTimeText (const juce::String& text, double& seconds) { return feedback::parseTime (text.trim(), 25.0, seconds) && seconds >= 0.0; }

private:
    static constexpr int kColW = 74;
    static void setupFader (juce::Slider& s)
    {
        s.setRange (-100.0, (double) kVolumeChangeMaxDb, 0.1);
        s.setSkewFactorFromMidPoint (0.0);                      // 0 dB (the track is left alone) is in the middle of the fader: +36 at the top, -inf at the bottom
        s.setDoubleClickReturnValue (true, 0.0);
        s.setValue (0.0, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 20);
        s.setTextValueSuffix (" dB");
        s.textFromValueFunction = [] (double v) { return v <= -99.9 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
        s.valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -100.0 : t.getDoubleValue(); };
    }

    void build (const juce::String& note)
    {
        const int n = (int) tracks.size();
        for (int i = 0; i < n; ++i)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
            setupFader (*s);
            s->setTooltip ("How much this track is raised or lowered inside the area: 0 = it is left alone. Double-click: 0. You can type a number in the box under it.");
            const float d = area.dbOf (tracks[(size_t) i]);
            s->setValue (d <= VolumeArea::kOff ? -100.0 : (double) d, juce::dontSendNotification);
            s->onValueChange = [this] { pending = true; };
            content.addAndMakeVisible (*s);
            auto l = std::make_unique<juce::Label>();
            l->setText (app.project.nodeName (tracks[(size_t) i]), juce::dontSendNotification);
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, chan::of (app.project, tracks[(size_t) i]));
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            content.addAndMakeVisible (*l);
            faders.push_back (std::move (s)); labels.push_back (std::move (l));
        }
        allFader.reset (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
        setupFader (*allFader);
        allFader->setTooltip ("Moves every fader together");
        allFader->onValueChange = [this]
        {
            const double d = allFader->getValue() - lastAll; lastAll = allFader->getValue();
            for (auto& f : faders) f->setValue (juce::jlimit (-100.0, (double) kVolumeChangeMaxDb, f->getValue() <= -99.9 && d > 0 ? d : f->getValue() + d), juce::dontSendNotification);
            pending = true;
        };
        addAndMakeVisible (*allFader);
        allLabel.setText ("All", juce::dontSendNotification); allLabel.setJustificationType (juce::Justification::centred);
        allLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold)); addAndMakeVisible (allLabel);
        viewport.setViewedComponent (&content, false); viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        title.setText ("Volume adjustment", juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold)); addAndMakeVisible (title);
        summary.setFont (juce::FontOptions (12.5f)); summary.setJustificationType (juce::Justification::topLeft); summary.setMinimumHorizontalScale (1.0f); addAndMakeVisible (summary);
        info.setFont (juce::FontOptions (11.5f)); info.setColour (juce::Label::textColourId, theme::dimText); info.setJustificationType (juce::Justification::topLeft);
        info.setMinimumHorizontalScale (1.0f); info.setText (note, juce::dontSendNotification); addAndMakeVisible (info);
        auto cap = [this] (juce::Label& l, const char* text) { l.setText (text, juce::dontSendNotification); addAndMakeVisible (l); };
        cap (fromCap, "From"); cap (toCap, "To"); cap (fadeInCap, "Fade in"); cap (fadeOutCap, "Fade out");
        auto edit = [this] (juce::TextEditor& ed, const juce::String& text, const char* tip)
        {
            ed.setText (text, false); ed.setTooltip (tip); ed.setSelectAllWhenFocused (true);
            ed.onReturnKey = [this] { pending = true; }; ed.onFocusLost = [this] { pending = true; };
            addAndMakeVisible (ed);
        };
        edit (fromEd, formatTimeShort ((double) area.from / rate, 2), "Where the adjustment begins on the edit's timeline (its fade in starts here). Type a time: 1:33.27");
        edit (toEd, formatTimeShort ((double) area.to / rate, 2), "Where the adjustment ends (its fade out ends here). Type a time: 2:10.63");
        edit (fadeInEd, juce::String (area.fadeIn, 2), "Seconds the level takes to glide to the new value at the start (0: it jumps)");
        edit (fadeOutEd, juce::String (area.fadeOut, 2), "Seconds the level takes to glide back at the end (0: it jumps back)");
        play.setButtonText ("Play"); play.setTooltip ("Plays the adjustment, from 2 seconds before it to 2 seconds after it (press again to stop)");
        play.onClick = [this] { togglePlay(); };
        done_.setButtonText ("Done"); cancel.setButtonText ("Cancel"); remove.setButtonText ("Remove this adjustment");
        done_.onClick = [this] { applyNow(); done = true; if (closeFn) closeFn(); };
        cancel.onClick = [this] { if (closeFn) closeFn(); };
        remove.onClick = [this] { removeIt(); };
        done_.setColour (juce::TextButton::buttonColourId, theme::accent);
        for (auto* b : { &play, &done_, &cancel }) addAndMakeVisible (b);
        addChildComponent (remove); remove.setVisible (existing);
        content.setSize (juce::jmax (1, n) * kColW, 330);
        setSize (juce::jlimit (560, 1000, (n + 1) * kColW + 30), note.isNotEmpty() ? 600 : 540);
        describe();
        startTimerHz (10);                                       // (a fader being dragged is heard as it moves, ten times a second)
    }

    void timerCallback() override
    {
        if (pending) applyNow();
        if (playing && ! app.isPlaying()) { playing = false; play.setButtonText ("Play"); }      // (it got to the end, or was stopped elsewhere)
    }

    void togglePlay()
    {
        if (app.isPlaying()) { app.stopPlayback(); playing = false; play.setButtonText ("Play"); return; }
        applyNow();
        const auto err = app.playEdit (editId, juce::jmax (0.0, (double) area.from / rate - 2.0), (double) area.to / rate + 2.0);
        if (err.isNotEmpty()) { showError ("Play", err); return; }
        playing = true; play.setButtonText ("Stop");
    }

    /** A field that cannot be read is written again as it stood (and says so in the summary line until the next change). */
    void readFields()
    {
        badField.clear();
        double s = 0.0;
        juce::int64 from = area.from, to = area.to;
        if (parseTimeText (fromEd.getText(), s)) from = (juce::int64) std::llround (s * rate); else badField = "From";
        if (parseTimeText (toEd.getText(), s))   to = (juce::int64) std::llround (s * rate);   else badField = "To";
        if (to <= from + (juce::int64) (0.01 * rate)) { badField = "To (it must come after From)"; from = area.from; to = area.to; }
        area.from = from; area.to = to;
        auto secs = [this] (juce::TextEditor& ed, double& v, const char* name)
        {
            const auto t = ed.getText().trim().replaceCharacter (',', '.').upToFirstOccurrenceOf ("s", false, true).trim();
            if (t.isNotEmpty() && t.containsOnly ("0123456789.")) v = juce::jlimit (0.0, 600.0, t.getDoubleValue()); else badField = name;
        };
        secs (fadeInEd, area.fadeIn, "Fade in"); secs (fadeOutEd, area.fadeOut, "Fade out");
        if (! fromEd.hasKeyboardFocus (true)) fromEd.setText (formatTimeShort ((double) area.from / rate, 2), false);
        if (! toEd.hasKeyboardFocus (true))   toEd.setText (formatTimeShort ((double) area.to / rate, 2), false);
        if (! fadeInEd.hasKeyboardFocus (true))  fadeInEd.setText (juce::String (area.fadeIn, 2), false);
        if (! fadeOutEd.hasKeyboardFocus (true)) fadeOutEd.setText (juce::String (area.fadeOut, 2), false);
    }

    /** Brings the edit's area up to date with the faders, the times and the fades (making it the first time; an area with every fader at 0 is none). */
    void applyNow()
    {
        pending = false;
        auto* e = app.project.findEdit (editId);
        if (e == nullptr) return;
        readFields();
        for (size_t i = 0; i < faders.size(); ++i) area.setDb (tracks[i], faders[i]->getValue() <= -99.9 ? VolumeArea::kOff : (float) faders[i]->getValue());
        auto* live = e->findArea (area.id);
        if (area.isEmpty()) { if (live != nullptr) e->areas.erase (e->areas.begin() + (live - e->areas.data())); }
        else if (live != nullptr) *live = area;
        else e->areas.push_back (area);
        app.project.changed();
        describe();
    }

    /** What the change does, in words: the line under the title. */
    void describe()
    {
        juce::StringArray what;
        for (auto& t : tracks)
            if (area.touches (t))
            {
                const float d = area.dbOf (t);
                what.add (app.project.nodeName (t) + " " + (d <= VolumeArea::kOff ? juce::String ("off") : (d > 0.0f ? "+" : "") + juce::String (d, 1) + " dB"));
            }
        juce::String text;
        if (what.isEmpty()) text = "No track is changed yet: move a fader (0 = that track is left alone).";
        else
        {
            if (what.size() > 5) { const int more = what.size() - 4; what.removeRange (4, more); what.add ("and " + juce::String (more) + " more"); }
            text = what.joinIntoString (",  ") + ".";
        }
        int pieces = 0;
        if (auto* e = app.project.findEdit (editId)) for (auto& r : e->regions) if (r.endSample() > area.from && r.startSample < area.to) ++pieces;
        juce::int64 in = 0, out = 0; area.fades (rate, in, out);
        text << "\nFrom " << formatTimeShort ((double) area.from / rate, 2) << " to " << formatTimeShort ((double) area.to / rate, 2)
             << " (" << juce::String ((double) (area.to - area.from) / rate, 2) << " s), over " << pieces << (pieces == 1 ? " piece" : " pieces")
             << ".  Fade in " << juce::String ((double) in / rate, 2) << " s, fade out " << juce::String ((double) out / rate, 2) << " s.";
        if (badField.isNotEmpty()) text << "   (" << badField << " could not be read: it is as it was.)";
        summary.setText (text, juce::dontSendNotification);
    }

    void removeIt()
    {
        if (auto* e = app.project.findEdit (editId))
        {
            e->areas.erase (std::remove_if (e->areas.begin(), e->areas.end(), [this] (const VolumeArea& a) { return a.id == area.id; }), e->areas.end());
            app.project.changed();
        }
        done = true;
        if (closeFn) closeFn();
    }

    AppContext& app; juce::Uuid editId; double rate = 48000.0; std::function<void()> closeFn;
    VolumeArea area;                                           // the area as the box has it
    std::vector<EditRegion> beforeRegions; std::vector<VolumeArea> beforeAreas;
    bool begun = false, done = false, pending = false, playing = false, existing = false;
    juce::String badField;
    std::vector<juce::Uuid> tracks;
    std::vector<std::unique_ptr<juce::Slider>> faders; std::vector<std::unique_ptr<juce::Label>> labels;
    std::unique_ptr<juce::Slider> allFader; juce::Label allLabel, title, summary, info, fromCap, toCap, fadeInCap, fadeOutCap; double lastAll = 0.0;
    juce::Component content; juce::Viewport viewport;
    juce::TextEditor fromEd, toEd, fadeInEd, fadeOutEd; juce::TextButton play, done_, cancel, remove;
};

inline void launchVolumeArea (AppContext& app, const juce::Uuid& editId, const VolumeAreaPanel::Start& st, const juce::String& title)
{
    auto closer = std::make_shared<juce::Component::SafePointer<juce::Component>>();
    auto panel = std::make_unique<VolumeAreaPanel> (app, editId, st, [closer] { if (auto* c = closer->getComponent()) if (auto* w = c->findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); });
    *closer = panel.get();
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = title;
    o.content.setOwned (panel.release());
    o.componentToCentreAround = place::anchor ();
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false; o.useBottomRightCornerResizer = false;
    o.launchAsync();
}

/** A new volume area over [from, to) of the edit's timeline ("from here to next edit"; markedOnly: "between the 1/2 marks", the marked tracks). */
inline void showNewVolumeArea (AppContext& app, const juce::Uuid& editId, juce::int64 from, juce::int64 to, bool markedOnly)
{
    VolumeAreaPanel::Start st; st.from = from; st.to = to; st.markedOnly = markedOnly;
    launchVolumeArea (app, editId, st, "Change volume");
}
/** An area that is there, opened to be changed (one click on its triangle). */
inline void showVolumeArea (AppContext& app, const juce::Uuid& editId, const juce::Uuid& areaId)
{
    VolumeAreaPanel::Start st; st.areaId = areaId;
    launchVolumeArea (app, editId, st, "Change the volume adjustment");       // (3.51.0: was "Change the volume change" - the same word twice)
}
/** A volume change made before 3.50.0 (its group; or, for a change on its own, the piece and the place), opened as the area it amounts to. False -
    nothing opened - when it cannot be an area (a piece of the overdub layer, the level a piece starts with, a change that moves nothing):
    the caller opens the piece's little mixer for it, as before. */
inline bool showOldChangeAsArea (AppContext& app, const juce::Uuid& editId, const juce::Uuid& regionId, juce::int64 at, const juce::String& group)
{
    auto* e = app.project.findEdit (editId);
    if (e == nullptr) return false;
    {
        EditDef probe = *e; VolumeArea a;                        // (tried on a copy: the box takes it out of the real edit when it opens)
        const bool ok = group.isNotEmpty() ? volumeareas::takeOutGroup (probe, group, a) : volumeareas::takeOutLoneChange (probe, regionId, at, a);
        if (! ok || a.isEmpty()) return false;
    }
    VolumeAreaPanel::Start st; st.old = true; st.oldGroup = group; st.oldRegion = regionId; st.oldAt = at;
    launchVolumeArea (app, editId, st, "Change the volume adjustment");       // (3.51.0: was "Change the volume change" - the same word twice)
    return true;
}
} // namespace td
