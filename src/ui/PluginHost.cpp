#include "PluginHost.h"
#include "TestPlugin.h"
#include "StallWatch.h"
#include "../core/CrashReport.h"
#include "WinOwner.h"
#include "../core/HostTransport.h"
#include "WindowPlace.h"
#include "KeyMap.h"

namespace td
{
/** Wraps a loaded plug-in as an insert. Runs the plug-in as a stereo effect, or (3.39.0) in the speaker layout of the surround bus it sits on: the
    plug-in is asked for that layout (as its VST3 speaker arrangement, so a Dolby Atmos Beam or a 9.1.6 reverb recognises the bus), and the bus's
    channels are handed to it in its own channel order (Surround.h: pluginIndex). It is also the plug-in's play head: it tells the plug-in whether
    Fermata is playing / recording and where on the timeline the sound it is processing is (see HostTransport and currentSlotLatency), which plug-ins
    that listen while the host plays (Auto-Align 2, the Dolby Atmos Composer and its Beams ...) need. */
class Vst3Insert : public InsertProcessor, private juce::AudioPlayHead, private juce::AudioProcessorListener
{
public:
    Vst3Insert (std::unique_ptr<juce::AudioPluginInstance> p, juce::PluginDescription d)
        : plugin (std::move (p)), description (std::move (d)) { plugin->setPlayHead (this); plugin->addListener (this); }

    ~Vst3Insert() override { plugin->removeListener (this); if (prepared) plugin->releaseResources(); plugin->setPlayHead (nullptr); }

    void prepare (double sr, int maxBlock, int layoutCode) override
    {
        if (prepared) plugin->releaseResources();
        format = surround::fromLayoutCode (layoutCode);                       // (1 and 2, mono and stereo, give Stereo)
        note.clear();
        for (auto& m : inMap) m = -1;
        for (auto& m : outMap) m = -1;
        busCh = surround::channels (format);
        if (! surround::isSurround (format))
        {
            // the main input and output as stereo, the proper VST3 way (other buses, a side-chain say, are left as the plug-in has them)
            applyMainLayouts (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
            surroundMode = false;
        }
        else
        {
            // 3.39.0: the bus's own layout - the plug-in may take it on both sides (a surround reverb, a Beam, the Composer), on its output only (a
            // reverb that takes stereo in), or not at all (then it runs in stereo on the bus's L and R, and the other channels pass it by)
            const auto want = surround::pluginChannelSet (format);
            const auto stereo = juce::AudioChannelSet::stereo();
            juce::AudioChannelSet gotIn, gotOut;
            bool ok = applyMainLayouts (want, want, &gotIn, &gotOut);
            if (! ok && surround::isMonitorFormat (format))                   // the Composer's bus: the speakers alone, if it will not take the headphone pair as well
            {
                const auto spk = surround::channelSet (surround::speakersOf (format));
                ok = applyMainLayouts (spk, spk, &gotIn, &gotOut);
                if (ok) note = surround::name (surround::speakersOf (format)) + " only: it would not take the headphone pair as well, so the headphone channels pass it by";
            }
            if (! ok) { ok = applyMainLayouts (stereo, want, &gotIn, &gotOut); if (ok) note = "stereo in, " + surround::name (format) + " out (it is fed the bus's L and R)"; }
            if (! ok) { applyMainLayouts (stereo, stereo, &gotIn, &gotOut); note = "stereo only: it works on L and R of this " + surround::name (format) + " bus; the other channels pass it by"; }
            // where each of the bus's channels goes in, and comes back out: by the plug-in's real layouts (whatever it finally accepted)
            const auto inSet = plugin->getMainBusNumInputChannels() > 0 ? plugin->getChannelLayoutOfBus (true, 0) : juce::AudioChannelSet();
            const auto outSet = plugin->getMainBusNumOutputChannels() > 0 ? plugin->getChannelLayoutOfBus (false, 0) : juce::AudioChannelSet();
            for (int c = 0; c < busCh; ++c)
            {
                inMap[c]  = mapChannel (c, inSet, want, true);
                outMap[c] = mapChannel (c, outSet, want, false);
            }
            surroundMode = true;
            juce::Logger::writeToLog ("Plug-in " + description.name + " on a " + surround::name (format) + " bus: " + (note.isEmpty() ? "runs in " + surround::name (format) : note));
        }
        plugin->setRateAndBufferSizeDetails (sr, maxBlock);
        plugin->prepareToPlay (sr, maxBlock);
        // the buffer handed to the plug-in has as many channels as it uses (at least 2); the ones not fed by the bus (a side-chain) are silent
        chans = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        monoOut = plugin->getMainBusNumOutputChannels() == 1;
        work.setSize (chans, maxBlock);
        midi.ensureSize (256);
        prepared = true;
    }

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        const auto* t = currentHostTransport();
        const double sr = t != nullptr && t->sampleRate > 0.0 ? t->sampleRate : 48000.0;
        // 3.39.0: the position of the sound this plug-in is processing - the transport's, less how late that sound is because of plug-ins before it
        const juce::int64 pos = t != nullptr ? juce::jmax<juce::int64> (0, t->samplePos - (juce::int64) currentSlotLatency()) : 0;
        const double secs = (double) pos / sr;
        info.setIsPlaying (t != nullptr && t->playing);
        info.setIsRecording (t != nullptr && t->recording);
        info.setIsLooping (false);
        info.setTimeInSamples (pos);
        info.setTimeInSeconds (secs);
        info.setBpm (120.0);                                                   // (classical recordings have no tempo map: a plain 120, 4/4)
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setPpqPosition (secs * 2.0);
        info.setPpqPositionOfLastBarStart (std::floor (secs * 2.0 / 4.0) * 4.0);
        info.setFrameRate (juce::AudioPlayHead::fps25);
        return info;
    }

    int getLatencySamples() const override { return plugin != nullptr ? plugin->getLatencySamples() : 0; }

    void process (juce::AudioBuffer<float>& b) override
    {
        const int total = b.getNumSamples(), nc = b.getNumChannels(), cap = work.getNumSamples();
        if (nc < 1 || cap < 1) return;
        // a block longer than the plug-in was prepared for goes through in pieces (it used to skip the plug-in altogether, so the sound went
        // past it untouched: a reverb loaded with a project while the interface ran a bigger buffer than 512 did nothing)
        for (int off = 0; off < total; off += cap)
        {
            const int n = juce::jmin (cap, total - off);
            if (! surroundMode)
            {
                work.copyFrom (0, 0, b, 0, off, n);
                work.copyFrom (1, 0, b, nc > 1 ? 1 : 0, off, n);
                for (int c = 2; c < chans; ++c) work.clear (c, 0, n);
                juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), chans, n);
                midi.clear();
                plugin->processBlock (view, midi);
                if (monoOut) work.copyFrom (1, 0, work, 0, 0, n);                  // a mono-only plug-in: its output on both sides
                b.copyFrom (0, off, work, 0, 0, n);
                if (nc > 1) b.copyFrom (1, off, work, 1, 0, n);
            }
            else
            {   // 3.39.0: the bus's channels into the plug-in's buffer where its layout has them; its outputs back to the channels they belong to
                for (int c = 0; c < chans; ++c) work.clear (c, 0, n);
                const int bc = juce::jmin (nc, busCh);
                for (int c = 0; c < bc; ++c) if (inMap[c] >= 0 && inMap[c] < chans) work.copyFrom (inMap[c], 0, b, c, off, n);
                juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), chans, n);
                midi.clear();
                plugin->processBlock (view, midi);
                for (int c = 0; c < bc; ++c) if (outMap[c] >= 0 && outMap[c] < chans) b.copyFrom (c, off, work, outMap[c], 0, n);     // (a channel it has no output for passes it by)
            }
        }
    }

    juce::String layoutNote() const override { return note; }
    void setNonRealtime (bool b) override { plugin->setNonRealtime (b); }

    juce::String getName() const override { return description.name; }
    juce::String getDescription() const override { if (auto x = description.createXml()) return x->toString(); return {}; }
    juce::String getStateBase64() const override
    {
        stateChanged.store (false);                                           // (before asking: a change made while it answers marks it again)
        juce::MemoryBlock mb; plugin->getStateInformation (mb);
        cachedState = mb.toBase64Encoding(); cachedAt = juce::Time::getMillisecondCounter();
        return cachedState;
    }
    /** The automatic save: the last state, unless the plug-in reported a change since, its window is open (some plug-ins change things there
        without reporting them: a reverb's impulse loaded from a file, say), or it is a minute old. */
    juce::String getStateBase64Quick() const override
    {
        const bool fresh = cachedAt != 0 && ! stateChanged.load() && plugin->getActiveEditor() == nullptr
                           && juce::Time::getMillisecondCounter() - cachedAt < 60000;
        return fresh ? cachedState : getStateBase64();
    }

    int numParams() const override { return plugin->getParameters().size(); }
    juce::String paramName (int i) const override
    {
        auto& ps = plugin->getParameters();
        return juce::isPositiveAndBelow (i, ps.size()) ? ps[i]->getName (64) : juce::String();
    }
    bool paramAutomatable (int i) const override
    {
        auto& ps = plugin->getParameters();
        return juce::isPositiveAndBelow (i, ps.size()) && ps[i]->isAutomatable() && ! ps[i]->isMetaParameter();
    }
    float getParamNorm (int i) const override
    {
        auto& ps = plugin->getParameters();
        return juce::isPositiveAndBelow (i, ps.size()) ? ps[i]->getValue() : 0.0f;
    }
    void setParamNorm (int i, float v) override
    {
        auto& ps = plugin->getParameters();
        if (juce::isPositiveAndBelow (i, ps.size())) { ps[i]->setValue (juce::jlimit (0.0f, 1.0f, v)); stateChanged.store (true); }     // (automation: setValue tells no listener)
    }
    int paramSteps (int i) const override
    {
        auto& ps = plugin->getParameters();
        if (! juce::isPositiveAndBelow (i, ps.size())) return 0;
        auto* p = ps[i];
        if (p->isBoolean()) return 2;
        const int n = p->getNumSteps();                                  // (a smooth control reports juce::AudioProcessor::getDefaultNumParameterSteps(), a huge number)
        return n >= 2 && n <= 100 ? n : 0;
    }
    juce::String paramText (int i, float v) const override
    {
        auto& ps = plugin->getParameters();
        return juce::isPositiveAndBelow (i, ps.size()) ? ps[i]->getText (juce::jlimit (0.0f, 1.0f, v), 24) : juce::String();
    }

    juce::AudioPluginInstance& getPlugin() { return *plugin; }
    const juce::PluginDescription& desc() const noexcept { return description; }
    /** 3.53.0: the channel this plug-in sits on, by name and colour, passed on to it when it changed (see PluginHost::setTrackName). */
    void tellTrack (const juce::String& name, juce::uint32 argb)
    {
        if (name == trackName && argb == trackArgb) return;
        trackName = name; trackArgb = argb;
        juce::AudioProcessor::TrackProperties tp;
        tp.name = name;
        if (argb != 0) tp.colour = juce::Colour (argb);
        plugin->updateTrackProperties (tp);
    }
    juce::String trackName; juce::uint32 trackArgb = 0;
    /** The state was set from outside (a project load, a copy): what is cached is no longer it. */
    void stateWasSet() noexcept { stateChanged.store (true); }

private:
    /** Asks the plug-in for these layouts on its main input and output buses (a bus it has not got is left out). True when it took both; the
        layouts it ended up with are given back. Only called before prepareToPlay (the plug-in is not running). */
    bool applyMainLayouts (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out, juce::AudioChannelSet* gotIn = nullptr, juce::AudioChannelSet* gotOut = nullptr)
    {
        auto layout = plugin->getBusesLayout();
        const bool hasIn = ! layout.inputBuses.isEmpty(), hasOut = ! layout.outputBuses.isEmpty();
        bool change = false;
        if (hasIn  && layout.inputBuses.getReference (0)  != in)  { layout.inputBuses.getReference (0)  = in;  change = true; }
        if (hasOut && layout.outputBuses.getReference (0) != out) { layout.outputBuses.getReference (0) = out; change = true; }
        if (change) plugin->setBusesLayout (layout);                         // (a plug-in that refuses keeps its own)
        const auto nowIn  = hasIn  ? plugin->getChannelLayoutOfBus (true, 0)  : juce::AudioChannelSet();
        const auto nowOut = hasOut ? plugin->getChannelLayoutOfBus (false, 0) : juce::AudioChannelSet();
        if (gotIn != nullptr) *gotIn = nowIn;
        if (gotOut != nullptr) *gotOut = nowOut;
        return (! hasIn || nowIn == in) && (! hasOut || nowOut == out);
    }
    /** Where the bus's channel c sits in the plug-in's process buffer for its main input (isInput) or output bus, given the layout that bus really
        has: the bus's own layout (by speaker, or by position for a monitor format: Surround.h), stereo (L and R only), or -1 for a channel it has no
        place for. */
    int mapChannel (int c, const juce::AudioChannelSet& have, const juce::AudioChannelSet& want, bool isInput) const
    {
        if (have.size() == 0) return -1;
        int k = -1;
        if (have == want) k = surround::pluginIndex (format, c);
        else if (have == juce::AudioChannelSet::stereo()) k = c < 2 ? c : -1;
        else if (have == surround::channelSet (surround::speakersOf (format)) && c < surround::channels (surround::speakersOf (format))) k = surround::juceIndex (surround::speakersOf (format), c);
        else if (have.isDiscreteLayout() && c < have.size()) k = c;
        if (k < 0) return -1;
        return plugin->getChannelIndexInProcessBlockBuffer (isInput, 0, k);
    }

    // AudioProcessorListener (any thread, the audio thread included: only a flag is set)
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { stateChanged.store (true); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override { stateChanged.store (true); }
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer midi;
    mutable juce::String cachedState;                                         // message thread
    mutable juce::uint32 cachedAt = 0;
    mutable std::atomic<bool> stateChanged { true };
    int chans = 2;
    bool monoOut = false;
    bool prepared = false;
    // 3.39.0: the surround bus the plug-in sits on
    BusFormat format = BusFormat::Stereo;
    bool surroundMode = false;
    int busCh = 2;
    int inMap[kMaxBusChannels], outMap[kMaxBusChannels];                      // the bus's channel c -> the plug-in's buffer channel (-1: none)
    juce::String note;                                                        // see layoutNote
};

/** A plug-in's own window. Space still plays / stops the take or edit window that was last in use, even while the plug-in's window has the keyboard
    (the plug-in's editor is a window of its own inside this one and keeps the keys to itself, so the space bar is watched instead). */
class PluginHost::EditorWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    /** 3.51.0: what the window holds - a thin bar of the program's own with the pin on it, and the plug-in's editor under it. (The pin cannot be laid
        over the editor as it is over the program's own windows: a plug-in's editor is a window of its own and would cover it.)
        The editor decides its own size (many change it when a panel is opened, and some refuse the size they are given), so this follows the
        editor; when the window itself is resized (a resizable editor) the editor is given the room under the bar. */
    /** A small key of the top row, drawn like the pin beside it. Lit (amber) while it is on. */
    struct BarKey : public juce::Button
    {
        BarKey (const juce::String& text, int width) : juce::Button (text)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (false);                        // (the keyboard stays with the plug-in)
            setClickingTogglesState (false);
            setSize (width, 22);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState(), live = isEnabled();
            g.setColour ((on ? juce::Colour (0xffc98a1b) : down && live ? theme::grid : over && live ? theme::button.brighter (0.15f) : theme::button).withAlpha (live ? 0.92f : 0.45f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (on ? juce::Colour (0xffffd98a) : theme::border); g.drawRoundedRectangle (b, 4.0f, 1.0f);
            g.setColour ((on ? juce::Colours::white : theme::inkOn (theme::button)).withAlpha (live ? 1.0f : 0.45f));
            g.setFont (juce::FontOptions (11.5f, on ? juce::Font::bold : juce::Font::plain));
            g.drawText (getName(), getLocalBounds(), juce::Justification::centred, false);
        }
    };
    struct Holder : public juce::Component, private juce::ComponentListener
    {
        static constexpr int kBarH = 26;
        Holder (juce::AudioProcessorEditor* e, const juce::String& n) : editor (e), name (n)
        {
            setOpaque (true);
            addAndMakeVisible (*editor); addAndMakeVisible (pin);
            addAndMakeVisible (bypass); addAndMakeVisible (copy); addAndMakeVisible (paste);
            bypass.setTooltip ("Bypass: the sound goes past this plug-in unchanged (the same switch as Bypass in the mixer's slot menu). Click again to bring it back.");
            copy.setTooltip ("Copy this plug-in's settings, to Paste into the same plug-in on any other channel, in any mixer.");
            editor->addComponentListener (this);
            setSize (juce::jmax (60, editor->getWidth()), editor->getHeight() + kBarH);
        }
        ~Holder() override { editor->removeComponentListener (this); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::window);
            g.setColour (theme::panel); g.fillRect (0, 0, getWidth(), kBarH);
            g.setColour (theme::border); g.fillRect (0, kBarH - 1, getWidth(), 1);
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (12.5f));
            g.drawText (place.isNotEmpty() ? name + "   |   " + place : name, 8, 0, juce::jmax (0, bypass.getX() - 14), kBarH - 1, juce::Justification::centredLeft, true);
        }
        void setPlace (const juce::String& p) { if (p != place) { place = p; repaint (0, 0, getWidth(), kBarH); } }
        void resized() override
        {
            const int y = (kBarH - 1 - pin.getHeight()) / 2;
            pin.setTopLeftPosition (getWidth() - pin.getWidth() - 4, y);
            // left to right: Bypass, Copy, Paste, the pin. A very narrow plug-in keeps the pin and Bypass (Copy and Paste are in the slot's menu too)
            const bool roomForAll = pin.getX() - (paste.getWidth() + copy.getWidth() + bypass.getWidth() + 19) >= 4;
            paste.setVisible (roomForAll); copy.setVisible (roomForAll);
            paste.setTopLeftPosition (pin.getX() - paste.getWidth() - 8, y);
            copy.setTopLeftPosition (paste.getX() - copy.getWidth() - 3, y);
            bypass.setTopLeftPosition ((roomForAll ? copy.getX() : pin.getX()) - bypass.getWidth() - 8, y);
            bypass.setVisible (bypass.getX() >= 2);
            if (fitting) return;
            const juce::ScopedValueSetter<bool> s (fitting, true);
            editor->setBounds (0, kBarH, getWidth(), juce::jmax (1, getHeight() - kBarH));
            if (editor->getWidth() != getWidth() || editor->getHeight() != getHeight() - kBarH)      // the editor did not take that size: its own size stands
                setSize (juce::jmax (60, editor->getWidth()), editor->getHeight() + kBarH);
        }
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (fitting) return;
            const juce::ScopedValueSetter<bool> s (fitting, true);
            editor->setTopLeftPosition (0, kBarH);
            if (wasResized) setSize (juce::jmax (60, editor->getWidth()), editor->getHeight() + kBarH);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::String name, place;                 // the plug-in's name, and the channel and mixer it sits on
        PinButton pin;
        BarKey bypass { "Bypass", 56 }, copy { "Copy", 44 }, paste { "Paste", 46 };
        bool fitting = false;
    };
    EditorWindow (PluginHost& h, Vst3Insert* i, juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow (i->getName(), juce::Colours::darkgrey, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          host (h), insert (i)
    {
        keymap::KeyMap::setRaw (*this);                          // 3.69.0: a plug-in gets the keys as they are pressed (Space is watched on the keyboard below)
        setUsingNativeTitleBar (true);
        getProperties().set (ownsRKeyProperty(), true);       // R typed into a plug-in (a preset name ...) is the plug-in's: it does not start a recording (Space still stops one)
        const bool resizable = editor->isResizable();
        auto* holder = new Holder (editor, i->getName());
        holder->pin.onClick = [this, holder] { const bool on = ! holder->pin.getToggleState(); Pins::get().set (*this, on); holder->pin.setToggleState (on, juce::dontSendNotification); };
        // 3.53.0: Bypass, Copy and Paste beside the pin
        holder->bypass.onClick = [this]
        {
            if (auto* slot = host.slotOf ? host.slotOf (insert) : nullptr)
            {
                slot->bypass.set (! slot->bypass.get());
                if (host.slotChanged) host.slotChanged();
            }
            syncBar();
        };
        holder->copy.onClick = [this] { host.copySettings (insert); host.forEachEditor ([] (juce::Component& c) { if (auto* w = dynamic_cast<EditorWindow*> (&c)) w->syncBar(); }); };
        holder->paste.onClick = [this]
        {
            if (host.pasteSettings (insert) && host.slotChanged) host.slotChanged();
            syncBar();
        };
        bar = holder;
        syncBar();
        setContentOwned (holder, true);
        setResizable (resizable, false);
        if (auto* a = place::anchor()) place::over (*this, a);         // 3.64.0: over the window you clicked in (it was centred on Windows' main screen)
        else centreWithSize (getWidth(), getHeight());
        if (host.adoptWindow) host.adoptWindow (*this);
        setVisible (true);
        startTimerHz (40);
    }
    ~EditorWindow() override { stopTimer(); Pins::get().forget (*this); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::spaceKey && ! k.getModifiers().isAnyModifierKeyDown()) return true;   // (the timer below acts on it, so it happens once)
        return juce::DocumentWindow::keyPressed (k);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([h = &host, ins = insert]() { h->closeEditorsFor (ins); });     // (by value: this window may be gone by then)
    }
    PluginHost& host;
    Vst3Insert* insert;
    /** The top row as things are now: Bypass lit while the slot is bypassed (it can be switched in the mixer too), Paste live while the copied
        settings are this plug-in's kind, and the channel and mixer after the plug-in's name. */
    void syncBar()
    {
        if (bar == nullptr) return;
        auto* slot = host.slotOf ? host.slotOf (insert) : nullptr;
        bar->bypass.setEnabled (slot != nullptr);
        bar->bypass.setToggleState (slot != nullptr && slot->bypass.get(), juce::dontSendNotification);
        const bool can = host.canPaste (insert);
        if (bar->paste.isEnabled() != can || pasteTipFor != host.copiedName())
        {
            pasteTipFor = host.copiedName();
            bar->paste.setEnabled (can);
            bar->paste.setTooltip (can ? "Paste the settings copied from " + host.copiedName() + " into this plug-in. Its settings as they are now are replaced."
                                       : host.hasCopied() ? "The copied settings are from " + host.copiedName() + ", another plug-in: they can only be pasted into the same plug-in."
                                                          : juce::String ("Nothing copied yet: press Copy in a plug-in's window (or Copy settings in a slot's menu) first."));
        }
        bar->setPlace (host.trackNameOf (insert));
    }
private:
    juce::Component::SafePointer<Holder> bar;
    juce::String pasteTipFor { "\x01" };
    int barTick = 0;
    bool spaceWasDown = false;
    void timerCallback() override
    {
        if (++barTick >= 8) { barTick = 0; syncBar(); }                                     // (five times a second)
        const bool down = keymap::KeyMap::isDown (keymap::KeyMap::get().current ("Everywhere", keymap::parse ("Space")));     // (3.69.0: the key that is Space's now)
        if (down && ! spaceWasDown && isVisible() && isForegroundWindow (*this))           // pressed (not held) while this window is the one in front
            if (auto* t = ToolWindow::lastTransportWindow())                                // (one down in the bar too, as from any other window)
                if (auto* c = t->getContentComponent()) c->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        spaceWasDown = down;
    }
};

PluginHost::PluginHost() : juce::Thread ("Plug-in scanner")
{
    formats.addFormat (new juce::VST3PluginFormat());
    listFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Fermata").getChildFile ("plugins.xml");
    ignoredFile = listFile.getSiblingFile ("plugins-ignored.txt");          // one line per ignored file: the file, a tab, the names of its plug-ins
    if (auto xml = juce::XmlDocument::parse (listFile))
        known.recreateFromXml (*xml);
    // 3.53.0, for developers: the built-in test plug-ins (TestPlugin.h), only with FERMATA_TEST_PLUGIN=1. Without it nothing of them is offered -
    // also not from a list saved by a run that had it.
    for (auto& t : known.getTypes()) if (t.pluginFormatName == "FermataTest") known.removeType (t);
    if (testplugin::wanted())
    {
        formats.addFormat (new testplugin::Format());
        for (auto& k : testplugin::Format::kinds()) known.addType (k);
    }
    for (auto& line : juce::StringArray::fromLines (ignoredFile.loadFileAsString()))
        if (const auto f = line.upToFirstOccurrenceOf ("\t", false, false).trim(); f.isNotEmpty() && ! ignored.contains (f, true))
        {
            ignored.add (f); ignoredNames.add (line.fromFirstOccurrenceOf ("\t", false, false).trim());
            known.addToBlacklist (f);                                              // (so a scan never opens it, even if plugins.xml was deleted)
            for (auto& t : known.getTypes()) if (t.fileOrIdentifier.equalsIgnoreCase (f)) known.removeType (t);
        }
    // a scan that crashed the program last time: the plug-in it was looking at is left out now (and the list saved), so it shows as CRASHED at once
    if (pedalFile().existsAsFile())
    {
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (known, pedalFile());
        saveLists();
    }
}

void PluginHost::forgetCrashNote (const juce::String& file)
{
    const auto pedal = pedalFile();
    if (! pedal.existsAsFile()) return;
    juce::StringArray lines; pedal.readLines (lines); lines.removeEmptyStrings();
    if (lines.indexOf (file, true) < 0) return;
    lines.removeString (file, true);
    pedal.replaceWithText (lines.joinIntoString ("\n"), true, true);
}

void PluginHost::saveLists()
{
    // (settings, not project data: a list that cannot be written only means a scan again next time, or an ignored plug-in offered again - but it is said in the log)
    listFile.getParentDirectory().createDirectory();
    if (auto xml = known.createXml()) if (! xml->writeTo (listFile)) juce::Logger::writeToLog ("Could not write the list of known plug-ins: " + listFile.getFullPathName());
    juce::StringArray lines;
    for (int i = 0; i < ignored.size(); ++i) lines.add (ignored[i] + "\t" + ignoredNames[i]);
    if (! ignoredFile.replaceWithText (lines.joinIntoString ("\n"))) juce::Logger::writeToLog ("Could not write the list of ignored plug-ins: " + ignoredFile.getFullPathName());
}

std::vector<PluginHost::FileEntry> PluginHost::listFiles() const
{
    std::vector<FileEntry> out;
    std::map<juce::String, juce::StringArray> byFile;
    for (auto& t : known.getTypes()) byFile[t.fileOrIdentifier].add (t.manufacturerName.isNotEmpty() ? t.name + " (" + t.manufacturerName + ")" : t.name);
    auto shortName = [] (const juce::String& f) { return juce::File::isAbsolutePath (f) ? juce::File (f).getFileNameWithoutExtension() : f; };
    for (auto& f : known.getBlacklistedFiles())
    {
        const int i = ignored.indexOf (f, true);
        if (i >= 0) out.push_back ({ f, ignoredNames[i].isNotEmpty() ? ignoredNames[i] : shortName (f), FileEntry::State::ignored });
        else        out.push_back ({ f, shortName (f), FileEntry::State::crashed });
    }
    for (int i = 0; i < ignored.size(); ++i)                                          // (ignored but somehow not on JUCE's list: still shown as ignored)
        if (! known.getBlacklistedFiles().contains (ignored[i], true)) out.push_back ({ ignored[i], ignoredNames[i].isNotEmpty() ? ignoredNames[i] : shortName (ignored[i]), FileEntry::State::ignored });
    for (auto& [f, names] : byFile) if (! ignored.contains (f, true)) out.push_back ({ f, names.joinIntoString (", "), FileEntry::State::found });
    std::stable_sort (out.begin(), out.end(), [] (const FileEntry& a, const FileEntry& b)
    {
        if (a.state != b.state) return (int) a.state > (int) b.state;                 // crashed first, then ignored, then the ones in use
        return a.names.compareIgnoreCase (b.names) < 0;
    });
    return out;
}

bool PluginHost::setIgnored (const juce::String& f, bool ignore, std::function<void()> done)
{
    if (scanning.load() || f.isEmpty()) return false;
    if (ignore)
    {
        if (! ignored.contains (f, true))
        {
            juce::StringArray names;
            for (auto& t : known.getTypes()) if (t.fileOrIdentifier.equalsIgnoreCase (f)) names.add (t.manufacturerName.isNotEmpty() ? t.name + " (" + t.manufacturerName + ")" : t.name);
            ignored.add (f); ignoredNames.add (names.joinIntoString (", "));
        }
        known.addToBlacklist (f);
        for (auto& t : known.getTypes()) if (t.fileOrIdentifier.equalsIgnoreCase (f)) known.removeType (t);
        saveLists();
        if (done) done();
        return true;
    }
    if (const int i = ignored.indexOf (f, true); i >= 0) { ignored.remove (i); ignoredNames.remove (i); }
    known.removeFromBlacklist (f);
    forgetCrashNote (f);
    saveLists();
    onlyFiles = juce::StringArray (f);                                                // scan it again, so its plug-ins are offered again
    return startScan (std::move (done));
}

bool PluginHost::tryAgain (const juce::String& f, std::function<void()> done)
{
    if (scanning.load() || f.isEmpty() || ignored.contains (f, true)) return false;
    known.removeFromBlacklist (f);
    forgetCrashNote (f);                                                              // (else the scanner would put it straight back on the list unscanned)
    saveLists();
    onlyFiles = juce::StringArray (f);
    return startScan (std::move (done));
}

PluginHost::~PluginHost()
{
    stopThread (5000);
    closeAllEditors();
}

bool PluginHost::startScan (std::function<void()> whenFinished)
{
    if (scanning.load()) return false;
    if (isThreadRunning()) waitForThreadToExit (3000);                              // (the last scan's thread may still be on its way out)
    finished = std::move (whenFinished);
    scanning = true;
    if (! startThread()) { scanning = false; onlyFiles.clear(); finished = nullptr; return false; }
    return true;
}

void PluginHost::run()
{
    td::crashreport::nameThisThread (getThreadName());                // (3.17.0: named for the log)
    // The "dead man's pedal": the name of the plug-in being looked at is written to this file before it is opened and taken out again after.
    // A plug-in that crashes the program while it is scanned is found there at the next scan and left out (blacklisted), instead of crashing
    // the program at every scan from then on.
    const auto pedal = pedalFile();
    listFile.getParentDirectory().createDirectory();
    const auto blacklistedBefore = known.getBlacklistedFiles();
    const auto only = onlyFiles;                                                      // (set by the message thread before the thread was started)
    for (auto* format : formats.getFormats())
    {
        juce::PluginDirectoryScanner scanner (known, *format, only.isEmpty() ? format->getDefaultLocationsToSearch() : juce::FileSearchPath(), true, pedal, false);
        if (! only.isEmpty())                                                          // try again / use again: just those files
        {
            juce::StringArray mine;
            for (auto& f : only) if (format->fileMightContainThisPluginType (f)) mine.add (f);
            scanner.setFilesOrIdentifiersToScan (mine);
        }
        if (auto xml = known.createXml()) xml->writeTo (listFile);                    // (the scanner has just left out what crashed last time: keep that even if this scan crashes too)
        juce::String name;
        while (! threadShouldExit() && scanner.scanNextFile (true, name))
        {
            const juce::ScopedLock sl (statusLock);
            status = "Scanning: " + name;
        }
        for (auto& f : scanner.getFailedFiles()) juce::Logger::writeToLog ("Plug-in scan: could not load " + f);
    }
    for (auto& f : known.getBlacklistedFiles())
        if (! blacklistedBefore.contains (f)) juce::Logger::writeToLog ("Plug-in scan: " + f + " crashed the program during an earlier scan and is now left out (Audio settings > Plug-ins... > Try again)");
    if (auto xml = known.createXml()) if (! xml->writeTo (listFile)) juce::Logger::writeToLog ("Plug-in scan: could not write the list of known plug-ins to " + listFile.getFullPathName() + " (the scan runs again next time)");
    {
        const juce::ScopedLock sl (statusLock);
        status = "Found " + juce::String (known.getNumTypes()) + " plug-ins";
    }
    juce::MessageManager::callAsync ([this]()
    {
        onlyFiles.clear();
        scanning = false;                                                             // (here, on the message thread: no new scan can start before onlyFiles is cleared)
        auto cb = std::move (finished); finished = nullptr;
        if (cb) cb();
    });
}

/** 3.45.1: a plug-in that takes a long time to start is named in the log with the time it took. Fermata waits for it on the main thread (a VST3 plug-in
    is made there), so the stall watch is told which plug-in it is waiting for and does not call it a freeze (see PluginStartLabel). */
static void logSlowStart (const juce::String& name, double startedAt, const char* what)
{
    const double ms = juce::Time::getMillisecondCounterHiRes() - startedAt;
    if (ms >= 2000.0)
        juce::Logger::writeToLog ("Plug-in slow to start: " + name + " took " + juce::String (ms / 1000.0, 1) + " s " + what
                                  + " (its own start-up: Fermata waits for it)");
}

std::unique_ptr<InsertProcessor> PluginHost::create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    const PluginStartLabel starting (d.name);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto instance = formats.createPluginInstance (d, sr, block, error);
    logSlowStart (d.name, t0, "to load");
    if (instance == nullptr) return nullptr;
    return std::make_unique<Vst3Insert> (std::move (instance), d);
}

InsertFactory PluginHost::makeFactory (std::function<double()> sampleRate, std::function<int()> blockSize)
{
    return [this, sampleRate, blockSize] (const juce::String& desc, const juce::String& state) -> std::unique_ptr<InsertProcessor>
    {
        // (a plug-in that cannot be started used to go without a word: the slot stayed empty and nothing said why. Now the reason is logged and kept)
        lastFactoryError.clear();
        auto xml = juce::XmlDocument::parse (desc);
        juce::PluginDescription d;
        if (xml == nullptr || ! d.loadFromXml (*xml))
        {
            lastFactoryError = "its description in the project could not be read";
            juce::Logger::writeToLog ("Plug-in not loaded: " + lastFactoryError);
            return nullptr;
        }
        juce::String err;
        auto p = create (d, sampleRate(), blockSize(), err);
        if (p == nullptr)
        {
            lastFactoryError = err.isNotEmpty() ? err : juce::String ("the plug-in gave no reason");
            juce::Logger::writeToLog ("Plug-in not loaded: " + d.name + " (" + d.fileOrIdentifier + "): " + lastFactoryError);
            return nullptr;
        }
        juce::MemoryBlock mb;
        if (state.isNotEmpty() && mb.fromBase64Encoding (state))
        {
            const PluginStartLabel starting (d.name);                         // (3.45.1: its settings going in can take a while too)
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            static_cast<Vst3Insert*> (p.get())->getPlugin().setStateInformation (mb.getData(), (int) mb.getSize());
            static_cast<Vst3Insert*> (p.get())->stateWasSet();
            logSlowStart (d.name, t0, "to take its settings");
        }
        return p;
    };
}

float PluginHost::valueOfText (InsertProcessor* p, int param, const juce::String& text)
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    if (v == nullptr) return -1.0f;
    auto& ps = v->getPlugin().getParameters();
    return juce::isPositiveAndBelow (param, ps.size()) ? juce::jlimit (0.0f, 1.0f, ps[param]->getValueForText (text)) : -1.0f;
}

void PluginHost::showEditor (InsertProcessor* p)
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    if (v == nullptr) return;
    for (auto& e : editors)
        if (e->insert == v) { e->toFront (true); return; }
    if (! v->getPlugin().hasEditor()) return;
    if (auto* ed = v->getPlugin().createEditorIfNeeded())
        editors.push_back (std::make_unique<EditorWindow> (*this, v, ed));
}

void PluginHost::closeEditorsFor (InsertProcessor* p)
{
    for (size_t i = editors.size(); i-- > 0;)
        if (editors[i]->insert == p) editors.erase (editors.begin() + (long) i);
}

void PluginHost::closeAllEditors() { editors.clear(); }

// ---- 3.53.0: copy and paste of a plug-in's settings; the channel's name -----------------------------------------------------------------------
void PluginHost::copySettings (InsertProcessor* p)
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    if (v == nullptr) return;
    clipIdentifier = v->desc().createIdentifierString();
    clipDescription = v->getDescription();
    clipName = v->getName();
    clipState = v->getStateBase64();
}

bool PluginHost::canPaste (InsertProcessor* p) const
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    return v != nullptr && clipState.isNotEmpty() && v->desc().createIdentifierString() == clipIdentifier;
}

bool PluginHost::pasteSettings (InsertProcessor* p)
{
    if (! canPaste (p)) return false;
    auto* v = static_cast<Vst3Insert*> (p);
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (clipState)) return false;
    const PluginStartLabel starting (v->getName());
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    v->getPlugin().setStateInformation (mb.getData(), (int) mb.getSize());
    v->stateWasSet();
    logSlowStart (v->getName(), t0, "to take the pasted settings");
    return true;
}

std::unique_ptr<InsertProcessor> PluginHost::createFromCopied (double sampleRate, int blockSize, juce::String& error)
{
    if (clipState.isEmpty()) { error = "Nothing has been copied."; return nullptr; }
    auto factory = makeFactory ([sampleRate] { return sampleRate; }, [blockSize] { return blockSize; });
    auto p = factory (clipDescription, clipState);
    if (p == nullptr) error = lastFactoryError;
    return p;
}

std::unique_ptr<InsertProcessor> PluginHost::duplicate (InsertProcessor* from, double sampleRate, int blockSize, juce::String& error)
{
    auto* v = dynamic_cast<Vst3Insert*> (from);
    if (v == nullptr) { error = "There is no plug-in to copy."; return nullptr; }
    auto factory = makeFactory ([sampleRate] { return sampleRate; }, [blockSize] { return blockSize; });
    auto p = factory (v->getDescription(), v->getStateBase64());
    if (p == nullptr) error = lastFactoryError;
    return p;
}

juce::String PluginHost::identityOf (InsertProcessor* p) const
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    return v != nullptr ? v->desc().createIdentifierString() : juce::String();
}

void PluginHost::setTrackName (InsertProcessor* p, const juce::String& name, juce::uint32 argb)
{
    if (auto* v = dynamic_cast<Vst3Insert*> (p)) v->tellTrack (name, argb);
}

juce::String PluginHost::trackNameOf (InsertProcessor* p) const
{
    auto* v = dynamic_cast<Vst3Insert*> (p);
    return v != nullptr ? v->trackName : juce::String();
}

void PluginHost::forEachEditor (const std::function<void (juce::Component&)>& fn) { for (auto& e : editors) fn (*e); }

juce::PopupMenu PluginHost::buildPluginMenu (int baseId, std::vector<juce::PluginDescription>& outDescs) const
{
    juce::PopupMenu menu;
    auto types = known.getTypes();
    std::map<juce::String, std::vector<juce::PluginDescription>> byMaker;
    for (auto& t : types)
        if (! isIgnored (t.fileOrIdentifier))                                          // (ignored ones are never offered)
            byMaker[t.manufacturerName.isEmpty() ? juce::String ("Other") : t.manufacturerName].push_back (t);
    int id = baseId;
    for (auto& [maker, list] : byMaker)
    {
        juce::PopupMenu sub;
        for (auto& d : list) { outDescs.push_back (d); sub.addItem (id++, d.name); }
        menu.addSubMenu (maker, sub);
    }
    return menu;
}
} // namespace td
