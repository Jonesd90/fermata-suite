#include "DriveWriter.h"
#include "Merge.h"
#include <set>

namespace td::live
{
DriveWriter::DriveWriter (const Ring& r, const SessionIndex& idx, Settings s, std::shared_ptr<Disk> d, Callbacks callbacks)
    : juce::Thread ("fermata:Live drive " + s.sessionFolder.getFileName()), ring (r), index (idx), settings (std::move (s)), disk (std::move (d)), cb (std::move (callbacks))
{
    const int ch = ring.channels();
    floats.assign ((size_t) ch, std::vector<float> ((size_t) kPiece, 0.0f));
    ints.assign ((size_t) ch, std::vector<int> ((size_t) kPiece, 0));
    zeros.assign ((size_t) kPiece, 0);
    floatPtrs.resize ((size_t) ch); intPtrs.resize ((size_t) ch);
    for (int c = 0; c < ch; ++c) { floatPtrs[(size_t) c] = floats[(size_t) c].data(); intPtrs[(size_t) c] = ints[(size_t) c].data(); }
    if (settings.resume)
    {
        SessionIndex old;
        if (old.load (indexFile(), true))
        {
            // this drive's own history goes on: its chunks and gaps stay listed, and so does ITS OWN merged state of each take (a backup's chunks stay
            // until the backup itself has been merged; the recorder's record gives the take's span). The stretch it missed is a gap (see start). A chunk
            // left open by the stop is measured now (its files get the header the stop never wrote: Merge::chunkLength), so the index is whole again;
            // one whose files are gone is dropped from the list
            index.chunks = old.chunks; index.gaps = old.gaps; index.lastSample = old.lastSample;
            for (auto& t : index.takes)
                if (auto* own = old.findTake (t.number))
                { t.merged = own->merged; t.registered = own->registered; t.mergedFiles = own->mergedFiles; t.mergeError = own->mergeError; t.mergedSamples = own->mergedSamples; t.mergeGaps = own->mergeGaps; }
                else { t.merged = false; t.registered = false; t.mergedFiles.clear(); t.mergeError.clear(); t.mergedSamples = 0; t.mergeGaps.clear(); }
            for (auto it = index.chunks.begin(); it != index.chunks.end();)
            {
                if (it->samples >= 0) { ++it; continue; }
                juce::int64 len = -1;
                for (auto& t : index.tracks)
                {
                    const auto n = Merge::chunkLength (SessionIndex::chunkFile (settings.sessionFolder, t, *it));
                    if (n < 0) { len = -1; break; }
                    len = len < 0 ? n : juce::jmin (len, n);
                }
                if (len <= 0) { it = index.chunks.erase (it); continue; }
                it->samples = len;
                index.lastSample = juce::jmax (index.lastSample, it->firstSample + len);
                ++it;
            }
            // 0.3.0: chunks the index never listed (its writes had been failing - a scanner holding session.json - when the program stopped): every
            // whole-numbered chunk ("c000012") with a file for every track is measured and listed, so the merge has them and the take's end is
            // where the audio really ends. (A chunk begun again inside one, "c000012-r1", cannot be placed from its name and is left as it is.)
            if (! index.tracks.empty() && index.chunkSamples > 0)
            {
                std::set<juce::String> listed;
                for (auto& c : index.chunks) listed.insert (c.name);
                const auto& t0 = index.tracks.front();
                int found = 0;
                for (auto& f : settings.sessionFolder.findChildFiles (juce::File::findFiles, false, t0.stem + ".c*.wav"))
                {
                    const auto name = f.getFileNameWithoutExtension().fromLastOccurrenceOf (".", false, false);
                    if (name.length() != 7 || ! name.startsWithChar ('c') || ! name.substring (1).containsOnly ("0123456789") || listed.count (name) > 0) continue;
                    ChunkRec c; c.name = name; c.firstSample = name.substring (1).getLargeIntValue() * index.chunkSamples;
                    juce::int64 len = -1;
                    for (auto& t : index.tracks)
                    {
                        const auto n = Merge::chunkLength (SessionIndex::chunkFile (settings.sessionFolder, t, c));
                        if (n < 0) { len = -1; break; }
                        len = len < 0 ? n : juce::jmin (len, n);
                    }
                    if (len <= 0) continue;
                    c.samples = len;
                    index.chunks.push_back (c); listed.insert (name); ++found;
                    index.lastSample = juce::jmax (index.lastSample, c.firstSample + len);
                }
                if (found > 0)
                {
                    std::sort (index.chunks.begin(), index.chunks.end(), [] (const ChunkRec& a, const ChunkRec& b) { return a.firstSample < b.firstSample; });
                    unlistedFound = found;
                }
            }
        }
    }
    index.state = "writing"; index.droppedWhy.clear();
}

DriveWriter::~DriveWriter()
{
    if (isThreadRunning()) { requestClose(); waitForClose (5000); }
    jassert (! isThreadRunning());                                   // (LiveRecorder keeps a writer whose thread is stuck alive instead of destroying it)
}

void DriveWriter::start (juce::int64 startSample)
{
    reader.pos = pos = startSample;
    readerPos.store (startSample);
    lastProgressMs.store (juce::Time::currentTimeMillis());
    if (settings.resume && index.lastSample < startSample && index.lastSample > 0)
    {
        GapRec g { index.lastSample, startSample - index.lastSample, index.timeText (index.lastSample), "the drive was not being written to" };
        index.gaps.push_back (g);
        gaps.fetch_add (1);
    }
    startThread (juce::Thread::Priority::high);
}

void DriveWriter::requestClose()
{
    closing.store (true);
    notify();
}

bool DriveWriter::waitForClose (int maxMs)
{
    // the thread ends on its own once it has written what is left and closed the chunk and the index (or dropped the drive when it stopped making
    // progress); it is never killed - a thread killed inside a disk call can hang the whole program
    return waitForThreadToExit (juce::jmax (100, maxMs));
}

juce::int64 DriveWriter::stalledMs() const noexcept
{
    if (droppedFlag.load() || closedFlag.load() || lagSamples() <= 0) return 0;
    return juce::jmax<juce::int64> (0, juce::Time::currentTimeMillis() - lastProgressMs.load (std::memory_order_relaxed));
}

static std::vector<std::unique_ptr<DriveWriter::OpenChunk>>& abandonedChunks() { static std::vector<std::unique_ptr<DriveWriter::OpenChunk>> v; return v; }

void DriveWriter::abandonForTests()
{
    abandoned.store (true);
    notify();
    stopThread (10000);
    // the open chunk's writers are kept alive (their destructors would write the final headers, which a crash never does) until finishAbandonedForTests
    if (chunk != nullptr) abandonedChunks().push_back (std::move (chunk));
}

void DriveWriter::finishAbandonedForTests()
{
    abandonedChunks().clear();
}

void DriveWriter::updateIndex (const std::function<void (SessionIndex&)>& change)
{
    { const std::lock_guard<std::mutex> l (indexLock); change (index); }
    indexDirty.store (true);
    {
        // (0.7.1: see handoverLock. The flag is set first and the lock taken after, and the thread looks at the flag one last time under the same
        // lock before it hands over: so this change is written either by the thread or here, never by nobody.)
        const std::lock_guard<std::mutex> h (handoverLock);
        if (threadDone)
        {   // the drive thread is gone: this caller writes the index (one caller at a time: the lock is held until the file is written)
            juce::String text;
            { const std::lock_guard<std::mutex> l (indexLock); text = juce::JSON::toString (index.toVar()); }
            juce::String err;
            if (disk->replaceTextSafely (indexFile(), text, &err)) indexDirty.store (false);
            else if (cb.log) cb.log ("Drive " + settings.sessionFolder.getFullPathName() + ": the session index could not be written after the session: " + err);
            return;
        }
    }
    notify();
}

juce::String DriveWriter::uniqueChunkName (juce::int64 at) const
{
    const auto base = index.chunkNameFor (at);
    auto name = base;
    for (int n = 1; n < 1000; ++n)
    {
        bool clash = false;
        { const std::lock_guard<std::mutex> l (indexLock); for (auto& c : index.chunks) if (c.name == name) { clash = true; break; } }
        if (! clash)
            for (auto& t : index.tracks) if (settings.sessionFolder.getChildFile (t.stem + "." + name + ".wav").exists()) { clash = true; break; }
        if (! clash) return name;
        name = base + "-r" + juce::String (n);
    }
    return base + "-r" + juce::String (juce::Time::currentTimeMillis() % 100000);
}

void DriveWriter::run()
{
    crashreport::nameThisThread (getThreadName());
    // 0.7.1: nothing that goes wrong in one drive's writer may end the recorder (an exception that leaves a thread ends the whole program - and
    // with it the other drives and the take). The drive is dropped, as for any other failure of it, and the rest goes on.
    try { writeLoop(); }
    catch (const std::exception& e) { try { drop (juce::String ("its writer failed (") + e.what() + ")"); } catch (...) {} }
    catch (...)                     { try { drop ("its writer failed"); } catch (...) {} }
    if (abandoned.load()) return;                                    // (tests: as a program that died leaves it - the index is not written again)
    // a writer that failed above stays until the session closes, as a dropped drive's does
    while (droppedFlag.load() && ! closing.load() && ! threadShouldExit()) wait (200);
    // the index is handed over (see handoverLock): what is still unwritten is written first - a few tries for a drive that is there (a scanner may
    // hold session.json for a moment; this is the write that says the session ended), one for a dropped drive (it may be gone)
    try
    {
        const int maxTries = droppedFlag.load() ? 1 : 10;
        for (int tries = 0;; ++tries)
        {
            if (indexDirty.load() && tries < maxTries && ! threadShouldExit())
            {
                if (! writeIndex (true)) wait (300);
                continue;
            }
            const std::lock_guard<std::mutex> h (handoverLock);
            if (indexDirty.load() && tries < maxTries && ! threadShouldExit()) continue;       // (an update came in at this very moment: written above, next time round)
            threadDone = true;
            break;
        }
    }
    catch (...) { const std::lock_guard<std::mutex> h (handoverLock); threadDone = true; }
}

void DriveWriter::writeLoop()
{
    if (! disk->createFolder (settings.sessionFolder)) drop ("cannot make the folder " + settings.sessionFolder.getFullPathName());
    else writeIndex (true);
    const int minWrite = juce::jmax (1, juce::jmin (kPiece, settings.minWriteSamples));
    juce::int64 closeProgressMs = 0;
    while (! threadShouldExit())
    {
        if (abandoned.load()) return;
        const bool finishing = closing.load();
        if (droppedFlag.load())
        {
            if (finishing) return;
            if (indexDirty.load()) writeIndex (false);               // (a dropped drive's index may still take the takes' records, if the drive is there)
            wait (200);
            continue;
        }
        if (indexDirty.load() && writeIndex (false)) {}
        const juce::int64 avail = ring.available (reader);
        const bool reachesEnd = chunk != nullptr && pos + avail >= chunk->end;        // (a chunk is closed as soon as the audio up to its end is there)
        if (avail < minWrite && ! finishing && ! reachesEnd) { wait (20); continue; }
        if (avail <= 0 && finishing)
        {
            if (chunk != nullptr && ! closeChunk()) continue;
            { const std::lock_guard<std::mutex> l (indexLock); index.state = "closed"; }
            closedFlag.store (true);
            writeIndex (true);
            return;
        }
        if (finishing)
        {   // at the close the drive may take as long as it keeps making progress; one that writes nothing for noProgressDropMs is dropped
            const auto now = juce::Time::currentTimeMillis();
            if (closeProgressMs == 0) closeProgressMs = now;
            if (now - lastProgressMs.load() > settings.noProgressDropMs && now - closeProgressMs > settings.noProgressDropMs)
            { drop ("the drive took nothing for " + juce::String (settings.noProgressDropMs / 1000) + " s at the end of the session"); continue; }
        }
        if (chunk == nullptr && ! openChunk (pos)) continue;
        const int n = (int) juce::jmin<juce::int64> (kPiece, juce::jmin (avail, chunk->end - pos));
        const int got = ring.take (reader, floatPtrs.data(), n);
        if (reader.gapSamples > 0)
        {
            // the samples were written over before this drive got to them: the chunk ends here, the gap is noted, and the next chunk starts where the
            // ring's oldest sample is (the merge fills the hole with silence; nothing is written to a drive that has just shown it cannot keep up)
            GapRec g { reader.gapAt, reader.gapSamples, index.timeText (reader.gapAt), "the drive fell " + juce::String ((int) std::lround ((double) reader.gapSamples / index.sampleRate)) + " s behind (its disk stalled)" };
            { const std::lock_guard<std::mutex> l (indexLock); index.gaps.push_back (g); }
            gaps.fetch_add (1);
            pos = reader.pos;
            readerPos.store (pos, std::memory_order_relaxed);
            indexDirty.store (true);
            if (cb.gap) cb.gap (g);
            if (cb.log) cb.log ("Drive " + settings.sessionFolder.getFullPathName() + ": gap of " + juce::String (g.samples) + " samples at " + g.time);
            if (chunk != nullptr) closeChunk();
            continue;
        }
        if (got <= 0) continue;
        for (int c = 0; c < ring.channels(); ++c)
        {
            const float* src = floats[(size_t) c].data();
            int* dst = ints[(size_t) c].data();
            for (int i = 0; i < got; ++i) dst[i] = TakeWriter::toInt24 (src[i]);
        }
        if (! writePiece (got)) continue;
        pos += got;
        readerPos.store (pos, std::memory_order_relaxed);
        lastProgressMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        if (pos >= chunk->end) closeChunk();
    }
}

bool DriveWriter::openChunk (juce::int64 at)
{
    auto c = std::make_unique<OpenChunk>();
    c->rec.name = uniqueChunkName (at);
    c->rec.firstSample = at;
    c->rec.samples = -1;
    c->end = (at / index.chunkSamples + 1) * index.chunkSamples;
    const auto now = juce::Time::getCurrentTime();
    const auto tod = index.timeOfDay (at);
    for (auto& t : index.tracks)
    {
        const auto file = SessionIndex::chunkFile (settings.sessionFolder, t, c->rec);
        auto stream = disk->openForWrite (file);
        if (stream == nullptr) { drop ("cannot create " + file.getFullPathName()); return false; }
        auto opts = juce::AudioFormatWriterOptions()
                        .withSampleRate (index.sampleRate)
                        .withNumChannels (t.channels)
                        .withBitsPerSample (24)
                        .withMetadata (juce::WavAudioFormat::bwavDescription, index.windowName + " / live chunk " + c->rec.name + " / " + t.name)
                        .withMetadata (juce::WavAudioFormat::bwavOriginator, "fermata:Live")
                        .withMetadata (juce::WavAudioFormat::bwavOriginationDate, now.formatted ("%Y-%m-%d"))
                        .withMetadata (juce::WavAudioFormat::bwavOriginationTime, now.formatted ("%H:%M:%S"))
                        .withMetadata (juce::WavAudioFormat::bwavTimeReference, juce::String ((juce::int64) std::llround (tod * index.sampleRate)))
                        .withMetadata (juce::WavAudioFormat::bwavCodingHistory, "A=PCM,F=" + juce::String ((int) std::lround (index.sampleRate)) + ",W=24,M="
                                                                                + (t.channels == 1 ? juce::String ("mono") : t.channels == 2 ? juce::String ("stereo") : juce::String ("multitrack"))
                                                                                + ",T=" + settings.programName + "\r\n")
                        .withMetadata (juce::WavAudioFormat::riffInfoSoftware, settings.programName);
        auto w = wav.createWriterFor (stream, opts);
        if (w == nullptr) { drop ("cannot write a 24-bit WAV file for " + t.name + " at " + file.getFullPathName()); return false; }
        c->writers.push_back (std::move (w));
    }
    { const std::lock_guard<std::mutex> l (indexLock); index.chunks.push_back (c->rec); }
    chunk = std::move (c);
    writeIndex (true);
    return true;
}

bool DriveWriter::closeChunk()
{
    if (chunk == nullptr) return true;
    for (auto& w : chunk->writers)
        if (! w->flush()) { drop ("the drive refused the end of a chunk"); return false; }
    chunk->writers.clear();                                                      // (the destructors write the final headers)
    const auto rec = chunk->rec;
    const auto n = chunk->writtenSamples;
    chunk.reset();
    {
        const std::lock_guard<std::mutex> l (indexLock);
        for (auto& c : index.chunks) if (c.name == rec.name) c.samples = n;
        index.lastSample = juce::jmax (index.lastSample, rec.firstSample + n);
    }
    chunkCount.fetch_add (1);
    deleteOldChunks();                                                           // (first: it may free what the check below is about to miss)
    // the room the next chunk needs, with a margin: a chunk's bytes, twice, or minFreeBytes - whichever is more
    juce::int64 bytesPerChunk = 0;
    for (auto& t : index.tracks) bytesPerChunk += index.chunkSamples * 3 * (juce::int64) t.channels;
    const juce::int64 need = juce::jmax (settings.minFreeBytes, 2 * bytesPerChunk);
    if (const auto freeBytes = disk->bytesFree (settings.sessionFolder); freeBytes >= 0 && freeBytes < need)
    {
        drop ("less than " + juce::File::descriptionOfSizeInBytes (need) + " free");
        return false;
    }
    writeIndex (true);
    if (cb.chunkClosed) { ChunkRec done = rec; done.samples = n; cb.chunkClosed (done); }
    return true;
}

bool DriveWriter::writePiece (int n)
{
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (size_t ti = 0; ti < index.tracks.size(); ++ti)
    {
        const auto& t = index.tracks[ti];
        const int* chans[kMaxTrackChannels] {};
        for (int c = 0; c < t.channels && c < (int) kMaxTrackChannels; ++c)
        {
            const int rc = t.ringFirst + c;
            chans[c] = juce::isPositiveAndBelow (rc, ring.channels()) ? intPtrs[(size_t) rc] : zeros.data();
        }
        if (! chunk->writers[ti]->write (chans, n)) { drop ("the drive refused a write (full, or gone?)"); return false; }
    }
    const auto us = (juce::int64) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6);
    if (us > maxWriteUs.load (std::memory_order_relaxed)) maxWriteUs.store (us, std::memory_order_relaxed);
    chunk->writtenSamples += n;
    written.fetch_add (n, std::memory_order_relaxed);
    chunk->sinceHeader += n;
    if (settings.headerEverySamples > 0 && chunk->sinceHeader >= settings.headerEverySamples)
    {
        chunk->sinceHeader = 0;
        for (auto& w : chunk->writers) if (! w->flush()) { drop ("the drive refused a header"); return false; }
    }
    return true;
}

bool DriveWriter::writeIndex (bool force)
{
    // An index that cannot be written is tried again soon (at most every 2 s), and never drops the drive: the audio files are what matters, and a
    // scanner or a sync program holding session.json for a moment is no fault of the drive. A session carried on takes in the chunks its index never
    // listed (0.3.0: see the constructor).
    const auto now = juce::Time::currentTimeMillis();
    if (! force && now - lastIndexTryMs < 2000) return false;
    lastIndexTryMs = now;
    juce::String text;
    { const std::lock_guard<std::mutex> l (indexLock); text = juce::JSON::toString (index.toVar()); }
    juce::String err;
    if (disk->replaceTextSafely (indexFile(), text, &err)) { indexDirty.store (false); return true; }
    indexDirty.store (true);
    const int failures = indexFailures.fetch_add (1) + 1;
    if (failures == 1 || failures % 30 == 0)
        if (cb.log) cb.log ("Drive " + settings.sessionFolder.getFullPathName() + ": the session index could not be written (" + err + "); the audio goes on, the index is tried again");
    return false;
}

void DriveWriter::drop (const juce::String& why)
{
    if (droppedFlag.exchange (true)) return;
    if (chunk != nullptr) { chunk->writers.clear(); chunk.reset(); }           // (best effort: the headers say what reached the disk)
    { const std::lock_guard<std::mutex> l (indexLock); index.state = "dropped"; index.droppedWhy = why; }
    juce::String text;
    { const std::lock_guard<std::mutex> l (indexLock); text = juce::JSON::toString (index.toVar()); }
    disk->replaceTextSafely (indexFile(), text, nullptr);                        // (if it can: the drive may be gone)
    if (cb.log) cb.log ("Drive " + settings.sessionFolder.getFullPathName() + " DROPPED: " + why);
    if (cb.dropped) cb.dropped (why);
}

void DriveWriter::deleteOldChunks()
{
    if (settings.preTakeSamples <= 0) return;
    const juce::int64 keepFrom = pos - settings.preTakeSamples - index.chunkSamples;
    std::vector<ChunkRec> old;
    {
        const std::lock_guard<std::mutex> l (indexLock);
        for (auto it = index.chunks.begin(); it != index.chunks.end();)
        {
            const auto& c = *it;
            bool deletable = c.samples >= 0 && c.firstSample + c.samples <= keepFrom;
            if (deletable)
                for (auto& k : index.takes)
                {
                    const juce::int64 kEnd = k.end < 0 ? std::numeric_limits<juce::int64>::max() : k.end;
                    if (c.firstSample < kEnd && c.firstSample + c.samples > k.start && ! k.merged) { deletable = false; break; }
                }
            if (deletable) { old.push_back (c); it = index.chunks.erase (it); }
            else ++it;
        }
    }
    for (auto& c : old)
        for (auto& t : index.tracks) disk->deleteFile (SessionIndex::chunkFile (settings.sessionFolder, t, c));
}
} // namespace td::live
