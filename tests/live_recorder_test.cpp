// fermata:Live 0.2.0: the recorder core's tests (tools/live-check.sh builds and runs them; also under the address, thread and realtime checks).
// Fake audio at many times real time, drives as folders, and a disk layer the tests can break: a drive that refuses writes, a drive that stalls
// for longer than the memory buffer, a program that dies in the middle of a chunk. Every merged file is checked sample for sample against what
// was fed in: each channel k's sample at timeline position s is the 24-bit value v (k, s) below, which the 24-bit files keep exactly.
#include "../src/live/LiveRecorder.h"
#include "../src/live/Merge.h"
#include "../src/live/TakeRegister.h"
#include <cstdio>
#include <map>
#include <random>
#include <thread>

using namespace td;
using namespace td::live;
static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL line %d: %s (%s)\n", __LINE__, #cond, juce::String (msg).toRawUTF8()); } } while (0)
static juce::uint32 sectionStart = 0;
#define SECTION(name) do { const auto now_ = juce::Time::getMillisecondCounter(); if (sectionStart != 0) std::printf ("   (%.1f s)\n", (now_ - sectionStart) / 1000.0); sectionStart = now_; std::printf ("-- %s\n", name); } while (0)

static constexpr double kRate = 48000.0;

/** The 24-bit value fed on ring channel k at timeline sample s (never 0, so silence is told from audio). */
static int v (int k, juce::int64 s)
{
    const juce::int64 x = ((s * 7919 + (juce::int64) k * 104729 + 12345) % 16777216) - 8388608;
    return x == 0 ? 1 : (int) x;
}
static float fv (int k, juce::int64 s) { return (float) v (k, s) / 8388608.0f; }
/** The int a 24-bit reader gives for the 24-bit value x. */
static int asRead (int x) { return (int) ((juce::uint32) x << 8); }

// ---------------------------------------------------------------- the breakable disk
struct TestDisk final : Disk
{
    RealDisk real;
    std::mutex lock;
    std::map<juce::String, int> failWrites, failOpens, stallMs, failIndex;   // by folder path prefix (failIndex: only session.json's writes fail)
    std::atomic<int> writes { 0 };

    int setting (std::map<juce::String, int>& m, const juce::File& f)
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto& [prefix, val] : m) if (f.getFullPathName().startsWith (prefix)) return val;
        return 0;
    }
    void set (std::map<juce::String, int>& m, const juce::File& folder, int val) { const std::lock_guard<std::mutex> l (lock); m[folder.getFullPathName()] = val; }

    struct Stream final : juce::OutputStream
    {
        Stream (std::unique_ptr<juce::OutputStream> i, TestDisk& d, juce::File f) : inner (std::move (i)), disk (d), file (std::move (f)) {}
        void flush() override { inner->flush(); }
        bool setPosition (juce::int64 p) override { return inner->setPosition (p); }
        juce::int64 getPosition() override { return inner->getPosition(); }
        bool write (const void* data, size_t n) override
        {
            disk.writes.fetch_add (1);
            if (const int ms = disk.setting (disk.stallMs, file); ms > 0) juce::Thread::sleep (ms);
            if (disk.setting (disk.failWrites, file) > 0) return false;
            return inner->write (data, n);
        }
        std::unique_ptr<juce::OutputStream> inner; TestDisk& disk; juce::File file;
    };
    std::unique_ptr<juce::OutputStream> openForWrite (const juce::File& f) override
    {
        if (setting (failOpens, f) > 0) return nullptr;
        auto s = real.openForWrite (f);
        if (s == nullptr) return nullptr;
        return std::make_unique<Stream> (std::move (s), *this, f);
    }
    bool createFolder (const juce::File& f) override { return real.createFolder (f); }
    juce::int64 bytesFree (const juce::File& f) override { return real.bytesFree (f); }
    bool replaceTextSafely (const juce::File& f, const juce::String& t, juce::String* e) override
    {
        if (setting (failWrites, f) > 0 || setting (failIndex, f) > 0) { if (e) *e = "test: the index cannot be written"; return false; }
        return real.replaceTextSafely (f, t, e);
    }
    bool deleteFile (const juce::File& f) override { return real.deleteFile (f); }
};

// ---------------------------------------------------------------- feeding audio
/** LIVE_TEST_PACE=n slows the feeding n times (the address and thread checks make the drives that much slower). */
static int pace()
{
    static const int p = [] { const auto* e = std::getenv ("LIVE_TEST_PACE"); return e != nullptr ? juce::jlimit (1, 100, atoi (e)) : 1; }();
    return p;
}

/** Feeds 'seconds' of the test signal through process(), in blocks of random size, pausing 'msPerSecond' per second of audio (0 = flat out). */
static void feed (LiveRecorder& rec, double seconds, int msPerSecond = 0, int numInputs = 8)
{
    msPerSecond *= pace();
    static std::mt19937 gen (42);
    std::uniform_int_distribution<int> blocks (64, 2048);
    std::vector<std::vector<float>> in ((size_t) numInputs, std::vector<float> (2048));
    std::vector<const float*> ptrs; for (auto& b : in) ptrs.push_back (b.data());
    const auto& tracks = rec.tracks();
    juce::int64 pos = rec.position();
    const juce::int64 end = pos + (juce::int64) std::llround (seconds * kRate);
    juce::int64 sinceSleep = 0;
    while (pos < end)
    {
        const int n = (int) juce::jmin<juce::int64> (blocks (gen), end - pos);
        for (int i = 0; i < numInputs; ++i) std::fill (in[(size_t) i].begin(), in[(size_t) i].begin() + n, 0.25f);   // (inputs no track records: anything)
        for (auto& t : tracks)
            for (int c = 0; c < t.channels; ++c)
                if (juce::isPositiveAndBelow (t.inputs[(size_t) c], numInputs))
                    for (int i = 0; i < n; ++i) in[(size_t) t.inputs[(size_t) c]][(size_t) i] = fv (t.ringFirst + c, pos + i);
        rec.process (ptrs.data(), numInputs, n);
        pos += n; sinceSleep += n;
        if (msPerSecond > 0 && sinceSleep >= (juce::int64) kRate / 10) { juce::Thread::sleep (juce::jmax (1, msPerSecond / 10)); sinceSleep = 0; }
    }
}

/** Waits until every drive that is not dropped has written up to 'sample' (or 'maxMs' passed). */
static bool waitWritten (LiveRecorder& rec, juce::int64 sample, int maxMs = 20000)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) maxMs;
    for (;;)
    {
        bool all = true;
        for (auto& w : rec.driveWriters()) if (! w->isDropped() && w->snapshot().lastSample < sample) all = false;
        if (all) return true;
        if (juce::Time::getMillisecondCounter() > until) return false;
        juce::Thread::sleep (10);
    }
}

/** The samples of a WAV file as 24-bit values per channel. */
static std::vector<std::vector<int>> readInts (const juce::File& f, juce::StringPairArray* meta = nullptr, double* rate = nullptr)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (new juce::FileInputStream (f), true));
    std::vector<std::vector<int>> out;
    if (r == nullptr) return out;
    if (meta) *meta = r->metadataValues;
    if (rate) *rate = r->sampleRate;
    out.assign ((size_t) r->numChannels, std::vector<int> ((size_t) r->lengthInSamples, 0));
    std::vector<int*> ptrs; for (auto& c : out) ptrs.push_back (c.data());
    r->read (ptrs.data(), (int) r->numChannels, 0, (int) r->lengthInSamples, false);
    for (auto& c : out) for (auto& x : c) x = x >> 8;                     // (the reader gives the value in the top 24 bits)
    return out;
}

/** Checks a merged file of track t for take [start, end): v (k, s) everywhere except inside 'silent' spans (timeline positions), where 0. */
static int checkMerged (const juce::File& f, const TrackSpec& t, juce::int64 start, juce::int64 end, const std::vector<std::pair<juce::int64, juce::int64>>& silent = {})
{
    const auto ints = readInts (f);
    if ((int) ints.size() != t.channels) { std::printf ("  %s: %d channels, not %d\n", f.getFileName().toRawUTF8(), (int) ints.size(), t.channels); return 1; }
    if ((juce::int64) ints[0].size() != end - start) { std::printf ("  %s: %d samples, not %d\n", f.getFileName().toRawUTF8(), (int) ints[0].size(), (int) (end - start)); return 1; }
    int bad = 0;
    for (int c = 0; c < t.channels; ++c)
        for (juce::int64 i = 0; i < end - start && bad < 5; ++i)
        {
            const juce::int64 s = start + i;
            bool quiet = false;
            for (auto& [a, b] : silent) if (s >= a && s < b) quiet = true;
            const int want = quiet ? 0 : v (t.ringFirst + c, s);
            if (ints[(size_t) c][(size_t) i] != want) { ++bad; std::printf ("  %s: channel %d sample %lld (timeline %lld) is %d, wanted %d\n", f.getFileName().toRawUTF8(), c, (long long) i, (long long) s, ints[(size_t) c][(size_t) i], want); }
        }
    return bad;
}

static LiveRecorder::Setup baseSetup (const juce::File& project, const juce::String& window)
{
    LiveRecorder::Setup s;
    s.projectName = "Concert"; s.windowName = window; s.projectFolder = project;
    TrackSpec a; a.name = "Main"; a.channels = 2; a.inputs = { 1, 2 }; a.id = juce::Uuid();
    TrackSpec b; b.name = "Solo"; b.channels = 1; b.inputs = { 0 }; b.id = juce::Uuid();
    TrackSpec c; c.name = "Room/L"; c.channels = 1; c.inputs = { 5 }; c.id = juce::Uuid();      // (a name that needs sanitising for a file)
    s.tracks = { a, b, c };
    s.sampleRate = kRate; s.bufferSeconds = 4; s.chunkSeconds = 2; s.preTakeSeconds = 3;
    s.programName = "fermata:Live test";
    return s;
}

static MergeResult mergeTake (const juce::File& sessionFolder, int number, const juce::File& out, bool allowOpen = false, Merge::Flag cancelled = {}, Merge::SaveProgress save = {}, const SessionIndex* index = nullptr, const TakeRec* take = nullptr)
{
    Merge::Job j; j.sessionFolder = sessionFolder; j.takeNumber = number; j.outputFolder = out; j.programName = "fermata:Live test"; j.projectName = "Concert"; j.allowOpenChunks = allowOpen; j.index = index; j.take = take;
    return Merge::run (j, {}, cancelled, {}, [] (const juce::String& t) { std::printf ("     merge: %s\n", t.toRawUTF8()); }, save);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fermata_live_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    auto disk = std::make_shared<TestDisk>();
    const auto sec = [] (double s) { return (juce::int64) std::llround (s * kRate); };

    SECTION ("the ring: a reader never gets a torn or stale copy");
    {
        Ring ring; ring.prepare (2, 6000);
        std::atomic<bool> stop { false };
        std::atomic<juce::int64> checked { 0 }, gaps { 0 }, bad { 0 };
        std::thread producer ([&]
        {
            std::vector<float> a (512), b (512); const float* ch[2] = { a.data(), b.data() };
            juce::int64 pos = 0;
            while (! stop.load())
            {
                const int n = 100 + (int) (pos % 400);
                for (int i = 0; i < n; ++i) { a[(size_t) i] = fv (0, pos + i); b[(size_t) i] = fv (1, pos + i); }
                ring.push (ch, n); pos += n;
                if (pos % 50000 < n) std::this_thread::yield();
            }
        });
        std::thread consumer ([&]
        {
            Ring::Reader r; std::vector<float> a (700), b (700); float* ch[2] = { a.data(), b.data() };
            const auto until = juce::Time::getMillisecondCounter() + 1500;
            while (juce::Time::getMillisecondCounter() < until)
            {
                const juce::int64 at = r.pos;
                const int got = ring.take (r, ch, 700);
                if (r.gapSamples > 0) { gaps.fetch_add (1); CHECK (r.gapAt == at && r.pos == at + r.gapSamples); continue; }
                for (int i = 0; i < got; ++i) if (a[(size_t) i] != fv (0, at + i) || b[(size_t) i] != fv (1, at + i)) bad.fetch_add (1);
                checked.fetch_add (got);
                if (got == 0) std::this_thread::yield();
                else if (at % 7 == 0) juce::Thread::sleep (1);               // (fall behind now and then, so the ring overruns)
            }
        });
        consumer.join(); stop = true; producer.join();
        std::printf ("   %lld samples checked, %lld gaps, %lld bad\n", (long long) checked.load(), (long long) gaps.load(), (long long) bad.load());
        CHECK (bad.load() == 0 && checked.load() > 100000 && gaps.load() > 0);
    }

    SECTION ("a session on two drives: chunks, index, a take with pre-take, the merge checked sample for sample");
    const auto project = tmp.getChildFile ("Concert");
    const auto backup = tmp.getChildFile ("BackupDrive");
    juce::File session1, backupSession1;
    std::vector<TrackSpec> tracks1;
    {
        LiveRecorder rec (disk);
        std::atomic<int> dropped { 0 };
        rec.callbacks.driveDropped = [&] (const juce::File&, const juce::String&) { dropped.fetch_add (1); };
        rec.callbacks.log = [] (const juce::String& t) { std::printf ("     %s\n", t.toRawUTF8()); };
        auto setup = baseSetup (project, "First half");
        setup.backupRoots = { backup };
        CHECK (rec.prepare (setup).isEmpty());
        CHECK (rec.ringChannels() == 4 && rec.bufferSamples() == sec (4));
        tracks1 = rec.tracks();
        CHECK (tracks1[0].ringFirst == 0 && tracks1[1].ringFirst == 2 && tracks1[2].ringFirst == 3 && tracks1[2].stem == "RoomL");
        session1 = rec.primaryFolder();
        CHECK (session1.getParentDirectory().getFileName() == kChunksFolderName && session1.getParentDirectory().getParentDirectory().getFileName() == "First half");
        backupSession1 = LiveRecorder::liveChunksFolder (LiveRecorder::backupProjectFolder (backup, project), "First half").getChildFile (session1.getFileName());
        CHECK (rec.driveWriters().size() == 2 && rec.driveWriters()[1]->folder() == backupSession1);
        feed (rec, 4.0, 50);
        CHECK (rec.startTake (1, "Symphony").isEmpty());
        CHECK (rec.recordingTake() == 1 && rec.startTake (2, "x") == "Already recording");
        feed (rec, 3.5, 50);                                         // (Stop in the middle of a chunk: the take ends at 7.5 s, inside chunk [6, 8))
        CHECK (rec.stopTake() == 1 && rec.recordingTake() == 0);
        const auto takes = rec.takesSoFar();
        CHECK (takes.size() == 1 && takes[0].start == sec (1) && takes[0].recordPoint == sec (4) && takes[0].end == sec (7.5));
        // the merge refuses while the drive has not written the take's end (the chunk that holds it cannot close before 8 s is there)
        CHECK (waitWritten (rec, sec (6)));
        {
            const auto now = rec.primaryIndex();                         // (as the recorder holds it: the service merges from this, not from the disk)
            CHECK (now.lastSample == sec (6));
            const auto early = mergeTake (session1, 1, project.getChildFile ("Recorded Media").getChildFile ("First half"), false, {}, {}, &now);
            CHECK_MSG (! early.ok && early.error.contains ("still being written"), early.error);
        }
        feed (rec, 2.7, 50);                                         // past the chunk boundary at 8 s... the chunk [8, 10) closes once 10 s is written
        CHECK (waitWritten (rec, sec (10)));
        auto idx = rec.primaryIndex();
        CHECK (idx.role == "primary"
 && idx.state == "writing" && idx.takes.size() == 1 && idx.tracks.size() == 3 && idx.chunkSamples == sec (2));
        CHECK (idx.chunks.size() >= 5 && idx.chunks[0].name == "c000000" && idx.chunks[0].samples == sec (2) && idx.chunks[4].name == "c000004" && idx.chunks[4].samples == sec (2));
        CHECK (idx.gaps.empty() && dropped.load() == 0);
        // the chunk files are complete BWF WAVs with the time of day
        {
            juce::StringPairArray meta; double rate = 0;
            const auto ints = readInts (SessionIndex::chunkFile (session1, tracks1[0], idx.chunks[1]), &meta, &rate);
            CHECK (rate == kRate && ints.size() == 2 && (juce::int64) ints[0].size() == sec (2));
            CHECK (ints[0][100] == v (0, sec (2) + 100) && ints[1][100] == v (1, sec (2) + 100));
            CHECK (meta[juce::WavAudioFormat::bwavOriginator] == "fermata:Live" && meta[juce::WavAudioFormat::bwavDescription].contains ("live chunk c000001 / Main"));
            CHECK (std::abs (meta[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue() - (juce::int64) std::llround (idx.timeOfDay (sec (2)) * kRate)) <= 1);
        }
        // the merge: fermata's file names, 6.5 s long (3 s of pre-take, 3.5 s recorded), every sample as fed, from the primary and from the backup alike
        const auto out = project.getChildFile ("Recorded Media").getChildFile ("First half");
        const auto res = mergeTake (session1, 1, out);
        CHECK_MSG (res.ok, res.error);
        CHECK (res.files.size() == 3 && res.lengthSamples == sec (6.5) && res.gaps.empty() && res.lostSamples == 0 && res.chunksUsed == 4);
        CHECK (res.files[0] == out.getChildFile ("001 - Symphony - Main.wav").getFullPathName() && res.files[2] == out.getChildFile ("001 - Symphony - RoomL.wav").getFullPathName());
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), tracks1[i], sec (1), sec (7.5)) == 0);
        {
            juce::StringPairArray meta;
            readInts (juce::File (res.files[0]), &meta);
            CHECK (meta[juce::WavAudioFormat::bwavDescription] == "First half / 001 - Symphony / Main" && meta[juce::WavAudioFormat::bwavOriginator] == "Fermata");
            CHECK (std::abs (meta[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue() - (juce::int64) std::llround (idx.timeOfDay (sec (1)) * kRate)) <= 1);
            CHECK (meta[juce::WavAudioFormat::riffInfoSoftware] == "fermata:Live test");
        }
        const auto outB = LiveRecorder::backupProjectFolder (backup, project).getChildFile ("Recorded Media").getChildFile ("First half");
        const auto resB = mergeTake (backupSession1, 1, outB);
        CHECK_MSG (resB.ok, resB.error);
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (resB.files[(int) i]), tracks1[i], sec (1), sec (7.5)) == 0);
        // merging again is refused to overwrite nothing: a second set of files gets "(2)"
        const auto res2 = mergeTake (session1, 1, out);
        CHECK (res2.ok && res2.files[0].contains ("Main (2).wav"));
        for (auto& f : res2.files) juce::File (f).deleteFile();
        // the take noted as merged: its chunks go once they are older than the pre-take (3 s + a chunk)
        auto merged = takes[0]; merged.merged = true; merged.mergedFiles = res.files;
        rec.updateTake (merged);
        feed (rec, 8.0, 50);
        CHECK (waitWritten (rec, sec (18)));
        rec.close();
        idx = rec.primaryIndex();
        CHECK (idx.state == "closed"
 && idx.lastSample == sec (18.2));
        CHECK (idx.takes.size() == 1 && idx.takes[0].merged && idx.takes[0].mergedFiles.size() == 3);
        bool oldGone = true;
        for (auto& c : idx.chunks) if (c.firstSample < sec (8)) oldGone = false;
        CHECK_MSG (oldGone, "chunks left: " + juce::String (idx.chunks.size()));
        CHECK (! SessionIndex::chunkFile (session1, tracks1[0], ChunkRec { "c000001", sec (2), sec (2) }).existsAsFile());
        CHECK (idx.chunks.back().samples == sec (0.2) && idx.chunks.back().firstSample == sec (18));
        // the index reads back as written
        SessionIndex again; CHECK (again.load (session1.getChildFile (kIndexFileName)));
        CHECK (juce::JSON::toString (again.toVar()) == juce::JSON::toString (idx.toVar()));
        CHECK (again.takes[0].recordedAt.toMilliseconds() / 1000 == idx.takes[0].recordedAt.toMilliseconds() / 1000 && again.origin.toISO8601 (true) == idx.origin.toISO8601 (true));
        // the project: the take goes into a fermata project as fermata would have recorded it
        {
            Project p; p.projectFile = project.getChildFile ("Concert.fermata");
            p.setInputCount (8); p.setOutputCount (2); p.tracks.clear();
            p.takeWindows.front()->name = "First half";                  // (a new project's one take window, named as the user named it)
            for (auto& t : tracks1) { auto& tr = p.addTrack (t.name, t.channels == 2 ? TrackFormat::Stereo : TrackFormat::Mono, t.inputs[0]); tr.id = t.id; }
            auto& w = TakeRegister::window (p, "First half");
            w.nextNumber = 1;
            const auto gid = TakeRegister::add (p, w, idx, merged, res);
            CHECK (w.groups.size() == 1 && w.groups[0].id == gid && w.groups[0].number == 1 && w.groups[0].label == "Symphony" && w.nextNumber == 2);
            CHECK (w.groups[0].files.size() == 3 && w.groups[0].files[0].trackId == tracks1[0].id && w.groups[0].files[0].file == juce::File (res.files[0]) && w.groups[0].files[0].numChannels == 2);
            CHECK (w.groups[0].lengthSamples == sec (6.5) && w.groups[0].sampleRate == kRate && w.groups[0].lostSamples == 0 && w.markers.empty());
            juce::String err; CHECK (p.save (err));
            Project q; CHECK (q.load (p.projectFile, err));
            CHECK (q.takeWindows.size() == 1 && q.takeWindows[0]->groups.size() == 1 && q.takeWindows[0]->groups[0].files[0].file.existsAsFile() && q.takeWindows[0]->displayName (q.takeWindows[0]->groups[0]) == "001 - Symphony");
            // the same take again (the merge ran twice): replaced, not doubled
            TakeRegister::add (q, *q.takeWindows[0], idx, merged, res);
            CHECK (q.takeWindows[0]->groups.size() == 1);
        }
    }

    SECTION ("a drive that fails is dropped; the other goes on; its own merge gives what it has");
    {
        tmp.getChildFile ("Concert2").deleteRecursively();
        const auto project2 = tmp.getChildFile ("Concert2");
        LiveRecorder rec (disk);
        std::vector<std::pair<juce::File, juce::String>> drops; std::mutex dl;
        rec.callbacks.driveDropped = [&] (const juce::File& f, const juce::String& why) { const std::lock_guard<std::mutex> l (dl); drops.emplace_back (f, why); };
        auto setup = baseSetup (project2, "Second half");
        setup.backupRoots = { backup };
        CHECK (rec.prepare (setup).isEmpty());
        const auto backupSession = rec.driveWriters()[1]->folder();
        feed (rec, 2.0, 50);
        CHECK (rec.startTake (3, "").isEmpty());
        feed (rec, 1.0, 50);
        CHECK (waitWritten (rec, sec (2)));
        disk->set (disk->failWrites, backupSession, 1);              // the backup drive goes
        feed (rec, 5.0, 50);
        CHECK (rec.stopTake() == 3);
        feed (rec, 2.5, 50);
        CHECK (waitWritten (rec, sec (10)));
        rec.close();
        { const std::lock_guard<std::mutex> l (dl); CHECK (drops.size() == 1 && drops[0].first == backupSession && drops[0].second.contains ("refused")); }
        const auto st = rec.drives();
        CHECK (st.size() == 2 && ! st[0].dropped && st[1].dropped && st[1].why.contains ("refused") && st[0].chunks >= 5);
        const auto out = project2.getChildFile ("Recorded Media").getChildFile ("Second half");
        const auto res = mergeTake (rec.primaryFolder(), 3, out);
        CHECK_MSG (res.ok, res.error);
        CHECK (res.lengthSamples == sec (8) && res.gaps.empty() && res.files[0].endsWith ("003 - Second half - Main.wav"));
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), rec.tracks()[i], 0, sec (8)) == 0);
        // the dropped backup: the index could not be written at the drop (its writes fail), so the one on disk is from the last chunk - the
        // merge from it needs allowOpenChunks and gives the audio up to where the drive went, then silence
        disk->set (disk->failWrites, backupSession, 0);
        SessionIndex bidx; CHECK (bidx.load (backupSession.getChildFile (kIndexFileName)));
        CHECK (bidx.takes.size() == 1 && bidx.takes[0].number == 3);
        const auto outB = LiveRecorder::backupProjectFolder (backup, project2).getChildFile ("Recorded Media").getChildFile ("Second half");
        // (its index stopped at the drop, before the take ended: the take's span comes from the recorder, as the service gives it)
        const auto takeRec = rec.takesSoFar()[0];
        CHECK (takeRec.number == 3 && takeRec.end == sec (8));
        const auto refused = mergeTake (backupSession, 3, outB, false, {}, {}, nullptr, &takeRec);
        CHECK (! refused.ok && refused.error.contains ("still being written"));
        const auto resB = mergeTake (backupSession, 3, outB, true, {}, {}, nullptr, &takeRec);
        CHECK_MSG (resB.ok, resB.error);
        std::printf ("     backup merge: %d gaps, %lld lost, %d chunks\n", (int) resB.gaps.size(), (long long) resB.lostSamples, resB.chunksUsed);
        if (resB.gaps.empty()) { std::printf ("  FAIL: no gap in the backup's merge\n"); ++failures; }
        else
        CHECK (resB.gaps.size() == 1 && resB.gaps[0].why == "no recording on this drive" && resB.lostSamples > sec (4) && resB.lostSamples < sec (6.5));
        const juce::int64 holeFrom = resB.gaps[0].firstSample;
        CHECK (holeFrom >= sec (2) && holeFrom <= sec (4));
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (resB.files[(int) i]), rec.tracks()[i], 0, sec (8), { { holeFrom, sec (8) } }) == 0);
    }

    SECTION ("a drive that stalls for longer than the buffer gets a gap of silence and goes on; the other drive is untouched");
    {
        tmp.getChildFile ("Concert3").deleteRecursively();
        const auto project3 = tmp.getChildFile ("Concert3");
        LiveRecorder rec (disk);
        std::vector<GapRec> gapsSeen; std::mutex gl;
        rec.callbacks.gap = [&] (const juce::File&, const GapRec& g) { const std::lock_guard<std::mutex> l (gl); gapsSeen.push_back (g); };
        auto setup = baseSetup (project3, "Stall");
        setup.backupRoots = { backup };
        setup.bufferSeconds = 2;
        CHECK (rec.prepare (setup).isEmpty());
        const auto backupSession = rec.driveWriters()[1]->folder();
        CHECK (rec.startTake (7, "Stalled").isEmpty());
        feed (rec, 2.0, 300);
        CHECK (waitWritten (rec, sec (1)));
        disk->set (disk->stallMs, backupSession, 1500 * pace());    // every write to the backup now takes 1.5 s: it falls far behind the 2 s buffer
        feed (rec, 8.0, 300);                                        // 8 s of audio in about 2.4 s
        disk->set (disk->stallMs, backupSession, 0);
        feed (rec, 4.0, 300);
        CHECK (rec.stopTake() == 7);
        feed (rec, 2.0, 300);
        CHECK (waitWritten (rec, sec (16)));
        rec.close();
        const auto st = rec.drives();
        CHECK (! st[0].dropped && ! st[1].dropped && st[0].gaps == 0 && st[1].gaps >= 1);
        { const std::lock_guard<std::mutex> l (gl); CHECK (! gapsSeen.empty() && gapsSeen[0].why.contains ("stalled") && gapsSeen[0].samples > 0); }
        const auto out = project3.getChildFile ("Recorded Media").getChildFile ("Stall");
        const auto res = mergeTake (rec.primaryFolder(), 7, out);
        CHECK_MSG (res.ok, res.error);
        CHECK (res.gaps.empty());
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), rec.tracks()[i], 0, sec (14)) == 0);
        const auto outB = LiveRecorder::backupProjectFolder (backup, project3).getChildFile ("Recorded Media").getChildFile ("Stall");
        const auto resB = mergeTake (backupSession, 7, outB);
        CHECK_MSG (resB.ok, resB.error);
        CHECK (! resB.gaps.empty() && resB.lostSamples > sec (1));
        std::vector<std::pair<juce::int64, juce::int64>> silent;
        for (auto& g : resB.gaps) silent.emplace_back (g.firstSample, g.firstSample + g.samples);
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (resB.files[(int) i]), rec.tracks()[i], 0, sec (14), silent) == 0);
        std::printf ("   backup: %d gap(s), %lld samples lost\n", (int) resB.gaps.size(), (long long) resB.lostSamples);
    }

    SECTION ("the program dies in the middle of a chunk: the session is carried on, the take merges with a gap where nothing was recorded");
    {
        tmp.getChildFile ("Concert4").deleteRecursively();
        const auto project4 = tmp.getChildFile ("Concert4");
        juce::File session; std::vector<TrackSpec> tracks;
        const auto origin = juce::Time::getCurrentTime();
        {
            LiveRecorder rec (disk);
            auto setup = baseSetup (project4, "Crash");
            setup.nowForTests = origin;
            CHECK (rec.prepare (setup).isEmpty());
            session = rec.primaryFolder(); tracks = rec.tracks();
            feed (rec, 1.0, 50);
            CHECK (rec.startTake (4, "").isEmpty());
            feed (rec, 4.3, 50);
            CHECK (waitWritten (rec, sec (4)));
            juce::Thread::sleep (300);                               // (the writer is inside chunk c000002, with up to 1.3 s of it written)
            rec.abandonForTests();
        }
        SessionIndex left; CHECK (left.load (session.getChildFile (kIndexFileName)));
        CHECK (left.state == "writing" && left.chunks.size() == 3 && left.chunks[2].samples == -1 && left.takes.size() == 1 && left.takes[0].end == -1);
        const auto openLen = Merge::chunkLength (SessionIndex::chunkFile (session, tracks[0], left.chunks[2]));
        CHECK (openLen > 0 && openLen <= sec (1.3));
        {   // the recorder starts again 6 s (of the clock) after the session began: the timeline goes on from there
            LiveRecorder rec (disk);
            auto setup = baseSetup (project4, "Crash");
            setup.resume = true;
            setup.nowForTests = origin + juce::RelativeTime::seconds (6.0);
            rec.callbacks.log = [] (const juce::String& t) { std::printf ("     %s\n", t.toRawUTF8()); };
            CHECK (rec.prepare (setup).isEmpty());
            CHECK (rec.primaryFolder() == session && rec.position() == sec (6) && rec.recordingTake() == 4);
            CHECK (rec.tracks().size() == 3 && rec.tracks()[0].id == tracks[0].id);
            feed (rec, 3.0, 50);
            CHECK (rec.stopTake() == 4);
            feed (rec, 1.2, 50);
            CHECK (waitWritten (rec, sec (10)));
            rec.close();
            const auto idx = rec.primaryIndex();
            CHECK (idx.gaps.size() == 1 && idx.gaps[0].firstSample == sec (4) + openLen && idx.gaps[0].samples == sec (2) - openLen && idx.gaps[0].why.contains ("not being written"));
            CHECK (idx.chunks.size() == 6 && idx.chunks[2].samples == openLen && idx.chunks[3].name == "c000003" && idx.chunks[3].firstSample == sec (6) && idx.chunks[3].samples == sec (2));
            const auto out = project4.getChildFile ("Recorded Media").getChildFile ("Crash");
            const auto res = mergeTake (session, 4, out);
            CHECK_MSG (res.ok, res.error);
            CHECK (res.lengthSamples == sec (9));                   // from 0 (the pre-take reaches the start) to 9 s
            // the hole: from the end of what the cut-off chunk holds to 6 s (the drive's own note of it and the missing stretch are one gap) - the
            // merged file is silent there and exact elsewhere
            CHECK (res.gaps.size() == 1 && res.gaps[0].firstSample == sec (4) + openLen && res.gaps[0].firstSample + res.gaps[0].samples == sec (6) && res.gaps[0].why.contains ("not being written"));
            CHECK (res.lostSamples == sec (2) - openLen);
            for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), tracks[i], 0, sec (9), { { sec (4) + openLen, sec (6) } }) == 0);
        }
    }

    SECTION ("pre-take and the rolling deletion: nothing recorded for a while, then a take that starts 2 s before Record");
    {
        tmp.getChildFile ("Concert5").deleteRecursively();
        const auto project5 = tmp.getChildFile ("Concert5");
        LiveRecorder rec (disk);
        auto setup = baseSetup (project5, "Pre");
        setup.chunkSeconds = 1; setup.preTakeSeconds = 2;
        CHECK (rec.prepare (setup).isEmpty());
        feed (rec, 10.0, 50);
        CHECK (waitWritten (rec, sec (10)));
        auto idx = rec.primaryIndex();
        CHECK_MSG (idx.chunks.size() <= 4 && idx.chunks.size() >= 3, "chunks kept: " + juce::String (idx.chunks.size()));
        CHECK (idx.chunks.front().firstSample >= sec (6));
        CHECK (rec.startTake (1, "").isEmpty());
        feed (rec, 3.0, 50);
        CHECK (rec.stopTake() == 1);
        feed (rec, 1.5, 50);
        CHECK (waitWritten (rec, sec (14)));
        rec.close();
        const auto res = mergeTake (rec.primaryFolder(), 1, project5.getChildFile ("Recorded Media").getChildFile ("Pre"));
        CHECK_MSG (res.ok, res.error);
        CHECK (res.lengthSamples == sec (5) && res.gaps.empty());
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), rec.tracks()[i], sec (8), sec (13)) == 0);
    }

    SECTION ("the merge: stopped after a track, it goes on from the next; a merged file that differs from its chunks is found");
    {
        tmp.getChildFile ("Concert6").deleteRecursively();
        const auto project6 = tmp.getChildFile ("Concert6");
        LiveRecorder rec (disk);
        auto setup = baseSetup (project6, "Cut");
        CHECK (rec.prepare (setup).isEmpty());
        CHECK (rec.startTake (2, "Encore").isEmpty());
        feed (rec, 5.0, 50);
        CHECK (rec.stopTake() == 2);
        feed (rec, 1.2, 50);
        CHECK (waitWritten (rec, sec (6)));
        rec.close();
        const auto out = project6.getChildFile ("Recorded Media").getChildFile ("Cut");
        std::atomic<int> saved { 0 };
        TakeRec progress;
        auto res = mergeTake (rec.primaryFolder(), 2, out, false, [&] { return saved.load() >= 1; }, [&] (const TakeRec& t) { progress = t; saved.fetch_add (1); });
        CHECK (! res.ok && res.error == "Cancelled" && saved.load() == 1 && progress.mergedFiles[0].isNotEmpty() && progress.mergedFiles[1].isEmpty());
        CHECK (juce::File (progress.mergedFiles[0]).existsAsFile() && out.findChildFiles (juce::File::findFiles, false, "*.merging.wav").isEmpty());
        // the recorder's index is told (as the service does), then the merge runs again and skips the first track
        auto* primary = rec.primary();
        primary->updateIndex ([&] (SessionIndex& i) { *i.findTake (2) = progress; });
        juce::Thread::sleep (300);                                   // (the writer thread has ended: write the index here as it would have)
        { auto snap = primary->snapshot(); CHECK (snap.save (primary->indexFile())); }
        const auto firstTime = juce::File (progress.mergedFiles[0]).getLastModificationTime();
        juce::Thread::sleep (1100);
        res = mergeTake (rec.primaryFolder(), 2, out, false, {}, [&] (const TakeRec& t) { progress = t; });
        CHECK_MSG (res.ok, res.error);
        CHECK (res.files.size() == 3 && res.files[0] == progress.mergedFiles[0] && juce::File (res.files[0]).getLastModificationTime() == firstTime);
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), rec.tracks()[i], 0, sec (5)) == 0);
        // the check itself: a byte changed in a merged file is found, and a byte beyond the audio is not
        {
            SessionIndex idx; idx.load (rec.primaryFolder().getChildFile (kIndexFileName));
            const auto parts = Merge::pieces (idx, 0, sec (5), [] (const ChunkRec& c) { return c.samples; });
            CHECK (parts.size() == 3 && ! parts[0].hole && parts[2].samples == sec (1));
            CHECK (Merge::verifyFile (juce::File (res.files[1]), rec.primaryFolder(), rec.tracks()[1], parts, sec (5), {}, {}).isEmpty());
            juce::File f (res.files[1]);
            {
                char was = 0;
                { juce::FileInputStream in (f); in.setPosition (f.getSize() / 2); was = in.readByte(); }
                juce::FileOutputStream o (f); o.setPosition (f.getSize() / 2); const char x = (char) ~was; o.write (&x, 1); o.flush();
            }
            const auto bad = Merge::verifyFile (f, rec.primaryFolder(), rec.tracks()[1], parts, sec (5), {}, {});
            CHECK_MSG (bad.contains ("differs from its chunks at sample"), bad);
        }
    }

    SECTION ("a backup taken on again keeps its own chunks; a take number cannot be used twice; a merge waits for the whole take");
    {
        tmp.getChildFile ("Concert7").deleteRecursively();
        const auto project7 = tmp.getChildFile ("Concert7");
        LiveRecorder rec (disk);
        auto setup = baseSetup (project7, "Again");
        setup.backupRoots = { backup }; setup.chunkSeconds = 1; setup.preTakeSeconds = 1;
        CHECK (rec.prepare (setup).isEmpty());
        const auto backupSession = rec.driveWriters()[1]->folder();
        feed (rec, 1.0, 50);
        CHECK (rec.startTake (5, "Five").isEmpty());
        CHECK (rec.startTake (5, "Five again") == "Already recording");
        feed (rec, 2.0, 50);
        CHECK (rec.stopTake() == 5);
        CHECK (rec.startTake (5, "Five again") == "Take 5 is already in this session");
        // the merge from the live primary is refused until the drive has written the take's end (and its chunk has closed)
        {
            const auto now = rec.primaryIndex();
            if (now.lastSample < sec (3))
            {
                const auto early = mergeTake (rec.primaryFolder(), 5, project7.getChildFile ("Recorded Media").getChildFile ("Again"), false, {}, {}, &now);
                CHECK_MSG (! early.ok && early.error.contains ("still being written"), early.error);
            }
        }
        feed (rec, 1.5, 50);
        CHECK (waitWritten (rec, sec (4)));
        const auto out = project7.getChildFile ("Recorded Media").getChildFile ("Again");
        auto res = mergeTake (rec.primaryFolder(), 5, out);
        CHECK_MSG (res.ok, res.error);
        auto merged = rec.takesSoFar()[0]; merged.merged = true; merged.mergedFiles = res.files;
        rec.updateTake (merged);
        TakeRec onBackup; CHECK (rec.takeOnDrive (backupSession, 5, onBackup) && ! onBackup.merged && onBackup.end == sec (3));
        // the backup goes, is taken on again, and the session runs on past the pre-take: the backup's chunks of take 5 must still be there
        disk->set (disk->failWrites, backupSession, 1);
        feed (rec, 1.5, 50);
        { const auto until = juce::Time::getMillisecondCounter() + 10000; while (! rec.driveWriters()[1]->isDropped() && juce::Time::getMillisecondCounter() < until) juce::Thread::sleep (10); }
        CHECK (rec.driveWriters()[1]->isDropped());
        disk->set (disk->failWrites, backupSession, 0);
        CHECK (rec.resumeDrive (backupSession, "backup").isEmpty());
        CHECK (rec.resumeDrive (backupSession, "backup") == "That drive is being written to");
        feed (rec, 6.0, 50);
        CHECK (waitWritten (rec, sec (12)));
        rec.close();
        SessionIndex bidx; CHECK (bidx.load (backupSession.getChildFile (kIndexFileName)));
        CHECK (bidx.state == "closed" && bidx.findTake (5) != nullptr && ! bidx.findTake (5)->merged);
        int kept = 0; for (auto& c : bidx.chunks) if (c.firstSample < sec (3)) ++kept;
        CHECK_MSG (kept >= 3, "backup chunks of the take kept: " + juce::String (kept));
        CHECK (! bidx.gaps.empty() && bidx.gaps.back().why.contains ("not being written"));
        const auto pidx = rec.primaryIndex();
        int primaryKept = 0; for (auto& c : pidx.chunks) if (c.firstSample < sec (3)) ++primaryKept;
        CHECK (primaryKept == 0);                                    // (the primary's chunks of the merged take went)
        const auto takeRec = rec.takesSoFar()[0];
        const auto resB = mergeTake (backupSession, 5, LiveRecorder::backupProjectFolder (backup, project7).getChildFile ("Recorded Media").getChildFile ("Again"), true, {}, {}, nullptr, &takeRec);
        CHECK_MSG (resB.ok, resB.error);
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (resB.files[(int) i]), rec.tracks()[i], 0, sec (3)) == 0);
    }

    SECTION ("an index that cannot be written does not stop the recording");
    {
        tmp.getChildFile ("Concert8").deleteRecursively();
        const auto project8 = tmp.getChildFile ("Concert8");
        LiveRecorder rec (disk);
        auto setup = baseSetup (project8, "Index");
        setup.chunkSeconds = 1;
        CHECK (rec.prepare (setup).isEmpty());
        feed (rec, 1.5, 50);
        CHECK (waitWritten (rec, sec (1)));
        disk->set (disk->failIndex, rec.primaryFolder(), 1);
        CHECK (rec.startTake (1, "").isEmpty());
        feed (rec, 3.0, 50);
        CHECK (waitWritten (rec, sec (4)));
        CHECK (! rec.primary()->isDropped() && rec.primary()->indexWriteFailures() > 0 && rec.primary()->chunksWritten() >= 4);
        SessionIndex onDisk; CHECK (onDisk.load (rec.primaryFolder().getChildFile (kIndexFileName)) && onDisk.takes.empty());       // (the index on the disk is behind)
        disk->set (disk->failIndex, rec.primaryFolder(), 0);
        CHECK (rec.stopTake() == 1);
        feed (rec, 1.5, 50);
        CHECK (waitWritten (rec, sec (6)));
        rec.close();
        CHECK (onDisk.load (rec.primaryFolder().getChildFile (kIndexFileName)) && onDisk.takes.size() == 1 && onDisk.takes[0].end == sec (4.5) && onDisk.state == "closed" && onDisk.chunks.size() >= 6);
        const auto res = mergeTake (rec.primaryFolder(), 1, project8.getChildFile ("Recorded Media").getChildFile ("Index"));
        CHECK_MSG (res.ok, res.error);
        for (size_t i = 0; i < 3; ++i) CHECK (checkMerged (juce::File (res.files[(int) i]), rec.tracks()[i], 0, sec (4.5)) == 0);
    }

    SECTION ("the index: a damaged file falls back to its backup");
    {
        SessionIndex idx; idx.projectName = "P"; idx.windowName = "W"; idx.sampleRate = kRate; idx.chunkSamples = sec (2); idx.origin = juce::Time::getCurrentTime();
        idx.tracks.push_back (TrackSpec { juce::Uuid(), "A", 1, { 3 }, "A", 0 });
        idx.chunks.push_back ({ "c000000", 0, sec (2) }); idx.gaps.push_back ({ 10, 20, "12:00:00", "why" });
        TakeRec t; t.number = 9; t.label = "L"; t.start = 5; t.end = 500; t.recordPoint = 50; t.recordedAt = juce::Time::getCurrentTime(); t.merged = true; t.mergedFiles.add ("/x/y.wav");
        t.mergedSamples = 495; t.mergeGaps.push_back ({ 7, 3, "12:00:01", "test" });
        idx.takes.push_back (t);
        const auto f = tmp.getChildFile ("idx").getChildFile (kIndexFileName);
        CHECK (idx.save (f) && idx.save (f));                       // (the second save leaves a .bak)
        SessionIndex back; CHECK (back.load (f) && back.takes.size() == 1 && back.takes[0].mergedFiles[0] == "/x/y.wav" && back.gaps[0].why == "why" && back.tracks[0].inputs[0] == 3);
        CHECK (back.takes[0].mergedSamples == 495 && back.takes[0].mergeGaps.size() == 1 && back.takes[0].mergeGaps[0].why == "test");
        f.replaceWithText ("{ broken");
        SessionIndex fromBak; CHECK (fromBak.load (f) && fromBak.takes.size() == 1 && fromBak.chunks[0].name == "c000000");
        CHECK (! f.getSiblingFile (juce::String (kIndexFileName) + ".damaged").existsAsFile() && f.loadFileAsString() == "{ broken");   // (a plain read repairs nothing: the file may be a live drive's)
        SessionIndex repaired; CHECK (repaired.load (f, true) && repaired.takes.size() == 1);
        CHECK (f.getSiblingFile (juce::String (kIndexFileName) + ".damaged").existsAsFile() && f.loadFileAsString() != "{ broken");
        CHECK (idx.chunkNameFor (sec (5)) == "c000002" && idx.timeText (0).length() == 8);
    }

    DriveWriter::finishAbandonedForTests();
    std::printf (failures == 0 ? "LIVE RECORDER TESTS PASSED\n" : "LIVE RECORDER TESTS: %d FAILURE(S)\n", failures);
    if (std::getenv ("LIVE_TEST_KEEP") == nullptr) tmp.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
