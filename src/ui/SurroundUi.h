#pragma once
#include "Uikit.h"
#include "../core/Mixer.h"

namespace td
{
/** 3.39.0: the meters of a surround bus - one thin meter per channel, in fermata's channel order (L R C LFE, the surrounds, the wides, the tops,
    the headphones), with a little more room between those groups so they can be told apart. They are drawn on the fader's scale, like the stereo
    meters they replace, so the markings beside the fader still line up. */
class SurroundMeterBank : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kMeterW = 4, kGap = 1, kGroupGap = 3;
    void setFormat (BusFormat f)
    {
        if (f == format && meters.size() == surround::channels (f)) return;
        format = f;
        meters.clear();
        for (int c = 0; c < surround::channels (f); ++c)
        {
            auto* m = meters.add (new LevelMeter());
            m->useFaderScale();
            m->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (m);
        }
        juce::StringArray names;
        for (int c = 0; c < surround::channels (f); ++c) names.add (surround::channelLabel (f, c));
        setTooltip (surround::name (f) + " meters, left to right: " + names.joinIntoString (" ") + ". The fader's scale is on their left.");
        resized();
    }
    BusFormat getFormat() const noexcept { return format; }
    /** The width the meters of format f take. */
    static int widthFor (BusFormat f)
    {
        const int n = surround::channels (f);
        int w = n * kMeterW + (n - 1) * kGap;
        for (int c = 1; c < n; ++c) if (group (f, c) != group (f, c - 1)) w += kGroupGap - kGap;
        return w;
    }
    /** Meters placed 'top' to 'bottom' (the fader's travel, plus the clip light above it). */
    void resized() override
    {
        int x = 0;
        for (int c = 0; c < meters.size(); ++c)
        {
            if (c > 0) x += group (format, c) != group (format, c - 1) ? kGroupGap : kGap;
            meters[c]->setBounds (x, 0, kMeterW, getHeight());
            x += kMeterW;
        }
    }
    /** The peak (since the last look) and RMS of each channel, as linear levels. */
    void push (const float* peaks, const float* rms)
    {
        for (int c = 0; c < meters.size(); ++c) meters[c]->setLevel (peaks[c], rms[c]);
    }
    float maxHoldDb() const { float m = -100.0f; for (auto* x : meters) m = juce::jmax (m, x->getHoldDb()); return m; }
    bool anyLatched() const { for (auto* x : meters) if (x->isLatched()) return true; return false; }
    void resetPeaks() { for (auto* x : meters) x->resetPeak(); }
private:
    /** Which group a channel is in: 0 front (L R C LFE), 1 surrounds, 2 wides, 3 tops, 4 headphones. */
    static int group (BusFormat f, int c)
    {
        const auto& l = surround::layout (f);
        if (! juce::isPositiveAndBelow (c, l.count)) return 0;
        using S = surround::Spk;
        switch (l.spk[c])
        {
            case S::L: case S::R: case S::C: case S::LFE: return 0;
            case S::Ls: case S::Rs: case S::Lss: case S::Rss: case S::Lrs: case S::Rrs: return 1;
            case S::Lw: case S::Rw: return 2;
            case S::HpL: case S::HpR: return 4;
            default: return 3;
        }
    }
    BusFormat format = BusFormat::Stereo;
    juce::OwnedArray<LevelMeter> meters;
};

/** 3.39.0: where a mono or stereo channel lands in a surround bus - the panner of one send (or of a strip's output) into it. A top view of the room:
    the bus's speakers (the ear-level ones on the outer square, the top ones on the inner square, each lit by how much of the sound it gets), and the
    sound as a dot (two for a stereo channel, joined). Drag the dot to move it; a height slider beside the room for the formats with top speakers;
    spread, centre and (a stereo channel) width below. Everything is applied at once to the send's settings (SendState::panX ...). */
class SurroundPanner : public juce::Component, private juce::Timer
{
public:
    /** srcChannels 1 or 2. pans(): the channel's own pan - mono, left, right (-1 .. +1), followed while Follow pan is ticked. */
    /** alive(): false once the mixer the send belongs to is gone (deleted while this is open): the panner closes itself without touching the send. */
    SurroundPanner (SendState& s, BusFormat destFormat, int srcChannels, std::function<std::array<float, 3>()> pans, std::function<void()> changed,
                    std::function<bool()> alive = {})
        : send (s), format (destFormat), channels (juce::jlimit (1, 2, srcChannels)), panOf (std::move (pans)), onChanged (std::move (changed)), stillThere (std::move (alive))
    {
        title.setText ("Surround position  (" + surround::name (format) + ")", juce::dontSendNotification);
        title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (title);
        follow.setButtonText (channels == 1 ? "Left-right follows the channel's pan" : "Left-right follows the channel's two pans");
        follow.setTooltip ("Ticked: the sound's place from left to right is the channel's own pan (so it lands where the stereo mix has it); drag the dot for front to back. "
                           "Not ticked: drag it anywhere.");
        follow.setColour (juce::ToggleButton::textColourId, theme::text);
        follow.onClick = [this] { send.panFollow.set (follow.getToggleState()); if (! follow.getToggleState()) send.panX.set (currentCentreX()); changedNow(); };
        addAndMakeVisible (follow);
        auto setupSlider = [this] (juce::Slider& sl, juce::Label& cap, const juce::String& text, const juce::String& tip, bool vertical)
        {
            sl.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
            sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            sl.setRange (0.0, 100.0, 1.0);
            sl.setScrollWheelEnabled (false);
            sl.setTooltip (tip);
            sl.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v)) + " %"; };
            addAndMakeVisible (sl);
            cap.setText (text, juce::dontSendNotification);
            cap.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            cap.setColour (juce::Label::textColourId, theme::dimText);
            addAndMakeVisible (cap);
        };
        setupSlider (height, heightCap, "Height", "How high the sound is: 0 % at ear level, 100 % overhead (the top speakers)", true);
        setupSlider (width, widthCap, "Width", "How far apart the two sides of the stereo channel are (100 %: full left and full right)", false);
        setupSlider (spread, spreadCap, "Spread", "How much of the sound is spread evenly over every speaker (0 %: only the nearest ones; 100 %: the same in all of them)", false);
        setupSlider (centre, centreCap, "Centre", "How much the centre speaker is used for a sound between left and right (0 %: never - left and right make a phantom centre)", false);
        height.onValueChange = [this] { send.panZ.set ((float) height.getValue() / 100.0f); changedNow(); };
        width.onValueChange  = [this] { send.panWidth.set ((float) width.getValue() / 100.0f); changedNow(); };
        spread.onValueChange = [this] { send.panSpread.set ((float) spread.getValue() / 100.0f); changedNow(); };
        centre.onValueChange = [this] { send.panCentre.set ((float) centre.getValue() / 100.0f); changedNow(); };
        height.setDoubleClickReturnValue (true, 0.0); spread.setDoubleClickReturnValue (true, 0.0); centre.setDoubleClickReturnValue (true, 100.0); width.setDoubleClickReturnValue (true, 100.0);
        for (auto* v : { &heightValue, &widthValue, &spreadValue, &centreValue })
        {
            v->setFont (juce::FontOptions (10.5f)); v->setJustificationType (juce::Justification::centredRight);
            v->setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (v);
        }
        const bool top = surround::layout (format).hasTop();
        height.setEnabled (top); heightCap.setEnabled (top);
        if (! top) height.setTooltip ("This bus (" + surround::name (format) + ") has no top speakers: height does nothing here");
        width.setVisible (channels == 2); widthCap.setVisible (channels == 2); widthValue.setVisible (channels == 2);
        reset.setButtonText ("Front");
        reset.setTooltip ("Back to the front, at ear level, following the channel's pan (as a new send starts)");
        reset.onClick = [this] { send.setPanPos (surround::PanPos()); refresh(); changedNow(); };
        addAndMakeVisible (reset);
        refresh();
        startTimerHz (10);                     // (the channel's pan may move while this is open: the dots follow it)
        setSize (236, channels == 2 ? 300 : 278);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::row);
        if (gone) return;
        const auto room = roomRect();
        g.setColour (theme::field); g.fillRoundedRectangle (room, 6.0f);
        g.setColour (theme::border); g.drawRoundedRectangle (room, 6.0f, 1.0f);
        g.setColour (theme::border.brighter (0.2f)); g.drawRect (ceilingRect(), 1.0f);          // the ceiling (the top speakers' square)
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (9.0f));          // (the front is plain from L C R at the top)
        g.drawText ("back", room.withTrimmedTop (room.getHeight() - 14.0f), juce::Justification::centred, false);
        // each speaker, lit by its gain
        float gains[kMaxBusChannels] = {};
        currentGains (gains);
        const auto& l = surround::layout (format);
        for (int c = 0; c < l.count; ++c)
        {
            const auto& s = surround::info (l.spk[c]);
            if (s.lfe || s.phones) continue;
            const auto p = toScreen (s.x, s.y, s.z > 0.5f);
            const float lit = juce::jlimit (0.0f, 1.0f, gains[c]);
            const float r = 5.0f + 3.0f * lit;
            const auto col = theme::accent.withAlpha (0.25f + 0.75f * lit);
            if (s.z > 0.5f) { g.setColour (col); g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f); }       // top speakers: rings
            else            { g.setColour (col); g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r); }
            g.setColour (theme::dimText.withAlpha (0.9f)); g.setFont (juce::FontOptions (8.0f));
            g.drawText (s.shortName, juce::Rectangle<float> (p.x - 14.0f, p.y + r, 28.0f, 10.0f), juce::Justification::centred, false);
        }
        // the sound
        float xs[2] = {};
        sourceXs (xs);
        const float y = send.panY.get(), z = send.panZ.get();
        const bool top = l.hasTop();
        auto at = [&] (float x) { return toScreen (x, y, false, top ? z : 0.0f); };
        g.setColour (juce::Colour (0xffffc040));
        if (channels == 2) g.drawLine (juce::Line<float> (at (xs[0]), at (xs[1])), 1.5f);
        for (int c = 0; c < channels; ++c)
        {
            const auto p = at (xs[c]);
            g.fillEllipse (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
            g.setColour (juce::Colours::black); g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText (channels == 2 ? (c == 0 ? "L" : "R") : juce::String(), juce::Rectangle<float> (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred, false);
            g.setColour (juce::Colour (0xffffc040));
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        title.setBounds (r.removeFromTop (18));
        r.removeFromTop (4);
        auto top = r.removeFromTop (kRoom);
        roomArea = top.removeFromLeft (kRoom);
        top.removeFromLeft (8);
        heightCap.setBounds (top.removeFromTop (14));
        heightValue.setBounds (top.removeFromBottom (14));
        height.setBounds (top);
        r.removeFromTop (6);
        follow.setBounds (r.removeFromTop (22));
        auto row = [&] (juce::Slider& s, juce::Label& cap, juce::Label& val)
        {
            if (! s.isVisible()) return;
            auto line = r.removeFromTop (22);
            cap.setBounds (line.removeFromLeft (52));
            val.setBounds (line.removeFromRight (40));
            s.setBounds (line);
        };
        row (width, widthCap, widthValue);
        row (spread, spreadCap, spreadValue);
        row (centre, centreCap, centreValue);
        r.removeFromTop (4);
        reset.setBounds (r.removeFromTop (22).removeFromRight (70));
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! gone && roomRect().contains (e.position)) moveTo (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! gone && dragging) moveTo (e.position); }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (gone || ! roomRect().contains (e.position)) return;
        send.panX.set (0.0f); send.panY.set (1.0f);                      // back to the front (height, spread and the rest are kept)
        refresh(); changedNow();
    }

private:
    static constexpr int kRoom = 150;
    juce::Rectangle<float> roomRect() const { return roomArea.toFloat().reduced (2.0f); }
    juce::Rectangle<float> ceilingRect() const { return roomRect().reduced (roomRect().getWidth() * 0.22f); }
    /** Room position (x -1 .. 1, y +1 front .. -1 back) to the screen; on the ceiling square (a top speaker), or between the two by z (the sound). */
    juce::Point<float> toScreen (float x, float y, bool onCeiling, float z = -1.0f) const
    {
        const float k = z >= 0.0f ? z : (onCeiling ? 1.0f : 0.0f);
        const auto a = roomRect().reduced (14.0f), b = ceilingRect();
        const auto box = juce::Rectangle<float> (a.getX() + (b.getX() - a.getX()) * k, a.getY() + (b.getY() - a.getY()) * k,
                                                 a.getWidth() + (b.getWidth() - a.getWidth()) * k, a.getHeight() + (b.getHeight() - a.getHeight()) * k);
        return { box.getCentreX() + x * box.getWidth() * 0.5f, box.getCentreY() - y * box.getHeight() * 0.5f };
    }
    void moveTo (juce::Point<float> p)
    {
        dragging = true;
        const auto a = roomRect().reduced (14.0f);
        const float x = juce::jlimit (-1.0f, 1.0f, (p.x - a.getCentreX()) / (a.getWidth() * 0.5f));
        const float y = juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - p.y) / (a.getHeight() * 0.5f));
        send.panY.set (y);
        if (! send.panFollow.get()) send.panX.set (x);                  // (following the pan: only front to back moves)
        changedNow();
    }
    std::array<float, 3> pans() const { return panOf ? panOf() : std::array<float, 3> { 0.0f, -1.0f, 1.0f }; }
    void sourceXs (float* xs) const
    {
        const auto p = pans();
        surround::sourceXs (send.panPos(), channels, p[0], p[1], p[2], xs);
    }
    float currentCentreX() const { float xs[2] = {}; sourceXs (xs); return channels == 2 ? 0.5f * (xs[0] + xs[1]) : xs[0]; }
    void currentGains (float* out) const
    {
        float xs[2] = {}, m[2 * kMaxBusChannels] = {};
        sourceXs (xs);
        surround::sourceMatrix (format, channels, xs, send.panPos(), m);
        const int n = surround::channels (format);
        for (int j = 0; j < n; ++j)
        {
            float pw = 0.0f;
            for (int c = 0; c < channels; ++c) pw += m[c * n + j] * m[c * n + j];
            out[j] = std::sqrt (pw / (float) channels);
        }
    }
    void refresh()
    {
        follow.setToggleState (send.panFollow.get(), juce::dontSendNotification);
        height.setValue (send.panZ.get() * 100.0, juce::dontSendNotification);
        width.setValue (send.panWidth.get() * 100.0, juce::dontSendNotification);
        spread.setValue (send.panSpread.get() * 100.0, juce::dontSendNotification);
        centre.setValue (send.panCentre.get() * 100.0, juce::dontSendNotification);
        width.setEnabled (! send.panFollow.get());
        heightValue.setText (juce::String (juce::roundToInt (send.panZ.get() * 100.0f)) + " %", juce::dontSendNotification);
        widthValue.setText (juce::String (juce::roundToInt (send.panWidth.get() * 100.0f)) + " %", juce::dontSendNotification);
        spreadValue.setText (juce::String (juce::roundToInt (send.panSpread.get() * 100.0f)) + " %", juce::dontSendNotification);
        centreValue.setText (juce::String (juce::roundToInt (send.panCentre.get() * 100.0f)) + " %", juce::dontSendNotification);
        repaint();
    }
    void changedNow() { refresh(); if (onChanged) onChanged(); }
    void timerCallback() override
    {
        if (stillThere && ! stillThere())
        {
            gone = true; stopTimer();
            for (auto* c : getChildren()) c->setEnabled (false);
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            return;
        }
        const auto p = pans();
        if (p != lastPans) { lastPans = p; repaint(); }
    }
    SendState& send; BusFormat format; int channels;
    std::function<std::array<float, 3>()> panOf; std::function<void()> onChanged; std::function<bool()> stillThere;
    bool gone = false;
    std::array<float, 3> lastPans {};
    juce::Label title, heightCap, widthCap, spreadCap, centreCap, heightValue, widthValue, spreadValue, centreValue;
    juce::ToggleButton follow;
    juce::Slider height, width, spread, centre;
    juce::TextButton reset;
    juce::Rectangle<int> roomArea;
    bool dragging = false;
};

/** 3.39.0: a little round button (a dot in a ring: the room seen from above) that opens a surround panner. */
class SurroundPanButton : public juce::Button
{
public:
    SurroundPanButton() : juce::Button ("Surround position") {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.withSizeKeepingCentre (d, d);
        g.setColour (down ? theme::accent.darker (0.3f) : over ? theme::accent.brighter (0.2f) : theme::accent);
        g.drawEllipse (c.reduced (1.0f), 1.5f);
        g.fillEllipse (c.withSizeKeepingCentre (d * 0.32f, d * 0.32f).translated (0.0f, -d * 0.14f));
    }
};
} // namespace td
