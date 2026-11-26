#pragma once
#include "Project.h"
#include "Dither.h"
#include "Playback.h"
#include "Ltc.h"
#include "Mtc.h"
#include "MixerWorkers.h"
#include "BackupCopier.h"
#include "HostTransport.h"
#include "TakeWriter.h"
#include "PitchPipe.h"
#include "RateBridge.h"
#include <mutex>

namespace td
{
/** Real-time engine: routes device inputs into every mixer, records armed tracks, writes outputs.
    Nothing on the audio thread allocates, locks (beyond try-locks) or touches the UI. */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (Project&);
    ~AudioEngine() override;

    // --- device ---
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;

    // --- also usable without a device (tests) ---
    void prepare (double sampleRate, int maxBlock);
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // --- 3.8.0: the project's sample rate, and an audio device at another rate (see RateBridge) ---
    /** The rate of the project's audio (Project::audioRate: the rate of its takes; 0 = none yet, the engine then simply runs at the device's rate).
        Message thread. Read when the device (re)starts: AppContext::matchEngineRate restarts it when this changes what the engine should run at. */
    void setProjectRate (double r) noexcept                { projectRate.store (r); }
    double getProjectRate() const noexcept                 { return projectRate.load(); }
    /** The rate the engine runs at on a device of this rate: the project's when it has one and the two can be bridged, otherwise the device's. */
    double rateFor (double deviceRate) const noexcept;
    /** True while the device runs at another rate than the engine and the live converter sits between them (listening only: recording is refused). */
    bool isConverting() const noexcept                     { return converting.load(); }
    /** The device's own rate (the same as getSampleRate() when nothing is converted). */
    double getDeviceRate() const noexcept                  { return deviceRateNow.load(); }
    /** How much later the outputs sound because of the converter, in ms (0 when nothing is converted). */
    double converterDelayMs() const noexcept               { return converterMs.load(); }
    /** What audioDeviceAboutToStart does for a device of this rate, block size and channels: the engine at rateFor (deviceRate), with the converter
        when that is not the device's rate (also the tests' way in). */
    void startForDevice (double deviceRate, int deviceBlock, int numInputs, int numOutputs);
    /** One block of the device, as the audio callback does it: through the converter when there is one (also the tests' way in). */
    void deviceBlock (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    /** Tests: times the converter had no input ready (always 0). */
    int converterShortfalls() const noexcept               { return bridge != nullptr ? bridge->inputShortfalls() : 0; }

    /** Call after any change to tracks / mixers / effects channels (message thread). */
    void rebuildPlan();
    /** True until the audio thread once failed to come round within half a second while something was being taken away from it (a plan, a playback
        session, the pre-roll ring: see waitForAudioThread). The old data is then leaked rather than freed under it, and because that data may point
        at mixers retired before it, those are never freed either for the rest of the run: false stays false. See AppContext::freeRetiredMixersSoon. */
    bool audioThreadNeverStalled() const noexcept { return neverStalled.load (std::memory_order_relaxed); }
    /** Delay compensation. Message thread, about a few times a second: asks every loaded plug-in how late it is and, when anything changed (a plug-in
        loaded, bypassed, or one that changed its own delay), rebuilds the plan so that all paths are lined up again. */
    void checkLatency();
    /** The most this mixer delays its outputs by, in samples (the longest plug-in path through it). 0 for an unknown mixer. */
    int mixerLatency (const MixerState*) const;
    /** Offline render: how many samples the captured signals are late by (the first that many samples of every capture are to be thrown away; render that many extra at the end). */
    int offlineLatency() const;
    /** 3.68.0 (video). Message thread. playMixerLatency: how late (samples) the mixer an edit plays through - its own, else the first - puts its
        sound on the outputs (its plug-ins' delay, lined up). heardDelaySeconds: how long after the playing session hands a sample over it is
        HEARD: that mixer's delay, the driver's output latency and the live rate converter's - the picture is drawn for the sound being heard,
        not the sound being made. setPlaybackMonitorGain: the level of the playing session's monitor track (the camera's sound; 0 = not heard). */
    int playMixerLatency (const juce::Uuid& editId) const;
    double heardDelaySeconds (const juce::Uuid& editId) const;
    void setPlaybackMonitorGain (float gain) noexcept;

    // --- offline rendering (bounce out). Use on a private engine that belongs to a private copy of the project; no audio device needed. ---
    /** Choose the mixer to render and what to capture from it: the post-fader signal of any audio tracks / Int Buses / Ext Buses
        (their ids). Each one is captured separately, in the order given. Call before prepare(). */
    void setOfflineTarget (const juce::Uuid& mixerId, std::vector<juce::Uuid> tapIds, std::vector<juce::Uuid> preFaderIds = {});
    // (preFaderIds: the same channels taken BEFORE their fader, after inserts and pan, not touched by mute / solo. Their captures come after the post-fader ones, in the order given.)
    /** Runs n samples (<= maxBlock) of the chosen mixer with the given audio on its tracks (one buffer per project track) and
        puts each chosen output into its own stereo buffer in 'captures' (one per output, see setOfflineTarget). */
    void renderOffline (const std::vector<juce::AudioBuffer<float>>& trackAudio, int n, std::vector<juce::AudioBuffer<float>>& captures);

    // --- recording ---
    /** Starts recording every armed track into a new take group of the given window.
        Returns an error message, or an empty string on success. */
    juce::String startRecording (TakeWindowDef&);
    /** Stops the take and returns at once (the group id). The take's length is known at once; its files are finished in the background (what is
        left in their buffers written, then their headers), see isFinishing. */
    juce::Uuid stopRecording();
    /** Message thread: a take whose files are still being finished on the disk (normally a fraction of a second after Stop). */
    bool isFinishing (const juce::Uuid& groupId) const;
    bool anyFinishing() const;
    /** Message thread: waits (at most maxMs) until every take's files are finished. True when they are. */
    bool waitForFinishedTakes (int maxMs);
    /** Message thread, often (a timer): tidies up the takes whose files are now finished. Returns the files of those takes (empty: none finished
        since the last call), so the windows can read their waveforms afresh. */
    std::vector<juce::File> reapFinishedTakes();
    /** 3.27.1: the takes (window id, take id) whose files refused data while they were finished, since the last call (message thread). */
    std::vector<std::pair<juce::Uuid, juce::Uuid>> takeFinishFailures() { auto v = std::move (finishFailures); finishFailures.clear(); return v; }
    bool   isRecording() const noexcept   { return rec.load() != nullptr; }

    /** 3.59.0: the sound of one track being recorded, as WaveColour takes a file (the mean of its first two channels), so its waveform can be
        coloured WHILE it records (fermata's take window: LiveWaveColours). fermata colours a file once it is finished; this is fed by the audio
        thread beside the live waveform, in the same order as the file gets its audio (the silence for a device that was away included), so its
        block k is the file's block k. One reader takes it in blocks from a ring of about 1.5 s: a reader that falls further behind loses the
        oldest part (read() says no), never the recording. The colours worked out from it are kept here too (under 'lock': the reader adds,
        the take window draws), with 'ended' (nothing more will come: Stop) and 'fileDone' (the file is closed). Lives while anyone holds it. */
    struct LiveFeed
    {
        static constexpr int kBlock = 4096;                     // WaveColour's block (WaveColorCache::kBlock)
        juce::File file;                                        // the take's file
        double sampleRate = 48000.0;
        /** Message thread, before the audio thread is handed it. */
        void prepare (double sr, const juce::File& takeFile);
        /** The writer (the audio thread; at Stop the message thread, once that thread has left the take). */
        void add (const float* const* chans, int numChans, int n) noexcept;
        void addSilence (juce::int64 n) noexcept;
        /** The reader: samples [from, from + n) into dest. False when they have not all come yet, or have been written over (fell behind). */
        bool read (juce::int64 from, float* dest, int n) const noexcept;
        juce::int64 written() const noexcept { return count.load (std::memory_order_acquire); }
        int capacity() const noexcept { return cap; }
        std::atomic<bool> ended { false }, fileDone { false };
        // the colours worked out so far, one per block: argb 0 = not known (the reader fell behind there); complete = every block, the last
        // part-block too; gap = some are not known
        mutable std::mutex lock;
        std::vector<juce::uint32> argb;
        std::vector<float> level;
        std::vector<juce::uint8> low;                  // 3.61.0: each block's share of sound below 100 Hz (0..255), for the knock weighting
        bool complete = false, gap = false;
    private:
        std::unique_ptr<std::atomic<float>[]> ring;
        int cap = 0;
        std::atomic<juce::int64> begun { 0 }, count { 0 };     // samples the writer has started to put in / has put in
    };
    /** 3.59.0: whether the takes started from now on are fed for their colours (fermata: while WaveColour is on). Message thread. */
    void setLiveFeeds (bool on) noexcept { liveFeedsOn = on; }
    /** 3.59.0: the feed of a track of the take recording now (groupId null) or of that take while its files are being finished; null when it has
        none. Message thread. */
    std::shared_ptr<LiveFeed> getLiveFeed (const juce::Uuid& trackId, const juce::Uuid& groupId = juce::Uuid::null()) const;
    /** 3.59.0: every feed of the take recording now and of the takes being finished. Message thread. */
    std::vector<std::shared_ptr<LiveFeed>> liveFeeds() const;

    // --- the second recording drive (see BackupCopier) ---
    /** Message thread: every new take is also copied, while it records, into this folder (normally on another drive), in a folder named after the
        project. An empty File switches it off. The main recording never waits for it. */
    void setBackupFolder (const juce::File& folder)        { backupRoot = folder; }
    juce::File getBackupFolder() const                     { return backupRoot; }
    /** Message thread: the copies that were thrown away since the last call (the second drive could not keep up, or could not be written). */
    juce::StringArray takeBackupProblems()                 { return backupCopier.takeBinned(); }
    int backupCopiesGoing() const                          { return backupCopier.busy(); }
    /** Seconds of audio the slowest second-drive copy is behind the recording right now (0: caught up or none going). */
    double backupWorstLagSeconds() const noexcept          { return backupCopier.worstLagSeconds(); }

    // --- the mixers of a block run side by side on helper threads (see MixerWorkers) ---
    /** Message thread: how many helper threads (0 = all the mixers run one after another on the audio thread). Only while the audio device is
        stopped (the threads cannot be replaced under a running block): false, and nothing changes, while it runs. */
    bool setMixerThreads (int count)                       { if (running.load()) return false; mixerWorkers.start (juce::jmax (0, count)); return true; }
    /** How many helpers a block may use right now (see MixerWorkers::setLimit). */
    int  mixerHelperLimit() const noexcept                 { return mixerWorkers.limit_(); }
    int  mixerThreads() const noexcept                     { return mixerWorkers.size(); }
    /** How many blocks have run their mixers side by side so far (a diagnostic). */
    juce::uint64 parallelBlockCount() const noexcept       { return parallelBlocks.load(); }
    /** Tests only: behave as if a device were running, so a rebuilt plan is picked up by the next process() call the way the audio thread would. */
    void setRunningForTests (bool r) noexcept              { running = r; }
    /** Tests only: how far (seconds) the second drive may fall behind before its copy is thrown away. */
    void setBackupMaxLag (double seconds)                  { backupCopier.maxLagSeconds.store (seconds); }
    double recordedSeconds() const;
    /** Blocks that did not reach the disk buffer in time (the disk stalled for longer than the buffer holds). Their time is kept as silence in the files. */
    int    overruns() const;
    /** How much audio those overruns cost, in samples (message thread, while recording). */
    juce::int64 samplesLost() const;
    /** Empty while the recording is fine. Otherwise what is wrong with it: a file stopped taking data (the drive is full, or gone). Any thread. */
    juce::String recordingProblem() const;
    /** Message thread: what the audio device did to a take (it stopped and started again during it; it came back at another sample rate, which
        ends the take), said once - the text is returned and cleared. Empty: nothing to say. See audioDeviceAboutToStart. */
    juce::String takeDeviceNote()                     { auto n = deviceNote; deviceNote.clear(); return n; }
    /** 3.53.0: something that happened to the take being recorded, with its place in the take ('at', samples from the take's start), for the
        recording monitor to say and to mark on the take:
          lateBlock     the audio thread took longer over a block than the block lasts: the driver has probably dropped sound here;
          gapFilled     the audio device stopped and started again: the 'amount' samples it was stopped for are in the files as silence, so the
                        take is still in real time;
          gapNotFilled  the same, but it was stopped for too long to write that much silence at once without losing sound after it (see
                        RecordingSession::gapFillMax): nothing was added, 'amount' samples of time are missing from the take at this place. */
    struct RecEvent { enum Kind { lateBlock = 1, gapFilled = 2, gapNotFilled = 3 }; int kind = 0; juce::int64 at = 0, amount = 0; };
    /** Message thread: the events since the last call, oldest first. */
    std::vector<RecEvent> takeRecEvents();
    /** Audio thread (public for the tests): a block of the take was late. Said at most twice a second. */
    void noteLateBlock() noexcept;
    /** How fast the current recording grows on the disk (bytes per second, all its files together); 0 when not recording. */
    double recordingBytesPerSecond() const noexcept   { return recBytesPerSecond.load (std::memory_order_relaxed); }
    /** The folder the current recording's files are in (message thread; a null File when not recording). */
    juce::File recordingFolder() const                { return session != nullptr ? recFolder : juce::File(); }
    /** How many seconds of audio the recording buffers hold: the disk may stall for this long before anything is lost. */
    static constexpr double kRecordBufferSeconds = 8.0;
    juce::Uuid currentTakeId() const      { return currentGroupId; }

    // --- session mode: the inputs are kept for the last few seconds, so a take starts that long BEFORE Record was pressed ---
    /** 3.18.0: how far back the take starts (the Pre button): 1 to 10 whole seconds, 5 unless the person chose otherwise (it was always 6 before). */
    static constexpr int kPreRollMin = 1, kPreRollMax = 10, kPreRollDefault = 5;
    /** Message thread. A new length makes a new memory in the background (as switching Pre on does); while a take records, the take keeps the one it
        started with and the new one goes in after Stop. */
    void setPreRollSeconds (int seconds);
    int getPreRollSeconds() const noexcept { return preRollSeconds.load(); }
    /** Switch session mode on / off (message thread). The listening stream is never touched by this, nor by starting or stopping a recording.
        The memory that keeps the last seconds (for 40 inputs at 96 kHz about 120 MB) is made on a background thread and put in place a moment later
        (servicePreRoll), so switching it on never holds the program up. */
    void setSessionMode (bool on);
    /** Message thread, often (a timer): puts a pre-roll memory made in the background in place. */
    void servicePreRoll();
    /** Message thread: waits (at most maxMs) until the pre-roll memory session mode needs is in place. True when it is (or none is needed). */
    bool waitForPreRoll (int maxMs);
    bool isSessionMode() const noexcept   { return sessionMode.load(); }

    // --- playback (the mixers are fed from disk instead of the live inputs while this plays) ---
    /** Takes over the session and starts it. Returns an error message, or an empty string. waitForPreload: see PlaybackSession::start (the
        program's own Play buttons pass false: the screen is never held up while the first moments are read). */
    juce::String startPlayback (std::unique_ptr<PlaybackSession>, bool waitForPreload = true);
    /** Stops what plays. handOff (a press of Space or Stop, 2.102): a session without automation is given to a thread of its own, which frees
        it once the audio thread has left it, and this returns at once - its files stay open a few milliseconds longer. Otherwise (the default)
        everything is done here, as it always was, and the sessions handed off earlier are waited for too: when it returns, no file that was
        playing is open any more (a process window deletes its preview files straight after). */
    void stopPlayback (bool handOff = false);
    /** Waits (at most ms) until the sessions handed off by Stop have been freed, their files closed. True when none is left. */
    bool waitForStoppedPlays (int ms);
    /** 3.26.0: the automation of the edit that plays changed (a point drawn, moved or deleted): 'next' (null = it has none any more) takes over from the plan
        playing now, between two audio blocks - no stop, no gap. A send that 'next' moves for the first time is put in the mixer plan first; what only the
        old plan drove is handed back once the audio thread has left it (serviceReplacedAutomation). False (nothing done) unless edit 'editId' plays as
        such (PlaybackSession::followsAutomation). Message thread. */
    bool replaceAutomation (const juce::Uuid& editId, std::shared_ptr<AutomationPlan> next);
    /** Message thread, often (a timer): the plans replaced while playing that the audio thread has certainly left are freed, and what they held that the
        plan playing now does not hold goes back - a send leaves the mixer plan again, a channel under a VCA lane goes back to the VCA's own fader. */
    void serviceReplacedAutomation();
    /** 3.27.0: a plan replaced while playing may still be applied by the audio thread (what only it drove must not be put back yet). */
    bool hasReplacedAutomation() const noexcept { return ! replacedPlans.empty(); }
    /** 3.27.1: true while the play running now (its plan, or one it replaced that the audio thread may still apply) drives the control 'it' names - the same
        fader / pan / send, the same plug-in parameter, or a channel under a VCA lane. A control another play drives is not put back yet (it would be
        driven straight back, with a blip). Message thread. */
    bool automationDrives (const AutomationPlan::Item& it) const noexcept;
    /** True while edit 'editId' plays as such, so a change to its automation can be heard at once. Message thread. */
    bool followsAutomationOf (const juce::Uuid& editId) const noexcept
    {
        return playSession != nullptr && playSession->followsAutomation && playSession->editId == editId && playback.load() == playSession.get();
    }
    /** Stopped sessions still waiting to be freed by that thread (the tests). */
    int sessionsWaitingToBeFreed() const;
    /** Turns looping of the running playback on or off (off: it plays on to its end). */
    void setPlaybackLooping (bool on) noexcept { if (auto* p = playback.load()) p->setLooping (on); }
    /** The take / edit window's Loop switched off while it plays: the sound carries straight on past mark 2 (see PlaybackSession::releaseLoop). */
    void releasePlaybackLoop() noexcept { if (auto* p = playback.load()) p->releaseLoop(); }
    /** Seconds along the timeline from where the play started, for the playhead: round and round in a loop, straight on once it was let go of. */
    double playbackTimelineSeconds (bool smooth) const noexcept
    {
        auto* p = playback.load();
        return p != nullptr ? p->timelineSecondsFromStart (smooth ? p->getSmoothPositionSeconds() : p->getPositionSeconds()) : 0.0;
    }
    bool isPlaying() const noexcept        { return playback.load() != nullptr; }
    /** True once a session has reached its end (call stopPlayback() from the message thread to tidy up). */
    bool playbackFinished() const noexcept { auto* p = playback.load(); return p != nullptr && p->isFinished(); }
    double playbackSeconds() const noexcept { auto* p = playback.load(); return p != nullptr ? p->getPositionSeconds() : 0.0; }
    /** The same, moving on smoothly between audio blocks: for drawing the playhead. */
    double playbackSecondsSmooth() const noexcept { auto* p = playback.load(); return p != nullptr ? p->getSmoothPositionSeconds() : 0.0; }
    int playbackUnderruns() const noexcept { auto* p = playback.load(); return p != nullptr ? p->getUnderruns() : 0; }
    /** Milliseconds from Play to the first sound of what is playing (0 while it is still being read, or when nothing plays). */
    double playbackFirstSoundMs() const noexcept { auto* p = playback.load(); return p != nullptr ? p->msToFirstSound() : 0.0; }

    /** The waveform recorded so far for one track (message thread, only while recording).
        data holds, for every bin and every channel, a min/max pair (+-32767): data[(bin * numChannels + channel) * 2].
        There are 'bins' bins, each 'binSamples' samples long. */
    bool getLivePeaks (const juce::Uuid& trackId, const juce::int16*& data, int& bins, int& binSamples, int* numChannels = nullptr,
                       const juce::Uuid& groupId = juce::Uuid::null()) const;      // groupId: that take (also while it is being finished); null = the one recording

    // --- engineer audition ---
    /** While set (to a mixer other than the first one), that mixer's mix is also played on the first mixer's outputs
        and the first mixer's own mix is not: the engineer listens to someone else's mix. nullptr = normal. */
    void setAuditionMixer (const MixerState* m) noexcept { auditionMixer.store (m); }

    // --- phase scope: the post-fader stereo signal of one audio track / Int Bus / Ext Bus (a StripState* or BusState* of a mixer) ---
    static constexpr int kScopeFrames = 16384;
    void setScopeSource (const void* stripOrBus) noexcept { scopeSource.store (stripOrBus); }
    const void* getScopeSource() const noexcept          { return scopeSource.load(); }
    /** Copies the most recent frames (up to maxFrames, oldest first) into l / r; returns how many. Message thread. */
    int readScope (float* l, float* r, int maxFrames) const noexcept;

    // --- talkback: the Control Room mic and playback to the studio, on one reserved stereo pair of driver outputs (TB) ---
    /** crMicInputs: the driver inputs that make up the CR mic (any number; they are summed). tbOutputFirst: the first driver output of the TB pair (-1 = none).
        Nothing the mixers send ever reaches the two TB outputs: they carry only the CR mic (while it is open) and, while the
        talkback playback is on and a take or edit is playing, the processing mixer's main output computed with EVERY live input off. */
    void setTalkbackRouting (const std::vector<int>& crMicInputs, int tbOutputFirst) noexcept
    {
        setCrMicInputs (crMicInputs);                                  // any number of inputs together make the CR mic
        setTalkbackOutputs (tbOutputFirst >= 0 ? std::vector<int> { tbOutputFirst } : std::vector<int>());
    }
    /** Any number of talkback pairs: each entry is the first output of a pair (that output and the next one: left and right). A pair that starts on the
        last output is simply mono (the right channel is ignored). Pairs may overlap; an output in two of them carries both. */
    void setTalkbackRouting (const std::vector<int>& crMicInputs, const std::vector<int>& tbFirsts) noexcept
    {
        setCrMicInputs (crMicInputs);
        setTalkbackOutputs (tbFirsts);
    }
    /** Which driver inputs make up the CR mic. Each flag is stored once, with its final value: the audio thread never sees the mic's inputs all off for a
        moment (this is called many times a second while talkback is used; clearing them all first could drop the mic for a block - a click). */
    void setCrMicInputs (const std::vector<int>& crMicInputs) noexcept
    {
        bool on[kMaxInputs] = {};
        for (int i : crMicInputs) if (i >= 0 && i < kMaxInputs) on[i] = true;
        for (int i = 0; i < kMaxInputs; ++i) if (crSel[(size_t) i].load (std::memory_order_relaxed) != on[i]) crSel[(size_t) i].store (on[i], std::memory_order_relaxed);
    }
    void setTalkbackOutputs (const std::vector<int>& firsts) noexcept
    {
        int m[kMaxInputs] = {};
        for (int f : firsts)
        {
            if (f < 0 || f >= kMaxInputs) continue;
            m[f] |= 1; if (f + 1 < kMaxInputs) m[f + 1] |= 2;
        }
        bool any = false;
        for (int o = 0; o < kMaxInputs; ++o) { tbMask[(size_t) o].store (m[o], std::memory_order_relaxed); any = any || m[o] != 0; }
        tbAny.store (any);
    }
    /** The stage speaker mixer (what the TB pair carries): a gain (dB) and a pan (-1 left .. +1 right) for each CR input, one gain for the playback,
        one each for the click and the pitch pipe (when their To talkback button is on: 3.1.0; two boxes since 3.1.1) and one for the whole output.
        Centre pan is full level on both sides, 0 dB everywhere is the old behaviour. -60 dB means off. Message thread. */
    void setStageMix (const std::vector<float>& inputDb, const std::vector<float>& inputPan, float playbackDb, float outputDb, float clickDb = 0.0f, float pipeDb = 0.0f) noexcept
    {
        auto lin = [] (float db) { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); };
        for (int i = 0; i < kMaxInputs; ++i)
        {
            const float g = i < (int) inputDb.size() ? lin (inputDb[(size_t) i]) : 1.0f;
            const float p = juce::jlimit (-1.0f, 1.0f, i < (int) inputPan.size() ? inputPan[(size_t) i] : 0.0f);
            stageL[(size_t) i].store (g * juce::jmin (1.0f, 1.0f - p), std::memory_order_relaxed);
            stageR[(size_t) i].store (g * juce::jmin (1.0f, 1.0f + p), std::memory_order_relaxed);
        }
        stagePb.store (lin (playbackDb), std::memory_order_relaxed);
        aux.setTalkbackLevels (lin (pipeDb), lin (clickDb));                // (the click and the pitch pipe glide to their levels in AuxSounds::talkbackMix)
        stageOut.store (lin (outputDb), std::memory_order_relaxed);
    }
    /** The loudest sample (absolute) sent to the stage speakers since the last call; then it starts again. Message thread (the red PEAK light). */
    float takeStagePeak() noexcept { return stagePeakSeen.exchange (0.0f); }
    void setCrMic (bool open) noexcept { crOn.store (open); }
    void setTalkbackPlayback (bool on) noexcept { tbPlay.store (on); }

    // --- LTC (timecode as audio): watch one input channel, and/or make LTC on one output channel ---
    /** Which driver input carries LTC (-1 = none). The lamp in the top bar reads ltcInStatus(). Message thread. */
    void setLtcInput (int inputChannel) noexcept { ltcInCh.store (inputChannel); }
    LtcLive::Status ltcInStatus() const noexcept { return ltcIn.status(); }
    /** 3.56.0: the input number that means "the LTC made from MIDI timecode" (not a driver input: the engine makes it, see Mtc.h). Its LTC is
        made only while a MIDI port is open with setLtcMidiInput. (A driver with all 256 inputs would lose its last one to it.) */
    static constexpr int kLtcFromMidi = kMaxInputs - 1;
    /** Opens the MIDI port timecode comes in on (by its name; empty: none). False when it would not open. Message thread. */
    bool setLtcMidiInput (const juce::String& portName) { if (portName.isEmpty()) { mtcIn.close(); return true; } return mtcIn.open (portName); }
    bool ltcMidiInputOpen() const noexcept { return mtcIn.isOpen(); }
    /** Opens the MIDI port the LTC generator's timecode is sent on as MTC (empty: none). It is sent while LTC output is on (setLtcOutput with
        on = true; the output channel may then be -1). False when it would not open. Message thread. */
    bool setLtcMidiOutput (const juce::String& portName) { if (portName.isEmpty()) { mtcOut.close(); return true; } return mtcOut.open (portName); }
    bool ltcMidiOutputOpen() const noexcept { return mtcOut.isOpen(); }
    /** 3.67.0: a MIDI timecode port that is open here but gone from the system's list is closed (message thread, every few seconds), so that
        setLtcMidiInput / setLtcMidiOutput open it afresh when it is back. */
    void closeLtcMidiPortsThatAreGone() { mtcIn.closeIfGone(); mtcOut.closeIfGone(); }
    /** 3.67.0: how late MIDI timecode arrives, in seconds (added to the timecode the LTC is made from; it may be negative). What is left of the
        timing once the driver's own latency is allowed for: the network's and rtpMIDI's delay. Measured with setLtcCompareInput. */
    void setLtcMidiInOffset (double seconds) noexcept { mtcInOffset.store (juce::jlimit (-0.5, 0.5, seconds), std::memory_order_relaxed); }
    double ltcMidiInOffset() const noexcept { return mtcInOffset.load (std::memory_order_relaxed); }
    /** 3.67.0: measuring that offset. The same timecode comes in as MIDI and as LTC on audio input 'inputChannel': every frame read on that input
        is compared with the LTC made from the MIDI timecode (LtcCompare). -1 stops. The result's mean is what to ADD to the offset in force. */
    void setLtcCompareInput (int inputChannel) noexcept { ltcCmp.arm (inputChannel >= 0 && inputChannel < kLtcFromMidi ? inputChannel : -1); }
    LtcCompare::Result ltcCompareResult() const noexcept { return ltcCmp.result(); }
    /** 3.67.0: what the driver says of its latencies, in seconds (0: not said). The block's first input sample is this old when the block arrives
        (never taken as less than the block's own length), its first output sample is heard this much later. */
    void setDeviceLatencies (double inputSeconds, double outputSeconds) noexcept
    {
        devInLatency.store (juce::jlimit (0.0, 0.5, inputSeconds), std::memory_order_relaxed); devOutLatency.store (juce::jlimit (0.0, 0.5, outputSeconds), std::memory_order_relaxed);
    }
    /** The MIDI ports there are now, by name (for the menus). */
    static juce::StringArray midiInputNames()  { juce::StringArray a; for (auto& d : juce::MidiInput::getAvailableDevices())  a.add (d.name); return a; }
    static juce::StringArray midiOutputNames() { juce::StringArray a; for (auto& d : juce::MidiOutput::getAvailableDevices()) a.add (d.name); return a; }
    /** Makes LTC on a driver output (on = false: off). formatCode: see LtcGenerator. The timecode (re)starts at startSeconds (seconds since midnight) when
        something other than the start time changed, or when restart is true; calling it again with the same settings changes nothing. Message thread. */
    void setLtcOutput (int outputChannel, bool on, int formatCode, double startSeconds, bool restart = false) noexcept
    {
        const int ch = on ? outputChannel : -1;
        const bool changed = restart || ch != ltcOutCh.load() || formatCode != ltcOutFormat.load() || on != ltcOutOnA.load();
        ltcOutStart.store (startSeconds);
        ltcOutCh.store (ch); ltcOutFormat.store (formatCode); ltcOutOnA.store (on);
        if (changed) ltcOutRestart.fetch_add (1);
    }
    bool ltcOutActive() const noexcept { return ltcOutOnA.load() && ltcOutRunning.load(); }

    // --- how hard the audio thread works (the load meter and the timing log) ---
    /** What the audio callback did since the last call (message thread; reading starts a new count). Load = time spent in the callback as a share
        of the time the block lasts (its length at the sample rate): 100% means the PC only just keeps up. */
    struct LoadStats
    {
        float average = 0.0f, peak = 0.0f;   // percent
        int blocks = 0, late = 0;             // callbacks, and how many of them took longer than their block lasts (a dropout is likely)
        float recordBufferPeak = 0.0f;        // percent: the fullest any recording buffer got (100% = an overrun)
    };
    LoadStats takeLoadStats() noexcept;

    // --- metering (message thread) ---
    float takeInputPeak (int inputIndex) noexcept;   // returns peak since last call, then resets
    float takeOutputPeak (int outputIndex) noexcept; // same, for what is sent to the driver outputs

    double getSampleRate() const noexcept { return sampleRate; }
    int    getMaxBlock()   const noexcept { return maxBlock; }
    int    getNumInputs()  const noexcept { return deviceInputs; }
    int    getNumOutputs() const noexcept { return deviceOutputs; }
    bool   isRunning() const noexcept     { return running.load(); }
    /** The pitch pipe and the click: added to the outputs chosen for them, after the mixers (heard, never recorded, never bounced); and to the talkback
        pair, through the stage speaker mixer, when their To talkback button is on. */
    AuxSounds& auxSounds() noexcept        { return aux; }

private:
    struct Node;
    /** One send (or a channel's output) into another channel of the mixer. delay: holds this path back so it arrives together with the others (delay
        compensation). 3.39.0: how the source's channels reach the destination's (decided when the plan is built, message thread):
          Direct  the same width, channel for channel (every send was this: stereo to stereo);
          Pan     a mono / stereo source placed in a surround bus by the send's panner (SendState::panX ...; the strip's own pans when it follows them):
                  the gains are worked out on the audio thread from the live settings each block, and crossfaded from the last block's (lastM), so a
                  drag of the panner never zips;
          Fold    one format into another (a surround bus into a stereo one: Lo/Ro; into a surround bus of another format): the fixed matrix;
          Across  a track wider than stereo into a surround bus: channel for channel, as far as both go.
        After that the send's own inserts, its delay line and its level all work on the destination's channels. */
    struct SendPlan
    {
        SendState* st; Node* dest; PathDelay delay;
        enum class Kind : juce::uint8 { Direct, Pan, Fold, Across } kind = Kind::Direct;
        int srcCh = 2, dstCh = 2;                       // channels taken from the source / given to the destination
        BusFormat dstFormat = BusFormat::Stereo;        // Pan: the bus's format
        bool stripPans = false;                         // Pan: the source is an audio track (its own pan / panL / panR are followed); false: a stereo bus (left and right)
        std::vector<float> matrix;                      // Fold: srcCh * dstCh gains (Surround.h foldMatrix)
        float lastM[2 * kMaxBusChannels] {};            // Pan, audio thread: the gains of the previous block
        bool hasLast = false;
        bool viaPost = false;                           // 3.40.0: takes the channel's signal after its post-fader slot (Node::takesPost, decided when the plan is built)
    };
    /** One channel of a mixer: an Audio Track, an Int Bus or an Ext Bus. The nodes are kept in an order in which every
        channel comes after all the channels that send to it. */
    struct Node
    {
        NodeKind kind = NodeKind::Track;
        StripState* strip = nullptr;
        BusState* bus = nullptr;
        int trackIndex = -1;
        std::vector<SendPlan> sends;
        bool soloHeard = false;                 // audio thread: feeds something that is soloed (so it stays open while others are muted by solo) - only on the paths that lead there (3.58.0)
        bool soloSelf = false;                  // audio thread (3.58.0): soloed itself (its SOLO, or a soloed VCA's)
        bool soloAfter = false;                 // audio thread (3.58.0): soloed, or fed by a soloed channel (a soloed track's reverb bus, the bus after a soloed bus): every path open
        bool receives = false;                  // other channels send here
        bool isMainOut = false;                 // the mixer's main Ext Bus (what 'talkback playback' takes from the processing mixer)
        int tapIdx = -1;                        // offline render: which capture output its post-fader signal goes to (-1 = none)
        int tapPreIdx = -1;                     // offline render: which capture output its pre-fader signal goes to (-1 = none)
        PathDelay tapDelayPre;
        /** 3.39.0: the channels this node's signal has from its inserts on (pre, post, incoming, the fader, the meters, the taps): 2 for an audio
            track (its stereo fold) and a stereo bus; a surround bus's format's count. */
        int width = 2;
        BusFormat format = BusFormat::Stereo;   // a bus: its format (stereo for a track)
        int speakerCh = 2;                      // Ext Bus: channels to the speaker outputs (outFirst ...); a monitor format's last two go to the headphone pair (hpFirst)
        std::vector<float> foldLoRo;            // a surround bus: width * 2 gains, its Lo/Ro fold (for the phase scope); empty for stereo
        juce::AudioBuffer<float> incoming;      // width channels: the sum of what they send
        // delay compensation (samples): when the signals arrive at this channel, and when they leave it, counted from the sources
        int inLat = 0, outLat = 0;
        /** 3.40.0: the post-fader slot (slots[kPostSlot]) while the project's switch is on, else null: run on 'post' right after the fader, before the
            meters. What is taken after it - the output, a post-fader send that follows mute (a bus's into any format; a track's that stays stereo), the
            post-fader tap, an Ext Bus's driver outputs - is postLat later than outLat; what is taken before it (pre-fader sends and taps, a send that
            carries on when muted, a track's send placed in a surround bus by its panner) is not. See takesPost. */
        InsertSlot* post = nullptr;
        int postLat = 0;
        PathDelay srcDelay;                     // Audio Track: holds back its own source (disk / live input) to meet what is sent to it
        PathDelay alignDelay;                   // Audio Track: the spot-mic time alignment (Project::alignSamplesFor): this track is heard this much later, on purpose
        PathDelay outDelay;                     // Ext Bus: holds back the driver outputs so every Ext Bus of the mixer comes out together
        PathDelay tapDelay;                     // offline render: the same, for the captured signals
        /** 3.40.0: does this send (or output) take its signal after the post-fader slot? Only one taken after the fader that follows mute (the post
            slot sits after the mute too, so a plug-in there - a Dolby Atmos Beam - goes quiet with the channel), and of a track only one that stays
            stereo (Direct: the stereo fold, which the post slot works on; one placed in a surround bus by its panner leaves from the track's own
            channels, before the fold). Read on the audio thread and when the plan is built; the flags it reads can change between plans, so the
            latency signature counts them while a post slot delays anything. Decided when the plan is built and kept in SendPlan::viaPost, so the
            audio thread and the delay lines always agree (a flip of PRE between two plans is heard as before the flip until the plan is rebuilt). */
        bool takesPost (const SendPlan& sp) const noexcept
        {
            return post != nullptr && ! sp.st->pre.get() && sp.st->followMute.get() && (kind != NodeKind::Track || sp.kind == SendPlan::Kind::Direct);
        }
        /** The lateness (samples, from the sources) of what this send takes from the node: outLat, or outLat + postLat after the post slot. */
        int sendLat (const SendPlan& sp) const noexcept { return outLat + (sp.viaPost ? postLat : 0); }
        /** After the post slot: the output of an Ext Bus, its post-fader tap and meters. */
        int postOutLat() const noexcept { return outLat + postLat; }
    };
    struct MixerPlan
    {
        MixerState* mixer = nullptr;
        std::vector<std::unique_ptr<Node>> order;
        juce::AudioBuffer<float> chanBuf, pre, post, preSend, sendTmp;
        int latency = 0;                        // the longest path through the mixer (samples): all Ext Bus outputs are this late
        int tapLatency = 0;                     // offline render: the captured signals are this late
        bool wasPlaying = false;                // audio thread
        bool resumeClear = false;               // audio thread: it rested (a cue mixer following the processing mixer): empty its delay lines when it runs again
        // the tail: after playback stops, the mixer that was playing goes on running (with silence going in) until its reverbs have died away,
        // like outboard gear after the tape stops (audio thread only)
        bool ranLive = false;                   // it ran as a live mixer in the last block
        bool rmsIdle = false;                   // it is not running and its RMS meters have been set back to nothing (they are only worked out while it runs)
        bool tailNow = false;                   // this block it runs only for its tail: no disk audio, no live inputs
        int tailLeft = 0;                       // samples of tail still allowed (at most 30 s)
        int quietFor = 0;                       // how long its outputs have been below -90 dB
        float outPeak = 0.0f;                   // the loudest Ext Bus output in this block
        // running side by side with the other mixers: this mixer writes its driver outputs here, and the audio thread adds them up afterwards
        juce::AudioBuffer<float> outScratch;
        std::vector<float*> outPtrs;            // per driver output: where this mixer writes it (into outScratch), or nullptr
        std::vector<int> touched;               // the driver outputs it wrote in this block (room for every Ext Bus's channels and the audition pair)
        int numTouched = 0;
        bool keepRunning = false;               // 3.39.0: it holds the Dolby Atmos Composer, which must be run whether or not its Ext Buses go anywhere (see mixerHasOutput)
    };
    struct TrackPlan { int numCh = 1; int inputs[kMaxTrackChannels] {}; std::shared_ptr<TrackLive> live; int alignSamples = 0; };
    /** One mixer to run in this block, and how. */
    struct MixerJob { MixerPlan* mp = nullptr; bool silenceOwnOutput = false; int alsoToFirst = -1; bool strict = false; bool capMain = false; };   // capMain: copy its main output aside (for the cue mixers that follow it)
    struct Plan
    {
        std::vector<TrackPlan> tracks;
        std::vector<std::unique_ptr<MixerPlan>> mixers;
        std::vector<MixerJob> jobs;                             // audio thread: the mixers that run in this block (room for all of them)
        std::vector<MixerPlan*> followers;                      // audio thread: the cue mixers that follow the processing mixer in this block (room for all)
        int outChannels = 0;                                    // driver outputs the mixers' outScratch buffers have room for
        std::vector<const juce::AudioBuffer<float>*> disk;      // per track: the audio to play instead of the live input (audio thread fills it)
    };
    /** Waveform overview of what is being recorded, written by the audio thread, read by the take window. */
    struct LivePeaks
    {
        static constexpr int kBinsPerSecond = 32, kMaxSeconds = 3 * 3600;
        std::vector<juce::int16> data;       // per bin and channel a min,max pair, scaled to +-32767: data[(bin * numCh + channel) * 2]
        std::atomic<int> bins { 0 };
        int binSamples = 1500, count = 0, numCh = 1;
        float curMin[kMaxTrackChannels] {}, curMax[kMaxTrackChannels] {};
        void prepare (double sampleRate, int channels)
        {
            numCh = juce::jlimit (1, (int) kMaxTrackChannels, channels);
            binSamples = juce::jmax (64, (int) std::lround (sampleRate / kBinsPerSecond));
            data.assign ((size_t) kBinsPerSecond * kMaxSeconds * 2 * (size_t) numCh, 0);
        }
        void add (const float* const* chans, int numCh, int n) noexcept;
        /** n samples of silence, as add() of that many zeros but without looking at any (3.53.0: the time a stopped device was away). */
        void addSilence (juce::int64 n) noexcept;
    private:
        void closeBin() noexcept;
    };
    struct RecTrack
    {
        TakeFile info;
        TrackPlan route;
        std::unique_ptr<TakeWriter> writer;
        std::unique_ptr<LivePeaks> peaks;
        std::shared_ptr<LiveFeed> feed;                         // 3.59.0: its sound for the colours while it records (null: not wanted, see setLiveFeeds)
        std::shared_ptr<BackupCopier::Job> backup;              // its copy on the second drive (null when there is none)
    };
    /** The last seconds of every driver input that a track is patched to (session mode). Written by the audio thread; never reallocated
        while a recording uses it. Positions are counted in samples since the ring was made. */
    struct PreRoll
    {
        int cap = 0, preLen = 0;                  // samples kept per input / samples added in front of a take
        std::vector<int> slotOf;                  // driver input -> slot (-1 = not kept)
        std::vector<int> inputs;                  // slot -> driver input
        std::vector<float> data;                  // slot * cap + position % cap
        std::atomic<juce::int64> writePos { 0 };  // the position of the next sample to arrive
    };
    struct RecordingSession
    {
        std::vector<RecTrack> tracks;
        std::atomic<juce::int64> samples { 0 };   // samples delivered to the files so far (pre-roll included)
        std::atomic<int> overruns { 0 };
        juce::int64 startSample = 0;
        juce::Uuid windowId, groupId;
        // session mode: the files are fed from the PreRoll ring, starting preLen samples before the first block of the session
        bool useRing = false;
        PreRoll* ring = nullptr;
        juce::int64 startPos = -1, first = 0, delivered = 0;
        std::atomic<juce::int64> livePos { 0 };
        /** 3.53.0: silence put into the files for the time the device was stopped (audio thread; Stop reads it once that thread has left), and
            the most that is written for one stop: silence is written by the disk thread before the sound that follows it, which waits in the
            files' buffers meanwhile - so only as much as a slow drive writes in half the buffer's time (see startRecording). */
        juce::int64 gapSamples = 0, gapFillMax = 0;
    };

    void processMixer (const Plan& pl, MixerPlan&, const float* const* in, int numIn, int offset, int n, float* const* out, int numOut, bool playing,
                       bool silenceOwnOutput, int alsoToFirst, bool playbackOnly = false, float* tapL = nullptr, float* tapR = nullptr,
                       float* mainL = nullptr, float* mainR = nullptr);
    /** The audio thread has just picked up a new plan: the sound that is on its way through the old one's delay lines (delay compensation) is moved
        into the new one, so a rebuild (starting or stopping a recording, a routing change) never puts a gap or a click into what you hear. */
    static void carryOver (Plan& from, Plan& to) noexcept;
    /** Does any Ext bus of this mixer go to a driver output? */
    static bool mixerHasOutput (const MixerPlan&) noexcept;
    /** Runs one mixer of the block on whichever thread takes it (see MixerWorkers). */
    static void runMixerJob (void* engine, int index) noexcept;
    struct BlockJobs { Plan* plan = nullptr; const float* const* in = nullptr; int numIn = 0; float* const* out = nullptr; int numOut = 0, n = 0; bool playing = false; float* tapL = nullptr; float* tapR = nullptr;
                       float* mainL = nullptr; float* mainR = nullptr; };
    BlockJobs blockJobs;                            // audio thread: what the mixer jobs of this block work on
    std::atomic<juce::uint64> parallelBlocks { 0 };
    std::atomic<Plan*> lastPlanSeen { nullptr };    // audio thread: the plan of the previous block (see carryOver)
    bool destroying = false;                          // the destructor is running (stopPlayback then rebuilds nothing)
    std::atomic<bool> neverStalled { true };          // false once waitForAudioThread gave up (something was leaked rather than freed under the audio thread)
    HostTransport transport;                          // what the plug-ins are told (filled per block on the audio thread, or per offline render step)
    juce::int64 offlinePos = 0;                       // offline render: the timeline position the plug-ins are told
    MixerWorkers mixerWorkers;
    BackupCopier backupCopier;
    juce::File backupRoot;
    bool waitForAudioThread();                      // true once the audio thread has certainly left the old data; false if it stalled for half a second (then the old data is left alone rather than freed under it, and neverStalled is cleared for good)
    void ensurePreRoll();                           // message thread: (re)makes the ring when session mode is on and nothing is recording
    void deliverFromRing (RecordingSession&, juce::int64 from, juce::int64 to);
    std::unique_ptr<Plan> buildPlan() const;
    juce::uint64 latencySignature (bool refresh) const;     // changes whenever any plug-in's delay (or whether it is loaded / bypassed) changes

    Project& project;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int deviceInputs = 0, deviceOutputs = 0;
    // 3.8.0: the live sample-rate converter between the engine and the device (null: the engine runs at the device's own rate). Only replaced while the
    // device is stopped (startForDevice); used only by the audio callback (deviceBlock).
    std::unique_ptr<RateBridge> bridge;
    std::atomic<bool> converting { false };
    std::atomic<double> projectRate { 0.0 }, deviceRateNow { 48000.0 }, converterMs { 0.0 };
    bool preparingForDevice = false;                // startForDevice is calling prepare (any other call of prepare means no device: no converter)
    bool bridgedBlock = false;                      // audio thread: process() is being run by the converter (the input meters were read from the device's own inputs)
    std::array<bool, kMaxInputs> wantIn {};         // audio thread: the device inputs the engine listens to in this block (only those are converted)

    std::atomic<Plan*> plan { nullptr };
    std::atomic<juce::uint64> planLatencySig { 0 };
    std::atomic<RecordingSession*> rec { nullptr };
    std::atomic<PlaybackSession*> playback { nullptr };
    std::atomic<PreRoll*> preRollPtr { nullptr };
    std::atomic<bool> sessionMode { false };
    std::atomic<int> preRollSeconds { kPreRollDefault };
    std::atomic<const MixerState*> auditionMixer { nullptr };
    juce::Uuid offlineMixerId = juce::Uuid::null();  // offline rendering (see setOfflineTarget); null = none
    std::vector<juce::Uuid> offlineTaps, offlinePreTaps;
    std::vector<juce::AudioBuffer<float>>* captureBufs = nullptr;  // only while renderOffline() runs
    std::unique_ptr<PlaybackSession> playSession;   // owned by the message thread
    struct ReplacedPlan { std::shared_ptr<const AutomationPlan> plan; juce::uint64 block0 = 0; };
    std::vector<ReplacedPlan> replacedPlans;        // 3.26.0, message thread: automation plans replaced while playing, kept until the audio thread has left them
    struct Retirer;                                 // frees stopped sessions on a thread of its own (see stopPlayback)
    std::unique_ptr<Retirer> retirer;
    std::atomic<juce::uint64> blockCounter { 0 };
    std::atomic<bool> running { false };
    juce::String deviceNote;                                   // message thread: see takeDeviceNote
    // 3.53.0: the take's events (see RecEvent). One writer - the audio thread - and one reader, the message thread.
    static constexpr int kRecEvents = 64;
    juce::AbstractFifo recEventFifo { kRecEvents };
    RecEvent recEvents[kRecEvents];
    void pushRecEvent (int kind, juce::int64 at, juce::int64 amount) noexcept;
    std::atomic<bool> recGapPending { false };                 // the device stopped during the take: the next block of the take measures how long for
    std::atomic<juce::int64> lastRecTicks { 0 };               // when the take's last block arrived (the high-resolution clock)
    std::atomic<juce::int64> lastRecAt { -1 };                 // where in the take that block began (-1: the last block was not a recording one)
    int lastRecLen = 0;                                        // audio thread: its length
    juce::int64 lastLateAt = -1;                               // audio thread: where the last late block was said
    void fillDeviceGap (RecordingSession&, PreRoll*, juce::int64 ringPos, juce::int64 nowTicks);
    void deviceRestartedWhileRecording (double newRate);       // message thread: the take goes on, or ends if the rate changed (see audioDeviceAboutToStart)
    std::atomic<juce::int64> samplePosition { 0 };
    std::atomic<float> inputPeak[kMaxInputs];
    std::atomic<float> outputPeak[kMaxInputs];

    std::atomic<double> loadBusy { 0.0 }, loadSpan { 0.0 };   // audio thread adds, message thread takes: seconds spent in the callback / seconds of audio
    std::atomic<float> loadPeakPct { 0.0f };
    std::atomic<int> loadBlocks { 0 }, loadLate { 0 };
    std::atomic<float> recBufPeakPct { 0.0f };

    std::atomic<const void*> scopeSource { nullptr };
    std::vector<float> scopeRing = std::vector<float> ((size_t) (2 * kScopeFrames), 0.0f);
    std::atomic<juce::int64> scopeWrite { 0 };

    LtcLive ltcIn; LtcGenerator ltcGen;
    // 3.56.0: timecode as MIDI (Mtc.h). The ports are opened on the message thread; the audio thread reads mtcIn's reader and stamps mtcOut.
    MtcInput mtcIn; MtcOutput mtcOut; MtcChase mtcChase;
    // 3.67.0 (the timing): the blocks' steadied clock, the driver's latencies, the offset of MIDI timecode coming in, and the measuring of it
    mtc::BlockClock blockClock; LtcCompare ltcCmp;
    std::atomic<double> devInLatency { 0.0 }, devOutLatency { 0.0 }, mtcInOffset { 0.0 };
    std::vector<float> ltcMidiIn, ltcScratch;                 // the LTC made from MIDI timecode (an input for the LTC track), and where LTC that goes only as MIDI is made
    std::array<const float*, kMaxInputs> inWithLtc {};        // the driver's inputs with that one added (audio thread)
    std::atomic<bool> ltcOutOnA { false };
    AuxSounds aux;
    std::atomic<int> ltcInCh { -1 }, ltcOutCh { -1 }, ltcOutFormat { 1 }, ltcOutRestart { 0 };
    std::atomic<double> ltcOutStart { 0.0 };
    std::atomic<bool> ltcOutRunning { false };
    int ltcSeenRestart = -1;                       // audio thread

    std::vector<float> zeros;
    dither::Bank outDither;                         // audio thread: one noise generator per output channel
    int outDitherBits[kMaxInputs] {};               // audio thread: this block, the word length each output channel is dithered to (0 = none)
    std::array<std::atomic<bool>, kMaxInputs> crSel {};
    std::array<std::atomic<int>, kMaxInputs> tbMask {};      // per output: bit 1 = it carries the left of a talkback pair, bit 2 = the right
    std::atomic<bool> tbAny { false };
    std::array<std::atomic<float>, kMaxInputs> stageL {}, stageR {};     // the stage speaker mixer: each CR input's level into the left / right of a TB pair
    std::atomic<float> stagePb { 1.0f }, stageOut { 1.0f };
    std::atomic<float> stagePeakSeen { 0.0f };                                                           // audio thread: loudest sample sent to a TB output since the last look
    float stagePrevL[kMaxInputs] {}, stagePrevR[kMaxInputs] {}, stagePrevPb = 1.0f, stagePrevOut = 1.0f;     // audio thread: last block's values (so a fader move glides)
    std::atomic<bool> crOn { false }, tbPlay { false };
    float crGain = 0.0f;                            // audio thread: the CR mic's own fade (no click when it opens or closes)
    std::vector<float> tbTap;                       // audio thread: the playback-only main output of the processing mixer, left then right
    std::vector<float> mainCap;                     // audio thread: the processing mixer's main output, left then right, for the cue mixers that follow it
    std::unique_ptr<RecordingSession> session;      // owned by the message thread
    bool liveFeedsOn = false;                       // 3.59.0: message thread (see setLiveFeeds)
    /** A stopped take whose files the finisher thread is closing. The message thread owns the list; the finisher only closes the writers of an
        entry it was handed and then sets 'done'. */
    struct Finishing
    {
        std::unique_ptr<RecordingSession> rs;
        std::vector<std::shared_ptr<BackupCopier::Job>> copies;     // the second drive's copies: told to finish only once the main files are closed
        std::vector<juce::File> files;
        juce::String report;                                        // the take's line for the log (the finisher adds how long the closing took)
        std::atomic<bool> done { false };
        std::atomic<bool> failedAtEnd { false };                    // 3.27.1: a file refused what was left in its buffer while it was finished (the drive full, or gone)
    };
    std::vector<std::unique_ptr<Finishing>> finishing;
    std::vector<std::pair<juce::Uuid, juce::Uuid>> finishFailures;     // (collected by reapFinishedTakes)
    /** Makes the pre-roll memory in the background (see setSessionMode). */
    struct RingBuilder : public juce::Thread
    {
        RingBuilder() : juce::Thread ("Pre-roll builder") {}
        ~RingBuilder() override { stopThread (4000); delete ready.exchange (nullptr); }
        void run() override;
        void request (std::vector<int> inputs, int cap, int preLen)
        {
            { const juce::ScopedLock sl (lock); wantInputs = std::move (inputs); wantCap = cap; wantPre = preLen; pending = true; }
            notify();
        }
        std::atomic<PreRoll*> ready { nullptr };    // made, waiting to be put in place by the message thread
        juce::CriticalSection lock;
        bool pending = false; std::vector<int> wantInputs; int wantCap = 0, wantPre = 0;
    } ringBuilder;
    std::vector<int> wantedRingInputs; int wantedRingCap = 0;      // message thread: the pre-roll memory asked for (0 = none)
    struct Finisher : public juce::Thread
    {
        Finisher() : juce::Thread ("Take finisher") {}
        void run() override;
        void add (Finishing* f) { { const juce::ScopedLock sl (lock); queue.push_back (f); } notify(); }
        juce::CriticalSection lock;
        std::vector<Finishing*> queue;
    } finisher;
    std::atomic<double> recBytesPerSecond { 0.0 };
    juce::File recFolder;
    juce::TimeSliceThread writerThread { "Disk writer" };
    juce::WavAudioFormat wav;
    juce::Uuid currentGroupId;

    juce::CriticalSection planLock;                 // serialises rebuildPlan() calls
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace td
