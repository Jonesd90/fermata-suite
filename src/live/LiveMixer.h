#pragma once
/** fermata:Live 0.3.0: a station's mixer - fermata's mixer strip, reduced to what monitoring needs: per track a fader, its pan (two for a stereo
    track), M, S and a meter; and the master (its level and the stereo pair of outputs it plays to). The Primary's is the project's processing
    mixer (what it hears live, and what Bounce out uses); the Fixer's is its own (kept in the Fixer's settings). Each station gives the view a
    Model; the view only draws it and reports what was moved. Message thread only. */
#include "../ui/MixerLook.h"
#include <functional>
#include <vector>

namespace td::live
{
struct MixStrip
{
    juce::Uuid id; juce::String name; int channels = 1; juce::Colour colour { 0xff3f7fc4 };
    float gainDb = 0.0f, pan = 0.0f, panL = -1.0f, panR = 1.0f; bool mute = false, solo = false;
};

class MixModel
{
public:
    virtual ~MixModel() = default;
    virtual std::vector<MixStrip> strips() const = 0;
    virtual void setGain (const juce::Uuid&, float db) = 0;
    virtual void setPan (const juce::Uuid&, int which, float value) = 0;       // which: 0 mono pan, 1 left channel, 2 right channel
    virtual void setMute (const juce::Uuid&, bool) = 0;
    virtual void setSolo (const juce::Uuid&, bool) = 0;
    virtual float masterDb() const = 0;
    virtual void setMasterDb (float) = 0;
    virtual int outputPair() const = 0;                                         // its first output (0-based), -1: none
    virtual void setOutputPair (int first) = 0;
    virtual juce::StringArray outputNames() const = 0;
    virtual float meter (const juce::Uuid&) const = 0;                          // the track's level now (linear peak), for its meter
    virtual float outputMeter (int channel) const { juce::ignoreUnused (channel); return 0.0f; }   // the level on the output pair now (0 left, 1 right)
    virtual juce::String title() const = 0;
};

class LiveMixerView final : public juce::Component, private juce::Timer
{
public:
    explicit LiveMixerView (MixModel&);
    ~LiveMixerView() override;
    void rebuild();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    class Strip;
    class Master;
    void timerCallback() override;
    MixModel& model;
    MixerLookAndFeel look;
    juce::Viewport view;
    juce::Component holder;
    juce::OwnedArray<Strip> strips;
    std::unique_ptr<Master> master;
    juce::Label title;
    juce::String shownIds;
};
} // namespace td::live
