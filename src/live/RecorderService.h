#pragma once
/** fermata:Live 0.2.0: the recorder process. "fermata-Live.exe --recorder <port>" runs this and nothing else: no window; the audio device, the
    session (LiveRecorder), the merge, and a control port on 127.0.0.1:<port> (the shared remote::Hub) that the fermata:Live window talks to in
    JSON lines (the protocol is listed in docs/LIVE-DEVELOPMENT-NOTES.md). Nothing else runs in this process - no plug-ins, no drawing, no waveform
    building - so nothing else can stop the recording: the window can be closed, or die, and the recorder goes on until it is told to stop; a
    window started again finds it on the same port and carries on with it. While a take records, no command may end it: end-session, close-device,
    open and quit are refused (Stop first).

    Threads: the message thread runs the commands and the status; the audio callback feeds the session (process() into the ring, the input
    peaks); the merge thread runs the merges, talking to the session only through its own thread-safe calls (it keeps the session alive with a
    shared_ptr, so a merge that ends after the session has been closed still finishes and writes the indexes). */
#include "LiveRecorder.h"
#include "Merge.h"
#include "../core/RemoteHub.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>

namespace td::live
{
class RecorderService final : private juce::AudioIODeviceCallback, private juce::Timer
{
public:
    /** port: the control port. version: the program's. */
    RecorderService (int port, const juce::String& version);
    ~RecorderService() override;

    bool isListening() const                       { return hub.isListening(); }
    /** The window said quit (with nothing recording and no merge going): the application ends. */
    std::function<void()> onQuit;

    /** The default control port. */
    static constexpr int kDefaultPort = 57120;

private:
    // ---- the control port (message thread) ----
    void handleLine (const juce::String& line);
    void send (const juce::var& event);
    juce::var statusEvent() const;
    void log (const juce::String& text);
    void sendDevices();
    void openDevice (const juce::var& c);
    void closeDevice();
    void startSession (const juce::var& c);
    void endSession();
    void record (const juce::var& c);
    void stop();
    void takeRegistered (const juce::var& c);
    void takeInfo (const juce::var& c);
    void mergeBackup (const juce::var& c);
    void resumeDrive (const juce::var& c);
    void sendTake (const TakeRec&, const juce::String& state);
    juce::var mergeDoneEvent (const TakeRec&, const SessionIndex&, const juce::File& sessionFolder, const juce::File& outputFolder, bool backup) const;
    std::shared_ptr<LiveRecorder> session() const  { const std::lock_guard<std::mutex> l (recorderLock); return recorder; }
    bool recording() const                         { auto r = session(); return r != nullptr && r->recordingTake() != 0; }
    bool merging() const                           { const std::lock_guard<std::mutex> l (mergeLock); return mergeActive.load() || ! mergeQueue.empty(); }
    bool mergeQueuedFor (const juce::File& sessionFolder, int take) const;

    // ---- the audio device ----
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

    // ---- the merge: its own thread, one job after another, waiting while a take records ----
    struct MergeJob { std::shared_ptr<LiveRecorder> rec; juce::File sessionFolder; int take = 0; juce::File outputFolder; bool backup = false; TakeRec takeRec; juce::String projectName; };
    void queueMerge (const MergeJob&);
    void mergeThreadRun();
    void runMerge (MergeJob job);
    void timerCallback() override;

    const juce::String version;
    remote::Hub hub;
    juce::AudioDeviceManager devices;
    std::shared_ptr<LiveRecorder> recorder;                     // the session (recorderLock for the pointer itself)
    mutable std::mutex recorderLock;
    std::atomic<LiveRecorder*> live { nullptr };               // the session the audio callback feeds (nullptr: none)
    std::atomic<bool> deviceRunning { false };
    std::atomic<int> callbacks { 0 };
    double deviceRate = 0.0; int deviceBuffer = 0;
    juce::StringArray inputNames, outputNames;
    std::vector<std::atomic<float>> peaks;                      // per input: the loudest sample since the last meters event (the audio thread raises it, the timer takes it)
    juce::int64 lastMeterMs = 0, lastStatusMs = 0;
    juce::String projectName, windowName; juce::File projectFolder;
    std::vector<juce::File> backupRoots;
    int preTakeSeconds = 120;

    struct MergeRunner final : juce::Thread { RecorderService& s; explicit MergeRunner (RecorderService& x) : juce::Thread ("fermata:Live merge"), s (x) {} void run() override { s.mergeThreadRun(); } };
    MergeRunner mergeRunner { *this };
    mutable std::mutex mergeLock; std::deque<MergeJob> mergeQueue;
    std::atomic<bool> mergeActive { false }, mergeCancel { false };
    std::atomic<double> mergeFraction { 0.0 };
    juce::String mergeWhat; int mergeTake = 0; juce::File mergeFolder;   // (mergeLock)

    JUCE_DECLARE_NON_COPYABLE (RecorderService)
};
} // namespace td::live
