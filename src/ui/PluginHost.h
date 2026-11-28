#pragma once
#include "Uikit.h"

namespace td
{
/** Hosts VST3 plug-ins in mixer insert slots. */
class PluginHost : private juce::Thread
{
public:
    PluginHost();
    ~PluginHost() override;

    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    bool isScanning() const noexcept { return scanning.load(); }
    /** False if a scan is already running (or the scan thread could not be started). */
    bool startScan (std::function<void()> whenFinished = {});
    juce::String scanStatus() const { const juce::ScopedLock sl (statusLock); return status; }

    /** Message thread. Returns null (with an error message) if the plug-in can't be loaded. */
    std::unique_ptr<InsertProcessor> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);
    /** The factory a project (or a render copy of one) uses to bring its saved plug-ins back. A plug-in that cannot be started is logged, with the
        reason, and the reason is kept in lastFactoryError (message thread) for whoever called the factory to tell the person. */
    InsertFactory makeFactory (std::function<double()> sampleRate, std::function<int()> blockSize);
    /** Why the last plug-in the factory was asked for could not be started (empty: it could). Message thread. */
    juce::String lastFactoryError;

    /** 3.48.0: the normalised value (0 .. 1) a plug-in reads from text typed in its own words ("-3.0 dB", "350 Hz"; a plug-in that reads no text takes
        a plain number as 0 .. 1), for a value typed onto an automation point. Below 0: not one of this host's plug-ins, or it has no such parameter.
        Message thread. */
    float valueOfText (InsertProcessor*, int param, const juce::String& text);
    void showEditor (InsertProcessor*);
    void closeEditorsFor (InsertProcessor*);
    void closeAllEditors();
    /** Set by the application: makes a plug-in window belong to the main window (so it cannot go behind it). Returns false if that is not possible here. */
    std::function<bool (juce::Component&)> adoptWindow;
    /** Every open plug-in window (to keep them in front of the main window). */
    void forEachEditor (const std::function<void (juce::Component&)>& fn);

    // ---- 3.53.0: the keys along the top of a plug-in's window - Bypass, Copy, Paste - and what the plug-in is told about where it sits ----
    /** (Set by the application.) The insert slot a plug-in sits in, or null: the window's Bypass key switches that slot's bypass. */
    std::function<InsertSlot* (InsertProcessor*)> slotOf;
    /** (Set by the application.) A slot's bypass was switched, or settings were pasted: the project has changed, the mixers' delays are looked at again. */
    std::function<void()> slotChanged;
    /** Copy: keeps this plug-in's settings (all of them, as a project saves them) for Paste. Kept until the next Copy or until the program closes. */
    void copySettings (InsertProcessor*);
    bool hasCopied() const noexcept               { return clipState.isNotEmpty(); }
    juce::String copiedName() const               { return clipName; }
    /** Are the copied settings from the same plug-in as this one (the same file and kind: an EQ's settings mean nothing to a reverb)? */
    bool canPaste (InsertProcessor*) const;
    /** Paste: gives the copied settings to this plug-in, which is running (as choosing one of its own presets does). False when they are not its kind. */
    bool pasteSettings (InsertProcessor*);
    /** Paste into an EMPTY slot: a new instance of the copied plug-in with the copied settings. Null (with the reason) when it cannot be started. */
    std::unique_ptr<InsertProcessor> createFromCopied (double sampleRate, int blockSize, juce::String& error);
    /** 3.75.0 (the Spectrogram Editing window: one plug-in on several tracks): a new instance of the plug-in 'from' is, with all of its settings as they
        are now. Nothing copied with Copy is touched. Null (with the reason) when it is not one of this host's plug-ins or cannot be started. */
    std::unique_ptr<InsertProcessor> duplicate (InsertProcessor* from, double sampleRate, int blockSize, juce::String& error);
    /** 3.75.2: is this plug-in's own window open (showEditor's)? */
    bool editorOpen (InsertProcessor*) const;
    /** 3.75.2 (the Spectrogram Editing window's Link the settings): every plug-in of 'to' is given all of 'from's settings as they are now - its
        whole state, as Paste settings gives one (so it also works on a plug-in whose mixer is resting, which a parameter set from outside does not
        reach until the mixer runs). Only instances of the same plug-in are written; how many were. Nothing copied with Copy is touched. */
    int copyState (InsertProcessor* from, const std::vector<InsertProcessor*>& to);
    /** 3.75.0: what tells one plug-in from another - its file and kind, as canPaste compares them ("" for none, or one that is not this host's). */
    juce::String identityOf (InsertProcessor*) const;
    /** Tells a plug-in the name (and colour, ARGB; 0 = none) of the channel it sits on - "Piano - The Egg Man EDIT": a plug-in that lists its
        instances across the project (FabFilter Pro-Q's instance list) shows them by this name instead of 1, 2, 3. Only passed on when it has
        changed since the last call. Message thread. */
    void setTrackName (InsertProcessor*, const juce::String& name, juce::uint32 argb);
    /** The name last told (for the window's own top row). */
    juce::String trackNameOf (InsertProcessor*) const;

    juce::PopupMenu buildPluginMenu (int baseId, std::vector<juce::PluginDescription>& outDescs) const;

    // ---- the plug-in manager (Audio settings > Plug-ins...). It works on plug-in FILES: a file that holds several plug-ins (a shell) goes with all of them.
    /** One plug-in file as the manager lists it. */
    struct FileEntry
    {
        enum class State { found, ignored, crashed };
        juce::String file, names;                 // the file (or identifier) and the plug-ins in it ("Name (Maker)", ...)
        State state = State::found;
    };
    std::vector<FileEntry> listFiles() const;                 // (not while scanning: JUCE's lists are not locked against the scan thread)
    /** A file the user chose to ignore: it is never scanned, never loaded with a project (the slot keeps it, with its settings, unloaded) and never
        offered. A copy already loaded keeps working (bounces, copies) until the project is opened again. */
    bool isIgnored (const juce::String& fileOrIdentifier) const { return ignored.contains (fileOrIdentifier, true); }
    /** Message thread, not while scanning (returns false then). Ignoring takes the file's plug-ins out of the lists at once; using it again scans the file
        again (then 'done' is called, on the message thread). */
    bool setIgnored (const juce::String& fileOrIdentifier, bool ignore, std::function<void()> done = {});
    /** A file left out because it crashed Fermata while it was being scanned: taken off that list and scanned again now ('done' afterwards). If it crashes
        again, the next scan leaves it out again. */
    bool tryAgain (const juce::String& fileOrIdentifier, std::function<void()> done = {});

private:
    void run() override;
    void saveLists();
    /** Takes 'file' out of the scan's crash note (plugin-scan-in-progress.txt), which would otherwise leave it out again before it is tried. */
    void forgetCrashNote (const juce::String& file);
    juce::File pedalFile() const { return listFile.getSiblingFile ("plugin-scan-in-progress.txt"); }
    class EditorWindow;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::atomic<bool> scanning { false };
    std::function<void()> finished;
    juce::StringArray onlyFiles;                  // a scan of just these files (try again / use again), set before the thread starts
    juce::StringArray ignored, ignoredNames;      // the ignored files and the names of the plug-ins that were in them (message thread)
    mutable juce::CriticalSection statusLock;
    juce::String status;
    juce::File listFile, ignoredFile;
    juce::String clipIdentifier, clipDescription, clipState, clipName;        // the copied settings (message thread)
    std::vector<std::unique_ptr<EditorWindow>> editors;
};
} // namespace td
