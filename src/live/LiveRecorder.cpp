#include "LiveRecorder.h"
#include <set>

namespace td::live
{
juce::File LiveRecorder::newestSession (const juce::File& liveChunks)
{
    juce::File best;
    for (auto& f : liveChunks.findChildFiles (juce::File::findDirectories, false))
        if ((f.getChildFile (kIndexFileName).existsAsFile() || filesync::backupOf (f.getChildFile (kIndexFileName)).existsAsFile())
            && (best == juce::File() || f.getFileName() > best.getFileName()))
            best = f;
    return best;
}

SessionIndex LiveRecorder::baseIndex (const juce::String& role) const
{
    SessionIndex idx;
    idx.projectName = setup.projectName; idx.windowName = setup.windowName; idx.projectFolder = setup.projectFolder.getFullPathName();
    idx.role = role; idx.recorder = setup.programName;
    idx.sampleRate = setup.sampleRate; idx.chunkSamples = (juce::int64) setup.chunkSeconds * (juce::int64) std::lround (setup.sampleRate);
    idx.origin = origin;
    idx.tracks = trackSpecs;
    { const std::lock_guard<std::mutex> l (takesLock); idx.takes = takes; }
    if (role != "primary")                                             // a backup's merged state is its own (see updateTake): it starts unmerged
        for (auto& t : idx.takes) { t.merged = false; t.registered = false; t.mergedFiles.clear(); t.mergeError.clear(); t.mergedSamples = 0; t.mergeGaps.clear(); }
    return idx;
}

std::unique_ptr<DriveWriter> LiveRecorder::makeWriter (const juce::File& sessionFolder, const juce::String& role, bool resume)
{
    DriveWriter::Settings s;
    s.sessionFolder = sessionFolder;
    s.minWriteSamples = (int) std::lround (0.5 * setup.sampleRate);
    s.headerEverySamples = (int) std::lround (10.0 * setup.sampleRate);
    s.preTakeSamples = (juce::int64) std::lround ((double) setup.preTakeSeconds * setup.sampleRate);
    s.resume = resume;
    s.programName = setup.programName;
    DriveWriter::Callbacks cb;
    cb.dropped = [this, sessionFolder] (const juce::String& why) { if (callbacks.driveDropped) callbacks.driveDropped (sessionFolder, why); };
    cb.gap = [this, sessionFolder] (const GapRec& g) { if (callbacks.gap) callbacks.gap (sessionFolder, g); };
    cb.log = [this] (const juce::String& t) { if (callbacks.log) callbacks.log (t); };
    return std::make_unique<DriveWriter> (ring, baseIndex (role), s, disk, cb);
}

juce::String LiveRecorder::prepare (const Setup& s)
{
    if (prepared.load()) return "The session is already prepared";
    setup = s;
    if (setup.sampleRate <= 0.0) return "No sample rate";
    if (setup.tracks.empty()) return "No tracks to record";
    if (setup.projectFolder == juce::File() || ! juce::File::isAbsolutePath (setup.projectFolder.getFullPathName())) return "No project folder";
    for (auto& b : setup.backupRoots)
        if (b == juce::File() || ! juce::File::isAbsolutePath (b.getFullPathName())) return "A backup drive's folder is not a full path: " + b.getFullPathName();
    setup.chunkSeconds = juce::jlimit (1, 600, setup.chunkSeconds);
    setup.preTakeSeconds = juce::jlimit (0, 300, setup.preTakeSeconds);
    setup.bufferSeconds = juce::jlimit (2, 60, setup.bufferSeconds);
    const auto now = setup.nowForTests != juce::Time() ? setup.nowForTests : juce::Time::getCurrentTime();
    origin = now;
    juce::int64 startAt = 0;

    // the tracks: ring channels in order, file stems unique
    trackSpecs = setup.tracks;
    std::set<juce::String> stems;
    int ch = 0;
    for (auto& t : trackSpecs)
    {
        t.channels = juce::jlimit (1, (int) kMaxTrackChannels, t.channels);
        t.inputs.resize ((size_t) t.channels, -1);
        t.ringFirst = ch; ch += t.channels;
        auto stem = sanitiseForFile (t.name).replaceCharacter ('.', '_');
        if (stem.isEmpty()) stem = "track";
        auto unique = stem;
        for (int n = 2; stems.count (unique) > 0; ++n) unique = stem + " (" + juce::String (n) + ")";
        stems.insert (unique); t.stem = unique;
    }

    const auto liveChunks = liveChunksFolder (setup.projectFolder, setup.windowName);
    if (setup.resume)
    {
        const auto found = newestSession (liveChunks);
        SessionIndex old;
        if (found == juce::File() || ! old.load (found.getChildFile (kIndexFileName), true)) return "No session to carry on in " + liveChunks.getFullPathName();
        if (std::abs (old.sampleRate - setup.sampleRate) > 0.5) return "The session to carry on was at " + juce::String (old.sampleRate) + " Hz";
        if (old.tracks.empty()) return "The session to carry on has no tracks";
        // the same timeline: sample 0 stays the old origin, and now is where the clock says
        origin = old.origin;
        trackSpecs = old.tracks;
        ch = 0;
        for (auto& t : trackSpecs) { t.channels = juce::jlimit (1, (int) kMaxTrackChannels, t.channels); t.inputs.resize ((size_t) t.channels, -1); t.ringFirst = ch; ch += t.channels; }
        setup.chunkSeconds = (int) juce::jmax<juce::int64> (1, old.chunkSamples / (juce::int64) std::lround (setup.sampleRate));
        { const std::lock_guard<std::mutex> l (takesLock); takes = old.takes; }
        sessionName = found.getFileName();
        startAt = juce::jmax<juce::int64> (old.lastSample, (juce::int64) std::llround ((now - origin).inSeconds() * setup.sampleRate));
    }
    else
        sessionName = origin.formatted ("%Y-%m-%d %H-%M-%S");
    ringInputs.clear();
    for (auto& t : trackSpecs) for (int c = 0; c < t.channels; ++c) ringInputs.push_back (t.inputs[(size_t) c]);
    chanPtrs.assign ((size_t) ch, nullptr);

    // the memory buffer: bufferSeconds of every channel, within the memory budget (a quarter of the machine's memory unless told otherwise), never under 5 s
    juce::int64 budget = setup.memoryBudgetBytes > 0 ? setup.memoryBudgetBytes : (juce::int64) juce::SystemStats::getMemorySizeInMegabytes() * 1024 * 1024 / 4;
    if (budget <= 0) budget = (juce::int64) 512 * 1024 * 1024;
    juce::int64 capacity = (juce::int64) std::lround ((double) setup.bufferSeconds * setup.sampleRate);
    const juce::int64 bytesPerSample = (juce::int64) juce::jmax (1, ch) * (juce::int64) sizeof (float);
    if (capacity * bytesPerSample > budget) capacity = juce::jmax<juce::int64> ((juce::int64) std::lround (5.0 * setup.sampleRate), budget / bytesPerSample);
    try { ring.prepare (ch, capacity); }
    catch (const std::bad_alloc&) { return "Not enough memory for a buffer of " + juce::String ((int) (capacity / (juce::int64) std::lround (setup.sampleRate))) + " s on " + juce::String (ch) + " channels"; }
    ring.startAt (startAt);                                          // (resume: the timeline goes on where the clock says it is)
    setup.bufferSeconds = (int) (capacity / (juce::int64) std::lround (setup.sampleRate));

    primarySessionFolder = liveChunks.getChildFile (sessionName);
    {
        const std::lock_guard<std::mutex> l (writersLock);
        writers.push_back (makeWriter (primarySessionFolder, "primary", setup.resume));
        for (auto& root : setup.backupRoots)
            writers.push_back (makeWriter (liveChunksFolder (backupProjectFolder (root, setup.projectFolder), setup.windowName).getChildFile (sessionName), "backup", setup.resume));
        for (auto& w : writers) w->start (startAt);
    }
    prepared.store (true);
    if (callbacks.log) callbacks.log ("Session " + sessionName + ": " + juce::String (trackSpecs.size()) + " tracks, " + juce::String (ch) + " channels, buffer "
                                      + juce::String (setup.bufferSeconds) + " s, chunks of " + juce::String (setup.chunkSeconds) + " s, pre-take " + juce::String (setup.preTakeSeconds)
                                      + " s, " + juce::String (writers.size()) + " drive(s)" + (setup.resume ? ", carried on from " + juce::String (startAt) + " samples" : ""));
    return {};
}

void LiveRecorder::process (const float* const* inputs, int numInputs, int n) noexcept
{
    if (! prepared.load (std::memory_order_acquire) || n <= 0) return;
    for (size_t k = 0; k < ringInputs.size(); ++k)
    {
        const int i = ringInputs[k];
        chanPtrs[k] = (inputs != nullptr && juce::isPositiveAndBelow (i, numInputs)) ? inputs[i] : nullptr;
    }
    ring.push (chanPtrs.data(), n);
}

juce::String LiveRecorder::startTake (int number, const juce::String& label)
{
    if (! prepared.load()) return "The session is not prepared";
    if (number <= 0) return "A take number must be 1 or more";
    TakeRec t;
    {
        const std::lock_guard<std::mutex> l (takesLock);
        for (auto& k : takes)
        {
            if (k.end < 0) return "Already recording";
            if (k.number == number) return "Take " + juce::String (number) + " is already in this session";
        }
        t.number = number; t.label = label;
        t.recordPoint = ring.position();
        t.start = juce::jmax<juce::int64> (0, t.recordPoint - (juce::int64) std::lround ((double) setup.preTakeSeconds * setup.sampleRate));
        t.end = -1;
        t.recordedAt = juce::Time::getCurrentTime();
        takes.push_back (t);
    }
    {
        const std::lock_guard<std::mutex> l (writersLock);
        for (auto& w : writers)
            w->updateIndex ([t] (SessionIndex& idx) { if (idx.findTake (t.number) == nullptr) idx.takes.push_back (t); });
    }
    if (callbacks.log) callbacks.log ("Take " + juce::String (number) + " (" + label + ") started at " + juce::String (t.recordPoint) + ", from " + juce::String (t.start));
    return {};
}

int LiveRecorder::stopTake()
{
    TakeRec done;
    {
        const std::lock_guard<std::mutex> l (takesLock);
        for (auto& t : takes) if (t.end < 0) { t.end = juce::jmax (t.start + 1, ring.position()); done = t; break; }
    }
    if (done.number == 0) return 0;
    updateTake (done);
    if (callbacks.log) callbacks.log ("Take " + juce::String (done.number) + " stopped at " + juce::String (done.end));
    return done.number;
}

void LiveRecorder::updateTake (const TakeRec& t)
{
    {
        const std::lock_guard<std::mutex> l (takesLock);
        bool found = false;
        for (auto& k : takes) if (k.number == t.number) { k = t; found = true; }
        if (! found) takes.push_back (t);
    }
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
    {
        const bool primaryDrive = w == writers.front();
        w->updateIndex ([t, primaryDrive] (SessionIndex& idx)
        {
            if (auto* k = idx.findTake (t.number))
            {
                if (primaryDrive) *k = t;
                else { k->label = t.label; k->start = t.start; k->end = t.end; k->recordPoint = t.recordPoint; k->recordedAt = t.recordedAt; }
            }
            else
            {
                auto fresh = t;
                if (! primaryDrive) { fresh.merged = false; fresh.registered = false; fresh.mergedFiles.clear(); fresh.mergeError.clear(); fresh.mergedSamples = 0; fresh.mergeGaps.clear(); }
                idx.takes.push_back (fresh);
            }
        });
    }
}

void LiveRecorder::updateTakeOnDrive (const juce::File& sessionFolder, const TakeRec& t)
{
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
        if (w->folder() == sessionFolder)
            w->updateIndex ([t] (SessionIndex& idx) { if (auto* k = idx.findTake (t.number)) *k = t; else idx.takes.push_back (t); });
}

bool LiveRecorder::takeOnDrive (const juce::File& sessionFolder, int number, TakeRec& out) const
{
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
        if (w->folder() == sessionFolder)
        {
            const auto snap = w->snapshot();
            if (auto* k = snap.findTake (number)) { out = *k; return true; }
        }
    return false;
}

std::vector<LiveRecorder::DriveStatus> LiveRecorder::drives() const
{
    std::vector<DriveStatus> out;
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
    {
        DriveStatus d;
        d.folder = w->folder(); d.role = w == writers.front() ? "primary" : "backup";
        d.dropped = w->isDropped(); d.why = d.dropped ? w->droppedWhy() : juce::String();
        d.lagSamples = w->lagSamples(); d.written = w->samplesWritten(); d.longestWriteMs = w->longestWriteMs(); d.stalledMs = w->stalledMs();
        d.chunks = w->chunksWritten(); d.gaps = w->gapCount(); d.indexFailures = w->indexWriteFailures();
        out.push_back (d);
    }
    return out;
}

SessionIndex LiveRecorder::primaryIndex() const
{
    const std::lock_guard<std::mutex> l (writersLock);
    return writers.empty() ? SessionIndex() : writers.front()->snapshot();
}

bool LiveRecorder::driveIndex (const juce::File& sessionFolder, SessionIndex& out, bool& live) const
{
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
        if (w->folder() == sessionFolder)
        {
            out = w->snapshot();
            live = ! w->isDropped() && ! w->isClosed() && ! w->threadHasEnded();
            return true;
        }
    return false;
}

std::vector<juce::File> LiveRecorder::driveFolders() const
{
    std::vector<juce::File> out;
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers) out.push_back (w->folder());
    return out;
}

juce::String LiveRecorder::resumeDrive (const juce::File& sessionFolder, const juce::String& role)
{
    if (! prepared.load()) return "The session is not prepared";
    if (sessionFolder == juce::File() || ! juce::File::isAbsolutePath (sessionFolder.getFullPathName())) return "Not a full path: " + sessionFolder.getFullPathName();
    if (sessionFolder.getFileName() != sessionName) return "That folder is not this session's (" + sessionName + ")";
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers)
        if (w->folder() == sessionFolder)
        {
            if (! w->isDropped()) return "That drive is being written to";
            w->requestClose(); w->waitForClose (2000);                   // (a dropped writer's thread ends at once)
            auto fresh = makeWriter (sessionFolder, w == writers.front() ? "primary" : "backup", true);
            fresh->start (ring.position());
            w = std::move (fresh);
            if (callbacks.log) callbacks.log ("Drive " + sessionFolder.getFullPathName() + " taken on again");
            return {};
        }
    auto fresh = makeWriter (sessionFolder, role == "primary" ? "backup" : role, true);   // (a drive added later is a backup: the primary is the project's own)
    fresh->start (ring.position());
    writers.push_back (std::move (fresh));
    if (callbacks.log) callbacks.log ("Drive " + sessionFolder.getFullPathName() + " added");
    return {};
}

void LiveRecorder::close()
{
    if (! prepared.exchange (false)) return;
    std::vector<std::unique_ptr<DriveWriter>> mine;
    { const std::lock_guard<std::mutex> l (writersLock); for (auto& w : writers) w->requestClose(); mine.swap (writers); }
    // every drive drains at once; a writer that is still inside a disk call after a long wait is left alive for good rather than killed
    static std::vector<std::unique_ptr<DriveWriter>> stuck;
    static std::mutex stuckLock;
    for (auto& w : mine)
    {
        if (! w->waitForClose (180000))
        {
            if (callbacks.log) callbacks.log ("Drive " + w->folder().getFullPathName() + ": its writer is stuck in a disk call and is left behind");
            const std::lock_guard<std::mutex> l (stuckLock);
            stuck.push_back (std::move (w));
        }
    }
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : mine) if (w != nullptr) writers.push_back (std::move (w));     // (closed writers stay: a merge that ends later still writes their indexes)
}

void LiveRecorder::abandonForTests()
{
    const std::lock_guard<std::mutex> l (writersLock);
    for (auto& w : writers) w->abandonForTests();
    prepared.store (false);
}
} // namespace td::live
