#pragma once
/** fermata:Live 0.2.0 (0.3.0: the monitoring, the talkback, the stations' playback streams, the waveform peaks, the flags): the recorder process. "fermata-Live.exe --recorder <port>" runs this and nothing else: no window; the audio device, the
    session (LiveRecorder), the merge, and a control port on 127.0.0.1:<port> (the shared remote::Hub) that the fermata:Live window talks to in
    JSON lines (the protocol is listed in docs/LIVE-DEVELOPMENT-NOTES.md). Nothing else runs in this process - no plug-ins, no drawing, no waveform
    building - so nothing else can stop the recording: the window can be closed, or die, and the recorder goes on until it is told to stop; a
    window started again finds it on the same port and carries on with it. (0.3.1: told by the window's Close everything, it ends the session, lets
    go of the device and closes once its merges are done; left with no window and nothing to do for a minute, it closes by itself.) While a take records, no command may end it: end-session, close-device,
    open and quit are refused (Stop first).

    Threads: the message thread runs the commands and the status; the audio callback feeds the session (process() into the ring, the input
    peaks); the merge thread runs the merges, talking to the session only through its own thread-safe calls (it keeps the session alive with a
    shared_ptr, so a merge that ends after the session has been closed still finishes and writes the indexes). */
#include "LiveRecorder.h"
#include "Merge.h"
#include "MonitorMix.h"
#include "PeakTap.h"
#include "StreamServer.h"
#include "../core/RemoteHub.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>
#include <map>

namespace td::live
{
class RecorderService final : private juce::AudioIODeviceCallback, private juce::Timer
{
public:
    /** port: the control port. version: the program's. */
    RecorderService (int port, const juce::String& version);
    ~RecorderService() override;

    bool isListening() const                       { return hub.isListening(); }
    /** The window said quit (with nothing recording and no merge going), or its Close everything has finished, or the recorder has been left with
        nothing to do (see timerCallback): the application ends. */
    std::function<void()> onQuit;
    /** 0.3.1, the first step of closing (the application's shutdown, before the closing safety net starts): the timer stops and the session ends
        (the drives finish their chunks; its index says it ended). The rest - the audio device, the threads, the connections - goes with the object,
        under the safety net (a driver that hangs as it is closed cannot keep the recorder). */
    void closeForQuit();
    /** 0.3.1: a recorder with no window connected for this long, and no session, no take and no merge, closes by itself - a window that died, or
        whose "quit" never arrived, must not leave it holding the audio device. (With the pre-take or a take running it never does.) */
    static constexpr int kAloneQuitMs = 60000;

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
    void mergeAgain (const juce::var& c);           // 0.3.1: a take of an ended session whose merge was cut short, merged again (into the project)
    void resumeDrive (const juce::var& c);
    void sendTake (const TakeRec&, const juce::String& state);
    // 0.3.0: monitoring, flags, fixes, peaks
    void setMix (const juce::var& c);
    void setTalkback (const juce::var& c);
    void putFlag (const juce::var& c, const juce::String& cmd);
    void reply (const juce::var& c);
    void putFix (const juce::var& c);
    void sendNotes();
    void sendPeaks();
    /** A station asks for waveform history: answered on its own connection's thread, to that station only (see the constructor). */
    void answerPeaks (remote::Hub::Connection&, const juce::var& c);
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
    /** 0.3.0, the timer: notices when the session stops getting audio (the driver stopped or restarted the device, gave nothing, or runs at
        another rate), warns while it lasts, and pins a flag where the missing time is once the audio comes back. */
    void watchAudio (juce::int64 nowMs);
    /** 0.3.0: a driver type's list of devices changed (a device plugged in or out): noted in the log and told to the window. Never acted on. */
    void deviceListChanged();
    void testSignal (const juce::var& c);           // (the "Test signal" device only: pretend faults, for trying the above without hardware)

    // ---- the merge: its own thread, one job after another, waiting while a take records ----
    struct MergeJob { std::shared_ptr<LiveRecorder> rec; juce::File sessionFolder; int take = 0; juce::File outputFolder; bool backup = false; TakeRec takeRec; juce::String projectName; };
    void queueMerge (const MergeJob&);
    void mergeThreadRun();
    void runMerge (MergeJob job);
    void timerCallback() override;

    const juce::String version;
    remote::Hub hub;
    juce::AudioDeviceManager devices;                           // 0.3.0: only the list of driver types - it never has a device open (see 'device')
    // 0.3.0: the device is the recorder's own: made, opened, started, stopped and closed here and nowhere else. (Opened through the
    // AudioDeviceManager, JUCE closed it and opened another one by itself when it left its driver's list - a different device, its inputs and
    // its rate, in the middle of a session. Nothing here ever changes the device: only "open" from the window does, and never while a take records.)
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String deviceTypeName;
    std::atomic<bool> ownStartStop { false };                   // (true while this code starts, stops or closes the device: not the driver's doing)
    std::atomic<int> driverStops { 0 };                         // the driver stopped the device by itself (a reset, the device gone)
    std::atomic<bool> rateWrong { false };                      // it started again at a rate other than the session's: the session is not fed
    std::atomic<double> rateNow { 0.0 };                        // the rate it last started at (the audio side's view)
    std::atomic<int> fed { 0 };                                 // blocks given to the session
    // a stretch the session got no audio, measured by the audio thread when the audio comes back (the time since the block before, less one
    // block) - so it is caught even when the driver restarts the device on the message thread, where the timer cannot look meanwhile
    std::atomic<juce::int64> holePos { 0 }, holeMs { 0 };      // where on the timeline, how long
    std::atomic<int> holeSeq { 0 };                             // +1 for each (holePos and holeMs are written first)
    std::atomic<int> sessionSerial { 0 };                       // +1 for each session (the audio thread starts measuring afresh)
    std::atomic<bool> rateWasWrong { false };                   // (since the last hole was reported)
    std::atomic<double> wrongRate { 0.0 };                      // (the rate it ran at then)
    int fedSerial = -1; juce::int64 lastFedTicks = 0;          // (the audio thread's own)
    int holeSeqSeen = 0, stopsSeen = 0;                         // (the timer's)
    struct Hole { bool on = false, warned = false; juce::int64 sinceMs = 0; int rank = 0; juce::String why; };
    Hole hole;                                                  // (the timer) the session getting no audio now: since when, and why
    int lastFed = 0, lastCallbacks = 0; juce::int64 lastFedMs = 0, lastCallbackMs = 0;
    bool deviceSilentWarned = false;                            // (no session) the device has given no audio for a while: said once
    struct ListWatch;
    std::unique_ptr<ListWatch> listWatch;                       // listens to every driver type's device list
    std::map<juce::String, juce::StringArray> knownDevices;    // per type: the device names last seen
    std::shared_ptr<LiveRecorder> recorder;                     // the session (recorderLock for the pointer itself)
    mutable std::mutex recorderLock;
    std::atomic<LiveRecorder*> live { nullptr };               // the session the audio callback feeds (nullptr: none)
    std::atomic<bool> deviceRunning { false };
    std::atomic<int> callbacks { 0 };
    double deviceRate = 0.0; int deviceBuffer = 0;
    juce::StringArray inputNames, outputNames;
    std::vector<std::atomic<float>> peaks, outPeaks;            // per input / output: the loudest sample since the last meters event (the audio thread raises it, the timer takes it)
    juce::int64 lastMeterMs = 0, lastStatusMs = 0, lastPeaksMs = 0;
    MonitorMix monitor;                                         // 0.3.0: what the Primary hears live, and the talkback
    StreamServer streams;                                       // 0.3.0: the stations' playback (the control port + 1)
    PeakTap peakTap;                                            // 0.3.0: the waveform of the session, for the stations
    int controlPort = kDefaultPort;
    juce::String lastMixText;                                   // (the last "mix" command, so a window that comes back can be told what is sounding)
    juce::String projectName, windowName; juce::File projectFolder;
    std::vector<juce::File> backupRoots;
    int preTakeSeconds = 120;

    struct MergeRunner final : juce::Thread { RecorderService& s; explicit MergeRunner (RecorderService& x) : juce::Thread ("fermata:Live merge"), s (x) {} void run() override { s.mergeThreadRun(); } };
    MergeRunner mergeRunner { *this };
    mutable std::mutex mergeLock; std::deque<MergeJob> mergeQueue;
    std::atomic<bool> mergeActive { false }, mergeCancel { false };
    std::atomic<double> mergeFraction { 0.0 };
    juce::String mergeWhat; int mergeTake = 0; juce::File mergeFolder;   // (mergeLock)
    std::atomic<int> mergeEventsUnsent { 0 };                   // 0.3.1: merge events on their way to the message thread, not yet sent (closing waits for them)

    // ---- closing (0.3.1) ----
    /** Closes the application (once): 'why' goes into the log first. */
    void quitNow (const juce::String& why);
    /** Runs 'close' (the audio device's) and ends the recorder if it has not returned in 20 s (a driver that hangs as it is closed). */
    void closeWithGuard (std::function<void()> close);
    /** The timer: Close everything finished (see "quit" with whenDone), or the recorder left alone with nothing to do. */
    void watchClosing (juce::int64 nowMs);
    /** A window opens a device or starts a session: a Close everything still waiting for its merge is called off (a fermata:Live started again
        meanwhile wants this recorder). */
    void keepRunning();
    bool quitWhenDone = false;                                  // "quit" with whenDone: the session ended, the device let go; closes once the merges are done
    juce::int64 quitReadyMs = 0;                                // (when nothing was left to wait for: it closes 1.5 s later, so its last events - and the
                                                                // window's "registered" - are through)
    juce::int64 aloneSinceMs = 0;                               // the last moment a window was connected, or something was running
    bool quitting = false;

    JUCE_DECLARE_NON_COPYABLE (RecorderService)
};
} // namespace td::live
