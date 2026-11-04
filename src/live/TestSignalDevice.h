#pragma once
/** fermata:Live 0.2.0: a pretend audio device, "Test signal", listed beside the real drivers in the recorder. It runs a clock of its own and
    hands the callback blocks of tones (input n: a sine at 110 Hz x (n + 1) at -12 dBFS, with a tick every second on input 1), at any of the
    project's sample rates, with 16 inputs and 2 outputs. For trying the whole recorder - session, takes, drives, merge, the take in the project -
    without an audio interface, here on Linux and on any PC. It never touches real audio. */
#include <juce_audio_devices/juce_audio_devices.h>
#include "../core/Project.h"

namespace td::live
{
class TestSignalDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    static constexpr int kInputs = 16, kOutputs = 2;
    TestSignalDevice() : juce::AudioIODevice ("Test signal (16 tones)", "Test signal"), juce::Thread ("fermata:Live test signal") {}
    ~TestSignalDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override { juce::StringArray a; for (int i = 0; i < kOutputs; ++i) a.add ("Test out " + juce::String (i + 1)); return a; }
    juce::StringArray getInputChannelNames() override  { juce::StringArray a; for (int i = 0; i < kInputs; ++i) a.add ("Tone " + juce::String (110 * (i + 1)) + " Hz"); return a; }
    juce::Array<double> getAvailableSampleRates() override { juce::Array<double> r; for (auto x : Project::kProjectRates) r.add (x); return r; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256, 512, 1024, 2048, 4096 }; }
    int getDefaultBufferSize() override { return 2048; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double rate, int buffer) override
    {
        close();
        sampleRate = rate > 0 ? rate : 48000.0; blockSize = buffer > 0 ? buffer : 2048;
        inMask = ins; outMask = outs;
        inputs.assign ((size_t) kInputs, std::vector<float> ((size_t) blockSize, 0.0f));
        outputs.assign ((size_t) kOutputs, std::vector<float> ((size_t) blockSize, 0.0f));
        inPtrs.clear(); outPtrs.clear();
        for (int i = 0; i < kInputs; ++i) if (inMask[i]) inPtrs.push_back (inputs[(size_t) i].data());
        for (int i = 0; i < kOutputs; ++i) if (outMask[i]) outPtrs.push_back (outputs[(size_t) i].data());
        opened = true;
        return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override
    {
        stop();
        callback = cb;
        if (callback != nullptr) { callback->audioDeviceAboutToStart (this); pos = 0; startThread (juce::Thread::Priority::highest); }
    }
    void stop() override
    {
        stopThread (5000);
        if (callback != nullptr) { callback->audioDeviceStopped(); callback = nullptr; }
    }
    bool isPlaying() override { return isThreadRunning(); }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outMask; }
    juce::BigInteger getActiveInputChannels() const override { return inMask; }
    int getOutputLatencyInSamples() override { return blockSize; }
    int getInputLatencyInSamples() override { return blockSize; }

private:
    void run() override
    {
        const double blockSeconds = (double) blockSize / sampleRate;
        auto next = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            int k = 0;
            for (int i = 0; i < kInputs; ++i)
            {
                if (! inMask[i]) continue;
                float* d = inputs[(size_t) i].data();
                const double f = 110.0 * (i + 1);
                for (int s = 0; s < blockSize; ++s)
                {
                    const juce::int64 t = pos + s;
                    float v = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) t / sampleRate);
                    if (i == 0 && (t % (juce::int64) sampleRate) < 200) v += 0.5f;             // a tick on the hour, every second: easy to see in the waveform
                    d[s] = v;
                }
                ++k;
            }
            if (callback != nullptr)
                callback->audioDeviceIOCallbackWithContext (inPtrs.data(), (int) inPtrs.size(), outPtrs.data(), (int) outPtrs.size(), blockSize, {});
            pos += blockSize;
            next += blockSeconds * 1000.0;
            const double waitMs = next - juce::Time::getMillisecondCounterHiRes();
            if (waitMs > 0.5) wait ((int) waitMs);
            else if (waitMs < -500.0) next = juce::Time::getMillisecondCounterHiRes();      // (the machine stalled: do not try to catch up in a burst)
        }
    }

    bool opened = false;
    double sampleRate = 48000.0; int blockSize = 2048;
    juce::BigInteger inMask, outMask;
    std::vector<std::vector<float>> inputs, outputs;
    std::vector<const float*> inPtrs; std::vector<float*> outPtrs;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::int64 pos = 0;
};

class TestSignalDeviceType final : public juce::AudioIODeviceType
{
public:
    TestSignalDeviceType() : juce::AudioIODeviceType ("Test signal") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { "Test signal (16 tones)" }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr && d->getName() == "Test signal (16 tones)" ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& outName, const juce::String& inName) override
    {
        return (outName == "Test signal (16 tones)" || inName == "Test signal (16 tones)") ? new TestSignalDevice() : nullptr;
    }
};
} // namespace td::live
