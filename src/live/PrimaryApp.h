#pragma once
/** fermata:Live 0.3.0: the Primary station - everything behind its windows. It owns the project (the only process that writes it), talks to the
    recorder (RecorderLink, StationModel), keeps the settings, sends the live monitoring mix and the talkback, plays takes back (Player), pins the
    flags, controls the preamps (the shared Ravenna code, as fermata does), and puts what the recorder has done into the project: every merged
    take (TakeRegister), the flags of its takes (markers) and the Fixer's fix versions.

    Message thread only. The windows hold a reference to it and listen to it (a ChangeBroadcaster: anything changed). */
#include "RecorderLink.h"
#include "StationModel.h"
#include "TakeLayout.h"
#include "Player.h"
#include "../core/Project.h"
#include "../core/Ravenna.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <set>

namespace td::live
{
class PrimaryApp final : public juce::ChangeBroadcaster, private juce::Timer, private juce::ChangeListener
{
public:
    PrimaryApp (const juce::String& version, int port);
    ~PrimaryApp() override;

    const juce::String version;
    const int port;
    RecorderLink link;
    StationModel model;
    Project project;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Player> player;

    // ---- the log (shown in the main window's status line and the log window)
    juce::StringArray recentLog;
    juce::uint32 logLines = 0;                                  // (goes up with every line: the log window refreshes on it - the list stops at 400 lines)
    void addLog (const juce::String&);
    juce::String lastWarning;                                   // a problem the user must see (a drive dropped, a refusal): shown until the next
    juce::uint32 lastWarningMs = 0;

    // ---- the project
    bool hasProject() const noexcept                            { return project.projectFile != juce::File(); }
    bool newProject (const juce::File& projectFile, double rate, juce::String& error);
    bool openProject (const juce::File& projectFile, juce::String& error);
    /** Saves now (the windows' changes are saved by themselves a moment later: markDirty). */
    bool saveNow (juce::String* error = nullptr);
    void markDirty()                                            { dirty = true; project.markDirty(); sendChangeMessage(); }
    juce::File recentProject() const                            { return juce::File (settings->getValue ("lastProject")); }

    // ---- the piece: the take window the Primary records into
    juce::String pieceName() const;
    void setPieceName (const juce::String& name);               // refused while a session runs (the chunks belong to the piece)
    TakeWindowDef* pieceWindow();
    /** The layout of the piece's take window as the stations show it (rebuilt as things change). */
    std::shared_ptr<const TakeLayout> layout() const            { return currentLayout; }
    void rebuildLayout();

    // ---- the device (the recorder opens it)
    juce::var devicesInfo;                                      // the last "devices" event: the driver types and their devices
    void askForDevices();
    void openDevice (const juce::String& type, const juce::String& name, double rate, int buffer);
    void closeDevice();
    juce::String deviceSummary() const;

    // ---- the session and the takes
    juce::StringArray backupFolders() const;
    void setBackupFolders (const juce::StringArray&);
    int preTakeSeconds() const                                  { return settings->getIntValue ("preTakeSeconds", 120); }
    void setPreTakeSeconds (int s)                              { settings->setValue ("preTakeSeconds", juce::jlimit (0, 300, s)); }
    int bufferSeconds() const                                   { return settings->getIntValue ("bufferSeconds", 60); }
    void setBufferSeconds (int s)                               { settings->setValue ("bufferSeconds", juce::jlimit (30, 60, s)); }
    /** "" when a session can start, else why not. */
    juce::String whyNoSession() const;
    void startSession (bool resume);
    void endSession();
    /** 0.3.0: the newest session of the piece's take window on the primary drive, when it was cut off - its index still says "writing" (fermata:Live,
        or the PC, stopped while it was on) - and has a take in it that is not in the project yet (one recording, or not merged / put in). Starting
        a session carries such a one on (so nothing recorded in it is left out of the project), and the window offers to carry it on as soon as it
        starts. */
    struct CutOffSession { juce::File folder; juce::Time lastWritten; int openTake = 0, unmergedTakes = 0; double rate = 0.0; };
    bool findCutOffSession (CutOffSession&);
    /** Record: starts the session first when there is none (then the take starts as soon as it is on). */
    void record();
    void stopTake();
    int nextTakeNumber() const;
    bool isRecording() const noexcept                           { return model.recordingTake != 0; }
    juce::String takeLabel;                                     // the label for the next take ("" = the piece's name)

    // ---- ARM: which tracks the session records (all, unless taken out)
    bool isArmed (const juce::Uuid& track) const                { return disarmed.count (track.toString()) == 0; }
    void setArmed (const juce::Uuid& track, bool on);
    void armAll (bool on);
    bool armingLocked() const                                   { return model.sessionOn; }

    // ---- flags (the Primary's: Enter pins one where the music is now, and opens its note)
    /** Returns the new flag's id ("" when there is no session). 'sample': where on the session's timeline (0.5.0: the flag point, or the playhead
        while playing back); < 0 = now (the moment the Primary heard). */
    juce::String pinFlag (juce::int64 sample = -1);
    void setFlagNote (const juce::String& id, const juce::String& note);
    void removeFlag (const juce::String& id);

    // ---- 0.5.0: notes (N) - the Primary's own; see StationNotes.h
    /** Pins a note at 'seconds' on the timeline shown ('now': where the music is now, in the session). Returns its id, or "" and why not. */
    juce::String pinNote (double seconds, bool now, juce::String& whyNot);
    void setNoteText (const juce::String& id, const juce::String& text);
    void removeNote (const juce::String& id);

    // ---- 0.5.0: the station's name ("by David" on its flags and notes), the time of day, the coloured waveforms
    juce::String stationName() const                            { auto n = settings->getValue ("stationName").trim(); return n.isNotEmpty() ? n : juce::String ("Primary"); }
    void setStationName (const juce::String& n)                 { settings->setValue ("stationName", n.trim().substring (0, 40)); settings->saveIfNeeded(); sendChangeMessage(); }
    bool timeOfDay() const                                      { return settings->getBoolValue ("timeOfDay", false); }
    void setTimeOfDay (bool on)                                 { settings->setValue ("timeOfDay", on); rebuildLayout(); sendChangeMessage(); }
    bool colourWaves() const                                    { return settings->getBoolValue ("colourWaves", false); }
    void setColourWaves (bool on)                               { settings->setValue ("colourWaves", on); sendChangeMessage(); }
    /** 0.5.0: the recording's report (a PDF in the project's Reports folder): every take of the piece with all the notes and flags at their places,
        who wrote them, the replies and the fix versions. False and why when it could not be written. */
    bool writeReport (juce::File& written, juce::String& error);

    // ---- monitoring: the processing mixer is what the Primary hears live, and what Bounce out uses
    MixerState* monitorMixer() const                            { return project.mixers.empty() ? nullptr : project.mixers.front().get(); }
    /** The processing mixer's main output pair (first output, 0-based; -1: none). */
    int monitorPair() const;
    void setMonitorPair (int first);
    void mixChanged();                                          // the mixer was changed: the recorder gets the new routes, the player its gains
    void setTalkback (bool open);
    bool talkbackOpen() const noexcept                          { return model.talkOpen; }
    void setTalkInput (int input, bool on);
    void setTalkOutput (int output, bool on);

    // ---- playback (the Primary's own: takes of the piece through the processing mixer; the live monitoring is muted while it plays)
    void play (double fromSeconds);
    void stopPlayback();
    bool isPlaying() const                                      { return player != nullptr && player->isPlaying(); }
    double playheadSeconds() const;
    double cursorSeconds = 0.0;                                 // where Play starts (the last click)

    // ---- preamps (Merging's devices, as fermata controls them)
    void setPreampDevices (const std::vector<PreampDeviceCfg>&);
    void findPreampsWithAneman (std::function<void (const juce::String& result)> done);
    RavennaPreampDriver* preampDriver() const                   { return dynamic_cast<RavennaPreampDriver*> (project.preampDriver.get()); }

    // ---- the Fixer
    void startFixer();

    // ---- closing (0.3.1: closing lets go of everything unless the user says otherwise - see LiveApplication::quitPrimary)
    /** What closing would end, or have to wait for (as far as the recorder has said): the pre-take, and the takes not yet merged. */
    struct StillGoing { bool session = false; int takesToMerge = 0; };
    StillGoing stillGoing() const;
    /** Close everything: playback stops, the project is saved, and the recorder is told to end the session, let go of the audio device at once and
        close as soon as its merges have finished. True when fermata:Live should wait for it (a take still to be merged: it is put into the project
        when its merge is done); onRecorderClosed is then called once the recorder has closed. */
    bool closeEverything();
    std::function<void()> onRecorderClosed;
    /** Close the window only (a take recording, or the user keeps the pre-take running): the recorder is left as it is, and found again next time. */
    void closeWindowOnly();
    /** The last save as the program closes (anything the closing windows changed). */
    void finalSave();

private:
    void onEvent (const juce::var&);
    void onConnection (bool);
    void registerTake (const juce::var& mergeDone);
    void askForUnregistered();
    void syncNotesIntoProject();                                // the session's flags and fix versions into the project
    /** Flags and fix versions into the take window 'windowName' (each flag once its take is in the project) - the running session's, or those a
        merge event carries (a take merged after the session ended: its notes would otherwise never get in). */
    void syncNotes (const juce::String& windowName, double sessionRate, const std::vector<FlagRec>& flags, const std::vector<juce::var>& fixes,
                    const std::vector<StationTake>& takes);
    void sendMix();
    Player::Gains playbackGains() const;
    void loadArming();
    void saveArming();
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void send (const juce::var& c)                              { link.send (c); }

    std::shared_ptr<const TakeLayout> currentLayout;
    std::set<juce::String> sentNotes;                           // (0.5.0: notes sent to the recorder - their text goes there even before its answer is back)
    std::set<juce::String> disarmed;
    std::set<int> registeredTakes;
    juce::var lastOpen, lastSession;
    bool wasConnected = false, restoreAfterStatus = false, recordWhenSessionStarts = false, dirty = false, layoutDirty = true;
    bool cutOffAsked = false;                                   // (the offer to carry on a cut-off session: once per project opened)
    bool unregisteredScanned = false;                           // (the ended sessions looked through for takes not in the project: once per project opened)
    bool carryingOnCutOff = false;                              // (startSession carries a cut-off session on instead of a new one: if that fails, a new one starts)
    juce::File cutOffFolder, ignoreCutOff;
    juce::uint32 sessionStarting = 0;                           // (when the last "session" was sent, until its answer: 0 = none on its way)
    juce::uint32 lastFixerStartMs = 0;
    void offerToCarryOn();
    juce::int64 lastAskMs = 0, lastSaveMs = 0;
    juce::uint32 lastMixMs = 0;
    bool mutedForPlayback = false;
    bool closingDown = false;                                   // (closing: nothing is opened or put back on the recorder any more)
    bool closingWait = false;                                   // (Close everything: waiting for the recorder to finish its merges and close)
    juce::uint32 closingMergeSeenMs = 0;                        // (the last moment the recorder said a merge was going)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
} // namespace td::live
