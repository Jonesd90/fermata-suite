#pragma once
#include "AppContext.h"
#include <set>

namespace td
{
/** The meter bridge: a floating window of tall vertical meters, one for every driver input (BEFORE any fader - what arrives from the
    preamps) and one for every driver output (AFTER the faders - what leaves for the speakers / headphones / foldback). Press B to open or close.
    3.60.0 (asked for on 23 November 2026): "Used I/O only" - only the inputs a track records from (a strip in the mixer) and the outputs something
    is routed to (an Ext Bus of any mixer, a talkback pair, the click, the pitch pipe, LTC out); it follows the routing by itself as it changes.
    Off: every input and output of the driver. Remembered. */
class MeterBridgeComponent : public juce::Component, private PeakListener, private juce::Timer
{
public:
    explicit MeterBridgeComponent (AppContext& a) : app (a)
    {
        resetButton.setButtonText ("Reset peaks");
        resetButton.setTooltip ("Clears every peak-hold marker. A peak above -2 dBFS stays marked until you press this; others fade after 10 s.");
        resetButton.onClick = [this] { for (auto* m : inMeters) m->resetPeak(); for (auto* m : outMeters) m->resetPeak(); };
        addAndMakeVisible (resetButton);
        usedOnly.setButtonText ("Used I/O only");
        usedOnly.setTooltip ("Show only the inputs a track records from (a strip in the mixer) and the outputs something is routed to (an Ext Bus, a talkback pair, the click, the pitch pipe, LTC out). It follows the routing as it changes. Untick to see every input and output of the driver.");
        if (auto* st = app.props.getUserSettings()) usedOnly.setToggleState (st->getBoolValue ("bridgeUsedOnly", false), juce::dontSendNotification);
        usedOnly.onClick = [this]
        {
            if (auto* st = app.props.getUserSettings()) { st->setValue ("bridgeUsedOnly", usedOnly.getToggleState()); app.props.saveIfNeeded(); }
            rebuild(); layoutHolder();
        };
        addAndMakeVisible (usedOnly);
        caption.setText ("Meter bridge   -   inputs: before the faders      outputs: after the faders", juce::dontSendNotification);
        caption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (caption);
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
        for (auto* l : { &inCaption, &outCaption })
        {
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, theme::dimText);
            holder.addAndMakeVisible (l);
        }
        inCaption.setText ("INPUTS (pre-fader)", juce::dontSendNotification);
        outCaption.setText ("OUTPUTS (post-fader)", juce::dontSendNotification);
        holder.addAndMakeVisible (inScale); holder.addAndMakeVisible (outScale);
        rebuild();
        app.project.structure.addChangeListener (&relay);
        relay.fn = [this] { rebuild(); resized(); };
        app.peakListeners.add (this);
        startTimer (500);                                    // (the routing can change without a message: a mixer's Ext Bus output, a talkback pair ...)
        setSize (1100, 420);
    }
    ~MeterBridgeComponent() override
    {
        stopTimer();
        app.peakListeners.remove (this);
        app.project.structure.removeChangeListener (&relay);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (28);
        resetButton.setBounds (top.removeFromRight (110).reduced (2));
        top.removeFromRight (8);
        usedOnly.setBounds (top.removeFromRight (130));
        caption.setBounds (top);
        viewport.setBounds (r);
        layoutHolder();
    }

private:
    struct Relay : juce::ChangeListener { std::function<void()> fn; void changeListenerCallback (juce::ChangeBroadcaster*) override { if (fn) fn(); } };
    struct Cell : juce::Component, juce::SettableTooltipClient
    {
        Cell() { meter.setInterceptsMouseClicks (false, false); addAndMakeVisible (meter); addAndMakeVisible (number); number.setJustificationType (juce::Justification::centred); number.setFont (juce::FontOptions (10.5f)); number.setMinimumHorizontalScale (0.6f); number.setInterceptsMouseClicks (false, false); }
        void resized() override { auto r = getLocalBounds(); number.setBounds (r.removeFromBottom (16)); meter.setBounds (r.reduced (1, 0)); }
        LevelMeter meter { true }; juce::Label number;
    };

    /** The inputs a track records from: every channel of every track (a strip in the mixer). */
    std::vector<int> usedInputs() const
    {
        std::set<int> used;
        const int n = (int) app.project.inputs.size();
        for (auto& t : app.project.tracks)
            for (int c = 0; c < t.channelCount(); ++c)
                if (const int i = t.inputOf (c); juce::isPositiveAndBelow (i, n)) used.insert (i);
        return { used.begin(), used.end() };
    }
    /** The outputs something is routed to: the Ext Buses of every mixer (a surround one: each speaker, and its headphone pair), the talkback pairs,
        the click, the pitch pipe and LTC out. */
    std::vector<int> usedOutputs() const
    {
        std::set<int> used;
        const int n = (int) app.project.outputs.size();
        auto add = [&] (int o) { if (juce::isPositiveAndBelow (o, n)) used.insert (o); };
        for (auto& m : app.project.mixers)
            for (auto& b : app.project.buses)
                if (b.external)
                    for (auto& bs : m->busPool)
                        if (bs->busId == b.id)
                        {
                            const int f = bs->outFirst.load(), h = bs->hpFirst.load();
                            if (f >= 0) for (int c = 0; c < surround::speakerChannels (b.format); ++c) add (f + c);
                            if (surround::isMonitorFormat (b.format) && h >= 0) { add (h); add (h + 1); }
                        }
        for (int o : app.project.tbOutputs) { add (o); add (o + 1); }          // (a pair: that output and the next; on the last output, mono)
        for (int o : app.project.click.outputs) add (o);
        for (int o : app.project.pitchPipe.outputs) add (o);
        if (app.project.ltcOutOn) add (app.project.ltcOutput);
        return { used.begin(), used.end() };
    }
    static std::vector<int> allOf (int n) { std::vector<int> v; for (int i = 0; i < n; ++i) v.push_back (i); return v; }
    void timerCallback() override
    {
        if (! usedOnly.getToggleState()) return;
        if (usedInputs() != inIdx || usedOutputs() != outIdx) { rebuild(); layoutHolder(); }
    }

    void rebuild()
    {
        inCells.clear(); outCells.clear(); inMeters.clear(); outMeters.clear();
        const bool used = usedOnly.getToggleState();
        const int nIn = (int) app.project.inputs.size(), nOut = (int) app.project.outputs.size();
        inIdx = used ? usedInputs() : allOf (nIn);
        outIdx = used ? usedOutputs() : allOf (nOut);
        for (int i : inIdx)
        {
            auto* c = inCells.add (new Cell());
            c->number.setText (juce::String (i + 1), juce::dontSendNotification);      // (its real number, whichever are shown)
            c->setTooltip ("Input " + app.project.inputLabel (i));
            holder.addAndMakeVisible (c); inMeters.add (&c->meter);
        }
        for (int i : outIdx)
        {
            auto* c = outCells.add (new Cell());
            c->number.setText (juce::String (i + 1), juce::dontSendNotification);
            c->setTooltip ("Output " + app.project.outputLabel (i));
            holder.addAndMakeVisible (c); outMeters.add (&c->meter);
        }
        inCaption.setText (used ? (inIdx.empty() ? juce::String ("INPUTS: none used by a track") : "INPUTS (pre-fader): " + juce::String ((int) inIdx.size()) + " of " + juce::String (nIn) + " used")
                                : juce::String ("INPUTS (pre-fader)"), juce::dontSendNotification);
        outCaption.setText (used ? (outIdx.empty() ? juce::String ("OUTPUTS: none routed") : "OUTPUTS (post-fader): " + juce::String ((int) outIdx.size()) + " of " + juce::String (nOut) + " used")
                                 : juce::String ("OUTPUTS (post-fader)"), juce::dontSendNotification);
    }
    void layoutHolder()
    {
        const int nIn = inCells.size(), nOut = outCells.size();
        const int availW = juce::jmax (200, viewport.getWidth() - 4);
        const int scaleW = 26, gap = 30, capH = 18;
        const int cellW = juce::jlimit (14, 34, (availW - 2 * scaleW - gap) / juce::jmax (1, nIn + nOut));
        const int h = juce::jmax (120, viewport.getHeight() - viewport.getScrollBarThickness());
        const int meterTop = capH, meterBottom = h - 16;
        int x = 0;
        inCaption.setBounds (x, 0, juce::jmax (usedOnly.getToggleState() ? 210 : 80, nIn * cellW + scaleW), capH);
        inScale.setBounds (x, meterTop, scaleW, meterBottom - meterTop); x += scaleW;
        for (auto* c : inCells) { c->setBounds (x, meterTop, cellW, h - meterTop); x += cellW; }
        x = juce::jmax (x + gap, usedOnly.getToggleState() ? inCaption.getRight() + 12 : 0);      // (a short row of used inputs: room for its caption)
        outCaption.setBounds (x, 0, juce::jmax (usedOnly.getToggleState() ? 210 : 80, nOut * cellW + scaleW), capH);
        outScale.setBounds (x, meterTop, scaleW, meterBottom - meterTop); x += scaleW;
        for (auto* c : outCells) { c->setBounds (x, meterTop, cellW, h - meterTop); x += cellW; }
        holder.setSize (juce::jmax (juce::jmax (x, outCaption.getRight()), availW), h);
    }
    void peaksUpdated() override
    {
        for (int k = 0; k < inMeters.size() && k < (int) inIdx.size(); ++k)            // (each cell is the input / output it is numbered with)
            if (const int i = inIdx[(size_t) k]; juce::isPositiveAndBelow (i, (int) app.inPeaks.size())) inMeters[k]->setLevel (app.inPeaks[(size_t) i]);
        for (int k = 0; k < outMeters.size() && k < (int) outIdx.size(); ++k)
            if (const int i = outIdx[(size_t) k]; juce::isPositiveAndBelow (i, (int) app.outPeaks.size())) outMeters[k]->setLevel (app.outPeaks[(size_t) i]);
    }

    AppContext& app;
    Relay relay;
    juce::Label caption, inCaption, outCaption;
    juce::TextButton resetButton;
    juce::ToggleButton usedOnly;
    std::vector<int> inIdx, outIdx;                       // the input / output each cell shows
    juce::Viewport viewport; juce::Component holder;
    MeterScale inScale, outScale;
    juce::OwnedArray<Cell> inCells, outCells;
    juce::Array<LevelMeter*> inMeters, outMeters;
};
} // namespace td
