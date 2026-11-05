#pragma once
// The pitch, pitch-curve and repair windows of the fix tools (3.18.6: from FixTools.cpp). Only FixToolsEdits.cpp includes this.
#include "FixToolsCommon.h"
#include "../core/Tuning.h"

namespace td { namespace fixtools
{
// ======================================================================================================== the pitch dialog
struct PitchContext
{
    bool allowPad = false;
    juce::String title, what;
    std::function<juce::String (double cents, double padSeconds, AudioJob&)> work;     // job thread
    std::function<void()> finish;                                                       // message thread, after success
};

class PitchDialog : public JobDialogBase
{
public:
    explicit PitchDialog (PitchContext c) : ctx (std::move (c))
    {
        setSize (470, ctx.allowPad ? 236 : 204);
        intro.setText (ctx.what, juce::dontSendNotification); intro.setJustificationType (juce::Justification::topLeft); intro.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (intro);
        for (auto* l : { &semiCap, &centCap, &padCap, &total }) { addAndMakeVisible (l); l->setColour (juce::Label::textColourId, theme::text); }
        semiCap.setText ("Semitones", juce::dontSendNotification); centCap.setText ("Cents", juce::dontSendNotification);
        padCap.setText ("Process extra, each end", juce::dontSendNotification);
        semi.setSliderStyle (juce::Slider::LinearHorizontal); semi.setRange (-12, 12, 1); semi.setValue (0, juce::dontSendNotification);
        cents.setSliderStyle (juce::Slider::LinearHorizontal); cents.setRange (-100, 100, 1); cents.setValue (0, juce::dontSendNotification);
        for (auto* s : { &semi, &cents }) { s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22); s->setScrollWheelEnabled (false); s->setDoubleClickReturnValue (true, 0.0); s->onValueChange = [this] { updateTotal(); }; addAndMakeVisible (s); }
        semi.setTooltip ("Whole semitones, -12 to +12 (double-click for 0)");
        cents.setTooltip ("Hundredths of a semitone: +100 cents is one semitone up. Use this for a bit that went slightly flat or sharp (double-click for 0)");
        for (int i = 0; i <= 9; ++i) pad.addItem (i == 0 ? juce::String ("none") : juce::String (i) + " s", i + 1);
        pad.setSelectedId (1, juce::dontSendNotification); pad.setTooltip ("Also shifts this much extra audio before and after the marked part, so you can move the edit point later and still be in the corrected audio");
        addChildComponent (padCap); addChildComponent (pad); padCap.setVisible (ctx.allowPad); pad.setVisible (ctx.allowPad);
        apply.onClick = [this] { start(); }; cancel.onClick = [this] { if (running) job.requestCancel(); else closeDialogOf (this); };
        addAndMakeVisible (apply); addAndMakeVisible (cancel);
        apply.setColour (juce::TextButton::buttonColourId, theme::accent);
        updateTotal();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        { auto t = r.removeFromTop (22); intro.placeRightOf (t); } r.removeFromTop (4);
        auto row = [&] (juce::Component& a, juce::Component& b) { auto x = r.removeFromTop (28); a.setBounds (x.removeFromLeft (150)); b.setBounds (x); r.removeFromTop (4); };
        row (semiCap, semi); row (centCap, cents);
        if (ctx.allowPad) { auto x = r.removeFromTop (28); padCap.setBounds (x.removeFromLeft (170)); pad.setBounds (x.removeFromLeft (100)); r.removeFromTop (4); }
        total.setBounds (r.removeFromTop (24));
        auto bottom = r.removeFromBottom (30);
        cancel.setBounds (bottom.removeFromRight (100)); bottom.removeFromRight (8); apply.setBounds (bottom.removeFromRight (140));
        auto s = r.removeFromBottom (24); bar.setBounds (s.withTrimmedRight (0)); status.setBounds (r.removeFromBottom (22));
    }
private:
    double totalCents() const { return semi.getValue() * 100.0 + cents.getValue(); }
    void updateTotal()
    {
        const double c = totalCents();
        total.setText ("Total: " + juce::String (c >= 0 ? "+" : "") + juce::String ((int) std::round (c)) + " cents  ("
                       + juce::String (c >= 0 ? "up" : "down") + " " + juce::String (std::abs (c) / 100.0, 2) + " semitones)", juce::dontSendNotification);
        apply.setEnabled (std::abs (c) >= 1.0 && ! running);
    }
    void start()
    {
        const double c = totalCents(), padSec = (double) (pad.getSelectedId() - 1);
        if (std::abs (c) < 1.0) return;
        apply.setEnabled (false); status.setText ("Working...", juce::dontSendNotification);
        auto work = ctx.work; auto fin = ctx.finish;
        juce::Component::SafePointer<PitchDialog> self (this);
        runJob ([work, c, padSec] (AudioJob& j) { return work (c, padSec, j); },
                [self, fin] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isEmpty()) { if (fin) fin(); closeDialogOf (self.getComponent()); }
                    else { self->status.setText (err, juce::dontSendNotification); self->updateTotal(); }
                });
    }
    PitchContext ctx;
    InfoNote intro; juce::Label semiCap, centCap, padCap, total;
    juce::Slider semi, cents; juce::ComboBox pad; juce::TextButton apply { "Correct pitch" }, cancel { "Cancel" };
};

// ======================================================================================================== the pitch-curve dialog
struct CurveContext
{
    bool allowPad = false;
    juce::String what;
    double rate = 48000.0;
    juce::int64 length = 0;                                                                // samples in the marked part
    std::function<std::vector<std::vector<float>> (AudioJob&)> load;                        // job thread: the marked part as one (mixed) vector, for the picture behind the line
    std::function<juce::String (const FixSpec&, double padSeconds, AudioJob&)> prepare;     // job thread: makes the corrected files (nothing is changed yet)
    std::function<bool()> apply;                                                            // message thread: puts the prepared files in place, for listening
    std::function<void()> stop, revert, accept;                                             // message thread
    std::function<void (double fromSec, double toSec, bool loop)> play;                     // message thread: plays from 'fromSec' to 'toSec' (seconds into the marked part), looped or not: nothing either side
    std::function<void()> unapply;                                                          // message thread: puts the original files back but keeps the corrected ones, to switch between the two
    std::function<void (bool)> setLooping;                                                  // message thread: loop switched on / off while playing
    std::function<double()> position;                                                       // message thread: seconds from the start of the marked part, or < 0 when this is not playing
    std::function<void (bool)> hold;                                                        // message thread: true while the dialog is open (see AppContext::undoHold)
    std::function<void (std::function<bool()>)> pairWritten;                                // one stream window of an OUT / IN pair: Accept only makes this stream ready (the pair puts both into the edit together)
};

/** The line the user draws: time across, pitch up and down (+100 cents at the top, -100 at the bottom, 0 in the middle). */
class CurveView : public juce::Component
{
public:
    PitchCurve curve;
    std::function<void()> onChanged;
    std::function<void (const juce::String&)> onInfo;

    CurveView() { setWantsKeyboardFocus (true); }
    void setDuration (double s) { duration = juce::jmax (0.01, s); vt0 = 0.0; vt1 = duration; repaint(); }
    // ---- the view: the part of the time that is shown (this is what Play plays) and how many cents are shown up and down
    double visT0() const { return vt0; }
    double visT1() const { return juce::jmin (duration, vt1); }
    void resetView() { vt0 = 0.0; vt1 = duration; kc = PitchCurve::kRange; repaint(); }
    void zoomTime (double factor)
    {
        const double span = juce::jlimit (juce::jmin (0.05, duration), duration, (vt1 - vt0) * factor);
        // the zoom keeps the playhead where it is on the screen: the one running across the picture, or else the one you placed by clicking the ruler;
        // with neither in view it zooms around the middle
        const double at = (playT >= vt0 && playT <= vt1) ? playT : (cueT >= vt0 && cueT <= vt1) ? cueT : 0.5 * (vt0 + vt1);
        const double frac = (vt1 - vt0) > 0.0 ? (at - vt0) / (vt1 - vt0) : 0.5;
        setWindow (at - frac * span, span);
    }
    void zoomCents (double factor) { kc = juce::jlimit (5.0, (double) PitchCurve::kRange, kc * factor); repaint(); }
    /** Left / right = zoom out / in in time (right in), up / down = zoom out / in vertically (down in). */
    bool zoomKey (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::rightKey) { zoomTime (0.7); return true; }
        if (k == juce::KeyPress::leftKey)  { zoomTime (1.0 / 0.7); return true; }
        if (k == juce::KeyPress::downKey)  { zoomCents (0.7); return true; }
        if (k == juce::KeyPress::upKey)    { zoomCents (1.0 / 0.7); return true; }
        return false;
    }
    void setEnvelope (std::vector<float> e)
    {
        float mx = 1.0e-6f; for (auto v : e) mx = juce::jmax (mx, v);
        for (auto& v : e) v /= mx;
        env = std::move (e); repaint();
    }
    int count() const { return (int) curve.pts.size(); }
    /** The play position (seconds from the start of the marked part); < 0 hides it. */
    void setPlayhead (double t) { if (std::abs (t - playT) > 1.0e-6) { playT = t; repaint(); } }
    /** The playhead you place by clicking the time ruler: where Play starts, and the point the zoom keys keep still. */
    double playFrom() const { return cueT >= vt0 && cueT < vt1 ? cueT : vt0; }
    void clearPoints() { curve.pts.clear(); selected = -1; edited(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::wellDark);
        const auto pl = plot();
        g.setColour (theme::wave); g.fillRect (pl);
        // what is there to correct: the sound itself, faintly
        if (! env.empty())
        {
            const float mid = (float) pl.getCentreY(), half = (float) pl.getHeight() * 0.5f * 0.9f;
            juce::Path p;
            const int n = (int) env.size();
            for (int x = 0; x < pl.getWidth(); ++x)
            {
                const double tt = vt0 + (double) x / (double) juce::jmax (1, pl.getWidth()) * (vt1 - vt0);
                const float v = env[(size_t) juce::jlimit (0, n - 1, (int) (tt / duration * (double) n))];
                p.addRectangle ((float) (pl.getX() + x), mid - v * half * 0.5f, 1.0f, juce::jmax (1.0f, v * half));
            }
            g.setColour (theme::dimText.withAlpha (0.28f)); g.fillPath (p);
        }
        g.setFont (juce::FontOptions (11.0f));
        for (double c : { 100.0, 50.0, 25.0, 0.0, -25.0, -50.0, -100.0 })
        {
            if (std::abs (c) > kc + 1.0e-9) continue;
            const int y = yOf (c);
            const bool major = c == 100.0 || c == 0.0 || c == -100.0;
            g.setColour (theme::grid.withAlpha (c == 0.0 ? 1.0f : major ? 0.8f : 0.45f)); g.drawHorizontalLine (y, (float) pl.getX(), (float) pl.getRight());
            g.setColour (theme::dimText); g.drawText ((c > 0 ? "+" : "") + juce::String ((int) c), 0, y - 7, pl.getX() - 6, 14, juce::Justification::right);
        }
        g.setColour (theme::dimText); g.drawText ("cents", 0, pl.getY() - 2, pl.getX() - 6, 12, juce::Justification::right);
        {   // the time ruler above the line: drag it to move the view left and right, double-click to see it all
            const juce::Rectangle<int> ruler (pl.getX(), 0, pl.getWidth(), pl.getY() - 2);
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (ruler);
            const double span = vt1 - vt0;
            static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
            double step = 600.0; for (double st : steps) if (st / span * pl.getWidth() >= 70.0) { step = st; break; }
            for (double t = std::ceil (vt0 / step - 1.0e-9) * step; t <= vt1 + 1.0e-9; t += step)
            {
                const int x = xOf (t);
                g.setColour (theme::grid.withAlpha (0.4f)); g.drawVerticalLine (x, (float) ruler.getY() + 12.0f, (float) pl.getBottom());
                g.setColour (theme::dimText); g.drawText (juce::String (t, step < 0.1 ? 2 : step < 1 ? 1 : 0) + " s", x + 2, ruler.getY(), 60, ruler.getHeight(), juce::Justification::centredLeft);
            }
        }
        juce::Graphics::ScopedSaveState clipToPlot (g);
        g.reduceClipRegion (pl);
        // the line: level (dashed) when nothing is drawn, otherwise the points joined, held level before the first and after the last
        juce::Path line;
        if (curve.pts.empty())
        {
            juce::Path flat; flat.startNewSubPath ((float) pl.getX(), (float) yOf (0)); flat.lineTo ((float) pl.getRight(), (float) yOf (0));
            const float dash[] = { 6.0f, 5.0f }; juce::PathStrokeType (2.0f).createDashedStroke (line, flat, dash, 2);
            g.setColour (theme::accent); g.fillPath (line);
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Click to add a point (the first sits at 0 cents, later ones where you click).  Drag points to shape the pitch line.  Double-click (or Delete) removes a point.", pl.reduced (10, 0).withHeight (pl.getHeight() / 2 - 14), juce::Justification::centred);
        }
        else
        {
            line.startNewSubPath ((float) pl.getX(), (float) yOf (curve.pts.front().cents));
            for (auto& p : curve.pts) line.lineTo ((float) xOf (p.t), (float) yOf (p.cents));
            line.lineTo ((float) pl.getRight(), (float) yOf (curve.pts.back().cents));
            g.setColour (theme::accent); g.strokePath (line, juce::PathStrokeType (2.2f));
            for (int i = 0; i < count(); ++i)
            {
                const auto c = juce::Point<float> ((float) xOf (curve.pts[(size_t) i].t), (float) yOf (curve.pts[(size_t) i].cents));
                const bool sel = i == selected;
                const float r = sel ? 7.0f : 5.5f;
                g.setColour (sel ? juce::Colour (0xffffd24a) : theme::text); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
                g.setColour (theme::wellDark); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.2f);
            }
        }
        if (playT < 0.0 && cueT >= vt0 && cueT <= vt1)         // the playhead you placed on the ruler
        {
            g.setColour (theme::playhead); g.drawVerticalLine (xOf (cueT), (float) pl.getY(), (float) pl.getBottom());
        }
        if (playT >= 0.0)                                  // the playhead runs across the line, with a dot on the line itself
        {
            const double t = juce::jlimit (0.0, duration, playT);
            const int x = xOf (t), y = yOf (curve.centsAt (t));
            g.setColour (theme::playhead); g.drawVerticalLine (x, (float) pl.getY(), (float) pl.getBottom());
            g.fillEllipse ((float) x - 5.0f, (float) y - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::wellDark); g.drawEllipse ((float) x - 5.0f, (float) y - 5.0f, 10.0f, 10.0f, 1.2f);
        }
        g.setColour (theme::border); g.drawRect (pl);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = hit (e.getPosition());
        if (i >= 0) say (curve.pts[(size_t) i]);
        else if (plot().contains (e.getPosition())) say ({ toT (e.x), curve.centsAt (toT (e.x)) }, true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.y < plot().getY() - 2 && e.x >= plot().getX()) { panning = true; panT0 = vt0; return; }          // the ruler: click to put the playhead there, drag to move the view
        int i = hit (e.getPosition());
        if (e.mods.isPopupMenu()) { if (i >= 0) removeAt (i); return; }
        if (i < 0)
        {
            if (! plot().contains (e.getPosition())) return;
            PitchCurve::Point p { juce::jlimit (0.0, duration, toT (e.x)), curve.pts.empty() ? 0.0 : snapC (toC (e.y)) };          // the first point anchors at 0 cents; every later one goes where it is clicked (snapping to 0 when close)
            curve.pts.push_back (p); curve.sortPoints();
            for (int k = 0; k < count(); ++k) if (curve.pts[(size_t) k].t == p.t && curve.pts[(size_t) k].cents == p.cents) i = k;
            edited();
        }
        selected = i; dragging = true; repaint();
        if (i >= 0) say (curve.pts[(size_t) i]);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (panning) { setWindow (panT0 - (double) e.getDistanceFromDragStartX() / (double) juce::jmax (1, plot().getWidth()) * (vt1 - vt0), vt1 - vt0); return; }
        if (! dragging || selected < 0 || selected >= count()) return;
        const double lo = selected > 0 ? curve.pts[(size_t) selected - 1].t + 0.002 : 0.0;
        const double hi = selected + 1 < count() ? curve.pts[(size_t) selected + 1].t - 0.002 : duration;
        auto& p = curve.pts[(size_t) selected];
        p.t = juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), toT (e.x)); p.cents = snapC (toC (e.y));       // whole cents; near the middle line it snaps to exactly 0
        say (p); edited();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (panning && e.getDistanceFromDragStart() < 4) { cueT = toT (e.x); repaint(); }                  // a click on the ruler (not a drag) places the playhead
        dragging = false; panning = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (e.y < plot().getY() - 2) { resetView(); return; } const int i = hit (e.getPosition()); if (i >= 0) removeAt (i); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selected >= 0 && selected < count()) { removeAt (selected); return true; }
        return zoomKey (k);
    }
private:
    juce::Rectangle<int> plot() const { return getLocalBounds().withTrimmedLeft (54).withTrimmedBottom (8).withTrimmedTop (26).withTrimmedRight (10); }
    int xOf (double t) const { const auto p = plot(); return p.getX() + (int) std::lround ((t - vt0) / (vt1 - vt0) * (double) p.getWidth()); }
    int yOf (double c) const { const auto p = plot(); return p.getCentreY() - (int) std::lround (c / kc * (double) p.getHeight() * 0.5); }
    double toT (int x) const { const auto p = plot(); return juce::jlimit (0.0, duration, vt0 + (double) (x - p.getX()) / (double) juce::jmax (1, p.getWidth()) * (vt1 - vt0)); }
    double toC (int y) const { const auto p = plot(); return juce::jlimit (-PitchCurve::kRange, PitchCurve::kRange, (double) (p.getCentreY() - y) / (double) juce::jmax (1, p.getHeight()) * 2.0 * kc); }
    /** Whole cents; within 1 cent, or within 7 pixels, of the middle line it snaps to exactly 0 (so you are not fighting to hit it, however far you have zoomed). */
    double snapC (double c) const { const double r = std::round (c); return std::abs (r) <= 1.0 || std::abs (yOf (c) - yOf (0.0)) <= 7 ? 0.0 : r; }
    int hit (juce::Point<int> pos) const
    {
        int best = -1; double bd = 11.0 * 11.0;
        for (int i = 0; i < count(); ++i)
        {
            const double dx = xOf (curve.pts[(size_t) i].t) - pos.x, dy = yOf (curve.pts[(size_t) i].cents) - pos.y, d = dx * dx + dy * dy;
            if (d <= bd) { bd = d; best = i; }
        }
        return best;
    }
    void removeAt (int i) { curve.pts.erase (curve.pts.begin() + i); selected = -1; edited(); }
    void edited() { repaint(); if (onChanged) onChanged(); }
    void say (const PitchCurve::Point& p, bool lineOnly = false)
    {
        if (onInfo) onInfo ((lineOnly ? "The line at " : "Point at ") + juce::String (p.t, 2) + " s:  " + juce::String (p.cents >= 0 ? "+" : "") + juce::String (p.cents, lineOnly ? 1 : 0) + " cents");
    }
    void setWindow (double t0, double span)
    {
        span = juce::jlimit (juce::jmin (0.05, duration), duration, span);
        vt0 = juce::jlimit (0.0, juce::jmax (0.0, duration - span), t0); vt1 = vt0 + span; repaint();
    }
    double cueT = -1.0, duration = 1.0, vt0 = 0.0, vt1 = 1.0, kc = PitchCurve::kRange, panT0 = 0.0; std::vector<float> env; int selected = -1; bool dragging = false, panning = false; double playT = -1.0;
};

class CurveDialog : public JobDialogBase
{
public:
    explicit CurveDialog (CurveContext c) : ctx (std::move (c))
    {
        setSize (1000, 560);
        if (ctx.hold) ctx.hold (true);
        intro.setText (ctx.what, juce::dontSendNotification); intro.setColour (juce::Label::textColourId, theme::text); intro.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (intro); addAndMakeVisible (view); addAndMakeVisible (info);
        info.setColour (juce::Label::textColourId, theme::dimText);
        view.setDuration ((double) ctx.length / ctx.rate);
        view.onInfo = [this] (const juce::String& t) { info.setText (t, juce::dontSendNotification); };
        view.onChanged = [this] { lineChanged(); };
        for (int i = 0; i <= 9; ++i) pad.addItem (i == 0 ? juce::String ("none") : juce::String (i) + " s", i + 1);
        pad.setSelectedId (1, juce::dontSendNotification);
        pad.setTooltip ("Also corrects this much extra audio before and after the marked part (the line stays level there), so you can move the edit points outward later");
        pad.onChange = [this] { lineChanged(); };
        padCap.setText ("Process extra, each end", juce::dontSendNotification); padCap.setColour (juce::Label::textColourId, theme::text);
        addChildComponent (pad); addChildComponent (padCap); pad.setVisible (ctx.allowPad); padCap.setVisible (ctx.allowPad);
        for (auto* b : { &auditionBtn, &playBtn, &stopBtn, &revertBtn, &acceptBtn, &closeBtn, &resetBtn }) addAndMakeVisible (b);
        addAndMakeVisible (loopBtn); addAndMakeVisible (hearToggle);
        playBtn.setTooltip ("Plays the part you can see (left / right arrows zoom in and out in time, down / up zoom in / out in cents; click the ruler to place the playhead, drag it to move; R = Preview) with the sound as it is now: the original until you Preview, then the corrected sound if 'Hear corrected' is lit. With Loop lit it goes round and round. Key: Space");
        hearToggle.setTooltip ("Lit: you hear the CORRECTED sound. Off: you hear the ORIGINAL. Click it while playing to switch at once and hear the difference, without touching the line. (Available after Preview.)");
        hearToggle.onClick = [this] { hearChanged(); };
        loopBtn.setTooltip ("Repeat the marked part over and over until you press Stop");
        loopBtn.onClick = [this] { const bool on = loopBtn.getToggleState(); if (! playing()) return; if (on) { if (ctx.play) ctx.play (view.visT0(), view.visT1(), true); } else if (ctx.setLooping) ctx.setLooping (false); };
        playBtn.onClick = [this] { if (ctx.stop) ctx.stop(); if (ctx.play) ctx.play (view.playFrom(), view.visT1(), loopBtn.getToggleState()); };
        auditionBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        auditionBtn.setButtonText ("Preview");
        auditionBtn.setTooltip ("Calculates the corrected sound (this takes a moment) and plays it. Then use 'Hear corrected' to switch between the corrected and the original sound. Nothing is final until you press Accept.");
        stopBtn.setTooltip ("Stops the playback");
        revertBtn.setTooltip ("Puts the original sound back");
        acceptBtn.setTooltip ("Keeps the corrected sound (Undo fix brings the original back)");
        resetBtn.setTooltip ("Removes every point: the line goes back to level");
        auditionBtn.onClick = [this] { audition(); };
        stopBtn.onClick = [this] { if (ctx.stop) ctx.stop(); };
        revertBtn.onClick = [this] { doRevert ("Back to the original sound."); };
        acceptBtn.onClick = [this] { accept(); };
        resetBtn.onClick = [this] { view.clearPoints(); };
        closeBtn.onClick = [this] { if (running) job.requestCancel(); else closeDialogOf (this); };
        status.setText ("Looking at the sound...", juce::dontSendNotification);
        if (ctx.pairWritten)
        {
            acceptBtn.setButtonText ("Accept stream"); closeBtn.setButtonText ("Cancel both");
            acceptBtn.setTooltip ("Gets this stream ready and closes this window (with no Preview first, it leaves this stream as it is). When BOTH streams have been accepted the corrected audio goes into the edit, with the edit point and its fades exactly where they are. Cancel (or the X) closes both windows and changes nothing.");
        }
        refresh();
        ticker.owner = this; ticker.startTimerHz (30);
        setWantsKeyboardFocus (true);                                              // Space plays / stops
        for (auto* ch : getChildren()) if (dynamic_cast<juce::Button*> (ch) != nullptr || dynamic_cast<juce::ComboBox*> (ch) != nullptr) ch->setWantsKeyboardFocus (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<CurveDialog> (this)] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        auto load = ctx.load;
        auto data = std::make_shared<std::vector<std::vector<float>>>();
        juce::Component::SafePointer<CurveDialog> self (this);
        runJob ([load, data] (AudioJob& j) -> juce::String { *data = load (j); return j.cancelled() ? juce::String ("Cancelled.") : juce::String(); },
                [self, data] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    self->loaded = true;
                    if (err.isEmpty() && ! data->empty() && ! data->front().empty())
                    {
                        const auto& x = data->front(); const int bins = 1200;
                        std::vector<float> env ((size_t) bins, 0.0f);
                        for (size_t i = 0; i < x.size(); ++i) { auto& b = env[i * (size_t) bins / x.size()]; b = juce::jmax (b, std::abs (x[i])); }
                        self->view.setEnvelope (std::move (env));
                    }
                    self->status.setText ("Click on the line area to put in points (each starts at 0 cents: drag it up for sharper, down for flatter). Then press Preview, and Space plays.", juce::dontSendNotification);
                    self->refresh();
                });
    }
    ~CurveDialog() override
    {
        ticker.stopTimer(); ticker.owner = nullptr;
        if (ctx.stop) ctx.stop();
        if (tentative && ! accepted && ctx.revert) ctx.revert();       // closing without Accept puts everything back
        if (ctx.hold) ctx.hold (false);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        intro.placeTopRight (*this, 6);                                          // the explanation is behind the blue i
        auto rowC = r.removeFromBottom (32);
        r.removeFromBottom (4);
        bar.setBounds (r.removeFromBottom (22).reduced (2, 2));                           // the progress bar has a full-width row of its own
        status.setBounds (r.removeFromBottom (22).reduced (2, 0));
        info.setBounds (r.removeFromBottom (20).reduced (2, 0));
        closeBtn.setBounds (rowC.removeFromRight (90)); rowC.removeFromRight (8);
        acceptBtn.setBounds (rowC.removeFromRight (100)); rowC.removeFromRight (8);
        revertBtn.setBounds (rowC.removeFromRight (100)); rowC.removeFromRight (16);
        stopBtn.setBounds (rowC.removeFromRight (70)); rowC.removeFromRight (8);
        loopBtn.setBounds (rowC.removeFromRight (66)); rowC.removeFromRight (8);
        playBtn.setBounds (rowC.removeFromRight (70)); rowC.removeFromRight (8);
        auditionBtn.setBounds (rowC.removeFromRight (100)); rowC.removeFromRight (14);
        hearToggle.setBounds (rowC.removeFromRight (150));
        resetBtn.setBounds (rowC.removeFromLeft (100)); rowC.removeFromLeft (14);
        if (ctx.allowPad) { padCap.setBounds (rowC.removeFromLeft (150)); pad.setBounds (rowC.removeFromLeft (80)); }
        view.setBounds (r);
    }
private:
    void refresh()
    {
        const bool ready = loaded && ! running;
        auditionBtn.setEnabled (ready && ! view.curve.isFlat());
        stopBtn.setEnabled (ready);
        playBtn.setEnabled (ready);
        hearToggle.setEnabled (ready && tentative && ! stale);
        revertBtn.setEnabled (ready && tentative);
        acceptBtn.setEnabled (ready && ((tentative && ! stale) || (ctx.pairWritten != nullptr && ! tentative)));
        resetBtn.setEnabled (ready && view.count() > 0);
        pad.setEnabled (ready);
    }
    void lineChanged()
    {
        if (tentative && ! stale) { stale = true; status.setText ("The line changed: press Preview to hear it (Accept is available after that).", juce::dontSendNotification); }
        refresh();
    }
    void doRevert (const juce::String& msg)
    {
        if (ctx.stop) ctx.stop();
        if (tentative && ctx.revert) ctx.revert();
        tentative = false; stale = false; corrected = false; hearToggle.setToggleState (false, juce::dontSendNotification);
        status.setText (msg, juce::dontSendNotification); refresh();
    }
    void audition()
    {
        if (running || ! loaded || view.curve.isFlat()) return;
        if (ctx.stop) ctx.stop();
        if (tentative && ctx.revert) ctx.revert();                         // always from the original sound
        tentative = false; stale = false; corrected = false; hearToggle.setToggleState (false, juce::dontSendNotification); refresh();
        FixSpec spec; spec.kind = FixSpec::Kind::Pitch; spec.useCurve = true; spec.curve = view.curve; spec.curve.sortPoints();
        const double padSec = ctx.allowPad ? (double) (pad.getSelectedId() - 1) : 0.0;
        spec.curveZero = padSec * ctx.rate;
        status.setText ("Calculating the corrected sound...", juce::dontSendNotification);
        auditionBtn.setEnabled (false); acceptBtn.setEnabled (false); revertBtn.setEnabled (false); resetBtn.setEnabled (false); pad.setEnabled (false);
        auto prepare = ctx.prepare;
        juce::Component::SafePointer<CurveDialog> self (this);
        runJob ([prepare, spec, padSec] (AudioJob& j) { return prepare (spec, padSec, j); },
                [self] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err == "Cancelled." ? juce::String ("Cancelled. Nothing was changed.") : err, juce::dontSendNotification); self->refresh(); return; }
                    if (self->ctx.apply && self->ctx.apply())
                    {
                        self->tentative = true; self->stale = false; self->corrected = true;
                        self->hearToggle.setToggleState (true, juce::dontSendNotification);
                        if (self->ctx.play) self->ctx.play (self->view.playFrom(), self->view.visT1(), self->loopBtn.getToggleState());
                        self->status.setText ("Playing the corrected sound.  'Hear corrected' switches to the original and back; Accept keeps it, Revert goes back, or change the line and Preview again.", juce::dontSendNotification);
                    }
                    else self->status.setText ("The audio changed while it was being calculated, so nothing was changed. Please try again.", juce::dontSendNotification);
                    self->refresh();
                });
    }
    void accept()
    {
        if (running) return;
        if (ctx.pairWritten && ! tentative)                                  // an untouched stream: it is left as it is
        {
            if (ctx.stop) ctx.stop();
            accepted = true; ctx.pairWritten ([] { return true; });
            return;
        }
        if (! tentative || stale) return;
        if (ctx.stop) ctx.stop();
        if (! corrected)                                                // the original was being heard: put the corrected sound back before keeping it
        {
            if (! (ctx.apply && ctx.apply())) { status.setText ("The audio changed, so the corrected sound is no longer available. Press Preview again.", juce::dontSendNotification); tentative = false; refresh(); return; }
            corrected = true;
        }
        if (ctx.pairWritten)
        {
            accepted = true; ctx.pairWritten ([] { return true; });
            return;
        }
        if (ctx.accept) ctx.accept();
        accepted = true;
        closeDialogOf (this);
    }
    bool playing() const { return ctx.position && ctx.position() >= 0.0; }
    /** 'Hear corrected': switches between the corrected and the original sound at the same place, so the difference can be heard straight away. */
    void hearChanged()
    {
        const bool wantCorrected = hearToggle.getToggleState();
        if (! tentative || stale) { hearToggle.setToggleState (corrected, juce::dontSendNotification); status.setText ("Press Preview first: there is no corrected sound to listen to yet.", juce::dontSendNotification); return; }
        const bool wasPlaying = playing(); const double at = wasPlaying ? ctx.position() : 0.0;
        if (ctx.stop) ctx.stop();
        if (wantCorrected) { if (! (ctx.apply && ctx.apply())) { status.setText ("The audio changed, so the corrected sound is no longer available. Press Preview again.", juce::dontSendNotification); tentative = false; refresh(); return; } }
        else if (ctx.unapply) ctx.unapply();
        corrected = wantCorrected;
        if (wasPlaying && ctx.play) ctx.play (loopBtn.getToggleState() || at < view.visT0() || at >= view.visT1() ? view.visT0() : at, view.visT1(), loopBtn.getToggleState());      // carries on from the same place (a loop starts again at the beginning)
        status.setText (wantCorrected ? "Hearing the CORRECTED sound." : "Hearing the ORIGINAL sound.", juce::dontSendNotification);
    }
public:
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey)                                      // Space: play the marked part, or stop
        {
            if (spaceIsForRecording()) return true;
            if (loaded && ! running) { if (playing()) { if (ctx.stop) ctx.stop(); } else if (ctx.play) ctx.play (view.playFrom(), view.visT1(), loopBtn.getToggleState()); }
            return true;
        }
        if ((k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') && ! k.getModifiers().isAnyModifierKeyDown())      // R: audition the line as it is
        {
            if (auditionBtn.isEnabled()) auditionBtn.triggerClick();
            return true;
        }
        return view.zoomKey (k);
    }
private:
    void tick()
    {
        const double t = ctx.position ? ctx.position() : -1.0;
        view.setPlayhead (t);
        if (t >= 0.0 && ! running) info.setText ("Playing:  " + juce::String (t, 2) + " s,  line " + juce::String (view.curve.centsAt (t) >= 0 ? "+" : "") + juce::String (view.curve.centsAt (t), 1) + " cents", juce::dontSendNotification);
        playBtn.setButtonText (t >= 0.0 ? "Restart" : "Play");
    }
    struct Ticker : juce::Timer { CurveDialog* owner = nullptr; void timerCallback() override { if (owner != nullptr) owner->tick(); } } ticker;
    CurveContext ctx;
    InfoNote intro; juce::Label info, padCap;
    CurveView view; juce::ComboBox pad;
    juce::TextButton auditionBtn { "Audition" }, playBtn { "Play" }, stopBtn { "Stop" }, revertBtn { "Revert" }, acceptBtn { "Accept" }, closeBtn { "Cancel" }, resetBtn { "Reset line" };
    juce::ToggleButton loopBtn { "Loop" }, hearToggle { "Hear corrected" };
    bool loaded = false, tentative = false, stale = false, accepted = false, corrected = false;
};

}} // namespace td::fixtools
