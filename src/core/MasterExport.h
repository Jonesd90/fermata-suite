#pragma once
#include "Bounce.h"
#include "MasterDef.h"
#include "Ddp.h"
#include "MasterRender.h"

namespace td
{
/** One file of an export: which edit, and its name (without the extension). */
struct MasterJobItem
{
    juce::Uuid   editId;
    juce::String baseName;
    MasterTags   tags;               // already merged with the album tags
    int          trackNo = 0;
    juce::String editName, projectName;   // for the listening copy (3.42.0: for a listening copy, the version's name - "The Lion - Edit 2 - More reverb")
    int          editVersion = 0;         // 3.42.0: the edit's version number a listening copy was made from (goes into the page, and comes back with the comments)
    double       editOffsetSeconds = 0.0;   // where the exported file's time 0 sits on the edit's timeline
};

struct MasterJobSpec
{
    enum class Kind { Files, Disc } kind = Kind::Files;
    MasterExportSettings out;        // format, rate, bit depth, levels (for a disc: 44.1 kHz / 16 bit WAV with the disc's level settings)
    std::vector<MasterJobItem> items;
    std::vector<int> clipIndex;      // Disc: for each item, its index in disc.clips
    juce::Uuid   mixerId, sourceId;
    bool         ownMixers = true;   // every piece goes through its own Edit's mixer (mixerId is only the fallback)
    double       tailSeconds = 0.0;
    juce::File   destFolder;         // Files: the audio files go here. Disc: the folder that gets the DDP folder, the zip and the WAV + CUE
    juce::File   workFolder;         // intermediates (deleted at the end)
    DdpDisc      disc;              // Disc only. disc.clips holds every clip; 'items' are the included ones, in order
    bool         formsOnly = false; // Files: make only the listening copies (one .html per piece, with its audio inside), no audio files
    bool         formSingle = false;// the listening copies: all the pieces in ONE .html (named after formTitle) instead of one per piece
    juce::String formTitle;         // the name of that one page (the album title, or the project's name)
    juce::String discName;
    /** 3.26.0, Files only: buses exported with the master, each as its own stereo file per piece (stems) in a "Stems" folder in destFolder - rendered in the
        same pass as the master, at the master's gain (so they keep its balance), in its main format. stemNames: what the files are named after. */
    std::vector<juce::Uuid> stemIds;
    std::vector<juce::String> stemNames;
    /** The answer to the loudness check (a piece that cannot reach the target under the ceiling): 0 = ask (the job pauses and calls onLoudnessCheck),
        1 = retain the peak (those pieces stop at the ceiling, below the target), 2 = ignore (the full gain; they clip), 3 = cancel. */
    int          loudnessDecision = 0;
};

/** What the loudness check found: every piece as measured, and the ones that cannot reach the target without going over the ceiling. */
struct LoudnessCheck
{
    struct Piece
    {
        juce::String name;
        double lufs = -100.0, truePeakDb = -100.0;   // as rendered
        double gainDb = 0.0;                         // the gain the target asks for (per album: the same for all)
        double peakAtTargetDb = -100.0;              // the true peak with that gain
        double excessDb = 0.0;                       // how far over the ceiling that is (> 0 = this piece is a problem: it needs this much limiting / compression)
    };
    std::vector<Piece> pieces;
    double targetLufs = -23.0, ceilingDbTp = -1.0;
    bool perAlbum = false;
    double albumLufs = -100.0;                       // perAlbum: the album measured as one programme
    int problems() const { int n = 0; for (auto& p : pieces) if (p.excessDb > 0.0) ++n; return n; }
    double worstExcessDb() const { double w = 0.0; for (auto& p : pieces) w = juce::jmax (w, p.excessDb); return w; }
    /** The lines of the report. */
    juce::String text() const;
};

/** The gain (dB) each piece gets for a loudness target, and the problems: the heart of the check (pure, tested). */
LoudnessCheck planLoudness (const std::vector<juce::String>& names, const std::vector<double>& lufs, const std::vector<double>& truePeakDb, double albumLufs,
                            double targetLufs, double ceilingDbTp, bool perAlbum);
/** The gains after a decision (1 retain the peak, 2 ignore). */
std::vector<double> loudnessGains (const LoudnessCheck&, int decision);

struct MasterExportResult
{
    juce::String error;              // empty = fine
    juce::StringArray files;         // what was written (Disc: the DDP folder, the zip and the WAV / CUE)
    float peakDb = -100.0f;          // loudest sample written
    bool clipped = false, cancelled = false;
    juce::String report;             // Disc: what the checker found in the finished DDP. Files with a loudness target: every piece's loudness, true peak and gain
    std::vector<juce::int64> frames; // the length of each item's audio at the target rate
    LoudnessCheck loudness;          // what was measured (a loudness target only)
    int stemFiles = 0;               // 3.26.0: how many of 'files' are stems
    juce::StringArray clippedStems;  // stems whose level went over 0 dBFS at the master's gain ("01 - Kyrie - Strings  +0.8 dB")
};

/** Builds the list of files for the Virtual Master (or says why it cannot): included edits, in list order, named and tagged. */
juce::String makeFilesSpec (const Project&, const MasteringDef&, MasterJobSpec& spec, bool listeningCopies = false);     // (3.42.0: listening copies are named after the edit's version: "The Lion - Edit 2 - More reverb")
/** The listening copies on their own (the Listening copy tab): the same pieces as the Virtual Master, at the Virtual Master's levels, with the audio
    inside each page as chosen on that tab (WAV / FLAC / MP3, its own sample rate and bit depth). No audio files are made. */
juce::String makeFormsSpec (const Project&, const MasteringDef&, MasterJobSpec& spec);
/** The same for the DDP: the included clips, in order, as 44.1 kHz / 16 bit files. */
juce::String makeDiscSpec (const Project&, const MasteringDef&, MasterJobSpec& spec);

/** The mixer and output used for rendering (the defaults: processing mixer, its first Ext bus). Returns an error text or "". */
juce::String resolveRenderSource (const Project&, const MasteringDef&, juce::Uuid& mixerId, juce::Uuid& sourceId);

double masterTargetRate (const MasterExportSettings&, double editRate);
juce::String masterFileExtension (MasterFormat);

/** The command that runs LAME (full path, or just "lame" when it is on the PATH); empty if it cannot be found. Looks next to the program first. */
juce::String findLame();

/** Splices a Vorbis-comment block (the tags of a FLAC file) into a finished FLAC file. */
bool addFlacTags (const juce::File& flac, const MasterTags&, int trackNo);

/** Renders, converts, levels and writes everything on a background thread. Create and destroy on the message thread; 'done' is called on the message thread. */
class MasterExportJob : private juce::Thread
{
public:
    /** startNow = false: nothing runs in the background; call execute() yourself (the tests do). */
    MasterExportJob (const Project& live, const MasterJobSpec&, std::function<void (MasterExportResult)> done, bool startNow = true);
    ~MasterExportJob() override;
    float getProgress() const;
    juce::String getStatus() const;
    /** Starts the work in the background (for a job made with startNow = false, once its callbacks are set). */
    void start() { if (! isThreadRunning()) startThread (juce::Thread::Priority::normal); }
    void cancel() noexcept { cancelFlag = true; decide (3); }
    bool isRunning() const { return isThreadRunning(); }
    /** The loudness check: when a piece cannot reach the target under the ceiling and the spec says ask, the job stops here (nothing written yet) and
        calls this on the message thread; it goes on when decide() is called (1 retain the peak, 2 ignore, 3 cancel). */
    std::function<void (const LoudnessCheck&)> onLoudnessCheck;
    void decide (int decision) { decision_.store (decision); decided.signal(); }
    bool isWaitingForDecision() const noexcept { return waiting.load(); }
    /** Does the whole job on the calling thread (and removes the intermediate files). */
    MasterExportResult execute();

private:
    void run() override;
    MasterExportResult executeInner();
    juce::String finalise (const juce::File& src, const juce::File& dest, double rate, float gain, const MasterJobItem& item, float& peakOut, MasterFormat fmt);

    MasterJobSpec spec;
    std::unique_ptr<MasterRenderBatch> batch;       // the renders to export (up to date ones are reused, the others are made first)
    std::function<void (MasterExportResult)> onDone;
    std::atomic<float> sub { 0.0f };
    std::atomic<int> stage { 0 };
    std::atomic<bool> cancelFlag { false };
    mutable juce::CriticalSection lock;
    juce::String status, earlyError;
    juce::WaitableEvent decided; std::atomic<int> decision_ { 0 }; std::atomic<bool> waiting { false };
};

// ----------------------------------------------------------------------------- 3.40.0: the Atmos tab - fermata's own Dolby Atmos master files
/** What the Atmos tab exports: the Virtual Master's pieces (included, in its order, its names + " (Atmos)"), each as one ADM BWF file holding
    the ticked surround buses, every bus as its own bed + objects (AdmWriter.h). Each piece covers exactly the stretch its Virtual Master file
    covers (first audio to last audio plus the tail: the delivery spec wants the Atmos master synced to the stereo one). */
struct AtmosJobSpec
{
    std::vector<MasterJobItem> items;
    std::vector<juce::Uuid> busIds;         // the ticked surround buses, in the mastering settings' order
    std::vector<juce::String> busNames;     // their names (the beds' names in the file)
    std::vector<BusFormat> busFormats;
    juce::Uuid mixerId, sourceId;           // as the Virtual Master renders (the source only decides the master render's staleness; it is not written)
    bool ownMixers = true;
    double tailSeconds = 0.0;
    bool lfeLowPass = true; float lfeHz = 120.0f;
    bool dither = true;
    juce::File destFolder, workFolder;
};
/** Builds the Atmos export from the mastering settings (or says why it cannot). */
juce::String makeAtmosSpec (const Project&, const MasteringDef&, AtmosJobSpec& spec);
/** Things worth telling the user before an Atmos export, none of which stops it: a ticked bus that feeds another ticked bus (its sound is in the
    file twice), the bus the ticked buses go to (its fader, its plug-ins) not being in the file, Spacelab left out of the render while a Composer is
    in the project. One line each; empty when there is nothing to say. */
juce::StringArray atmosExportNotes (const Project&, const MasteringDef&);
struct AtmosExportResult
{
    juce::String error;                     // empty = fine
    juce::StringArray files;                // the files written
    std::vector<juce::int64> frames;        // the length of each, in samples
    double sampleRate = 0.0;
    float peakDb = -100.0f;                 // the loudest sample written (before the 24-bit rounding)
    bool clipped = false, cancelled = false;
    int channels = 0;                       // per file
};
/** Renders the ticked buses (through each piece's own mixer, as the Virtual Master does; a bus at its full width) and writes the ADM BWF files
    on a background thread. Create and destroy on the message thread; 'done' is called on the message thread. */
class AtmosExportJob : private juce::Thread
{
public:
    AtmosExportJob (const Project& live, const AtmosJobSpec&, std::function<void (AtmosExportResult)> done, bool startNow = true);
    ~AtmosExportJob() override;
    float getProgress() const;
    juce::String getStatus() const;
    void start() { if (! isThreadRunning()) startThread (juce::Thread::Priority::normal); }
    void cancel() noexcept { cancelFlag = true; }
    bool isRunning() const { return isThreadRunning(); }
    /** Does the whole job on the calling thread (the tests). */
    AtmosExportResult execute();
    /** The tests: the RIFF size from which a file is written as RF64 (the profile's 4 GB otherwise). */
    juce::int64 rf64At = (juce::int64) 0xFFFFFFFFll - 1024;
private:
    void run() override;
    AtmosJobSpec spec;
    std::unique_ptr<MasterRenderBatch> batch;
    std::function<void (AtmosExportResult)> onDone;
    std::atomic<float> sub { 0.0f };
    std::atomic<int> stage { 0 };
    std::atomic<bool> cancelFlag { false };
    mutable juce::CriticalSection lock;
    juce::String status, earlyError;
};
} // namespace td
