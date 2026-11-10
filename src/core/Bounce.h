#pragma once
#include "AudioEngine.h"

namespace td
{
/** One file to make: the part [start, end) of the playback timeline, in samples. */
struct BounceItem
{
    juce::String name;
    juce::int64  start = 0, end = 0;
    std::vector<PlaySegment> segments;       // the audio this piece comes from; empty = BounceSettings::segments (different takes need their own)
    juce::Uuid mixerId = juce::Uuid::null();          // the mixer this piece goes through (null = BounceSettings::mixerId): an Edit's own mixer, so each piece keeps its own mix
    juce::Uuid automationEdit = juce::Uuid::null();   // the edit whose automation is applied to this item (null = BounceSettings::automationEdit)
    juce::String alignOwner;                          // whose spot-mic alignment it plays with (Project::alignOwnerFor; empty = BounceSettings::alignOwner)
};

/** 3.52.0: a frozen version of an edit to render in place of the edit as it is now (a listening copy of "Edit 2" made when the work is at Edit 4).
    The render goes through a private copy of the project (see Bouncer), so the version is put into THAT copy and the project itself is never
    touched: the copy's edit becomes 'edit' (its pieces, fades, levels, volume areas and automation as frozen), and the copy's mixer 'mixerId'
    is given the settings and plug-ins of 'mixer' (Project::mixSnapshot with inserts, as the version kept it: EditVersion::mixer; void = the
    mixer stays as it is now). The items of that edit must be given the version's own segments (segmentsForEdit (project, *edit)). */
struct BounceEditOverride
{
    std::shared_ptr<const EditDef> edit;
    juce::var  mixer;
    juce::Uuid mixerId = juce::Uuid::null();
};

/** What to bounce out and how. */
struct BounceSettings
{
    std::vector<BounceEditOverride> overrides;   // 3.52.0: frozen versions rendered in place of their edits (see BounceEditOverride)
    std::vector<PlaySegment> segments;       // the audio on the timeline (a take, or an edit with its crossfades) used by every item that has none of its own
    double sampleRate = 48000.0;
    std::vector<BounceItem> items;           // one file each
    double leadSeconds = 0.0, tailSeconds = 0.0;     // extra time before / after each item (the mixer keeps running, so plug-ins settle and tails ring out)
    juce::Uuid automationEdit = juce::Uuid::null();   // the edit whose automation is applied while bouncing (null = none)
    juce::String alignOwner;                 // whose spot-mic alignment the items play with (the piece's: Project::alignOwnerFor)
    juce::Uuid mixerId = juce::Uuid::null();          // the mixer whose settings are used (normally the processing mixer)
    std::vector<juce::Uuid> sources;         // the post-fader signal of these audio tracks / Int Buses / Ext Buses. Every one chosen is written to its own stereo file.
    std::vector<juce::Uuid> sourcesPre;      // the same, taken BEFORE the fader (after inserts and pan, not affected by mute / solo): files are named "... - <name> (before fader)"
    bool addOutputName = true;              // file names end with " - <output name>" (always done when there is more than one output)
    bool floatFiles = false;                 // write 32-bit float files (no clipping, no rounding): used for the intermediate files of the Mastering window
    bool normalise = false;
    float peakDb = -0.1f;                    // the loudest sample ends up at this level (dBFS)
    bool normaliseTogether = false;          // false: every file is normalised on its own. true: one gain for all the files of a piece (the loudest one reaches peakDb), so stems keep their balance
    juce::File folder;
};

struct BounceResult
{
    juce::String error;                      // empty = fine
    juce::StringArray files;                 // what was written
    float peakDb = -100.0f;                  // loudest sample of all that was written
    bool cancelled = false;
    bool clipped = false;                    // went over 0 dBFS (only possible without normalising)
};

/** Renders audio through a private copy of the project (so the live mixers, their plug-ins and the audio device are never disturbed),
    faster than real time, into 24-bit WAV files. */
class Bouncer
{
public:
    /** Message thread: copies the project (re-creating the plug-ins) and prepares the offline engine. */
    Bouncer (const Project& live, const BounceSettings&);
    ~Bouncer();
    juce::String getPrepareError() const { return prepareError; }
    /** Any thread; blocks until done. progress 0..1; set *cancel to stop early. */
    BounceResult render (std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

private:
    juce::File uniqueFile (const juce::String& name) const;
    juce::String fileName (const BounceItem&, const juce::String& outputName, bool several) const;
    BounceSettings settings;
    std::unique_ptr<Project> shadow;
    std::unique_ptr<AudioEngine> engine;
    juce::String prepareError;
    static constexpr int kBlock = 512;
};

/** The same, on a background thread. Create and destroy on the message thread; 'done' is called on the message thread. */
class BounceJob : private juce::Thread
{
public:
    BounceJob (const Project& live, const BounceSettings&, std::function<void (BounceResult)> done);
    ~BounceJob() override;
    float getProgress() const noexcept { return progress.load(); }
    void cancel() noexcept { cancelFlag = true; }
    bool isRunning() const { return isThreadRunning(); }
private:
    void run() override;
    std::unique_ptr<Bouncer> bouncer;
    std::function<void (BounceResult)> onDone;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelFlag { false };
    juce::String earlyError;
};
} // namespace td
