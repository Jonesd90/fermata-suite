#pragma once
/** fermata:Live 0.2.0: the index of a recording session on one drive - what the recorder wrote there, in order, so the chunks read as one
    timeline. One "session" is one run of the recorder with the device open: a timeline of samples from 0 (the first block the device gave)
    onwards, cut into chunks of kChunkSeconds, with the takes as spans of that timeline. Every drive the session writes to has its own copy of
    the index (session.json in that drive's session folder, beside the chunks), written by that drive's writer thread and nobody else.

    The index is the truth about a recording until its take has been merged and put into the project: a program that stops (or a PC that goes
    off) leaves the chunks and the index, and the next start merges from them. It is written safely (a complete file or the old one, with a .bak,
    see filesync) at every chunk, so at worst the last chunk is not yet listed - and a chunk's files are themselves complete WAVs (a cut-short one
    is mended by WavRepair, as fermata's own recordings are).

    What is in it is settled by COMPATIBILITY.md (contract 2): its format name and version live in src/core/Compat.h. */
#include "../core/Compat.h"
#include "../core/FileSync.h"
#include "../core/Common.h"
#include <vector>

namespace td::live
{
constexpr int kChunkSecondsDefault = 30;           // a chunk is this long (the brief: 30 seconds)
constexpr const char* kIndexFileName = "session.json";
constexpr const char* kChunksFolderName = "Live Chunks";

/** One recorded track of the session: its channels come from the ring in order (ringFirst .. ringFirst + channels - 1). */
struct TrackSpec
{
    juce::Uuid   id;                 // the project track (null: a track made up by the recorder, e.g. "Input 7")
    juce::String name;
    int          channels = 1;
    std::vector<int> inputs;         // the driver inputs (0-based) of each channel, -1 = none (silence)
    juce::String stem;               // the chunk files' name stem: "<stem>.c000012.wav" (unique among the session's tracks)
    int          ringFirst = 0;      // its first channel in the ring
};

/** One chunk: the piece of the timeline [firstSample, firstSample + samples) that the chunk files "<stem>.<name>.wav" hold, for every track.
    samples = -1 while the chunk is still being written (its files have no final header yet). */
struct ChunkRec
{
    juce::String name;               // "c000012": chunk 12 of the timeline (firstSample / chunkSamples); "c000012-r1" when this drive began again inside chunk 12
    juce::int64  firstSample = 0;
    juce::int64  samples = -1;
};

/** A stretch of the timeline this drive has no audio for (its disk stalled for longer than the memory buffer holds, or it was dropped and taken
    on again): the chunk holds silence there (or no chunk covers it), and the merge marks it. */
struct GapRec
{
    juce::int64  firstSample = 0, samples = 0;
    juce::String time;               // the time of day it began ("20:14:07"), for the marker and the warning
    juce::String why;
};

/** One press of Record: the span of the timeline [start, end), end = -1 while it records. start is up to the pre-take length before recordPoint. */
struct TakeRec
{
    int          number = 0;
    juce::String label;
    juce::int64  start = 0, end = -1, recordPoint = 0;
    juce::Time   recordedAt;
    bool         merged = false;     // its files have been merged from this drive's chunks and checked against them: the chunks may then go (deleteOldChunks)
    bool         registered = false; // the merged files are in the project (the take window lists them) - the window says so; not needed for the chunks to go
    juce::String mergeError;         // what stopped the last merge (empty: nothing)
    juce::StringArray mergedFiles;   // the merged files, one per track, in the tracks' order (full paths); filled in as the merge goes (a merge that stopped carries on from the next)
    juce::int64  mergedSamples = 0;  // the merged files' length
    std::vector<GapRec> mergeGaps;   // the gaps the merge found in the take (so the take can be put into the project later from the index alone)
};

struct SessionIndex
{
    juce::String projectName, windowName;       // the project and the take window the takes belong to
    juce::String projectFolder;                 // the project's folder (full path) - the primary drive's chunks are inside it
    juce::String role { "primary" };            // "primary" or "backup"
    juce::String state { "writing" };           // "writing", "dropped" (this drive failed: droppedWhy), "closed" (the session ended)
    juce::String droppedWhy;
    juce::String recorder;                      // the program that wrote it ("fermata:Live 0.2.0")
    double       sampleRate = 48000.0;
    juce::int64  chunkSamples = 0;
    juce::Time   origin;                        // the time of day of sample 0 (so every sample has a time of day: BWF time references, the gaps' times)
    juce::int64  lastSample = 0;                // this drive has written the timeline up to here
    std::vector<TrackSpec> tracks;
    std::vector<ChunkRec>  chunks;
    std::vector<GapRec>    gaps;
    std::vector<TakeRec>   takes;

    /** The time of day of a sample of the timeline, in seconds since midnight (wrapping at midnight). */
    double timeOfDay (juce::int64 sample) const
    {
        double tod = origin.getHours() * 3600.0 + origin.getMinutes() * 60.0 + origin.getSeconds() + origin.getMilliseconds() / 1000.0
                     + (sampleRate > 0 ? (double) sample / sampleRate : 0.0);
        while (tod >= 86400.0) tod -= 86400.0;
        return tod;
    }
    juce::String timeText (juce::int64 sample) const
    {
        const int s = (int) timeOfDay (sample);
        return juce::String (s / 3600).paddedLeft ('0', 2) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
    /** "c000012" for the chunk that holds 'sample'. */
    juce::String chunkNameFor (juce::int64 sample) const { return "c" + juce::String (chunkSamples > 0 ? sample / chunkSamples : 0).paddedLeft ('0', 6); }
    /** The chunk file of one track: "<stem>.<chunk name>.wav" in 'folder'. */
    static juce::File chunkFile (const juce::File& folder, const TrackSpec& t, const ChunkRec& c) { return folder.getChildFile (t.stem + "." + c.name + ".wav"); }

    TakeRec* findTake (int number) { for (auto& t : takes) if (t.number == number) return &t; return nullptr; }
    const TakeRec* findTake (int number) const { for (auto& t : takes) if (t.number == number) return &t; return nullptr; }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        juce::var root (o);
        o->setProperty ("format", compat::kLiveSessionFormatName);
        o->setProperty ("version", compat::kLiveSessionFormatVersion);
        o->setProperty ("projectName", projectName); o->setProperty ("windowName", windowName); o->setProperty ("projectFolder", projectFolder);
        o->setProperty ("role", role); o->setProperty ("state", state); o->setProperty ("droppedWhy", droppedWhy); o->setProperty ("recorder", recorder);
        o->setProperty ("sampleRate", sampleRate); o->setProperty ("chunkSamples", chunkSamples);
        o->setProperty ("origin", origin.toISO8601 (true)); o->setProperty ("lastSample", lastSample);
        juce::Array<juce::var> ts, cs, gs, ks;
        for (auto& t : tracks)
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("id", t.id.isNull() ? juce::String() : t.id.toString()); x->setProperty ("name", t.name); x->setProperty ("channels", t.channels);
            juce::Array<juce::var> ins; for (auto i : t.inputs) ins.add (i); x->setProperty ("inputs", ins);
            x->setProperty ("stem", t.stem); x->setProperty ("ringFirst", t.ringFirst);
            ts.add (juce::var (x));
        }
        for (auto& c : chunks) { auto* x = new juce::DynamicObject(); x->setProperty ("name", c.name); x->setProperty ("first", c.firstSample); x->setProperty ("samples", c.samples); cs.add (juce::var (x)); }
        for (auto& g : gaps)   { auto* x = new juce::DynamicObject(); x->setProperty ("first", g.firstSample); x->setProperty ("samples", g.samples); x->setProperty ("time", g.time); x->setProperty ("why", g.why); gs.add (juce::var (x)); }
        for (auto& k : takes)
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("number", k.number); x->setProperty ("label", k.label); x->setProperty ("start", k.start); x->setProperty ("end", k.end);
            x->setProperty ("recordPoint", k.recordPoint); x->setProperty ("recordedAt", k.recordedAt.toISO8601 (true));
            x->setProperty ("merged", k.merged); x->setProperty ("registered", k.registered); x->setProperty ("mergeError", k.mergeError);
            juce::Array<juce::var> fs; for (auto& f : k.mergedFiles) fs.add (f); x->setProperty ("mergedFiles", fs);
            x->setProperty ("mergedSamples", k.mergedSamples);
            juce::Array<juce::var> mg; for (auto& g : k.mergeGaps) { auto* y = new juce::DynamicObject(); y->setProperty ("first", g.firstSample); y->setProperty ("samples", g.samples); y->setProperty ("time", g.time); y->setProperty ("why", g.why); mg.add (juce::var (y)); }
            x->setProperty ("mergeGaps", mg);
            ks.add (juce::var (x));
        }
        o->setProperty ("tracks", ts); o->setProperty ("chunks", cs); o->setProperty ("gaps", gs); o->setProperty ("takes", ks);
        return root;
    }

    /** False if v is not a session index (the format name is checked; a newer version is read as far as it is understood). */
    bool fromVar (const juce::var& v)
    {
        if (! v.isObject() || v["format"].toString() != compat::kLiveSessionFormatName) return false;
        projectName = v["projectName"].toString(); windowName = v["windowName"].toString(); projectFolder = v["projectFolder"].toString();
        role = v["role"].toString(); state = v["state"].toString(); droppedWhy = v["droppedWhy"].toString(); recorder = v["recorder"].toString();
        sampleRate = (double) v["sampleRate"]; chunkSamples = (juce::int64) v["chunkSamples"];
        origin = juce::Time::fromISO8601 (v["origin"].toString()); lastSample = (juce::int64) v["lastSample"];
        tracks.clear(); chunks.clear(); gaps.clear(); takes.clear();
        if (auto* a = v["tracks"].getArray())
            for (auto& x : *a)
            {
                TrackSpec t;
                const auto id = x["id"].toString(); t.id = id.isEmpty() ? juce::Uuid::null() : juce::Uuid (id);
                t.name = x["name"].toString(); t.channels = juce::jlimit (1, (int) kMaxTrackChannels, (int) x["channels"]); t.stem = x["stem"].toString(); t.ringFirst = juce::jmax (0, (int) x["ringFirst"]);
                if (auto* ins = x["inputs"].getArray()) for (auto& i : *ins) t.inputs.push_back ((int) i);
                t.inputs.resize ((size_t) t.channels, -1);
                tracks.push_back (t);
            }
        if (auto* a = v["chunks"].getArray()) for (auto& x : *a) chunks.push_back ({ x["name"].toString(), (juce::int64) x["first"], x.hasProperty ("samples") ? (juce::int64) x["samples"] : -1 });
        if (auto* a = v["gaps"].getArray())   for (auto& x : *a) gaps.push_back ({ (juce::int64) x["first"], (juce::int64) x["samples"], x["time"].toString(), x["why"].toString() });
        if (auto* a = v["takes"].getArray())
            for (auto& x : *a)
            {
                TakeRec k;
                k.number = (int) x["number"]; k.label = x["label"].toString(); k.start = (juce::int64) x["start"]; k.end = x.hasProperty ("end") ? (juce::int64) x["end"] : -1;
                k.recordPoint = (juce::int64) x["recordPoint"]; k.recordedAt = juce::Time::fromISO8601 (x["recordedAt"].toString());
                k.merged = (bool) x["merged"]; k.registered = (bool) x["registered"]; k.mergeError = x["mergeError"].toString();
                if (auto* fs = x["mergedFiles"].getArray()) for (auto& f : *fs) k.mergedFiles.add (f.toString());
                k.mergedSamples = (juce::int64) x["mergedSamples"];
                if (auto* mg = x["mergeGaps"].getArray()) for (auto& g : *mg) k.mergeGaps.push_back ({ (juce::int64) g["first"], (juce::int64) g["samples"], g["time"].toString(), g["why"].toString() });
                takes.push_back (k);
            }
        return true;
    }

    /** Writes the index safely (a complete file or the old one; the previous version kept as .bak). */
    bool save (const juce::File& file, juce::String* error = nullptr) const
    {
        return filesync::replaceTextSafely (file, juce::JSON::toString (toVar()), true, error);
    }
    /** Reads the index, or its .bak when the file itself cannot be read. False when neither can. 'repair' (only for a session whose writer is gone -
        a program carrying it on): a file that cannot be read is put aside as .damaged and the .bak copied into its place. Never on a live drive's
        index, which its writer replaces at every chunk (a read that falls between the two renames would put the old version back). */
    bool load (const juce::File& file, bool repair = false)
    {
        juce::var v;
        auto accept = [] (const juce::var& x) { return x.isObject() && x["format"].toString() == compat::kLiveSessionFormatName; };
        if (repair) filesync::readJsonWithBackup (file, v, accept);
        else
        {
            if (file.existsAsFile() && ! juce::JSON::parse (file.loadFileAsString(), v).failed() && accept (v)) return fromVar (v);
            v = juce::var();
            const auto bak = filesync::backupOf (file);
            if (! bak.existsAsFile() || juce::JSON::parse (bak.loadFileAsString(), v).failed() || ! accept (v)) return false;
        }
        return fromVar (v);
    }
};
} // namespace td::live
