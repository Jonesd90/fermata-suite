#pragma once
/** fermata:Live 0.3.0: the Fixer station ("fermata-Live.exe --fixer"): a program of its own, so it can be closed (or crash) without touching the
    recording. It finds the recorder on the control port (it never starts one), reads the project the session records into (never writes it: the
    Primary's window owns the project), plays the session from the drive's chunks through its own mixer on its own output pair (through the
    recorder's device), follows the Primary a few seconds behind the audio on the disk, shows the Primary's flags at once, sends replies, and
    makes fix versions with ReTouch (new files; the recording is never changed) that the Primary's window puts into the project. */
#include "RecorderLink.h"
#include "StationModel.h"
#include "TakeLayout.h"
#include "Player.h"
#include "LiveMixer.h"
#include "../core/Project.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace td::live
{
class FixerWindow;

class FixerApp final : public juce::ChangeBroadcaster, private juce::Timer, private juce::ChangeListener
{
public:
    FixerApp (const juce::String& version, int port);
    ~FixerApp() override;
    std::function<void()> onQuit;
    /** 0.3.1: the Fixer's own settings and mixer saved (the first step of closing, before the closing safety net starts; again as it goes). */
    void saveSettings();

    const juce::String version;
    const int port;
    RecorderLink link;
    StationModel model;
    Project project;                                         // a copy, read again when the Primary saves; never saved here
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Player> player;

    bool hasProject() const noexcept                         { return project.projectFile != juce::File(); }
    juce::String pieceName() const                           { return model.sessionOn ? model.windowName : shownPiece; }
    TakeWindowDef* pieceWindow();
    std::shared_ptr<const TakeLayout> layout() const         { return currentLayout; }

    // ---- following the Primary
    bool follow = true;                                      // the playhead stays 'delaySeconds' behind what is on the disk, and cannot be moved
    double delaySeconds = 3.0;
    void setFollow (bool on);
    /** The end of the audio on the disk, on the window's timeline (-1: no take is recording). */
    double writtenEndSeconds() const;
    /** The live end (what the Primary hears now), on the window's timeline (-1: no take is recording). */
    double liveEndSeconds() const;

    // ---- playback (the Fixer's own)
    void play (double fromSeconds);
    void stop();
    bool isPlaying() const                                   { return player != nullptr && player->isPlaying(); }
    double playheadSeconds() const;
    double cursorSeconds = 0.0;
    void setAudition (std::shared_ptr<const Player::Audition> a) { if (player) player->setAudition (std::move (a)); }

    // ---- the Fixer's own mixer
    struct MixTrack { float gainDb = 0.0f, pan = 0.0f, panL = -1.0f, panR = 1.0f; bool mute = false, solo = false; };
    std::map<juce::String, MixTrack> mix;                    // by track id
    float masterDb = 0.0f;
    int outPair = 2;                                         // the first of the pair it plays to (0-based): outputs 3-4 unless chosen
    /** The pair it really plays to: outPair, or outputs 1-2 when the device has no such pair (a device with 2 outputs: the Fixer is heard with
        the Primary there, rather than not at all). -1: none chosen. */
    bool pairNoteShown = false;
    int playPair() const
    {
        const int n = model.outputNames.size();
        if (outPair < 0 || n < 2) return outPair;
        return outPair + 1 < n ? outPair : 0;
    }
    void mixChanged();
    Player::Gains gains() const;

    // ---- replies and fix versions
    void reply (const juce::String& flagId, const juce::String& text);
    /** Announces a fix version to the recorder (which keeps it with the session and hands it to the Primary's window for the project). */
    void announceFix (const juce::var& fix);
    juce::String lastMessage;                                // (the status line)
    void say (const juce::String& t)                         { lastMessage = t; juce::Logger::writeToLog ("[Fixer] " + t); sendChangeMessage(); }

private:
    void onEvent (const juce::var&);
    void loadProjectFor (const juce::File& projectFile);
    void checkProjectFile();
    void rebuildLayout();
    void loadMix();
    void saveMix();
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    std::unique_ptr<FixerWindow> window;
    std::shared_ptr<const TakeLayout> currentLayout;
    juce::File watchedProject;
    juce::int64 watchedTime = 0;
    juce::uint32 lastProjectCheck = 0, lastFollowFix = 0;
    juce::String shownPiece;
    bool layoutDirty = true;
};

/** The Fixer's mixer, for the shared mixer view. */
class FixerMixModel final : public MixModel
{
public:
    explicit FixerMixModel (FixerApp& a) : app (a) {}
    std::vector<MixStrip> strips() const override;
    void setGain (const juce::Uuid&, float) override;
    void setPan (const juce::Uuid&, int which, float) override;
    void setMute (const juce::Uuid&, bool) override;
    void setSolo (const juce::Uuid&, bool) override;
    float masterDb() const override                          { return app.masterDb; }
    void setMasterDb (float db) override                     { app.masterDb = db; app.mixChanged(); }
    int outputPair() const override                          { return app.playPair(); }
    void setOutputPair (int first) override                  { app.outPair = first; app.mixChanged(); }
    juce::StringArray outputNames() const override           { return app.model.outputNames; }
    float meter (const juce::Uuid& id) const override        { return app.player != nullptr ? app.player->takePeak (id) : 0.0f; }   // (what the Fixer plays)
    float outputMeter (int ch) const override                { const int o = app.playPair() < 0 ? -1 : app.playPair() + ch; return o >= 0 && o < (int) app.model.outPeaks.size() ? app.model.outPeaks[(size_t) o] : 0.0f; }
    juce::String title() const override                      { return "The Fixer's mixer: only the Fixer hears it (its own output pair, through the recorder's device). Nothing here reaches the Primary or the recording."; }
private:
    FixerApp& app;
};
} // namespace td::live
