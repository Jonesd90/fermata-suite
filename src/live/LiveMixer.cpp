#include "LiveMixer.h"
#include "LiveMainWindow.h"

namespace td::live
{
static double faderToDb (double v) { return v <= 0.0001 ? -144.0 : juce::jlimit (-90.0, 12.0, -90.0 + 102.0 * std::pow (v, 0.42)); }
static double dbToFader (double db) { return db <= -90.0 ? 0.0 : std::pow (juce::jlimit (0.0, 1.0, (db + 90.0) / 102.0), 1.0 / 0.42); }
static juce::String dbText (double db) { return db <= -90.0 ? juce::String ("-inf") : (db > 0 ? "+" : "") + juce::String (db, 1); }

/** One track's strip: name, pan(s), M S, fader with its meter, the level. */
class LiveMixerView::Strip final : public juce::Component
{
public:
    Strip (MixModel& m, const MixStrip& s) : id (s.id), model (m), channels (s.channels), colour (s.colour)
    {
        name.setText (s.name, juce::dontSendNotification); name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (12.0f, juce::Font::bold)); name.setColour (juce::Label::textColourId, juce::Colours::white);
        name.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (name);
        fader.setSliderStyle (juce::Slider::LinearVertical); fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 1.0); fader.setDoubleClickReturnValue (true, dbToFader (0.0));
        fader.onValueChange = [this] { if (! updating) model.setGain (id, (float) faderToDb (fader.getValue())); value.setText (dbText (faderToDb (fader.getValue())), juce::dontSendNotification); };
        addAndMakeVisible (fader);
        value.setJustificationType (juce::Justification::centred); value.setFont (juce::FontOptions (11.0f)); value.setColour (juce::Label::textColourId, mixTheme().text);
        value.setEditable (false, true);
        value.onTextChange = [this] { const double db = value.getText().getDoubleValue(); fader.setValue (dbToFader (db), juce::sendNotificationSync); };
        addAndMakeVisible (value);
        for (int k = 0; k < (channels == 2 ? 2 : 1); ++k)
        {
            auto* p = pans.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            p->setRange (-1.0, 1.0); p->setDoubleClickReturnValue (true, channels == 2 ? (k == 0 ? -1.0 : 1.0) : 0.0);
            p->setTooltip (channels == 2 ? (k == 0 ? "Where the left channel goes" : "Where the right channel goes") : "Pan");
            p->onValueChange = [this, k, p] { if (! updating) model.setPan (id, channels == 2 ? 1 + k : 0, (float) p->getValue()); };
            addAndMakeVisible (p);
        }
        mute.setClickingTogglesState (true); solo.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0a040)); solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e9e4f));
        mute.onClick = [this] { if (! updating) model.setMute (id, mute.getToggleState()); };
        solo.onClick = [this] { if (! updating) model.setSolo (id, solo.getToggleState()); };
        addAndMakeVisible (mute); addAndMakeVisible (solo);
        mute.setWantsKeyboardFocus (false); solo.setWantsKeyboardFocus (false);       // (the keys go on to the take window)
        addAndMakeVisible (meter);
        set (s);
    }
    void set (const MixStrip& s)
    {
        updating = true;
        if (! fader.isMouseButtonDown()) fader.setValue (dbToFader (s.gainDb), juce::dontSendNotification);
        value.setText (dbText (s.gainDb), juce::dontSendNotification);
        if (pans.size() == 1 && ! pans[0]->isMouseButtonDown()) pans[0]->setValue (s.pan, juce::dontSendNotification);
        if (pans.size() == 2) { if (! pans[0]->isMouseButtonDown()) pans[0]->setValue (s.panL, juce::dontSendNotification); if (! pans[1]->isMouseButtonDown()) pans[1]->setValue (s.panR, juce::dontSendNotification); }
        mute.setToggleState (s.mute, juce::dontSendNotification); solo.setToggleState (s.solo, juce::dontSendNotification);
        name.setText (s.name, juce::dontSendNotification);
        updating = false;
    }
    void tick() { meter.setLevel (model.meter (id)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 4);
        name.setBounds (r.removeFromBottom (22));
        r.removeFromBottom (4);
        auto panRow = r.removeFromTop (40);
        if (pans.size() == 1) pans[0]->setBounds (panRow.withSizeKeepingCentre (40, 40));
        else if (pans.size() == 2) { auto a = panRow.removeFromLeft (panRow.getWidth() / 2); pans[0]->setBounds (a.withSizeKeepingCentre (34, 34)); pans[1]->setBounds (panRow.withSizeKeepingCentre (34, 34)); }
        r.removeFromTop (4);
        auto ms = r.removeFromTop (22);
        mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0)); solo.setBounds (ms.reduced (1, 0));
        r.removeFromTop (4);
        value.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (2);
        meter.setBounds (r.removeFromRight (8));
        r.removeFromRight (2);
        fader.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = mixTheme();
        auto b = getLocalBounds();
        g.setColour (t.strip); g.fillRect (b);
        g.setColour (t.line); g.drawRect (b, 1);
        g.setColour (colour); g.fillRect (b.removeFromBottom (26).reduced (3, 2));
    }
    juce::Uuid id;
private:
    MixModel& model;
    int channels;
    juce::Colour colour;
    bool updating = false;
    juce::Label name, value;
    juce::Slider fader;
    juce::OwnedArray<juce::Slider> pans;
    juce::TextButton mute { "M" }, solo { "S" };
    LevelMeter meter { true };
};

/** The master: its level, and the output pair it plays to. */
class LiveMixerView::Master final : public juce::Component
{
public:
    explicit Master (MixModel& m) : model (m)
    {
        caption.setText ("MAIN OUT", juce::dontSendNotification); caption.setJustificationType (juce::Justification::centred);
        caption.setFont (juce::FontOptions (12.0f, juce::Font::bold)); caption.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (caption);
        outs.setTooltip ("The stereo pair of driver outputs this mixer plays to (any pair; two mixers on one pair add up)");
        outs.onChange = [this] { if (! updating) model.setOutputPair (outs.getSelectedId() - 2); };
        addAndMakeVisible (outs);
        fader.setSliderStyle (juce::Slider::LinearVertical); fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 1.0); fader.setDoubleClickReturnValue (true, dbToFader (0.0));
        fader.onValueChange = [this] { if (! updating) model.setMasterDb ((float) faderToDb (fader.getValue())); value.setText (dbText (faderToDb (fader.getValue())), juce::dontSendNotification); };
        addAndMakeVisible (fader);
        value.setJustificationType (juce::Justification::centred); value.setFont (juce::FontOptions (11.0f)); value.setColour (juce::Label::textColourId, mixTheme().text);
        addAndMakeVisible (value);
        addAndMakeVisible (meterL); addAndMakeVisible (meterR);
        refresh();
    }
    void tick() { meterL.setLevel (model.outputMeter (0)); meterR.setLevel (model.outputMeter (1)); }
    void refresh()
    {
        updating = true;
        const auto names = model.outputNames();
        if (names.size() != shownOuts)
        {
            shownOuts = names.size();
            outs.clear (juce::dontSendNotification);
            outs.addItem ("(no output)", 1);
            for (int i = 0; i + 1 < names.size(); i += 2) outs.addItem (juce::String (i + 1) + "-" + juce::String (i + 2) + "  " + names[i] + " / " + names[i + 1], i + 2);
            for (int i = 1; i + 1 < names.size(); i += 2) outs.addItem (juce::String (i + 1) + "-" + juce::String (i + 2) + "  (odd pair)", i + 2);
        }
        outs.setSelectedId (model.outputPair() >= 0 ? model.outputPair() + 2 : 1, juce::dontSendNotification);
        if (! fader.isMouseButtonDown()) fader.setValue (dbToFader (model.masterDb()), juce::dontSendNotification);
        value.setText (dbText (model.masterDb()), juce::dontSendNotification);
        updating = false;
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        caption.setBounds (r.removeFromBottom (22));
        outs.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        value.setBounds (r.removeFromBottom (18));
        auto meters = r.removeFromRight (22);
        meterL.setBounds (meters.removeFromLeft (10)); meters.removeFromLeft (2); meterR.setBounds (meters.removeFromLeft (10));
        fader.setBounds (r.withSizeKeepingCentre (40, r.getHeight()));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = mixTheme();
        g.setColour (t.strip.darker (0.15f)); g.fillRect (getLocalBounds());
        g.setColour (t.line); g.drawRect (getLocalBounds(), 1);
        g.setColour (juce::Colour (0xffc9503f)); g.fillRect (getLocalBounds().removeFromBottom (26).reduced (3, 2));
    }
private:
    MixModel& model;
    bool updating = false;
    int shownOuts = -1;
    juce::Label caption, value;
    juce::ComboBox outs;
    juce::Slider fader;
    LevelMeter meterL { true }, meterR { true };
};

LiveMixerView::LiveMixerView (MixModel& m) : model (m)
{
    setLookAndFeel (&look);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold)); title.setColour (juce::Label::textColourId, mixTheme().text);
    addAndMakeVisible (title);
    view.setViewedComponent (&holder, false); view.setScrollBarsShown (false, true);
    addAndMakeVisible (view);
    master = std::make_unique<Master> (model);
    addAndMakeVisible (*master);
    rebuild();
    startTimerHz (20);
}
LiveMixerView::~LiveMixerView() { strips.clear(); master.reset(); setLookAndFeel (nullptr); }

void LiveMixerView::rebuild()
{
    const auto list = model.strips();
    juce::String ids;
    for (auto& s : list) ids << s.id.toString() << s.channels << ";";
    if (ids != shownIds)
    {
        shownIds = ids;
        strips.clear();
        for (auto& s : list) holder.addAndMakeVisible (strips.add (new Strip (model, s)));
        resized();
    }
    else for (size_t i = 0; i < list.size() && (int) i < strips.size(); ++i) strips[(int) i]->set (list[i]);
    master->refresh();
    title.setText (model.title(), juce::dontSendNotification);
}

void LiveMixerView::timerCallback()
{
    for (auto* s : strips) s->tick();
    master->tick();
    static int n = 0;
    if (++n % 10 == 0) rebuild();
}

void LiveMixerView::resized()
{
    auto r = getLocalBounds().reduced (6);
    title.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);
    master->setBounds (r.removeFromRight (150));
    r.removeFromRight (6);
    view.setBounds (r);
    const int h = r.getHeight() - 14;
    int x = 0;
    for (auto* s : strips) { s->setBounds (x, 0, 86, h); x += 88; }
    holder.setSize (juce::jmax (x, r.getWidth()), h);
}

void LiveMixerView::paint (juce::Graphics& g) { g.fillAll (mixTheme().bg); }

//==============================================================================================================================================
/** The Primary's mixer: the project's processing mixer (its strips' levels, pans, mutes and solos, and its main Ext Bus's level and pair). */
class PrimaryMixModel final : public MixModel
{
public:
    explicit PrimaryMixModel (PrimaryApp& a) : app (a) {}
    std::vector<MixStrip> strips() const override
    {
        std::vector<MixStrip> out;
        auto* m = app.monitorMixer();
        if (m == nullptr) return out;
        for (auto& t : app.project.tracks)
        {
            if (t.isLtc) continue;
            auto* s = m->stripFor (t.id);
            if (s == nullptr || std::find (m->hiddenStrips.begin(), m->hiddenStrips.end(), t.id) != m->hiddenStrips.end()) continue;
            MixStrip x; x.id = t.id; x.name = t.name; x.channels = t.channelCount(); x.colour = chan::of (app.project, t.id);
            x.gainDb = s->gainDb.get(); x.pan = s->pan.get(); x.panL = s->panL.get(); x.panR = s->panR.get(); x.mute = s->mute.get(); x.solo = s->solo.get();
            out.push_back (x);
        }
        return out;
    }
    StripState* strip (const juce::Uuid& id) const { auto* m = app.monitorMixer(); return m != nullptr ? m->stripFor (id) : nullptr; }
    void setGain (const juce::Uuid& id, float db) override { if (auto* s = strip (id)) { s->gainDb.set (db); if (! s->outOn.get()) s->outOn.set (true); changed(); } }
    void setPan (const juce::Uuid& id, int which, float v) override { if (auto* s = strip (id)) { (which == 0 ? s->pan : which == 1 ? s->panL : s->panR).set (v); changed(); } }
    void setMute (const juce::Uuid& id, bool b) override { if (auto* s = strip (id)) { s->mute.set (b); changed(); } }
    void setSolo (const juce::Uuid& id, bool b) override { if (auto* s = strip (id)) { s->solo.set (b); changed(); } }
    BusState* mainBus() const
    {
        auto* m = app.monitorMixer();
        if (m == nullptr) return nullptr;
        for (int bi : app.project.busIndices (true)) if (auto* bs = m->busFor (app.project.buses[(size_t) bi].id)) if (bs->isMain.get()) return bs;
        return nullptr;
    }
    float masterDb() const override { auto* b = mainBus(); return b != nullptr ? b->gainDb.get() : 0.0f; }
    void setMasterDb (float db) override { if (auto* b = mainBus()) { b->gainDb.set (db); changed(); } }
    int outputPair() const override { return app.monitorPair(); }
    void setOutputPair (int first) override { app.setMonitorPair (first); }
    juce::StringArray outputNames() const override
    {
        juce::StringArray n;
        for (int i = 0; i < (int) app.project.outputs.size(); ++i) n.add (app.project.outputLabel (i));
        return n;
    }
    float meter (const juce::Uuid& id) const override
    {
        auto* t = app.project.findTrack (id);
        if (t == nullptr) return 0.0f;
        float pk = 0.0f;
        for (int c = 0; c < t->channelCount(); ++c)
        {
            const int in = t->inputOf (c);
            if (juce::isPositiveAndBelow (in, (int) app.model.inPeaks.size())) pk = juce::jmax (pk, app.model.inPeaks[(size_t) in]);
        }
        return pk;
    }
    float outputMeter (int ch) const override
    {
        const int o = app.monitorPair() + ch;
        return o >= 0 && o < (int) app.model.outPeaks.size() ? app.model.outPeaks[(size_t) o] : 0.0f;
    }
    juce::String title() const override { return "Processing mixer - what the Primary hears live (and plays back), and what Bounce out bounces. No plug-ins in fermata:Live's monitoring."; }
private:
    void changed() { app.markDirty(); app.mixChanged(); }
    PrimaryApp& app;
};

class PrimaryMixerComponent final : public juce::Component
{
public:
    explicit PrimaryMixerComponent (PrimaryApp& a) : model (a), view (model) { addAndMakeVisible (view); }
    void resized() override { view.setBounds (getLocalBounds()); }
private:
    PrimaryMixModel model;
    LiveMixerView view;
};

std::unique_ptr<juce::Component> makePrimaryMixer (PrimaryApp& app) { return std::make_unique<PrimaryMixerComponent> (app); }
} // namespace td::live
