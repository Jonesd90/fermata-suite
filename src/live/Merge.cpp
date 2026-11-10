#include "Merge.h"
#include "../core/WavRepair.h"
#include "../core/Takes.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <map>

namespace td::live
{
juce::int64 Merge::chunkLength (const juce::File& f)
{
    if (! f.existsAsFile()) return -1;
    const auto r = wavrepair::inspect (f, true, true);
    return r.ok ? r.frames : -1;
}

juce::String Merge::fileNameFor (const TakeRec& t, const juce::String& windowName, const juce::String& trackName)
{
    return TakeWindowDef::fileNameFor (t.number, labelOf (t, windowName), trackName);
}

std::vector<Merge::Piece> Merge::pieces (const SessionIndex& idx, juce::int64 start, juce::int64 end, const std::function<juce::int64 (const ChunkRec&)>& lengthOf)
{
    std::vector<const ChunkRec*> sorted;
    for (auto& c : idx.chunks) sorted.push_back (&c);
    std::stable_sort (sorted.begin(), sorted.end(), [] (const ChunkRec* a, const ChunkRec* b) { return a->firstSample < b->firstSample; });
    std::vector<Piece> out;
    juce::int64 cursor = start;
    for (auto* c : sorted)
    {
        if (cursor >= end) break;
        // 0.7.1: a chunk that cannot be part of the span is not measured at all. One that begins at or after its end (and so does every chunk
        // after it: they are in order) may be the chunk the drive is writing now - measuring mends a header that looks unfinished, which must
        // never be tried on a file being written. One that ends before the span has no part in it, and a long session has thousands of those.
        if (c->firstSample >= end) break;
        if (c->samples >= 0 && c->firstSample + c->samples <= cursor) continue;
        const juce::int64 len = lengthOf (*c);
        if (len <= 0) continue;
        const juce::int64 cEnd = c->firstSample + len;
        if (cEnd <= cursor || c->firstSample >= end) continue;
        if (c->firstSample > cursor) { Piece h; h.hole = true; h.first = cursor; h.samples = c->firstSample - cursor; out.push_back (h); cursor = c->firstSample; }
        Piece p; p.chunk = c; p.first = cursor; p.inChunk = cursor - c->firstSample; p.samples = juce::jmin (end, cEnd) - cursor;
        out.push_back (p);
        cursor += p.samples;
    }
    if (cursor < end) { Piece h; h.hole = true; h.first = cursor; h.samples = end - cursor; out.push_back (h); }
    return out;
}

juce::String Merge::verifyFile (const juce::File& mergedFile, const juce::File& sessionFolder, const TrackSpec& t, const std::vector<Piece>& parts, juce::int64 lengthSamples, Flag paused, Flag cancelled)
{
    constexpr int kPiece = 65536;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> merged (wav.createReaderFor (new juce::FileInputStream (mergedFile), true));
    if (merged == nullptr || merged->lengthInSamples != lengthSamples || (int) merged->numChannels != t.channels)
        return "the merged file " + mergedFile.getFileName() + " does not read back as " + juce::String (lengthSamples) + " samples of " + juce::String (t.channels) + " channel(s)";
    std::vector<std::vector<int>> a ((size_t) t.channels, std::vector<int> ((size_t) kPiece, 0)), b ((size_t) t.channels, std::vector<int> ((size_t) kPiece, 0));
    std::vector<int*> pa, pb; for (auto& v : a) pa.push_back (v.data()); for (auto& v : b) pb.push_back (v.data());
    juce::int64 outPos = 0;
    for (auto& p : parts)
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        if (! p.hole)
        {
            reader.reset (wav.createReaderFor (new juce::FileInputStream (SessionIndex::chunkFile (sessionFolder, t, *p.chunk)), true));
            if (reader == nullptr) return "cannot read " + p.chunk->name + " again for the check";
        }
        for (juce::int64 at = 0; at < p.samples; at += kPiece)
        {
            while (paused && paused() && ! (cancelled && cancelled())) juce::Thread::sleep (200);
            if (cancelled && cancelled()) return "Cancelled";
            const int n = (int) juce::jmin<juce::int64> (kPiece, p.samples - at);
            if (! merged->read (pa.data(), t.channels, outPos, n, false)) return "cannot read the merged file back";
            if (p.hole) { for (auto& v : b) std::fill (v.begin(), v.begin() + n, 0); }
            else if (! reader->read (pb.data(), t.channels, p.inChunk + at, n, false)) return "cannot read " + p.chunk->name + " again for the check";
            for (int c = 0; c < t.channels; ++c)
                if (std::memcmp (a[(size_t) c].data(), b[(size_t) c].data(), (size_t) n * sizeof (int)) != 0)
                {
                    for (int i = 0; i < n; ++i)
                        if (a[(size_t) c][(size_t) i] != b[(size_t) c][(size_t) i])
                            return "the merged file for " + t.name + " differs from its chunks at sample " + juce::String (outPos + i) + " (channel " + juce::String (c + 1) + ")";
                }
            outPos += n;
        }
    }
    if (outPos != lengthSamples) return "the pieces add up to " + juce::String (outPos) + " samples, not " + juce::String (lengthSamples);
    return {};
}

MergeResult Merge::run (const Job& job, Flag paused, Flag cancelled, Progress progress, Log log, SaveProgress saveProgress)
{
    MergeResult res;
    auto fail = [&] (const juce::String& why) { res.ok = false; res.error = why; if (log) log ("Merge of take " + juce::String (job.takeNumber) + " stopped: " + why); return res; };
    auto waitIfPaused = [&]
    {
        while (paused && paused() && ! (cancelled && cancelled())) juce::Thread::sleep (200);
        return ! (cancelled && cancelled());
    };
    SessionIndex idx;
    if (job.index != nullptr) idx = *job.index;
    else if (! idx.load (job.sessionFolder.getChildFile (kIndexFileName))) return fail ("cannot read the session index in " + job.sessionFolder.getFullPathName());
    TakeRec take;
    if (job.take != nullptr) take = *job.take;
    else if (auto* takePtr = idx.findTake (job.takeNumber)) take = *takePtr;
    else return fail ("take " + juce::String (job.takeNumber) + " is not in the session index");
    if (take.end < 0) return fail ("take " + juce::String (job.takeNumber) + " is still recording");
    if (idx.tracks.empty()) return fail ("the session has no tracks");
    const juce::int64 chunkSamples = juce::jmax<juce::int64> (1, idx.chunkSamples);
    if (! job.allowOpenChunks)
    {   // a live drive: everything up to the take's end must be on the disk, in closed chunks, before the take is merged (else the end would be a hole,
        // the take marked merged, and the chunk that holds the real end deleted later)
        if (idx.lastSample < take.end) return fail ("the take is still being written to the drive (written up to " + juce::String (idx.lastSample) + " of " + juce::String (take.end) + ")");
        for (auto& c : idx.chunks)
            if (c.samples < 0 && c.firstSample < take.end)
                return fail ("the take is still being written to the drive (chunk " + c.name + " is open)");
    }

    // the chunks' real lengths: what is in the files, never more than the index says (a chunk the recorder could not finish is measured and mended
    // here; a file shorter than its record - the drive went while it was written - counts only as far as it goes, and the rest is a hole)
    std::map<juce::String, juce::int64> lengths;
    auto lengthOf = [&] (const ChunkRec& c) -> juce::int64
    {
        if (auto it = lengths.find (c.name); it != lengths.end()) return it->second;
        juce::int64 real = -1;                   // the shortest of its track files (they are all the same length unless the recorder stopped half way through a write)
        for (auto& t : idx.tracks)
        {
            const auto n = chunkLength (SessionIndex::chunkFile (job.sessionFolder, t, c));
            if (n < 0) { real = 0; break; }      // (a track's file missing: the chunk is no use)
            real = real < 0 ? n : juce::jmin (real, n);
        }
        if (real < 0) real = 0;
        juce::int64 len = c.samples >= 0 ? juce::jmin (c.samples, real) : real;
        len = juce::jmin (len, chunkSamples);
        if (c.samples >= 0 && real < c.samples && log) log ("Chunk " + c.name + " holds " + juce::String (real) + " of its " + juce::String (c.samples) + " samples");
        lengths[c.name] = len;
        return len;
    };
    const auto parts = pieces (idx, take.start, take.end, lengthOf);
    res.lengthSamples = take.end - take.start;
    for (auto& p : parts)
    {
        if (p.hole) { GapRec g { p.first, p.samples, idx.timeText (p.first), "no recording on this drive" }; res.gaps.push_back (g); res.lostSamples += g.samples; }
        else ++res.chunksUsed;
    }
    for (auto& g : idx.gaps)                                   // the drive's own gaps (silence it wrote itself) that fall in the take
        if (g.firstSample < take.end && g.firstSample + g.samples > take.start)
        {
            GapRec in = g; in.firstSample = juce::jmax (g.firstSample, take.start); in.samples = juce::jmin (g.firstSample + g.samples, take.end) - in.firstSample;
            res.gaps.push_back (in); res.lostSamples += in.samples;
        }
    std::sort (res.gaps.begin(), res.gaps.end(), [] (const GapRec& a, const GapRec& b) { return a.firstSample < b.firstSample; });
    for (size_t i = 1; i < res.gaps.size();)                   // a hole and the drive's own note of the same stretch: one gap, the drive's reason kept
    {
        auto& a = res.gaps[i - 1]; auto& b = res.gaps[i];
        if (b.firstSample <= a.firstSample + a.samples)
        {
            const auto end = juce::jmax (a.firstSample + a.samples, b.firstSample + b.samples);
            if (a.why == "no recording on this drive") a.why = b.why;
            a.samples = end - a.firstSample;
            res.gaps.erase (res.gaps.begin() + (std::ptrdiff_t) i);
        }
        else ++i;
    }
    res.lostSamples = 0;
    for (auto& g : res.gaps) res.lostSamples += g.samples;

    if (! job.outputFolder.createDirectory()) return fail ("cannot make " + job.outputFolder.getFullPathName());
    while (take.mergedFiles.size() < (int) idx.tracks.size()) take.mergedFiles.add ({});
    while (take.mergedFiles.size() > (int) idx.tracks.size()) take.mergedFiles.remove (take.mergedFiles.size() - 1);
    res.files = take.mergedFiles;

    juce::WavAudioFormat wav;
    const auto now = juce::Time::getCurrentTime();
    const double tod = idx.timeOfDay (take.start);
    const auto label = labelOf (take, idx.windowName);
    const auto displayName = pad3 (take.number) + " - " + label;
    constexpr int kPiece = 65536;
    const juce::int64 total = juce::jmax<juce::int64> (1, res.lengthSamples * (juce::int64) idx.tracks.size() * 2);
    juce::int64 doneSamples = 0;

    for (size_t ti = 0; ti < idx.tracks.size(); ++ti)
    {
        const auto& t = idx.tracks[ti];
        if (take.mergedFiles[(int) ti].isNotEmpty() && juce::File (take.mergedFiles[(int) ti]).existsAsFile())
        {   // done by an earlier run of this merge
            doneSamples += res.lengthSamples * 2;
            continue;
        }
        if (! waitIfPaused()) return fail ("Cancelled");
        const auto finalName = fileNameFor (take, idx.windowName, t.name);
        const auto temp = job.outputFolder.getChildFile (finalName).withFileExtension (".merging.wav");
        temp.deleteFile();
        if (progress) progress ((double) doneSamples / (double) total, "Merging " + t.name);

        // ---- write ----
        std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;        // (opened as needed, one chunk at a time)
        {
            auto opts = juce::AudioFormatWriterOptions()
                            .withSampleRate (idx.sampleRate)
                            .withNumChannels (t.channels)
                            .withBitsPerSample (24)
                            .withMetadata (juce::WavAudioFormat::bwavDescription, idx.windowName + " / " + displayName + " / " + t.name)
                            .withMetadata (juce::WavAudioFormat::bwavOriginator, "Fermata")
                            .withMetadata (juce::WavAudioFormat::bwavOriginationDate, take.recordedAt.formatted ("%Y-%m-%d"))
                            .withMetadata (juce::WavAudioFormat::bwavOriginationTime, take.recordedAt.formatted ("%H:%M:%S"))
                            .withMetadata (juce::WavAudioFormat::bwavTimeReference, juce::String ((juce::int64) std::llround (tod * idx.sampleRate)))
                            .withMetadata (juce::WavAudioFormat::bwavOriginatorRef, ("FERMATA" + pad3 (take.number) + take.recordedAt.formatted ("%H%M%S")).substring (0, 32))
                            .withMetadata (juce::WavAudioFormat::bwavCodingHistory, "A=PCM,F=" + juce::String ((int) std::lround (idx.sampleRate)) + ",W=24,M="
                                                                                    + (t.channels == 1 ? juce::String ("mono") : t.channels == 2 ? juce::String ("stereo") : juce::String ("multitrack"))
                                                                                    + ",T=" + job.programName + "\r\n")
                            .withMetadata (juce::WavAudioFormat::riffInfoTitle, displayName + " - " + t.name)
                            .withMetadata (juce::WavAudioFormat::riffInfoProductName, job.projectName)
                            .withMetadata (juce::WavAudioFormat::riffInfoComment, "Take " + juce::String (take.number) + " of \"" + idx.windowName + "\", track \"" + t.name + "\" (live recording"
                                                                                  + (res.gaps.empty() ? juce::String (")") : ", " + juce::String (res.gaps.size()) + " gap(s))"))
                            .withMetadata (juce::WavAudioFormat::riffInfoDateCreated, now.formatted ("%Y-%m-%d"))
                            .withMetadata (juce::WavAudioFormat::riffInfoSoftware, job.programName);
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp, 65536);
            if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen()) return fail ("cannot create " + temp.getFullPathName());
            auto writer = wav.createWriterFor (stream, opts);
            if (writer == nullptr) return fail ("cannot write a 24-bit WAV file at " + temp.getFullPathName());
            std::vector<std::vector<int>> ints ((size_t) t.channels, std::vector<int> ((size_t) kPiece, 0));
            std::vector<int*> ptrs; for (auto& v : ints) ptrs.push_back (v.data());
            for (auto& p : parts)
            {
                std::unique_ptr<juce::AudioFormatReader> reader;
                if (! p.hole)
                {
                    reader.reset (wav.createReaderFor (new juce::FileInputStream (SessionIndex::chunkFile (job.sessionFolder, t, *p.chunk)), true));
                    if (reader == nullptr) return fail ("cannot read " + SessionIndex::chunkFile (job.sessionFolder, t, *p.chunk).getFullPathName());
                }
                for (juce::int64 at = 0; at < p.samples; at += kPiece)
                {
                    if (! waitIfPaused()) { writer.reset(); temp.deleteFile(); return fail ("Cancelled"); }
                    const int n = (int) juce::jmin<juce::int64> (kPiece, p.samples - at);
                    if (p.hole) { for (auto& v : ints) std::fill (v.begin(), v.begin() + n, 0); }
                    else if (! reader->read (ptrs.data(), t.channels, p.inChunk + at, n, false)) { writer.reset(); temp.deleteFile(); return fail ("cannot read a piece of " + p.chunk->name + " for " + t.name); }
                    if (! writer->write (const_cast<const int**> (ptrs.data()), n)) { writer.reset(); temp.deleteFile(); return fail ("the drive refused a write to " + temp.getFullPathName()); }
                    doneSamples += n;
                }
            }
            if (! writer->flush()) { writer.reset(); temp.deleteFile(); return fail ("the drive refused the end of " + temp.getFullPathName()); }
            writer.reset();
        }

        // ---- check: the merged file against the chunks, sample for sample ----
        if (progress) progress ((double) doneSamples / (double) total, "Checking " + t.name);
        if (const auto bad = verifyFile (temp, job.sessionFolder, t, parts, res.lengthSamples, paused, cancelled); bad.isNotEmpty())
        {
            temp.deleteFile();
            return fail (bad == "Cancelled" ? bad : bad + " (the chunks are kept)");
        }
        doneSamples += res.lengthSamples;

        // ---- it passed: give it its name (never over an existing file) ----
        auto finalFile = job.outputFolder.getChildFile (finalName);
        for (int dup = 2; finalFile.existsAsFile(); ++dup)
            finalFile = job.outputFolder.getChildFile (TakeWindowDef::fileNameFor (take.number, label, t.name + " (" + juce::String (dup) + ")"));
        if (! temp.moveFileTo (finalFile)) { temp.deleteFile(); return fail ("cannot rename " + temp.getFullPathName() + " to " + finalFile.getFileName()); }
        take.mergedFiles.set ((int) ti, finalFile.getFullPathName());
        res.files = take.mergedFiles;
        if (saveProgress) saveProgress (take);
        if (log) log ("Merged and checked " + finalFile.getFileName() + " (" + juce::String (res.chunksUsed) + " chunks)");
    }
    res.ok = true;
    if (progress) progress (1.0, "Merged");
    return res;
}
} // namespace td::live
