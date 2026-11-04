#pragma once
/** fermata:Live 0.2.0: the merge. After Stop, a take's span of the timeline is read from one drive's chunks and written, one track at a time, as
    ordinary take files - named and tagged exactly as fermata records them ("005 - First half - Main pair.wav", 24-bit BWF with the time of day),
    so fermata opens the take as its own. Each merged file is then read back and compared, sample for sample, with the chunks it came from; only a
    file that passes is given its name (it is written as "<name>.merging.wav" until then), and only chunks of merged takes are ever deleted (see
    DriveWriter::deleteOldChunks).

    A stretch of the take no chunk covers (the drive was dropped and taken on again, or it stopped before the take ended) becomes silence with a
    gap in the result; a gap the drive noted itself (its disk stalled) is already silence in its chunk and is reported from the index.

    Interruptible: between pieces it asks 'paused' (a take is recording: wait) and 'cancelled' (stop now). A merge that is run again goes on from
    its last completed track: the files already merged are given in TakeRec::mergedFiles (one per track, "" = not yet), and saveProgress is
    called with the take as each track completes, so the caller can keep that in the index. */
#include "SessionIndex.h"
#include <functional>

namespace td::live
{
struct MergeResult
{
    bool ok = false;
    juce::String error;                 // why it stopped ("Cancelled", or what went wrong)
    juce::StringArray files;            // the merged files, one per track (full paths)
    juce::int64 lengthSamples = 0;      // the take's length
    std::vector<GapRec> gaps;           // the gaps inside the take (timeline positions)
    juce::int64 lostSamples = 0;        // their total
    int chunksUsed = 0;
};

class Merge
{
public:
    struct Job
    {
        juce::File   sessionFolder;     // the drive's session folder (chunks and session.json)
        int          takeNumber = 0;
        juce::File   outputFolder;      // where the take files go: <project>/Recorded Media/<window> (or the backup drive's mirror of it)
        juce::String programName;       // written into the files
        juce::String projectName;
        bool         allowOpenChunks = false;   // ONLY for a drive whose writer is gone (dropped, or the session over): the take is merged from what is there, a chunk with no
                                                // length in the index measured, missing stretches as holes. False (a live drive): refused until the drive has written the take's end
        const SessionIndex* index = nullptr;    // the index to merge from as the recorder holds it now (the live session); nullptr: read session.json from the folder (after a stop)
        const TakeRec* take = nullptr;          // the take's span as the recorder knows it (a dropped drive's own index may not have its end); nullptr: the index's record
    };
    using Flag = std::function<bool()>;
    using Progress = std::function<void (double fraction, const juce::String& what)>;
    using Log = std::function<void (const juce::String&)>;
    using SaveProgress = std::function<void (const TakeRec&)>;

    /** Runs the whole merge on the calling thread (a background one). */
    static MergeResult run (const Job&, Flag paused, Flag cancelled, Progress progress, Log log, SaveProgress saveProgress);

    /** The frames in a chunk file; a header the recorder never finished (it was cut off) is mended first (WavRepair). -1 when the file is unreadable. */
    static juce::int64 chunkLength (const juce::File&);

    /** The take's file name for a track, as fermata names its recordings ("005 - First half - Main pair.wav"). */
    static juce::String fileNameFor (const TakeRec&, const juce::String& windowName, const juce::String& trackName);
    static juce::String labelOf (const TakeRec& t, const juce::String& windowName) { return t.label.trim().isNotEmpty() ? t.label.trim() : windowName.trim().isNotEmpty() ? windowName.trim() : juce::String ("Take"); }

    /** The pieces of [start, end) in order: chunks (clipped to the span) and the holes between them. For the merge and its check, and the Fixer's reader. */
    struct Piece { bool hole = false; const ChunkRec* chunk = nullptr; juce::int64 first = 0, samples = 0; juce::int64 inChunk = 0; };
    static std::vector<Piece> pieces (const SessionIndex&, juce::int64 start, juce::int64 end, const std::function<juce::int64 (const ChunkRec&)>& lengthOf);

    /** Reads 'merged' back and compares it, sample for sample, with the pieces it was made from. "" when identical, else what differs. */
    static juce::String verifyFile (const juce::File& merged, const juce::File& sessionFolder, const TrackSpec& track, const std::vector<Piece>& parts, juce::int64 lengthSamples, Flag paused, Flag cancelled);
};
} // namespace td::live
