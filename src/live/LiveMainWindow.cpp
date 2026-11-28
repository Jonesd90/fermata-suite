#include "LiveMainWindow.h"
#include "../ui/HotKeysWindow.h"
#include "../ui/ValueEntry.h"
#include "LiveLook.h"

namespace td::live
{
/** 0.10.17: the right-click menu of an input strip's headroom (as fermata's): reset it or every input's, and show the headroom or the peak. */
static void showPeakMenu (juce::Component* over, PrimaryApp& app, int idx)
{
    juce::PopupMenu m;
    m.addItem (1, "Reset this input's peak");
    m.addItem (2, "Reset every input's peak  (RESET PEAKS)");
    m.addSeparator();
    m.addItem (3, "Show the headroom  (16.9 dB below 0 dBFS)", true, ! desk::PeakHold::showPeak());
    m.addItem (4, "Show the peak  (-16.9 dBFS)", true, desk::PeakHold::showPeak());
    auto* a = &app;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (over).withMousePosition(), [a, idx] (int r)
    {
        if (r == 1) a->model.resetHeld (idx);
        else if (r == 2) a->model.resetHeld();
        else if (r == 3 || r == 4) { desk::PeakHold::showPeak() = r == 4; a->settings->setValue ("inputPeakShown", r == 4 ? "peak" : "headroom"); a->settings->saveIfNeeded(); }
    });
}

//==============================================================================================================================================
/** One input of the driver as a strip of the desk (fermata's input strip without the monitor keys: fermata:Live always hears the inputs live). */
class InputStrip final : public juce::Component
{
public:
    InputStrip (PrimaryApp& a, int index) : app (a), idx (index)
    {
        using desk::Ic;
        colour = desk::stripColour (idx);
        driver.setJustificationType (juce::Justification::centredLeft);
        driver.setFont (theme::figureFont (9.0f, juce::Font::plain));
        driver.setColour (juce::Label::textColourId, colour);
        driver.setMinimumHorizontalScale (0.6f);
        driver.setInterceptsMouseClicks (true, false);
        alias.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        for (auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId }) alias.setColour (id, juce::Colours::transparentBlack);
        alias.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        alias.setIndents (0, 0); alias.setBorder (juce::BorderSize<int> (0));
        alias.setTextToShowWhenEmpty ("name", juce::Colours::white.withAlpha (0.35f));
        alias.setTooltip ("Your own name for this input (click to type it). It is shown beside the driver's name everywhere, in fermata too.");
        auto commit = [this] { if (idx < (int) app.project.inputs.size()) { app.project.setInputName (idx, alias.getText()); app.markDirty(); } };
        alias.onReturnKey = [this, commit] { commit(); alias.giveAwayKeyboardFocus(); };
        alias.onEscapeKey = [this] { if (idx < (int) app.project.inputs.size()) alias.setText (app.project.inputs[(size_t) idx].name, juce::dontSendNotification); alias.giveAwayKeyboardFocus(); };
        alias.onFocusLost = commit;
        for (auto* c : std::initializer_list<juce::Component*> { &driver, &alias, &meter, &scale, &readout, &peakHold }) addAndMakeVisible (c);
        scale.fine = true;                                       // (0.10.17: the meter runs the strip's length: more marks)
        peakHold.onReset = [this] { app.model.resetHeld (idx); };
        peakHold.onMenu = [this] { showPeakMenu (this, app, idx); };
        gain.setRange (0.0, 66.0, 0.5);
        gain.setDoubleClickReturnValue (true, 20.0);
        gain.setRing (colour);
        gain.setTooltip ("Preamp gain: drag up or down (double-click for 20 dB; the mouse wheel never changes it)");
        gain.onValueChange = [this] { updateReadout(); push(); };
        addAndMakeVisible (gain);
        readout.setTooltip ("Preamp gain in dB. Double-click to type a value.");
        readout.onDouble = [this]
        {
            juce::Component::SafePointer<InputStrip> safe (this);
            const double off = shownOffset();
            valueentry::ask (readout, "Gain in dB (" + juce::String (off, 0) + " to " + juce::String (66.0 + off, 0) + ")", juce::String (gain.getValue() + off, 1), [safe, off] (const juce::String& t)
            {
                double v = 0.0;
                if (safe != nullptr && valueentry::parseDb (t, off, off, 66.0 + off, v)) safe->gain.setValue (v - off, juce::sendNotificationSync);
            });
        };
        talk.setTooltip ("TALK: this input is (part of) the talkback mic. While the talkback is open (the Talkback square, or numpad +) the TALK inputs go to the outputs marked TB.");
        talk.onClick = [this] { if (! updating) app.setTalkInput (idx, talk.getToggleState()); };
        addAndMakeVisible (talk);
        mic = make ("MIC", Ic::Mic, "Mic / Line: lit = LINE input, off = MIC input", juce::Colour (0xffffb02e));
        v48 = make ("48V", Ic::Bolt, "Phantom power (+48 V)", juce::Colour (0xffff4a3d));
        pol = make (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), Ic::None, "Polarity invert", juce::Colour (0xffffb02e));
        lc  = make ("LC", Ic::Ramp, "Low cut filter", juce::Colour (0xffffb02e));
        pad = make ("PAD", Ic::Pillow, "Pad: the mic preamp's input attenuator", juce::Colour (0xffffb02e));
        zhi = make ("Z HI", Ic::None, "High input impedance", juce::Colour (0xffffb02e));
        boost = make ("BOOST", Ic::Rocket, "Boost: the mic preamp's extra gain (the device's 'lift')", juce::Colour (0xffffb02e));
        refresh();
    }

    void refresh()
    {
        using desk::Ic;
        if (idx >= (int) app.project.inputs.size()) return;
        const auto& in = app.project.inputs[(size_t) idx];
        updating = true;
        const auto tag = app.project.inputTag (idx);
        if (driver.getText() != tag)
        {
            driver.setText (tag, juce::dontSendNotification);
            const auto dev = app.project.inputDeviceLabel (idx);
            driver.setTooltip ((dev.isNotEmpty() ? "Device " + dev + ". " : juce::String()) + "The driver's own name for this input: " + in.driverName);
        }
        if (! alias.hasKeyboardFocus (false) && alias.getText() != in.name) { alias.setText (in.name, juce::dontSendNotification); alias.moveCaretToTop (false); }
        if (std::abs (gain.getValue() - in.preamp.gainDb) > 0.01 && ! gain.isMouseButtonDown()) gain.setValue (in.preamp.gainDb, juce::dontSendNotification);
        gain.setDoubleClickReturnValue (true, juce::jlimit (0.0, 66.0, 20.0 - (double) in.preamp.shownGainOffset()));
        updateReadout();
        const bool lo = in.preamp.lineOnly;
        if (lo)
        {
            mic->setToggleState (in.preamp.zHigh, juce::dontSendNotification); mic->setButtonText (in.preamp.zHigh ? "INSTR" : "LINE"); mic->setIcon (Ic::Jack);
            mic->setEnabled (in.preamp.hasInstrument); mic->setTooltip ("Line / Instrument: lit = INSTRUMENT input, plain = LINE input");
        }
        else
        {
            mic->setToggleState (in.preamp.line, juce::dontSendNotification); mic->setButtonText (in.preamp.line ? "LINE" : "MIC"); mic->setIcon (in.preamp.line ? Ic::Jack : Ic::Mic);
            mic->setEnabled (true); mic->setTooltip ("Mic / Line: lit = LINE input, off = MIC input");
        }
        const bool stepped = in.preamp.steppedPad;
        v48->setVisible (preampShown && ! lo);
        pad->setVisible (preampShown && ! lo && ! (stepped && in.preamp.line));
        boost->setVisible (preampShown && ! lo && ! stepped);
        zhi->setVisible (preampShown && ! lo && ! stepped);
        v48->setToggleState (in.preamp.phantom, juce::dontSendNotification);
        pol->setToggleState (in.preamp.polarity, juce::dontSendNotification);
        lcHzNow = in.preamp.lowCut ? (in.preamp.lowCutChoice ? in.preamp.lowCutHz : 80) : 0;
        lc->setToggleState (in.preamp.lowCut, juce::dontSendNotification);
        lc->setButtonText (in.preamp.lowCutChoice && in.preamp.lowCut ? juce::String (in.preamp.lowCutHz) + " Hz" : juce::String ("LC"));
        boost->setToggleState (in.preamp.boost, juce::dontSendNotification);
        padStepNow = in.preamp.padStep();
        pad->setToggleState (stepped ? padStepNow > 0 : in.preamp.pad, juce::dontSendNotification);
        pad->setButtonText (stepped ? (padStepNow == 0 ? juce::String ("PAD") : padStepNow == 1 ? juce::String ("-12") : juce::String ("-24")) : juce::String ("PAD"));
        zhi->setToggleState (in.preamp.zHigh, juce::dontSendNotification);
        talk.setToggleState (app.project.isCrMic (idx), juce::dontSendNotification);
        recording = false;
        for (auto& t : app.model.tracks) for (auto i : t.inputs) if (i == idx) recording = true;
        updating = false;
        checkPreampShown();
    }

    void tick()
    {
        meter.setLevel (juce::isPositiveAndBelow (idx, (int) app.model.inPeaks.size()) ? app.model.inPeaks[(size_t) idx] : 0.0f);
        peakHold.setPeak (app.model.held (idx));
        checkPreampShown();
    }

    struct Sizes { int top, disp, gainCap, knob, afterKnob, readout, gap1, key, keyGap, gap2, lower, gap3, talk, bottom; };
    static Sizes sizesFor (float t)
    {
        auto l = [t] (int full, int compact) { return juce::roundToInt ((float) full + ((float) compact - (float) full) * juce::jlimit (0.0f, 1.0f, t)); };
        return { l (12, 9), l (50, 40), l (16, 13), l (72, 48), l (2, 1), l (22, 18), l (10, 6), l (32, 26), l (3, 2), l (10, 6), l (118, 70), l (8, 5), l (24, 20), l (10, 8) };
    }
    static int heightFor (float t)
    {
        const auto z = sizesFor (t);
        return z.top + z.disp + z.gainCap + z.knob + z.afterKnob + z.readout + z.gap1 + 4 * z.key + 3 * z.keyGap + z.gap2 + z.lower + z.gap3 + z.talk + z.bottom;
    }
    void setSqueeze (float t) { if (std::abs (t - squeeze) > 0.001f) { squeeze = t; resized(); repaint(); } }

    void resized() override
    {
        // 0.10.17 (fermata 3.70.0, asked for on 26 November 2026): the meter runs the strip's whole length on the left, its scale beside it (it was
        // a short meter at the bottom); the strip is wider for it (84 -> 112), the preamp controls keep their width to the right. At the bottom of
        // that column: the REC / PRE lamp, then the headroom held since the last reset.
        constexpr int meterW = 10, scaleW = 19;
        const auto z = sizesFor (squeeze);
        auto r = getLocalBounds().reduced (6, 0);
        r.removeFromTop (z.top);
        dispArea = r.removeFromTop (z.disp);
        alias.setBounds (dispArea.reduced (6, 0).removeFromTop (juce::roundToInt (z.disp * 0.6f)).withTrimmedTop (z.disp >= 46 ? 5 : 3));
        driver.setBounds (dispArea.reduced (3, 0).removeFromBottom (juce::jmin (18, z.disp / 3)));
        const int keysH = 4 * z.key + 3 * z.keyGap;
        auto body = r.removeFromTop (z.gainCap + z.knob + z.afterKnob + z.readout + z.gap1 + keysH + z.gap2 + z.lower);
        {
            auto col = body.removeFromLeft (meterW + scaleW).withTrimmedTop (z.gap1);
            meter.setBounds (col.removeFromLeft (meterW));
            scale.setBounds (col.withTrimmedLeft (1));
            body.removeFromLeft (3);
        }
        {
            auto lower = body.removeFromBottom (z.lower);
            lowerArea = lower;                                                 // (the REC / PRE lamp is drawn at its top, "no preamp control" above it)
            peakHold.setBounds (lower.removeFromBottom (juce::roundToInt ((float) z.lower * 0.27f)));
        }
        gainCapArea = body.removeFromTop (z.gainCap);
        gain.setBounds (body.removeFromTop (z.knob).withSizeKeepingCentre (juce::jmin (z.knob, body.getWidth()), juce::jmin (z.knob, body.getWidth())));
        body.removeFromTop (z.afterKnob);
        readout.setBounds (body.removeFromTop (z.readout));
        body.removeFromTop (z.gap1);
        {
            auto keys = body.removeFromTop (keysH);
            const int w = (keys.getWidth() - 3) / 2;
            auto row = [&] (desk::DeskKey* a, desk::DeskKey* b)
            {
                auto line = keys.removeFromTop (z.key); keys.removeFromTop (z.keyGap);
                a->setBounds (line.removeFromLeft (w)); line.removeFromLeft (3); b->setBounds (line);
            };
            row (mic, v48); row (pol, lc); row (pad, zhi);
            boost->setBounds (keys.removeFromTop (z.key));
        }
        r.removeFromTop (z.gap3);
        talk.setBounds (r.removeFromTop (z.talk));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = desk::pal();
        auto b = getLocalBounds();
        juce::Graphics::ScopedSaveState rounded (g);                        // (3.73.0: the strip's corners rounded - its colour bars follow them)
        { juce::Path shape; shape.addRoundedRectangle (b.toFloat(), theme::kCorner); g.reduceClipRegion (shape); }
        g.setColour (p.strip); g.fillRect (b);
        g.setColour (p.stripEdge); theme::drawBox (g, b);
        g.setColour (colour); g.fillRect (0, 0, b.getWidth(), 4); g.fillRect (0, b.getHeight() - 4, b.getWidth(), 4);
        g.setColour (p.disp); theme::fillBox (g, dispArea);
        g.setColour (p.dispEdge); theme::drawBox (g, dispArea);
        g.setColour (p.cream); g.setFont (juce::FontOptions (juce::jmin (10.5f, (float) gainCapArea.getHeight() * 0.75f), juce::Font::bold));
        g.drawText (preampShown ? "GAIN" : "", gainCapArea.getX(), gainCapArea.getY() + 1, gainCapArea.getWidth(), gainCapArea.getHeight() - 2, juce::Justification::centred, false);
        // at the bottom of the column, over the headroom: REC while a take records this input, else "in the session" / "not recorded"
        auto side = lowerArea.withTrimmedBottom (peakHold.getHeight() + 3);
        if (! side.isEmpty())
        {
            const bool takeOn = app.model.recordingTake != 0 && recording;
            auto lamp = side.removeFromBottom (22).reduced (0, 2);
            g.setColour (takeOn ? juce::Colour (0xffe53935) : recording ? juce::Colour (0xff2e9e4f).withAlpha (0.85f) : p.disp);
            g.fillRoundedRectangle (lamp.toFloat(), 4.0f);
            g.setColour (takeOn || recording ? juce::Colours::white : p.cream.withAlpha (0.35f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (takeOn ? "REC" : recording ? "PRE" : "-", lamp, juce::Justification::centred, false);
            if (! preampShown)
            {
                g.setColour (p.cream.withAlpha (0.45f)); g.setFont (juce::FontOptions (9.5f));
                g.drawFittedText ("no preamp\ncontrol", side.withTrimmedTop (2), juce::Justification::centredTop, 3);
            }
        }
    }
    static constexpr int kStripW = 112;                     // (0.10.17: 84 -> 112, for the meter the strip's whole length)

private:
    desk::DeskKey* make (const juce::String& text, desk::Ic ic, const juce::String& tip, juce::Colour led)
    {
        auto* b = buttons.add (new desk::DeskKey (text, ic, 10.0f));
        b->setLed (led); b->setTooltip (tip);
        b->onClick = [this, b]
        {
            if (updating || idx >= (int) app.project.inputs.size()) return;
            const auto& cur = app.project.inputs[(size_t) idx].preamp;
            padStepNow = cur.padStep(); lcHzNow = cur.lowCut ? cur.lowCutHz : 0;
            if (b == pad && cur.steppedPad) padStepNow = (padStepNow + 1) % 3;
            if (b == lc && cur.lowCutChoice) lcHzNow = lcHzNow == 0 ? 40 : lcHzNow == 40 ? 60 : lcHzNow == 60 ? 80 : 0;
            push();
            refresh();
        };
        addAndMakeVisible (b);
        return b;
    }
    double shownOffset() const { return idx < (int) app.project.inputs.size() ? (double) app.project.inputs[(size_t) idx].preamp.shownGainOffset() : 0.0; }
    void updateReadout() { readout.setValueText (juce::String (gain.getValue() + shownOffset(), 1)); }
    void push()
    {
        if (updating || idx >= (int) app.project.inputs.size()) return;
        PreampSettings s;
        const auto& cur = app.project.inputs[(size_t) idx].preamp;
        const bool lo = cur.lineOnly;
        s.gainDb = (float) gain.getValue(); s.line = lo ? true : mic->getToggleState(); s.phantom = v48->getToggleState();
        s.polarity = pol->getToggleState(); s.lowCut = lc->getToggleState(); s.boost = boost->getToggleState(); s.pad = pad->getToggleState(); s.zHigh = lo ? mic->getToggleState() : zhi->getToggleState();
        s.lineOnly = cur.lineOnly; s.hasInstrument = cur.hasInstrument; s.lowCutChoice = cur.lowCutChoice; s.steppedPad = cur.steppedPad; s.lowCutHz = cur.lowCutHz;
        if (cur.lowCutChoice) { s.lowCut = lcHzNow != 0; if (lcHzNow != 0) s.lowCutHz = lcHzNow; }
        if (cur.steppedPad) { s.setPadStep (padStepNow); s.zHigh = false; }
        app.project.setPreamp (idx, s);
        app.markDirty();
    }
    bool hasPreamp() const { return app.project.preampDriver != nullptr && app.project.preampDriver->controls (idx); }
    void checkPreampShown()
    {
        const bool hw = hasPreamp();
        if (hw == preampShown) return;
        preampShown = hw; gain.setVisible (hw); readout.setVisible (hw);
        for (auto* b : { mic, v48, pol, lc, pad, zhi, boost }) b->setVisible (hw);
        refresh(); resized(); repaint();
    }

    PrimaryApp& app; int idx; bool updating = false, preampShown = true, recording = false;
    int padStepNow = 0, lcHzNow = 0;
    float squeeze = 0.0f;
    juce::Rectangle<int> dispArea, gainCapArea, lowerArea;
    juce::Colour colour;
    juce::Label driver; juce::TextEditor alias; LevelMeter meter { true }; desk::DeskScale scale; desk::DeskEncoder gain; desk::DeskReadout readout;
    desk::PeakHold peakHold;                                 // 0.10.17: the headroom since the last reset
    juce::OwnedArray<desk::DeskKey> buttons;
    desk::TalkKey talk;
    desk::DeskKey *mic = nullptr, *v48 = nullptr, *pol = nullptr, *lc = nullptr, *boost = nullptr, *pad = nullptr, *zhi = nullptr;
};

//==============================================================================================================================================
/** One output of the driver: your name for it, what feeds it, its number and the driver's name, TB, the meter. */
class OutputStrip final : public juce::Component
{
public:
    OutputStrip (PrimaryApp& a, int index) : app (a), idx (index)
    {
        colour = desk::stripColour (idx + 3);
        driver.setFont (theme::figureFont (9.5f, juce::Font::plain));
        driver.setColour (juce::Label::textColourId, desk::pal().cream);
        driver.setMinimumHorizontalScale (0.7f);
        driver.setInterceptsMouseClicks (false, false);
        alias.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        for (auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId }) alias.setColour (id, juce::Colours::transparentBlack);
        alias.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        alias.setIndents (0, 0); alias.setBorder (juce::BorderSize<int> (0));
        alias.setTextToShowWhenEmpty ("name", juce::Colours::white.withAlpha (0.35f));
        alias.setTooltip ("Your own name for this output (click to type it).");
        auto commit = [this]
        {
            if (idx >= (int) app.project.outputs.size()) return;
            if (app.project.outputs[(size_t) idx].name != alias.getText()) { app.project.outputs[(size_t) idx].name = alias.getText(); app.markDirty(); }
        };
        alias.onReturnKey = [this, commit] { commit(); alias.giveAwayKeyboardFocus(); };
        alias.onEscapeKey = [this] { if (idx < (int) app.project.outputs.size()) alias.setText (app.project.outputs[(size_t) idx].name, juce::dontSendNotification); alias.giveAwayKeyboardFocus(); };
        alias.onFocusLost = commit;
        fedBy.setColour (juce::Label::textColourId, desk::pal().cyan.withMultipliedBrightness (0.85f));
        fedBy.setFont (juce::FontOptions (10.5f)); fedBy.setBorderSize (juce::BorderSize<int> (0)); fedBy.setMinimumHorizontalScale (0.8f);
        fedBy.setInterceptsMouseClicks (false, false);
        for (auto* c : std::initializer_list<juce::Component*> { &driver, &alias, &meter, &fedBy }) addAndMakeVisible (c);
        tb.setLed (juce::Colour (0xffff8a2e));
        tb.setTooltip ("TB: this output and the next are a talkback pair (to the stage). While the talkback is open, the inputs marked TALK go here. On the last output it is mono. Click again to take the pair off.");
        tb.onClick = [this] { if (! updating) app.setTalkOutput (tbFirst(), ! isTb()); };
        addAndMakeVisible (tb);
        refresh();
    }
    void refresh()
    {
        if (idx >= (int) app.project.outputs.size()) return;
        auto& o = app.project.outputs[(size_t) idx];
        updating = true;
        const bool on = isTb(), asLeft = hasPair (idx);
        tb.setToggleState (on, juce::dontSendNotification);
        tb.setButtonText (on ? (asLeft ? "TB L" : "TB R") : "TB");
        updating = false;
        const auto drv = juce::String (idx + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + o.driverName;
        if (driver.getText() != drv) driver.setText (drv, juce::dontSendNotification);
        if (! alias.hasKeyboardFocus (false) && alias.getText() != o.name) { alias.setText (o.name, juce::dontSendNotification); alias.moveCaretToTop (false); }
        // what feeds it: the Primary's monitoring (the processing mixer's pair), the talkback, a station's playback
        juce::StringArray fed;
        const int pair = app.monitorPair();
        if (pair >= 0 && (idx == pair || idx == pair + 1)) fed.add ("Primary (monitor " + juce::String (idx == pair ? "L" : "R") + ")");
        if (on) fed.add ("Talkback");
        for (auto& s : app.model.streams) if (s.name != "Primary" && (s.outL == idx || s.outR == idx)) fed.add (s.name);
        const auto t = fed.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 ")) + fed.joinIntoString (", ");
        if (fedBy.getText() != t) fedBy.setText (t, juce::dontSendNotification);
    }
    void tick() { meter.setLevel (juce::isPositiveAndBelow (idx, (int) app.model.outPeaks.size()) ? app.model.outPeaks[(size_t) idx] : 0.0f); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        r.removeFromTop (9);
        auto disp = r.removeFromTop (36).reduced (7, 0);
        alias.setBounds (disp.removeFromTop (20).withTrimmedTop (4));
        fedBy.setBounds (disp.withTrimmedBottom (2));
        r.removeFromTop (4);
        { auto line = r.removeFromTop (24); tb.setBounds (line.removeFromRight (58)); driver.setBounds (line); }
        r.removeFromTop (4);
        meter.setBounds (r.removeFromTop (6));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& p = desk::pal();
        auto b = getLocalBounds();
        juce::Graphics::ScopedSaveState rounded (g);                        // (3.73.0: the strip's corners rounded - its colour bars follow them)
        { juce::Path shape; shape.addRoundedRectangle (b.toFloat(), theme::kCorner); g.reduceClipRegion (shape); }
        g.setColour (p.strip); g.fillRect (b);
        g.setColour (p.stripEdge); theme::drawBox (g, b);
        g.setColour (colour); g.fillRect (0, 0, b.getWidth(), 4);
        auto disp = juce::Rectangle<int> (8, 9, b.getWidth() - 16, 36);
        g.setColour (p.disp); theme::fillBox (g, disp);
        g.setColour (p.dispEdge); theme::drawBox (g, disp);
    }
    static constexpr int kStripW = 170, kStripH = 88;
private:
    bool hasPair (int first) const { auto& v = app.project.tbOutputs; return std::find (v.begin(), v.end(), first) != v.end(); }
    bool isTb() const { return hasPair (idx) || hasPair (idx - 1); }
    int tbFirst() const { return hasPair (idx) ? idx : hasPair (idx - 1) ? idx - 1 : idx; }
    PrimaryApp& app; int idx; bool updating = false;
    juce::Colour colour;
    desk::DeskKey tb { "TB", desk::Ic::Mega, 10.0f };
    juce::Label driver, fedBy; juce::TextEditor alias; LevelMeter meter { false };
};

//==============================================================================================================================================
/** The first thing seen with no project open: make one, or open one (a fermata project opens as it is). */
class StartPanel final : public juce::Component
{
public:
    StartPanel (std::function<void()> onNew, std::function<void()> onOpen, std::function<void()> onRecent, const juce::String& recentName)
    {
        title.setText ("fermata:Live", juce::dontSendNotification);
        title.setFont (juce::FontOptions (30.0f, juce::Font::bold)); title.setJustificationType (juce::Justification::centred);
        title.setColour (juce::Label::textColourId, desk::pal().cream);
        text.setText ("The live-concert recorder. Make a project for the concert, or open one (a fermata project opens as it is).\n"
                      "Then choose the audio device in Audio settings, the tracks in Project Designer, and open the Take Window.", juce::dontSendNotification);
        text.setFont (juce::FontOptions (14.0f)); text.setJustificationType (juce::Justification::centred);
        text.setColour (juce::Label::textColourId, desk::pal().cream.withAlpha (0.85f));
        for (auto* c : std::initializer_list<juce::Component*> { &title, &text, &newBtn, &openBtn }) addAndMakeVisible (c);
        newBtn.onClick = std::move (onNew); openBtn.onClick = std::move (onOpen);
        if (recentName.isNotEmpty()) { recentBtn.setButtonText ("Open " + recentName); recentBtn.onClick = std::move (onRecent); addAndMakeVisible (recentBtn); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (desk::pal().strip); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (desk::pal().stripEdge); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24);
        title.setBounds (r.removeFromTop (44));
        text.setBounds (r.removeFromTop (56));
        r.removeFromTop (14);
        auto row = r.removeFromTop (34);
        const int n = recentBtn.isVisible() ? 3 : 2, w = juce::jmin (230, (row.getWidth() - 12 * (n - 1)) / n);
        auto line = row.withSizeKeepingCentre (n * w + 12 * (n - 1), 34);
        newBtn.setBounds (line.removeFromLeft (w)); line.removeFromLeft (12);
        openBtn.setBounds (line.removeFromLeft (w)); line.removeFromLeft (12);
        if (recentBtn.isVisible()) recentBtn.setBounds (line.removeFromLeft (w));
    }
private:
    juce::Label title, text;
    juce::TextButton newBtn { "New project..." }, openBtn { "Open project..." }, recentBtn;
};

//==============================================================================================================================================
LiveMainComponent::LiveMainComponent (PrimaryApp& a) : app (a)
{
    keymap::KeyMap::setSection (*this, "fermata:Live: the Primary's take window");      // 0.10.16: its keys go on to the take window (HotKeys)
    setOpaque (true);
    {   // 0.10.17: RESET PEAKS - the headroom held on every input strip (each strip's own display resets that input); how it is shown, kept in the settings
        resetPeaksKey.setClickingTogglesState (false);
        resetPeaksKey.setTooltip ("Resets the headroom held on every input strip (each strip shows the highest peak since its last reset; click a strip's own headroom to reset just that one)");
        resetPeaksKey.onClick = [this] { app.model.resetHeld(); };
        addAndMakeVisible (resetPeaksKey);
        desk::PeakHold::showPeak() = app.settings->getValue ("inputPeakShown") == "peak";
    }
    for (auto* b : menuItems()) { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
    projectButton.onClick = [this] { showProjectMenu(); };
    audioButton.onClick = [this] { openWindow ("audio"); };
    preampsButton.onClick = [this] { openWindow ("preamps"); };
    designButton.onClick = [this] { openWindow ("designer"); };
    takeButton.onClick = [this] { openWindow ("take"); };
    mixerButton.onClick = [this] { openWindow ("mixer"); };
    bounceButton.onClick = [this] { openWindow ("bounce"); };
    fixerButton.onClick = [this] { app.startFixer(); };
    helpButton.onClick = [this] { showHelpMenu(); };
    audioButton.setTooltip ("The audio device (the MAD driver), its rate and buffer, the backup drives, the pre-take and the memory buffer");
    preampsButton.setTooltip ("The Anubis / Hapi / MT48 devices whose preamps fermata:Live controls (found with ANEMAN)");
    designButton.setTooltip ("The tracks: their names, mono or stereo, and the inputs they record");
    takeButton.setTooltip ("The Primary's take window: Record, Stop, the takes filling live, flags (Enter), markers, playback");
    mixerButton.setTooltip ("The processing mixer: what you hear live (and what Bounce out uses)");
    bounceButton.setTooltip ("Bounce out: a stereo mix of a take, whole or between markers 1 and 2");
    fixerButton.setTooltip ("Opens the Fixer's station: the delayed timeline, Follow Primary, replies to your flags, ReTouch. It is a program of its own: close it any time.");
    for (auto* s : { &talkSquare, &preSquare, &recSquare, &drivesSquare, &mergeSquare }) addAndMakeVisible (s);
    talkSquare.onClick = [this] { app.setTalkback (! app.talkbackOpen()); };
    preSquare.onClick = [this]
    {
        if (app.model.sessionOn)
        {
            if (app.isRecording()) { app.addLog ("A take is recording: Stop it first"); return; }
            juce::Component::SafePointer<LiveMainComponent> safe (this);
            confirmAsync ("End the session", "End the session? Nothing more is written to the drives until it is started again (Record starts it too).", "End the session",
                          [safe] { if (safe != nullptr) safe->app.endSession(); });
        }
        else app.startSession (false);
    };
    addAndMakeVisible (badge);
    takeBox.setCompact (true); addAndMakeVisible (takeBox);
    for (auto* c : std::initializer_list<juce::Component*> { &projectBox, &rateBox, &diskBox, &recorderBox, &statusLine, &inputsCaption, &outputsCaption }) addAndMakeVisible (c);
    projectBox.onClick = [this] { showProjectMenu(); };
    statusLine.setFont (juce::FontOptions (13.0f)); statusLine.setColour (juce::Label::textColourId, desk::pal().cream);
    statusLine.setMinimumHorizontalScale (0.7f);
    inputsView.setViewedComponent (&inputsHolder, false); inputsView.setScrollBarsShown (false, true);
    outputsView.setViewedComponent (&outputsHolder, false); outputsView.setScrollBarsShown (false, true);
    addAndMakeVisible (inputsView); addAndMakeVisible (outputsView);
    app.addChangeListener (this);
    rebuildStrips();
    refreshTop();
    showStartPanel (! app.hasProject());
    setWantsKeyboardFocus (true);
    startTimerHz (25);
    setSize (1400, 920);
}

LiveMainComponent::~LiveMainComponent()
{
    app.removeChangeListener (this);
    closeAllWindows();
}

void LiveMainComponent::closeAllWindows() { windows.clear(); }

void LiveMainComponent::openWindow (const juce::String& key)
{
    if (key == "mixer-toggle")                                          // (0.5.0: M in the take window - the mixer opens, or closes when it is open)
    {
        if (auto it = windows.find ("mixer"); it != windows.end() && it->second != nullptr && it->second->isVisible())
        {
            juce::Component::SafePointer<LiveMainComponent> safe (this);      // (later: the key may have come from the mixer window itself)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->windows.erase ("mixer"); });
            return;
        }
        openWindow ("mixer");
        return;
    }
    if (auto it = windows.find (key); it != windows.end() && it->second != nullptr)
    {
        it->second->setVisible (true); it->second->toFront (true);
        return;
    }
    if (! app.hasProject() && key != "audio" && key != "log" && key != "hotkeys") { app.addLog ("Make or open a project first (Project menu)"); showStartPanel (true); return; }
    std::unique_ptr<juce::Component> content;
    juce::String title;
    juce::Rectangle<int> size (700, 520);
    if (key == "audio")         { content = makeAudioSettings (app); title = "Audio settings"; size = { 640, 600 }; }
    else if (key == "preamps")  { content = makePreampsPanel (app); title = "Preamps"; size = { 720, 520 }; }
    else if (key == "designer") { content = makeDesigner (app); title = "Project Designer"; size = { 820, 620 }; }
    else if (key == "take")     { content = makePrimaryTakeWindow (app, [this] (const juce::String& k) { openWindow (k); }); title = "Take window - Primary"; size = { 1360, 820 }; }
    else if (key == "mixer")    { content = makePrimaryMixer (app); title = "Mixer - processing mixer (the Primary's monitoring)"; size = { 1100, 640 }; }
    else if (key == "bounce")   { content = makeBouncePanel (app); title = "Bounce out"; size = { 620, 430 }; }
    else if (key == "log")      { content = makeLogPanel (app); title = "fermata:Live log"; size = { 900, 520 }; }
    else if (key == "hotkeys")  { content = std::make_unique<HotKeysComponent> (true); title = "HotKeys - fermata:Live"; size = { 900, 780 }; }     // (0.10.16)
    if (content == nullptr) return;
    if (key == "take" || key == "mixer") fit::markSelfFitting (*content);    // (0.10.18: these look after a small window themselves; the others scroll - FitLayout.h)
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    auto w = std::make_unique<ToolWindow> (title, [safe, key] (ToolWindow*)
    {
        if (safe == nullptr) return;
        juce::MessageManager::callAsync ([safe, key] { if (safe != nullptr) safe->windows.erase (key); });
    });
    w->isTransportWindow = key == "take";
    w->forwardKeys = key == "mixer";
    // the window fits the screen it opens on (a laptop's screen is smaller than the sizes above), its title bar on the screen
    juce::Rectangle<int> area (0, 0, 1920, 1080);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) area = d->userArea;
    size.setSize (juce::jmin (size.getWidth(), area.getWidth() - 40), juce::jmin (size.getHeight(), area.getHeight() - 70));
    content->setSize (size.getWidth(), size.getHeight());
    w->setContentOwned (content.release(), true);
    w->setResizeLimits (420, 300, 4000, 3000);
    if (auto* top = getTopLevelComponent())
    {
        const int x = top->getX() + 60 + (int) windows.size() * 24, y = top->getY() + 90 + (int) windows.size() * 24;
        w->setTopLeftPosition (juce::jlimit (area.getX() + 10, juce::jmax (area.getX() + 10, area.getRight() - w->getWidth() - 10), x),
                               juce::jlimit (area.getY() + 40, juce::jmax (area.getY() + 40, area.getBottom() - w->getHeight() - 10), y));
    }
    else w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    if (auto* top = getTopLevelComponent()) makeOwnedBy (*w, *top);
    w->toFront (true);
    windows[key] = std::move (w);
}

void LiveMainComponent::showStartPanel (bool show)
{
    if (! show) { startPanel.reset(); return; }
    if (startPanel != nullptr) return;
    const auto recent = app.recentProject();
    startPanel = std::make_unique<StartPanel> ([this] { newProjectDialog(); }, [this] { openProjectDialog(); },
                                               [this, recent] { juce::String err; if (! app.openProject (recent, err)) showError ("Open project", err); else showStartPanel (false); },
                                               recent.existsAsFile() ? recent.getFileNameWithoutExtension() : juce::String());
    addAndMakeVisible (*startPanel);
    resized();
}

void LiveMainComponent::newProjectDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("New project: choose where, and type its name (a folder of that name is made)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fermata");
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        const auto file = fc.getResult().withFileExtension (".fermata");
        // the rate: one of fermata's (the device's own, when it is one of them)
        juce::PopupMenu m;
        int id = 1;
        for (auto r : Project::kProjectRates) m.addItem (id++, juce::String (r / 1000.0, 1) + " kHz" + (std::abs (r - safe->app.model.deviceRate) < 0.5 ? "  (the device's rate now)" : ""));
        m.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (240).withTargetComponent (&safe->projectButton), [safe, file] (int choice)
        {
            if (safe == nullptr || choice <= 0) return;
            juce::String err;
            if (! safe->app.newProject (file, Project::kProjectRates[choice - 1], err)) showError ("New project", err);
            else { safe->showStartPanel (false); safe->rebuildStrips(); }
        });
    });
}

void LiveMainComponent::openProjectDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a project (fermata:Live's or fermata's)", app.recentProject().existsAsFile() ? app.recentProject().getParentDirectory().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fermata");
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        juce::String err;
        if (! safe->app.openProject (fc.getResult(), err)) showError ("Open project", err);
        else { safe->showStartPanel (false); safe->rebuildStrips(); }
    });
}

void LiveMainComponent::showProjectMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New project...", ! app.model.sessionOn);
    m.addItem (2, "Open project...", ! app.model.sessionOn);
    const auto recent = app.recentProject();
    if (recent.existsAsFile() && recent != app.project.projectFile) m.addItem (3, "Open " + recent.getFileNameWithoutExtension(), ! app.model.sessionOn);
    m.addSeparator();
    m.addItem (4, "Save", app.hasProject());
    m.addItem (5, "Show the project folder", app.hasProject());
    m.addSeparator();
    m.addItem (7, "Your name (on your flags and notes): " + app.stationName() + "...");
    m.addItem (6, "Log...");
    m.addSeparator();
    const auto picked = std::make_shared<int> (-1);
    look::addToMenu (m, picked);                                                // 0.9.0: fermata's looks - UI style (the desk, or the Manuscript UI) and the desk's colour
    if (app.model.sessionOn) { m.addSeparator(); m.addSectionHeader ("A session is running: end it to change the project"); }
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&projectButton), [safe, recent, picked] (int r)
    {
        if (safe == nullptr) return;
        auto& app = safe->app;
        if (look::chosen (r, picked, *app.settings)) { safe->lookChanged(); return; }
        if (r == 1) safe->newProjectDialog();
        else if (r == 2) safe->openProjectDialog();
        else if (r == 3) { juce::String err; if (! app.openProject (recent, err)) showError ("Open project", err); else { safe->showStartPanel (false); safe->rebuildStrips(); } }
        else if (r == 4) { juce::String err; if (! app.saveNow (&err)) showError ("Save", err); }
        else if (r == 5) app.project.projectFolder().revealToUser();
        else if (r == 6) safe->openWindow ("log");
        else if (r == 7) safe->askStationName();
    });
}

/** 0.9.0: the look was changed in the Project menu. fermata's own code has given every open window the new palette and typeface (applyStyle,
    applyDeskColour); what this window made for itself is made again - the strips hold colours and fonts of their own. */
void LiveMainComponent::lookChanged()
{
    shownInputs = shownOutputs = -1;
    rebuildStrips();
    resized(); repaint();
}

/** 0.5.0: the Primary's name, as its flags and notes say who wrote them ("by David"); the Fixer gives its own in its window. */
void LiveMainComponent::askStationName()
{
    auto* w = new juce::AlertWindow ("Your name", "The name your flags and notes carry (\"by ...\"), and the Fixer sees. Empty: \"Primary\".", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", app.settings->getValue ("stationName"), "Name:");
    w->addButton ("Use this name", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (safe != nullptr && r == 1) safe->app.setStationName (w->getTextEditorContents ("name"));
    }), true);
    if (auto* ed = w->getTextEditor ("name")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

void LiveMainComponent::showHelpMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "About fermata:Live");
    m.addItem (2, "The log...");
    m.addItem (3, "Show the log files");
    m.addSeparator();
    m.addItem (4, "HotKeys: every key, and changing them...");
    juce::Component::SafePointer<LiveMainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "fermata:Live " + safe->app.version,
                "The live-concert recorder that goes with fermata.\n\n"
                "The recording runs in a program of its own (fermata-Live.exe --recorder, in Task Manager), at high priority: this window, the Fixer and anything else can be closed, or crash, and the recording goes on.\n\n"
                "Recorder " + safe->app.model.recorderVersion + (safe->app.model.connected ? ", connected." : ", not connected."));
        else if (r == 2) safe->openWindow ("log");
        else if (r == 3) juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fermata").getChildFile ("fermata-live-log.txt").revealToUser();
        else if (r == 4) safe->openWindow ("hotkeys");
    });
}

void LiveMainComponent::rebuildStrips()
{
    const int nIn = app.hasProject() ? (int) app.project.inputs.size() : 0, nOut = app.hasProject() ? (int) app.project.outputs.size() : 0;
    if (nIn != shownInputs)
    {
        inputStrips.clear();
        for (int i = 0; i < nIn; ++i) inputsHolder.addAndMakeVisible (inputStrips.add (new InputStrip (app, i)));
        shownInputs = nIn;
    }
    if (nOut != shownOutputs)
    {
        outputStrips.clear();
        for (int i = 0; i < nOut; ++i) outputsHolder.addAndMakeVisible (outputStrips.add (new OutputStrip (app, i)));
        shownOutputs = nOut;
    }
    for (auto* s : inputStrips) s->refresh();
    for (auto* s : outputStrips) s->refresh();
    resized();
}

void LiveMainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const int nIn = app.hasProject() ? (int) app.project.inputs.size() : 0, nOut = app.hasProject() ? (int) app.project.outputs.size() : 0;
    if (nIn != shownInputs || nOut != shownOutputs) rebuildStrips();
    else { for (auto* s : inputStrips) s->refresh(); for (auto* s : outputStrips) s->refresh(); }
    if (app.hasProject() && startPanel != nullptr) showStartPanel (false);
    refreshTop();
}

void LiveMainComponent::refreshTop()
{
    const auto& m = app.model;
    takeBox.set (m.recordingTake != 0, m.recordingTake != 0 ? m.recordingTake : (app.hasProject() ? app.nextTakeNumber() : 0));
    projectBox.setText (app.hasProject() ? app.project.name.toUpperCase() + "  -  " + app.pieceName() : juce::String ("NO PROJECT"));
    rateBox.setText (m.deviceRunning ? juce::String (m.deviceRate / 1000.0, 1) + " kHz" : (app.project.sampleRate > 0 ? juce::String (app.project.sampleRate / 1000.0, 1) + " kHz" : juce::String ("-")),
                     app.project.sampleRate > 0 && m.deviceRunning && std::abs (app.project.sampleRate - m.deviceRate) > 0.5 ? juce::Colour (0xffff6b5a) : juce::Colour());
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastDiskMs > 3000)
    {
        lastDiskMs = now;
        const auto f = app.hasProject() ? app.project.projectFolder() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const auto free = f.getBytesFreeOnVolume();
        diskText = free > 0 ? juce::String ((juce::int64) (free / (1024 * 1024 * 1024))) + " GB" : juce::String ("-");
        if (app.model.sessionOn && app.model.sessionRate > 0 && ! app.model.tracks.empty())
        {
            int ch = 0; for (auto& t : app.model.tracks) ch += t.channels;
            const double bytesPerHour = app.model.sessionRate * 3.0 * ch * 3600.0;
            if (bytesPerHour > 0) diskText << " (" << juce::String ((double) free / bytesPerHour, 1) << " h)";
        }
    }
    diskBox.setText (diskText);
    juce::String rec;
    juce::Colour recCol;
    if (! m.connected) { rec = "STARTING..."; recCol = juce::Colour (0xffe0a040); }
    else if (m.audioMissing > 0.0)                               // (0.3.0: ahead of everything else, the take that records included)
    {
        rec = m.audioMissingInSession ? "NO AUDIO - NOT RECORDING" + (m.recordingTake != 0 ? " (TAKE " + juce::String (m.recordingTake) + ")" : juce::String()) : juce::String ("NO AUDIO");
        recCol = juce::Colour (0xffff5a4a);
    }
    else if (! m.deviceRunning) { rec = "NO DEVICE"; recCol = juce::Colour (0xffe0a040); }
    else if (m.recordingTake != 0) { rec = "RECORDING TAKE " + juce::String (m.recordingTake); recCol = juce::Colour (0xffff5a4a); }
    else if (m.sessionOn) rec = "PRE-TAKE ON";
    else rec = "READY";
    recorderBox.setText (rec, recCol);
    talkSquare.set (m.talkOpen);
    preSquare.set (m.sessionOn);
    recSquare.set (m.recordingTake != 0, {}, m.recordingTake != 0 ? "REC " + juce::String (m.recordingTake) : juce::String ("REC"));
    bool dropped = false, behind = false;
    for (auto& d : m.drives) { dropped = dropped || d.dropped; behind = behind || d.gaps > 0 || d.lagSeconds > 5.0 || d.stalledSeconds > 2.0; }
    drivesSquare.set (m.sessionOn, dropped ? juce::Colour (0xffe53935) : behind ? juce::Colour (0xffe0a040) : juce::Colour (0xff2e9e4f),
                      m.sessionOn ? "Drives " + juce::String ((int) m.drives.size()) : juce::String ("Drives"));
    mergeSquare.set (m.mergeActive || m.mergeQueued > 0, {}, m.mergeActive ? "Merge " + juce::String ((int) std::lround (m.mergeFraction * 100.0)) + "%" : juce::String ("Merge"));
    // the status line: no audio while it lasts (0.3.0), else a warning while it is fresh, else what the recorder is doing
    juce::String line;
    const bool fresh = app.lastWarning.isNotEmpty() && now - app.lastWarningMs < 20000;
    if (m.connected && m.audioMissing > 0.0)
        line = m.audioMissingInSession ? "NO AUDIO FOR " + juce::String (juce::roundToInt (m.audioMissing)) + " s (" + m.audioWhy + "): NOTHING IS BEING RECORDED - the log says what to do"
                                       : "The audio device gives no audio (" + m.audioWhy + ")";
    else if (fresh) line = app.lastWarning;
    else line = app.deviceSummary() + (m.sessionOn ? "   |   session " + juce::File (m.sessionFolder).getFileName() + ", " + juce::String ((int) m.drives.size()) + " drive(s)" : juce::String())
              + (m.mergeActive ? "   |   merging take " + juce::String (m.mergeTake) + ": " + m.mergeWhat : juce::String());
    statusLine.setText (line, juce::dontSendNotification);
    statusLine.setColour (juce::Label::textColourId, m.connected && m.audioMissing > 0.0 ? juce::Colour (0xffff7a68) : fresh ? juce::Colour (0xffffb0a0) : desk::pal().cream);
}

void LiveMainComponent::timerCallback()
{
    for (auto* s : inputStrips) s->tick();
    for (auto* s : outputStrips) s->tick();
    static int n = 0;
    if (++n % 5 == 0) refreshTop();
}

void LiveMainComponent::resized()
{
    auto r = getLocalBounds();
    auto menu = r.removeFromTop (menuH);
    {
        auto m = menu.reduced (6, 0);
        for (auto* s : { &mergeSquare, &drivesSquare, &recSquare, &preSquare, &talkSquare }) { s->setBounds (m.removeFromRight (s->wantedWidth())); m.removeFromRight (4); }
        for (auto* b : menuItems()) b->setBounds (m.removeFromLeft (b->wantedWidth()));
    }
    r.removeFromLeft (kCheekW); r.removeFromRight (kCheekW); r.removeFromBottom (kRailH);
    auto top = r.removeFromTop (topH).reduced (14, 8);
    {
        auto line1 = top.removeFromTop (28);
        badge.setBounds (line1.removeFromLeft (170));
        line1.removeFromLeft (10);
        takeBox.setBounds (line1.removeFromLeft (40).reduced (0, 1));
        line1.removeFromLeft (10);
        projectBox.setBounds (line1.removeFromLeft (juce::jmin (380, line1.getWidth() / 3)));
        line1.removeFromLeft (16);
        rateBox.setBounds (line1.removeFromLeft (rateBox.captionWidth() + 90));
        line1.removeFromLeft (16);
        diskBox.setBounds (line1.removeFromLeft (diskBox.captionWidth() + 130));
        line1.removeFromLeft (16);
        recorderBox.setBounds (line1.removeFromLeft (juce::jmin (line1.getWidth(), recorderBox.captionWidth() + 220)));
        top.removeFromTop (4);
        statusLine.setBounds (top.removeFromTop (20));
    }
    auto outArea = r.removeFromBottom (outputsH);
    outputsCaption.setBounds (outArea.removeFromTop (20).withTrimmedLeft (16));
    outputsView.setBounds (outArea.reduced (10, 0));
    {
        auto cap = r.removeFromTop (20).withTrimmedLeft (16);
        inputsCaption.setBounds (cap.removeFromLeft (110));
        resetPeaksKey.setBounds (cap.removeFromLeft (92).reduced (0, 1));                                   // (0.10.17: beside the caption)
    }
    inputsView.setBounds (r.reduced (10, 2));
    // the input strips: full height when there is room, squeezed when the screen is short
    const int avail = inputsView.getHeight() - 10;
    float squeeze = 0.0f;
    if (avail < InputStrip::heightFor (0.0f)) squeeze = juce::jlimit (0.0f, 1.0f, (float) (InputStrip::heightFor (0.0f) - avail) / (float) (InputStrip::heightFor (0.0f) - InputStrip::heightFor (1.0f)));
    const int h = juce::jmin (avail, InputStrip::heightFor (squeeze));
    int x = 0;
    for (auto* s : inputStrips) { s->setSqueeze (squeeze); s->setBounds (x, 0, InputStrip::kStripW, h); x += InputStrip::kStripW + 2; }
    inputsHolder.setSize (juce::jmax (x, inputsView.getWidth()), h);
    x = 0;
    for (auto* s : outputStrips) { s->setBounds (x, 0, OutputStrip::kStripW, OutputStrip::kStripH); x += OutputStrip::kStripW + 2; }
    outputsHolder.setSize (juce::jmax (x, outputsView.getWidth()), OutputStrip::kStripH);
    if (startPanel != nullptr) startPanel->setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 80), 210));
}

void LiveMainComponent::paint (juce::Graphics& g)
{
    const auto& p = desk::pal();
    g.fillAll (p.window);
    auto r = getLocalBounds();
    auto menu = r.removeFromTop (menuH);
    g.setColour (p.menu); g.fillRect (menu);
    g.setColour (p.menuEdge); g.fillRect (menu.removeFromBottom (1));
    desk::paintCheek (g, r.removeFromLeft (kCheekW).withTrimmedBottom (kRailH), true);
    desk::paintCheek (g, r.removeFromRight (kCheekW).withTrimmedBottom (kRailH), false);
    auto rail = getLocalBounds().removeFromBottom (kRailH);
    desk::paintRail (g, rail);
    desk::paintNameplate (g, rail.reduced (12, 4).removeFromRight (160));
    g.setColour (p.stripEdge.withAlpha (0.8f)); g.fillRect (kCheekW + 10, menuH + topH - 2, getWidth() - 2 * kCheekW - 20, 1);
}

bool LiveMainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::numberPadAdd) { app.setTalkback (! app.talkbackOpen()); return true; }
    // the take window's keys work from here too (Record, Stop, a flag): the Primary's window is the one that has them
    if (auto it = windows.find ("take"); it != windows.end() && it->second != nullptr)
        if (auto* c = it->second->getContentComponent()) if (c->keyPressed (k)) return true;
    return false;
}

//==============================================================================================================================================
LiveMainWindow::LiveMainWindow (PrimaryApp& app)
    : juce::DocumentWindow ("fermata:Live " + app.version + " - Primary", desk::pal().window, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    content = new LiveMainComponent (app);
    fit::markSelfFitting (*content);                 // (0.10.18: its strips look after a short window themselves, as fermata's main window's do)
    setContentOwned (content, true);
    setResizeLimits (900, 560, 6000, 4000);
    centreWithSize (juce::jmin (1500, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                    juce::jmin (940, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 40));
    setVisible (true);
}

void LiveMainWindow::closeButtonPressed() { if (onClose) onClose(); }
} // namespace td::live
