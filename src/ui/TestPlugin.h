#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace td
{
/** 3.53.0, for developers: a plug-in built into fermata, offered in the slots' menus only when fermata is started with the environment variable
    FERMATA_TEST_PLUGIN=1. It is there so that everything fermata does AROUND a plug-in - the slot, the plug-in's window with its top row
    (pin, Bypass, Copy, Paste), settings saved and pasted, the channel's name told to it - can be tried on a machine that has no plug-ins
    (the Linux test build; a PC before anything is installed). It is a gain and a second control that does nothing to the sound, and its window
    shows the channel name it was told. Nothing of it is reached, listed or saved unless the variable is set. */
namespace testplugin
{
inline bool wanted()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("FERMATA_TEST_PLUGIN", {}).trim() == "1";
    return on;
}

inline juce::PluginDescription describe (const juce::String& name, int uid)
{
    juce::PluginDescription d;
    d.name = name; d.descriptiveName = name; d.pluginFormatName = "FermataTest"; d.category = "Fx"; d.manufacturerName = "fermata (test)";
    d.version = "1"; d.fileOrIdentifier = "fermata-test:" + name; d.uniqueId = d.deprecatedUid = uid;
    d.isInstrument = false; d.numInputChannels = 2; d.numOutputChannels = 2;
    return d;
}

class Instance final : public juce::AudioPluginInstance
{
public:
    explicit Instance (const juce::PluginDescription& d)
        : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)), desc (d)
    {
        // (a hosted instance's own addParameter is closed off; these are plain parameters, added as any processor's are)
        auto& base = static_cast<juce::AudioProcessor&> (*this);
        base.addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID ("gain", 1), "Gain", 0.0f, 2.0f, 1.0f));
        base.addParameter (colour = new juce::AudioParameterFloat (juce::ParameterID ("colour", 1), "Colour", 0.0f, 1.0f, 0.25f));
    }
    const juce::String getName() const override { return desc.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain (gain->get()); }
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream o (data, false);
        o.writeFloat (gain->get()); o.writeFloat (colour->get());
    }
    void setStateInformation (const void* data, int size) override
    {
        if (size < 8) return;
        juce::MemoryInputStream i (data, (size_t) size, false);
        *gain = i.readFloat(); *colour = i.readFloat();
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    void updateTrackProperties (const TrackProperties& p) override
    {
        trackName = p.name.has_value() ? *p.name : juce::String();
        trackColour = p.colour.has_value() ? *p.colour : juce::Colours::transparentBlack;
    }
    juce::AudioParameterFloat* gain = nullptr; juce::AudioParameterFloat* colour = nullptr;
    juce::String trackName; juce::Colour trackColour;               // message thread
private:
    juce::PluginDescription desc;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Instance& i) : juce::AudioProcessorEditor (i), inst (i), gainAtt (*i.gain, gain), colourAtt (*i.colour, colour)
    {
        for (auto* s : { &gain, &colour }) { s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20); addAndMakeVisible (*s); }
        setSize (420, 150);
        startTimerHz (5);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff20252c));
        g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (inst.getName(), 12, 8, getWidth() - 24, 20, juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (inst.trackColour.isTransparent() ? juce::Colours::lightgrey : inst.trackColour.brighter (0.4f));
        g.drawText ("Told its channel is: " + (shown.isEmpty() ? juce::String ("(nothing yet)") : shown), 12, 30, getWidth() - 24, 20, juce::Justification::centredLeft, true);
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Gain", 12, 62, 60, 24, juce::Justification::centredLeft); g.drawText ("Colour", 12, 100, 60, 24, juce::Justification::centredLeft);
    }
    void resized() override { gain.setBounds (76, 62, getWidth() - 88, 24); colour.setBounds (76, 100, getWidth() - 88, 24); }
private:
    void timerCallback() override { if (shown != inst.trackName) { shown = inst.trackName; repaint(); } }
    Instance& inst;
    juce::Slider gain, colour;
    juce::SliderParameterAttachment gainAtt, colourAtt;
    juce::String shown;
};

inline juce::AudioProcessorEditor* Instance::createEditor() { return new Editor (*this); }

/** The "format" that makes them: two kinds (so that Paste can be seen to refuse the other kind's settings). */
class Format final : public juce::AudioPluginFormat
{
public:
    static std::vector<juce::PluginDescription> kinds() { return { describe ("Test plug-in A", 0x7e5701), describe ("Test plug-in B", 0x7e5702) }; }
    juce::String getName() const override { return "FermataTest"; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override
    {
        for (auto& k : kinds()) if (k.fileOrIdentifier == fileOrIdentifier) results.add (new juce::PluginDescription (k));
    }
    bool fileMightContainThisPluginType (const juce::String& f) override { return f.startsWith ("fermata-test:"); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& f) override { return f.fromFirstOccurrenceOf (":", false, false); }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
protected:
    void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback) override
    {
        callback (std::make_unique<Instance> (d), {});
    }
};
} // namespace testplugin
} // namespace td
