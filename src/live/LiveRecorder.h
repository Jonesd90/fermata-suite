#pragma once
/** fermata:Live 0.2.0: one recording session - the recorder's core, with no audio device and no window in it (so it runs the same in the tests on
    Linux, on fake audio, as on the MAD driver in the recorder process).

    The timeline: sample 0 is the first block the audio device gives after prepare(); every block is pushed into the memory buffer (Ring), and every
    drive writes the timeline to disk in chunks (DriveWriter) for as long as the session lasts - the "pre-take": a take (Record) is only a span of
    the timeline that is noted in every drive's index, starting up to preTakeSeconds before the moment Record was pressed, and Stop notes its end.
    So Record and Stop never touch the audio or the drives; the only thing that can disturb a recording is the machine, and a drive that fails or
    stalls is dropped or given a gap on its own (see DriveWriter) while the others carry on.

    After Stop, the take is merged from the primary drive's chunks into fermata's own take files (Merge) and put into the project (TakeRegister);
    the chunks of a merged take go once they are older than the pre-take. A session that was cut off (the recorder, or the PC, stopped) is carried
    on by prepare() with resume = true: its index is read, the timeline goes on from the time of day, and the stretch missed is a gap.

    Threads: process() is the audio thread's; everything else may be called from the message thread and from the merge thread (the drive list is
    under a lock); the object may outlive its session (close()) - a merge that ends later still updates the indexes. */
#include "DriveWriter.h"
#include <mutex>

namespace td::live
{
class LiveRecorder
{
public:
    struct Setup
    {
        juce::String projectName, windowName;
        juce::File   projectFolder;                      // the project's folder: the primary drive's chunks go in <it>/Recorded Media/<window>/Live Chunks/<session>
        std::vector<juce::File> backupRoots;             // the backup drives' folders: <root>/<project folder name>/Recorded Media/<window>/Live Chunks/<session>
        std::vector<TrackSpec> tracks;                   // ringFirst and stem are filled in by prepare()
        double sampleRate = 48000.0;
        int    bufferSeconds = 60;                       // the memory buffer: how long a drive may stall before it gets a gap (30 - 60; less if the memory is short)
        int    chunkSeconds = kChunkSecondsDefault;
        int    preTakeSeconds = 120;                     // a take starts this long before Record (0 - 300)
        juce::int64 memoryBudgetBytes = 0;               // the most the buffer may take (0: a quarter of the machine's memory)
        juce::String programName { "fermata:Live" };
        bool   resume = false;                           // carry on the newest session found in the primary's Live Chunks folder (after a crash)
        juce::Time nowForTests;                          // tests only: the clock (the session's origin, and where a carried-on session is now)
    };
    struct DriveStatus
    {
        juce::File folder; juce::String role;
        bool dropped = false; juce::String why;
        juce::int64 lagSamples = 0, written = 0, stalledMs = 0; double longestWriteMs = 0.0; int chunks = 0, gaps = 0, indexFailures = 0;
    };
    struct Callbacks                                     // called on the drives' threads
    {
        std::function<void (const juce::File& folder, const juce::String& why)> driveDropped;
        std::function<void (const juce::File& folder, const GapRec&)> gap;
        std::function<void (const juce::String&)> log;
    };

    explicit LiveRecorder (std::shared_ptr<Disk> d = std::make_shared<RealDisk>()) : disk (std::move (d)) {}
    ~LiveRecorder() { close(); }
    Callbacks callbacks;

    /** Makes the buffer and the drive writers and starts them writing: from here on every process() call is recorded. "" when fine, else why not. */
    juce::String prepare (const Setup&);
    bool isPrepared() const noexcept                     { return prepared.load(); }
    /** Audio thread: one block of the device's inputs (inputs[i] = driver input i, numInputs of them). Never waits. */
    void process (const float* const* inputs, int numInputs, int n) noexcept;
    /** The timeline position (samples) now. */
    juce::int64 position() const noexcept                { return ring.position(); }
    double sampleRate() const noexcept                   { return setup.sampleRate; }
    const Setup& settings() const noexcept               { return setup; }
    const std::vector<TrackSpec>& tracks() const         { return trackSpecs; }
    int ringChannels() const noexcept                    { return ring.channels(); }
    juce::int64 bufferSamples() const noexcept           { return ring.capacity(); }

    // ---- takes (message thread) ----
    /** Notes the start of a take (up to the pre-take before now). "" when fine; a number already used in this session is refused. */
    juce::String startTake (int number, const juce::String& label);
    /** Notes the end of the take that is recording. Returns its number (0: none was). */
    int stopTake();
    int recordingTake() const                            { const std::lock_guard<std::mutex> l (takesLock); for (auto& t : takes) if (t.end < 0) return t.number; return 0; }
    std::vector<TakeRec> takesSoFar() const              { const std::lock_guard<std::mutex> l (takesLock); return takes; }
    /** A take's record changed outside (merged, registered): the primary's index gets all of it; the backups' indexes get the span (start, end,
        label) and keep their own merged state - a backup is merged on demand, and its chunks stay until it has been. Any thread. */
    void updateTake (const TakeRec&);
    /** One drive's own record of a take (a backup's merge): that drive's index only. Any thread. */
    void updateTakeOnDrive (const juce::File& sessionFolder, const TakeRec&);
    /** A drive's own record of a take, as its index holds it (a backup's merged state), or none. Any thread. */
    bool takeOnDrive (const juce::File& sessionFolder, int number, TakeRec& out) const;

    // ---- drives (any thread) ----
    std::vector<DriveStatus> drives() const;
    juce::File primaryFolder() const                     { return primarySessionFolder; }
    /** The primary drive's index as it stands. */
    SessionIndex primaryIndex() const;
    /** A drive's index as it stands, by its session folder; 'live' = its writer is running and not dropped (so its chunks may still be open). False: no such drive. */
    bool driveIndex (const juce::File& sessionFolder, SessionIndex& out, bool& live) const;
    /** The session folders of the drives (the primary first). */
    std::vector<juce::File> driveFolders() const;
    /** Takes a dropped drive on again (or adds a new backup): its writing goes on from the timeline position now, with a gap for what it missed. */
    juce::String resumeDrive (const juce::File& sessionFolder, const juce::String& role);
    /** Tests: the writers (the primary first). Message thread only. */
    const std::vector<std::unique_ptr<DriveWriter>>& driveWriters() const { return writers; }
    DriveWriter* primary() const                         { return writers.empty() ? nullptr : writers.front().get(); }

    /** Ends the session: every drive writes what is left and closes its chunk and index. The device must have been stopped first (no more process()).
        Takes as long as the slowest drive keeps making progress (a drive that stops making progress is dropped after a minute). A drive whose thread
        is stuck inside a disk call is kept alive (never killed) and left behind. */
    void close();
    /** Tests only: pretends the program died here - the drives stop at once, with their chunks open and their indexes as last written. */
    void abandonForTests();

    /** The session folder for a drive root (where the chunks of THIS session go on that drive), and the Live Chunks folder it sits in. */
    static juce::File liveChunksFolder (const juce::File& projectFolder, const juce::String& windowName)
    {
        return projectFolder.getChildFile ("Recorded Media").getChildFile (sanitiseForFile (windowName)).getChildFile (kChunksFolderName);
    }
    static juce::File backupProjectFolder (const juce::File& backupRoot, const juce::File& projectFolder)
    {
        return backupRoot.getChildFile (juce::File::createLegalFileName (projectFolder.getFileName()));
    }
    /** The newest session folder under a Live Chunks folder (by name: the names are time stamps) that has an index (or its .bak), or a null File. */
    static juce::File newestSession (const juce::File& liveChunks);

private:
    std::unique_ptr<DriveWriter> makeWriter (const juce::File& sessionFolder, const juce::String& role, bool resume);
    SessionIndex baseIndex (const juce::String& role) const;

    std::shared_ptr<Disk> disk;
    Setup setup;
    std::atomic<bool> prepared { false };
    Ring ring;
    std::vector<TrackSpec> trackSpecs;
    std::vector<int> ringInputs;                         // ring channel -> driver input (-1: silence)
    std::vector<const float*> chanPtrs;                  // (audio thread scratch)
    juce::Time origin;
    juce::String sessionName;
    juce::File primarySessionFolder;
    std::vector<std::unique_ptr<DriveWriter>> writers;   // the primary first (writersLock for the list itself; the writers are thread-safe)
    mutable std::mutex writersLock;
    std::vector<TakeRec> takes;
    mutable std::mutex takesLock;
};
} // namespace td::live
