#pragma once
/** fermata:Live 0.3.0: what a station (the Primary's window, the Fixer's) knows of the recorder: the device, the session, its takes and drives,
    the merge, the meters, the talkback, the flags and fix versions, and the waveform (PeakStore) - all from the recorder's events (RecorderLink).
    It also asks the recorder for the waveform history the station does not have yet (a station that started after the session), one piece at a
    time. Message thread only; listeners hear of every change (sendChangeMessage) and of each event (onEvent). */
#include "PeakStore.h"
#include "SessionIndex.h"
#include <juce_events/juce_events.h>
#include <functional>

namespace td::live
{
struct StationTrack
{
    juce::Uuid id; juce::String name; int channels = 1; int ringFirst = 0; juce::String stem; std::vector<int> inputs;
};
struct StationTake
{
    int number = 0; juce::String label; juce::int64 start = 0, end = -1, recordPoint = 0; bool merged = false, registered = false; juce::String mergeError;
    bool recording() const noexcept { return end < 0; }
};
struct StationDrive
{
    juce::String folder, role; bool dropped = false; juce::String why; double lagSeconds = 0.0, stalledSeconds = 0.0; int chunks = 0, gaps = 0, indexFailures = 0;
    juce::int64 writtenTo = 0;
};

class StationModel : public juce::ChangeBroadcaster
{
public:
    /** The way to the recorder (RecorderLink::send). */
    std::function<void (const juce::var&)> send;
    /** Every event, after the model has taken what it needs from it. */
    std::function<void (const juce::var&)> onEvent;

    // ---- the link
    bool connected = false;
    juce::String recorderVersion;
    // ---- the device
    bool deviceRunning = false; double deviceRate = 0.0; int deviceBuffer = 0; double latencyMs = 0.0;
    juce::String deviceName, deviceType;
    double audioMissing = 0.0;                               // 0.3.0: > 0 while the device (or the session) has had no audio for that many seconds
    juce::String audioWhy; bool audioMissingInSession = false;
    juce::StringArray inputNames, outputNames;
    bool talkOpen = false;
    std::vector<float> inPeaks, outPeaks;                    // the last meters event (linear)
    /** 0.10.17: the highest peak of each input since it was last reset (the headroom on the input strips, for setting the gains in the rehearsal). */
    std::vector<float> inHeld;
    void resetHeld (int input = -1) { for (size_t i = 0; i < inHeld.size(); ++i) if (input < 0 || (int) i == input) inHeld[i] = 0.0f; }
    float held (int input) const { return input >= 0 && input < (int) inHeld.size() ? inHeld[(size_t) input] : 0.0f; }
    // ---- the session
    bool sessionOn = false;
    juce::String sessionFolder, projectName, windowName;
    juce::int64 position = 0; double sessionRate = 0.0; juce::Time origin;
    int recordingTake = 0, preTakeSeconds = 0, bufferSeconds = 0, chunkSeconds = 0;
    std::vector<StationTrack> tracks;
    std::vector<StationTake> takes;
    std::vector<StationDrive> drives;
    // ---- the merge
    bool mergeActive = false; int mergeTake = 0, mergeQueued = 0; juce::String mergeWhat; double mergeFraction = 0.0;
    // ---- the stations' streams
    struct StreamInfo { juce::String name; int outL = -1, outR = -1; bool playing = false; int underruns = 0; };
    std::vector<StreamInfo> streams;
    int streamPort = 0;
    // ---- flags and fix versions (of the session)
    std::vector<FlagRec> flags;
    std::vector<juce::var> fixes;
    // ---- the waveform
    PeakStore peaks;

    /** The timeline position now: the last one the recorder reported, moved on by the time since (while the device runs). */
    juce::int64 positionNow() const
    {
        if (! sessionOn) return 0;
        if (! deviceRunning || sessionRate <= 0.0 || audioMissing > 0.0) return position;      // (0.3.0: no audio, the timeline stands still)
        const double since = (juce::Time::getMillisecondCounterHiRes() - positionAtMs) / 1000.0;
        return position + (juce::int64) (juce::jlimit (0.0, 2.0, since) * sessionRate);
    }
    /** How far the primary drive has written the timeline (the Fixer's "finalised" point). */
    juce::int64 writtenTo() const
    {
        for (auto& d : drives) if (d.role == "primary") return d.writtenTo;
        return 0;
    }
    const StationTake* findTake (int number) const { for (auto& t : takes) if (t.number == number) return &t; return nullptr; }
    const FlagRec* findFlag (const juce::String& id) const { for (auto& f : flags) if (f.id == id) return &f; return nullptr; }
    const StationTrack* findTrack (const juce::Uuid& id) const { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }

    /** RecorderLink's connection came or went. */
    void setConnected (bool c)
    {
        connected = c;
        if (! c) { historyAsked = false; pendingHistory = 0; }
        else if (send) send (cmd ("notes"));
        sendChangeMessage();
    }
    /** One event from the recorder. */
    void handle (const juce::var& e);

    static juce::var cmd (const juce::String& name) { auto* o = new juce::DynamicObject(); o->setProperty ("cmd", name); return juce::var (o); }

private:
    void readStatus (const juce::var& e);
    void readPeaks (const juce::var& e, bool coarseLevel);
    void askHistory();
    double positionAtMs = 0.0;
    bool historyAsked = false;
    int pendingHistory = 0, historyId = 0;
    juce::String peaksSession;                          // the session the peaks belong to (a new one clears them)
    juce::int64 fineFirst = 0, fineEndKnown = 0, coarseFirst = 0, coarseEndKnown = 0;
};
} // namespace td::live
