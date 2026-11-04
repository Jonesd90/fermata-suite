#pragma once
/** fermata:Live 0.2.0: one recording drive. A thread of its own reads the memory buffer (Ring) at this drive's own pace and writes the session's
    timeline to the drive in chunks of kChunkSeconds: one WAV file per track per chunk, each a complete 24-bit BWF file with the time of day of
    its first sample, and the index (SessionIndex) beside them, written again at every chunk.

    What it promises:
      - It never touches the audio thread or the other drives. A write that takes long only makes this drive fall behind in the ring; a drive that
        falls further behind than the ring holds gets a gap (a GapRec in its index, and no chunk covers that stretch - the merge fills it with
        silence) and carries on from the oldest audio still in the ring.
      - A write or open of an AUDIO file that fails, or a drive with less than minFreeBytes left, drops the drive: it stops writing, its files are
        closed with good headers as far as they go, its index says "dropped" and why, and the owner is told. Nothing else changes. An index write
        that fails does NOT drop the drive (a scanner or a sync program may hold the file for a moment): it is tried again, and the audio goes on.
      - Record and Stop never reach it: a take is a span of the timeline noted in the index (updateIndex), so pressing Record cannot disturb the
        writing.
      - close() writes what is still in the ring for as long as the drive keeps making progress, then closes the chunk and the index. A drive that
        makes no progress for a long time is dropped; a thread stuck inside the drive's own code is left to itself (never killed: a killed thread
        can hang the whole program). */
#include "Ring.h"
#include "SessionIndex.h"
#include "../core/TakeWriter.h"
#include "../core/CrashReport.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <mutex>

namespace td::live
{
/** The disk as the writer sees it, so a test can make a drive fail or stall (TestDisk in tests/live_recorder_test.cpp). */
struct Disk
{
    virtual ~Disk() = default;
    virtual std::unique_ptr<juce::OutputStream> openForWrite (const juce::File&) = 0;         // nullptr: cannot
    virtual bool createFolder (const juce::File&) = 0;
    virtual juce::int64 bytesFree (const juce::File&) = 0;                                      // < 0: unknown
    virtual bool replaceTextSafely (const juce::File&, const juce::String& text, juce::String* error) = 0;
    virtual bool deleteFile (const juce::File&) = 0;
};

struct RealDisk final : Disk
{
    std::unique_ptr<juce::OutputStream> openForWrite (const juce::File& f) override
    {
        auto s = std::make_unique<juce::FileOutputStream> (f, 1024);              // (a small buffer: the audio goes to the file in big pieces anyway, and a big buffer would hold the last moments back)
        if (s->failedToOpen()) return nullptr;
        return s;
    }
    bool createFolder (const juce::File& f) override                             { return f.createDirectory(); }
    juce::int64 bytesFree (const juce::File& f) override                         { return f.getBytesFreeOnVolume(); }
    bool replaceTextSafely (const juce::File& f, const juce::String& t, juce::String* e) override { return filesync::replaceTextSafely (f, t, true, e); }
    bool deleteFile (const juce::File& f) override                               { return f.deleteFile(); }
};

class DriveWriter final : private juce::Thread
{
public:
    struct Settings
    {
        juce::File  sessionFolder;                      // where the chunks and session.json go (made if missing)
        int         minWriteSamples = 0;                // the disk is written to once this much is waiting (0.5 s: few, big writes keep a drive ahead); each write is at most kPiece frames
        int         headerEverySamples = 0;             // the open chunk's headers are written again this often (a crash then leaves readable files)
        juce::int64 minFreeBytes = 500LL * 1024 * 1024; // below this (or below two chunks' worth, whichever is more) the drive is dropped before a write can fail half way
        juce::int64 preTakeSamples = 0;                 // chunks older than this (and in no unmerged take) are deleted as the session goes on
        bool        resume = false;                     // the folder already has a session (this drive was dropped and is taken on again, or the recorder started again)
        juce::String programName;                       // "fermata:Live 0.2.0": written into the files
        int         noProgressDropMs = 60000;           // at close(): a drive that has written nothing for this long is dropped
    };
    struct Callbacks                                    // all called on the drive's thread
    {
        std::function<void (const juce::String& why)> dropped;
        std::function<void (const GapRec&)> gap;
        std::function<void (const ChunkRec&)> chunkClosed;
        std::function<void (const juce::String&)> log;
    };

    /** 'index': the session as it is (tracks, rates, takes so far); the drive keeps its own copy from here on. With Settings::resume, the drive's
        own index on disk gives its chunks, gaps and the merged state of its takes (a backup's merged state is its own, never the primary's). */
    DriveWriter (const Ring& r, const SessionIndex& index, Settings s, std::shared_ptr<Disk> d, Callbacks cb);
    ~DriveWriter() override;

    /** Starts the thread at 'startSample' of the timeline (the ring's position now: a drive never writes what came before it). */
    void start (juce::int64 startSample);
    /** Asks the drive to write what is still in the ring, close the chunk and the index ("closed") and end its thread. Returns at once. */
    void requestClose();
    /** Waits (at most maxMs) for the thread to end after requestClose(). True when it has; false: it is still going (stuck in a write), and the
        writer must be kept alive (see LiveRecorder::close). The thread is never killed. */
    bool waitForClose (int maxMs);
    /** requestClose() and waitForClose() in one. */
    bool close (int maxMs = 120000)                      { requestClose(); return waitForClose (maxMs); }
    bool threadHasEnded() const                          { return ! isThreadRunning(); }
    /** Tests only: the thread stops at once, the open chunk's files are left as they are (no final header) and the index is not written again -
        what a program that died in the middle of a chunk leaves behind. */
    void abandonForTests();
    /** Tests only: writes the headers of the chunks abandoned so far (as a program coming back would not) - before the test's disk goes. */
    static void finishAbandonedForTests();

    // ---- any thread ----
    /** Changes the index (a take noted, ended, merged...) under the lock and has the drive write it soon. Once the thread has ended (the session
        is over), the index is written here and now (a merge that finishes after the session must still reach the disk). Only 'takes' may be
        changed this way: chunks and gaps belong to the drive thread. */
    void updateIndex (const std::function<void (SessionIndex&)>& change);
    SessionIndex snapshot() const                       { const std::lock_guard<std::mutex> l (indexLock); return index; }
    juce::File folder() const                           { return settings.sessionFolder; }
    juce::File indexFile() const                        { return settings.sessionFolder.getChildFile (kIndexFileName); }
    bool isDropped() const noexcept                     { return droppedFlag.load(); }
    bool isClosed() const noexcept                      { return closedFlag.load(); }
    juce::String droppedWhy() const                     { const std::lock_guard<std::mutex> l (indexLock); return index.droppedWhy; }
    juce::int64 samplesWritten() const noexcept         { return written.load (std::memory_order_relaxed); }
    /** How far behind the ring this drive is (samples): the stall it is in the middle of, if any. */
    juce::int64 lagSamples() const noexcept             { return juce::jmax<juce::int64> (0, ring.position() - readerPos.load (std::memory_order_relaxed)); }
    double longestWriteMs() const noexcept              { return maxWriteUs.load (std::memory_order_relaxed) / 1000.0; }
    /** How long ago the drive last took audio (ms) while there is audio to take: 0 when up to date; a growing number is a drive that is not answering. */
    juce::int64 stalledMs() const noexcept;
    int gapCount() const noexcept                       { return gaps.load (std::memory_order_relaxed); }
    int chunksWritten() const noexcept                  { return chunkCount.load (std::memory_order_relaxed); }
    int indexWriteFailures() const noexcept             { return indexFailures.load (std::memory_order_relaxed); }

    struct OpenChunk
    {
        ChunkRec rec;
        juce::int64 end = 0;                                         // the timeline position it ends at (exclusive)
        juce::int64 writtenSamples = 0, sinceHeader = 0;
        std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;   // one per track
    };

private:
    void run() override;
    bool openChunk (juce::int64 at);
    bool closeChunk();
    bool writePiece (int n);                                         // ints[] hold n converted frames of every ring channel
    bool writeIndex (bool force = false);                            // false: not written this time (tried again soon); never drops the drive
    void drop (const juce::String& why);
    void deleteOldChunks();
    juce::String uniqueChunkName (juce::int64 at) const;

    const Ring& ring;
    SessionIndex index;                                              // this drive's copy (indexLock)
    mutable std::mutex indexLock;
    std::atomic<bool> indexDirty { false };
    const Settings settings;
    std::shared_ptr<Disk> disk;
    Callbacks cb;
    juce::WavAudioFormat wav;

    Ring::Reader reader;
    std::atomic<juce::int64> readerPos { 0 }, written { 0 }, maxWriteUs { 0 }, lastProgressMs { 0 };
    std::atomic<int> gaps { 0 }, chunkCount { 0 }, indexFailures { 0 };
    std::atomic<bool> droppedFlag { false }, closing { false }, abandoned { false }, closedFlag { false };
    juce::int64 lastIndexTryMs = 0;

    static constexpr int kPiece = 32768;                             // the most frames in one disk write
    std::vector<std::vector<float>> floats;                          // per ring channel, kPiece
    std::vector<std::vector<int>> ints;
    std::vector<int> zeros;
    std::vector<float*> floatPtrs;
    std::vector<const int*> intPtrs;
    std::unique_ptr<OpenChunk> chunk;
    juce::int64 pos = 0;                                             // the timeline position written up to (== reader.pos, kept by the drive thread)

    JUCE_DECLARE_NON_COPYABLE (DriveWriter)
};
} // namespace td::live
