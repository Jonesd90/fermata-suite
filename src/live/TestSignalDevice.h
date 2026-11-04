#pragma once
/** fermata:Live 0.2.0: a pretend audio device, "Test signal", listed beside the real drivers in the recorder. It runs a clock of its own and
    hands the callback blocks of tones (input n: a sine at 110 Hz x (n + 1) at -24 dBFS, with a tick every second on input 1 - 0.3.0: quieter than 0.2.0's -12 dBFS, so the 16
    tones added up in the monitoring and in Bounce out stay under 0 dBFS), at any of the
    project's sample rates, with 16 inputs and 2 outputs. For trying the whole recorder - session, takes, drives, merge, the take in the project -
    without an audio interface, here on Linux and on any PC. It never touches real audio.
    0.3.0: it can also pretend the faults a real driver has (the recorder's "test-signal" command): give no audio for a while, stop and start
    itself again (as an ASIO driver does when it asks to be reset) - at another rate, too - or stop for good (a USB device pulled out); and its
    type can pretend that a second device was plugged in or out. */
#include <juce_audio_devices/juce_audio_devices.h>
#include "../core/Project.h"

namespace td::live
{
class TestSignalDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    static constexpr int kInputs = 16, kOutputs = 2;
    explicit TestSignalDevice (const juce::String& name = "Test signal (16 tones)") : juce::AudioIODevice (name, "Test signal"), juce::Thread ("fermata:Live test signal") {}
    ~TestSignalDevice() override { close(); }

    // ---- pretend faults (0.3.0, the message thread) ----
    /** Gives no audio for this long (the callback is not called), then carries on. */
    void pauseFor (int ms) { pauseMs = juce::jmax (0, ms); notify(); }
    /** Stops and starts itself again after 0.6 s, as a driver that asked to be reset is started again by JUCE (at another rate if rate > 0). */
    void restartItself (double rate)
    {
        auto* cb = callback;
        if (cb == nullptr) return;
        stop();
        juce::Thread::sleep (600);
        if (rate > 0.0) sampleRate = rate;
        start (cb);
    }
    /** Stops for good, as a device that was pulled out. */
    void dieForTests() { stop(); }

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
            if (const int p = pauseMs.exchange (0); p > 0)
            {   // (a pretend fault: no audio for a while - and no burst afterwards to catch up, as a real driver gives none)
                wait (p);
                next = juce::Time::getMillisecondCounterHiRes();
                pos += (juce::int64) (p * sampleRate / 1000.0);
                continue;
            }
            int k = 0;
            for (int i = 0; i < kInputs; ++i)
            {
                if (! inMask[i]) continue;
                float* d = inputs[(size_t) i].data();
                const double f = 110.0 * (i + 1);
                for (int s = 0; s < blockSize; ++s)
                {
                    const juce::int64 t = pos + s;
                    float v = 0.063f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) t / sampleRate);
                    if (i == 0 && (t % (juce::int64) sampleRate) < 200) v += 0.25f;            // a tick on the hour, every second: easy to see in the waveform
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
    std::atomic<int> pauseMs { 0 };
};

class TestSignalDeviceType final : public juce::AudioIODeviceType
{
public:
    static constexpr const char* kFirst = "Test signal (16 tones)";
    static constexpr const char* kSecond = "Test signal 2 (16 tones)";       // (0.3.0: the pretend second device, while it is "plugged in")
    TestSignalDeviceType() : juce::AudioIODeviceType ("Test signal") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { juce::StringArray a { kFirst }; if (second) a.add (kSecond); return a; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override { return d != nullptr ? getDeviceNames (asInput).indexOf (d->getName()) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& outName, const juce::String& inName) override
    {
        const auto& n = outName.isNotEmpty() ? outName : inName;
        return getDeviceNames (true).contains (n) ? new TestSignalDevice (n) : nullptr;
    }
    /** Pretends the second device was plugged in (or pulled out): the list changes and the type's listeners are told, as a real driver's are. */
    void plugSecond (bool in) { if (in == second) return; second = in; callDeviceChangeListeners(); }

private:
    bool second = false;
};
} // namespace td::live
