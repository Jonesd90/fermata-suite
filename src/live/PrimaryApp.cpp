#include "PrimaryApp.h"
#include "TakeRegister.h"
#include "StationNotes.h"
#include "LiveReport.h"
#include "../ui/WinOwner.h"
#include "../ui/ClosingLog.h"
#include "LiveRecorder.h"
#include "Merge.h"

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static juce::var cmd (const juce::String& name) { auto c = obj(); put (c, "cmd", name); return c; }
static float dbToGain (float db) { return db <= -144.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
/** fermata's pan law: constant power, -3 dB in the middle. */
static void panGains (float pan, float& l, float& r)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    l = std::cos (a); r = std::sin (a);
}

PrimaryApp::PrimaryApp (const juce::String& ver, int p) : version (ver), port (p), link (p)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "fermata-Live"; o.filenameSuffix = ".settings"; o.folderName = "Fermata"; o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (o);
    model.send = [this] (const juce::var& c) { link.send (c); };
    model.onEvent = [this] (const juce::var& e) { onEvent (e); };
    model.addChangeListener (this);
    project.addChangeListener (this);
    project.newStripsToMain = true;                             // (as fermata: a new track's strip goes to the main output - heard, played back, bounced)
    link.onEvent = [this] (const juce::var& e) { model.handle (e); };
    link.onConnection = [this] (bool on) { onConnection (on); };
    player = std::make_unique<Player> (port + 1, "Primary", 0);
    link.start (true);
    // the preamp devices the user set up before (a project of its own may say otherwise)
    auto v = juce::JSON::parse (settings->getValue ("preampDevices"));
    if (auto* a = v.getArray()) for (auto& d : *a) project.preampDevices.push_back (PreampDeviceCfg::fromVar (d));
    startTimer (50);
}

PrimaryApp::~PrimaryApp()
{
    // (0.3.1: each step into the log with its time - fermata's closing log, written only while the program closes)
    *alive = false;
    stopTimer();
    if (dirty) { saveNow(); closing::step ("the project saved again (the windows had changed it)"); }
    model.removeChangeListener (this);
    project.removeChangeListener (this);
    player.reset();
    closing::step ("playback stopped (its stream from the recorder closed)");
    project.preampDriver = std::make_shared<NullPreampDriver>();
    closing::step ("the preamp connections closed");
    link.stop();
    closing::step ("the connection to the recorder closed");
    settings->saveIfNeeded();
}

void PrimaryApp::addLog (const juce::String& text)
{
    juce::Logger::writeToLog (text);
    recentLog.add (juce::Time::getCurrentTime().formatted ("%H:%M:%S  ") + text);
    while (recentLog.size() > 400) recentLog.remove (0);
    ++logLines;
    sendChangeMessage();
}

// ---- the project ------------------------------------------------------------------------------------------------------------------------------
bool PrimaryApp::newProject (const juce::File& file, double rate, juce::String& error)
{
    if (model.sessionOn) { error = "End the session first (the recording belongs to the project that is open)."; return false; }
    if (dirty) saveNow();
    const auto folder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension());
    if (! folder.createDirectory()) { error = "Cannot make the folder " + folder.getFullPathName(); return false; }
    project.createDefaultDesign();
    project.addDefaultMainBus();
    project.name = file.getFileNameWithoutExtension();
    project.projectFile = folder.getChildFile (file.getFileNameWithoutExtension() + ".fermata");
    project.sampleRate = rate;
    if (project.takeWindows.size() == 1 && project.takeWindows.front()->name == "Piece 1") project.takeWindows.front()->name = "Concert";
    if (! model.inputNames.isEmpty()) project.syncWithDevice (model.inputNames, model.outputNames);
    auto v = juce::JSON::parse (settings->getValue ("preampDevices"));
    if (auto* a = v.getArray()) for (auto& d : *a) project.preampDevices.push_back (PreampDeviceCfg::fromVar (d));
    project.createFolders();
    if (! project.save (error)) return false;
    settings->setValue ("lastProject", project.projectFile.getFullPathName());
    settings->setValue ("piece", project.takeWindows.empty() ? juce::String ("Concert") : project.takeWindows.front()->name);
    setPreampDevices (project.preampDevices);
    loadArming();
    registeredTakes.clear();
    addLog ("New project " + project.name + " in " + project.projectFolder().getFullPathName() + " at " + juce::String (rate, 0) + " Hz");
    layoutDirty = true; mixChanged();
    sendChangeMessage();
    return true;
}

bool PrimaryApp::openProject (const juce::File& file, juce::String& error)
{
    if (model.sessionOn && hasProject() && file != project.projectFile) { error = "End the session first (the recording belongs to the project that is open)."; return false; }
    if (dirty) saveNow();
    Project fresh;
    if (! fresh.load (file, error)) return false;
    if (! project.load (file, error)) return false;
    if (project.mixers.empty()) project.addMixer ("Processing mixer");
    if (project.busIndices (true).empty()) project.addDefaultMainBus();
    project.syncMixers();
    if (auto* m = monitorMixer())
    {   // a processing mixer with no track going to its main output (a project from fermata:Live 0.3.0's first builds) is silent: monitoring,
        // playback and Bounce out would give nothing - every track goes there, as a new one does
        bool any = false;
        for (auto& t : project.tracks) if (auto* st = m->stripFor (t.id)) any = any || st->outOn.get();
        if (! any && ! project.tracks.empty())
        {
            for (auto& t : project.tracks) if (! t.isLtc) if (auto* st = m->stripFor (t.id)) st->outOn.set (true);
            markDirty();
            addLog ("The processing mixer had no track going to its main output (nothing could be heard): every track goes there now");
        }
    }
    if (! model.inputNames.isEmpty()) project.syncWithDevice (model.inputNames, model.outputNames);
    if (project.sampleRate <= 0.0 && model.deviceRate > 0.0) project.sampleRate = model.deviceRate;
    settings->setValue ("lastProject", project.projectFile.getFullPathName());
    // the piece: the one the session records, else the last one used in this project, else its first take window
    juce::String piece = model.sessionOn ? model.windowName : settings->getValue ("piece:" + project.projectFile.getFullPathName());
    if (piece.isEmpty() && ! project.takeWindows.empty()) piece = project.takeWindows.front()->name;
    if (piece.isEmpty()) piece = "Concert";
    settings->setValue ("piece", piece);
    TakeRegister::window (project, piece);
    setPreampDevices (project.preampDevices);
    loadArming();
    registeredTakes.clear();
    cutOffAsked = false; unregisteredScanned = false;
    addLog ("Opened " + project.name + " (" + juce::String ((int) project.tracks.size()) + " tracks, " + juce::String ((int) project.takeWindows.size()) + " take windows)");
    layoutDirty = true; mixChanged();
    sendChangeMessage();
    return true;
}

bool PrimaryApp::saveNow (juce::String* error)
{
    if (! hasProject()) return false;
    juce::String err;
    const bool ok = project.save (err);
    if (! ok) { addLog ("The project could not be saved: " + err); lastWarning = "The project could not be saved: " + err; lastWarningMs = juce::Time::getMillisecondCounter(); }
    if (error != nullptr) *error = err;
    dirty = ! ok; lastSaveMs = juce::Time::currentTimeMillis();
    return ok;
}

juce::String PrimaryApp::pieceName() const
{
    if (model.sessionOn && model.windowName.isNotEmpty()) return model.windowName;
    const auto p = settings->getValue ("piece");
    return p.isNotEmpty() ? p : juce::String ("Concert");
}

void PrimaryApp::setPieceName (const juce::String& name)
{
    const auto n = name.trim();
    if (n.isEmpty() || n == pieceName()) return;
    if (model.sessionOn) { addLog ("The piece cannot change while the session runs (End the session first)"); return; }
    settings->setValue ("piece", n);
    if (hasProject())
    {
        settings->setValue ("piece:" + project.projectFile.getFullPathName(), n);
        TakeRegister::window (project, n);
        markDirty();
    }
    layoutDirty = true;
    sendChangeMessage();
}

TakeWindowDef* PrimaryApp::pieceWindow()
{
    if (! hasProject()) return nullptr;
    const auto n = pieceName();
    for (auto& w : project.takeWindows) if (w->name == n) return w.get();
    return &TakeRegister::window (project, n);
}

void PrimaryApp::rebuildLayout()
{
    if (! hasProject()) { currentLayout.reset(); if (player) player->setLayout (nullptr); return; }
    auto* w = pieceWindow();
    if (w == nullptr) return;
    auto L = std::make_shared<TakeLayout> (TakeLayout::build (project, *w, model, timeOfDay()));
    currentLayout = L;
    if (player) player->setLayout (L);
}

// ---- the device ---------------------------------------------------------------------------------------------------------------------------------
void PrimaryApp::askForDevices() { send (cmd ("devices")); }

void PrimaryApp::openDevice (const juce::String& type, const juce::String& name, double rate, int buffer)
{
    auto c = cmd ("open"); put (c, "type", type); put (c, "device", name); put (c, "rate", rate); put (c, "buffer", buffer);
    lastOpen = c;
    settings->setValue ("deviceType", type); settings->setValue ("deviceName", name); settings->setValue ("deviceRate", rate); settings->setValue ("deviceBuffer", buffer);
    send (c);
}

void PrimaryApp::closeDevice() { send (cmd ("close-device")); lastOpen = juce::var(); }

juce::String PrimaryApp::deviceSummary() const
{
    if (! model.connected) return "The recorder is starting...";
    if (! model.deviceRunning) return "No audio device - open Audio settings";
    return model.deviceName + " (" + model.deviceType + "), " + juce::String (model.deviceRate / 1000.0, 1) + " kHz, " + juce::String (model.deviceBuffer) + " samples, "
         + juce::String (model.inputNames.size()) + " in / " + juce::String (model.outputNames.size()) + " out";
}

// ---- the session --------------------------------------------------------------------------------------------------------------------------------
juce::StringArray PrimaryApp::backupFolders() const
{
    juce::StringArray a; a.addLines (settings->getValue ("backups")); a.removeEmptyStrings(); a.trim();
    return a;
}
void PrimaryApp::setBackupFolders (const juce::StringArray& a) { settings->setValue ("backups", a.joinIntoString ("\n")); }

juce::String PrimaryApp::whyNoSession() const
{
    if (! model.connected) return "The recorder is not answering yet.";
    if (! hasProject()) return "Make or open a project first (Project menu).";
    if (! model.deviceRunning) return "Open the audio device first (Audio settings).";
    if (project.sampleRate > 0.0 && std::abs (project.sampleRate - model.deviceRate) > 0.5)
        return "The project is at " + juce::String (project.sampleRate, 0) + " Hz and the device at " + juce::String (model.deviceRate, 0) + " Hz: set the device to the project's rate.";
    int armed = 0;
    for (auto& t : project.tracks) if (! t.isLtc && isArmed (t.id)) ++armed;
    if (project.tracks.empty()) return "The project has no tracks yet (Project Designer).";
    if (armed == 0) return "No track is armed (ARM in the take window).";
    return {};
}

void PrimaryApp::startSession (bool resume)
{
    if (model.sessionOn) return;
    if (sessionStarting != 0 && juce::Time::getMillisecondCounter() - sessionStarting < 15000) return;     // (one is on its way: REC pressed twice)
    const auto why = whyNoSession();
    if (why.isNotEmpty()) { addLog ("No session: " + why); lastWarning = why; lastWarningMs = juce::Time::getMillisecondCounter(); sendChangeMessage(); return; }
    auto* w = pieceWindow();
    CutOffSession cut;
    carryingOnCutOff = false;
    if (! resume && findCutOffSession (cut))
    {   // (a new session would leave the takes of the cut-off one out of the project for good: it is carried on instead)
        resume = true;
        carryingOnCutOff = true; cutOffFolder = cut.folder;
        addLog ("The last session of " + w->name + " was cut off at " + cut.lastWritten.toString (false, true, true, true) + ": it is carried on (nothing recorded in it is lost)");
    }
    if (project.sampleRate <= 0.0) project.sampleRate = model.deviceRate;
    saveNow();
    auto c = cmd ("session");
    put (c, "projectName", project.name); put (c, "projectFolder", project.projectFolder().getFullPathName()); put (c, "window", w->name);
    juce::Array<juce::var> ts;
    for (auto& t : project.tracks)
    {
        if (t.isLtc || ! isArmed (t.id)) continue;
        auto x = obj(); put (x, "id", t.id.toString()); put (x, "name", t.name); put (x, "channels", t.channelCount());
        juce::Array<juce::var> ins; for (int ch = 0; ch < t.channelCount(); ++ch) ins.add (t.inputOf (ch)); put (x, "inputs", ins);
        ts.add (x);
    }
    put (c, "tracks", ts);
    juce::Array<juce::var> bs; for (auto& b : backupFolders()) bs.add (b);
    put (c, "backups", bs);
    put (c, "preTakeSeconds", preTakeSeconds()); put (c, "bufferSeconds", bufferSeconds()); put (c, "resume", resume);
    lastSession = c;
    send (c);
    sessionStarting = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
    addLog (resume ? "Carrying on the last session" : "Starting the session (the pre-take): " + juce::String (ts.size()) + " tracks");
}

void PrimaryApp::endSession()
{
    if (isRecording()) { addLog ("A take is recording: Stop first"); return; }
    send (cmd ("end-session"));
    lastSession = juce::var();
}

int PrimaryApp::nextTakeNumber() const
{
    int n = 1;
    for (auto& w : project.takeWindows) if (w->name == pieceName()) n = juce::jmax (n, w->nextNumber);
    for (auto& t : model.takes) n = juce::jmax (n, t.number + 1);
    return n;
}

void PrimaryApp::record()
{
    if (isRecording()) return;
    if (! model.sessionOn && sessionStarting != 0) { recordWhenSessionStarts = true; return; }       // (the session is starting: the take follows it)
    if (! model.sessionOn)
    {
        const auto why = whyNoSession();
        if (why.isNotEmpty()) { lastWarning = why; lastWarningMs = juce::Time::getMillisecondCounter(); addLog ("Record: " + why); sendChangeMessage(); return; }
        recordWhenSessionStarts = true;
        startSession (false);
        return;
    }
    auto c = cmd ("record"); put (c, "number", nextTakeNumber()); put (c, "label", takeLabel.trim());
    send (c);
    addLog ("REC (take " + juce::String (nextTakeNumber()) + ")");
}

void PrimaryApp::stopTake()
{
    recordWhenSessionStarts = false;
    if (isRecording()) { addLog ("STOP TAKE (take " + juce::String (model.recordingTake) + ")"); send (cmd ("stop")); }
}

void PrimaryApp::setArmed (const juce::Uuid& id, bool on)
{
    if (armingLocked()) { addLog ("The tracks a session records are chosen when it starts: End the session to change them"); return; }
    if (on) disarmed.erase (id.toString()); else disarmed.insert (id.toString());
    saveArming(); sendChangeMessage();
}
void PrimaryApp::armAll (bool on)
{
    if (armingLocked()) { addLog ("The tracks a session records are chosen when it starts: End the session to change them"); return; }
    disarmed.clear();
    if (! on) for (auto& t : project.tracks) disarmed.insert (t.id.toString());
    saveArming(); sendChangeMessage();
}
void PrimaryApp::loadArming()
{
    disarmed.clear();
    juce::StringArray a; a.addTokens (settings->getValue ("disarmed:" + project.projectFile.getFullPathName()), ",", "");
    for (auto& s : a) if (s.isNotEmpty()) disarmed.insert (s);
}
void PrimaryApp::saveArming()
{
    juce::StringArray a; for (auto& s : disarmed) a.add (s);
    settings->setValue ("disarmed:" + project.projectFile.getFullPathName(), a.joinIntoString (","));
}

// ---- flags --------------------------------------------------------------------------------------------------------------------------------------
juce::String PrimaryApp::pinFlag (juce::int64 sample)
{
    if (! model.sessionOn) { addLog ("A flag needs the session running (it marks the recording)"); return {}; }
    const auto id = juce::Uuid().toString();
    auto c = cmd ("flag"); put (c, "id", id); put (c, "by", stationName()); put (c, "station", "Primary");
    if (sample >= 0) put (c, "sample", sample);
    send (c);
    addLog (sample >= 0 ? "Flag pinned at the flag point" : "Flag pinned");
    return id;
}

// ---- notes (0.5.0) ------------------------------------------------------------------------------------------------------------------------------
juce::String PrimaryApp::pinNote (double seconds, bool now, juce::String& why)
{
    const auto L = layout();
    const auto id = juce::Uuid().toString();
    if (model.sessionOn && model.windowName == pieceName())
    {   // the session's (its takes, and on the time of day anywhere since it started): through the recorder, as a flag goes
        const juce::int64 sample = now || L == nullptr ? -1 : L->sessionSampleAt (seconds);
        if (now || sample >= 0)
        {
            auto c = notes::command ("flag", id); put (c, "by", stationName()); put (c, "station", "Primary");
            if (sample >= 0) put (c, "sample", sample);
            send (c);
            sentNotes.insert (id);
            addLog ("Note pinned");
            return id;
        }
    }
    // a take the project has (an earlier session's): straight into the project
    auto* w = pieceWindow();
    if (w == nullptr || L == nullptr || now) { why = "A note marks the recording: start the session, or click a take first."; return {}; }
    const double ws = L->windowOfShown (seconds);
    if (ws < 0.0) { why = "There is no take there: a note goes on a take."; return {}; }
    Marker m; m.id = juce::Uuid (id); m.liveNote = true; m.seconds = ws; m.by = stationName(); m.station = "Primary"; m.pinnedAt = juce::Time::getCurrentTime();
    m.name = noteName ({}, m.by);
    w->markers.push_back (m);
    markDirty();
    addLog ("Note pinned (in the project)");
    return id;
}

void PrimaryApp::setNoteText (const juce::String& id, const juce::String& text)
{
    const bool session = model.sessionOn && (model.findFlag (id) != nullptr || sentNotes.count (id) != 0);
    if (session) { auto c = notes::command ("flag-note", id); put (c, "note", text); send (c); }
    if (auto* w = pieceWindow())
        if (auto* m = findMarker (w->markers, juce::Uuid (id)); m != nullptr && m->liveNote && ! session)
        { m->note = text; m->name = noteName (text, m->by); markDirty(); }
}

void PrimaryApp::removeNote (const juce::String& id)
{
    if (model.sessionOn && (model.findFlag (id) != nullptr || sentNotes.count (id) != 0)) send (notes::command ("flag-remove", id));
    if (auto* w = pieceWindow())
        if (auto* m = findMarker (w->markers, juce::Uuid (id)); m != nullptr && m->liveNote) { removeMarker (w->markers, juce::Uuid (id)); markDirty(); }
    addLog ("Note taken away");
}

bool PrimaryApp::writeReport (juce::File& written, juce::String& error)
{
    auto* w = pieceWindow();
    const auto L = layout();
    if (! hasProject() || w == nullptr || L == nullptr) { error = "No project, or no take window."; return false; }
    report::Input in;
    in.projectName = project.name; in.piece = w->name; in.version = version; in.layout = L; in.window = w;
    in.notes = notes::collect (model, w, L.get(), {});
    if (model.sessionOn && model.windowName == w->name) { in.sessionFlags = model.flags; in.sessionFixes = model.fixes; }
    const auto folder = project.projectFolder().getChildFile ("Reports");
    if (! folder.createDirectory()) { error = "Could not make the folder " + folder.getFullPathName(); return false; }
    written = folder.getChildFile (sanitiseForFile (project.name + " - " + w->name + " - report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M")) + ".pdf");
    return report::write (in, written, error);
}
void PrimaryApp::setFlagNote (const juce::String& id, const juce::String& note)
{
    auto c = cmd ("flag-note"); put (c, "id", id); put (c, "note", note.trim());
    send (c);
    if (note.trim().isNotEmpty()) addLog ("Flag note: " + note.trim());
}
void PrimaryApp::removeFlag (const juce::String& id)
{
    auto c = cmd ("flag-remove"); put (c, "id", id);
    send (c);
}

// ---- monitoring ---------------------------------------------------------------------------------------------------------------------------------
int PrimaryApp::monitorPair() const
{
    auto* m = monitorMixer();
    if (m == nullptr) return -1;
    for (int bi : project.busIndices (true))
        if (auto* bs = m->busFor (project.buses[(size_t) bi].id)) if (bs->isMain.get()) return bs->outFirst.load();
    return -1;
}

void PrimaryApp::setMonitorPair (int first)
{
    auto* m = monitorMixer();
    if (m == nullptr) return;
    for (int bi : project.busIndices (true))
        if (auto* bs = m->busFor (project.buses[(size_t) bi].id)) if (bs->isMain.get()) { bs->outFirst.store (first); break; }
    markDirty(); mixChanged();
}

void PrimaryApp::mixChanged()
{
    sendMix();
    if (player != nullptr) { player->setGains (playbackGains()); player->setOutputs (monitorPair(), monitorPair() >= 0 ? monitorPair() + 1 : -1); }
}

/** For every audio strip of the processing mixer: each channel's (left, right) gain to the main pair - what the Primary hears of each track. */
Player::Gains PrimaryApp::playbackGains() const
{
    Player::Gains out;
    auto* m = monitorMixer();
    if (m == nullptr) return out;
    bool anySolo = false;
    for (auto& t : project.tracks) if (auto* st = m->stripFor (t.id)) anySolo = anySolo || st->solo.get();
    float busGain = 1.0f;
    for (int bi : project.busIndices (true))
        if (auto* bs = m->busFor (project.buses[(size_t) bi].id)) if (bs->isMain.get()) busGain = bs->mute.get() ? 0.0f : dbToGain (bs->gainDb.get());
    for (auto& t : project.tracks)
    {
        if (t.isLtc) continue;
        auto* st = m->stripFor (t.id);
        if (st == nullptr) continue;
        const bool hidden = std::find (m->hiddenStrips.begin(), m->hiddenStrips.end(), t.id) != m->hiddenStrips.end();
        const bool heard = ! hidden && st->outOn.get() && ! st->mute.get() && (! anySolo || st->solo.get());
        const float g = (heard ? dbToGain (st->gainDb.get() + st->inGainDb.get()) * busGain : 0.0f) * (st->polarity.get() ? -1.0f : 1.0f);   // (0.3.2: the polarity key)
        std::vector<std::pair<float, float>> ch;
        const int n = t.channelCount();
        for (int c = 0; c < n; ++c)
        {
            float l = 0.0f, r = 0.0f;
            if (n == 1) panGains (st->pan.get(), l, r);
            else if (n == 2) panGains (c == 0 ? st->panL.get() : st->panR.get(), l, r);
            else panGains (n > 1 ? -1.0f + 2.0f * (float) c / (float) (n - 1) : 0.0f, l, r);
            ch.push_back ({ g * l, g * r });
        }
        out[t.id] = ch;
    }
    return out;
}

void PrimaryApp::sendMix()
{
    if (! model.connected) return;
    auto c = cmd ("mix");
    juce::Array<juce::var> routes, talk;
    const int pair = monitorPair();
    if (pair >= 0 && ! mutedForPlayback)
    {
        // every input of every heard track to the main pair, at the track's gains (two tracks on one input: their gains add up)
        std::map<std::pair<int, int>, float> sum;
        for (auto& [id, ch] : playbackGains())
            if (auto* t = project.findTrack (id))
                for (int c2 = 0; c2 < (int) ch.size(); ++c2)
                {
                    const int in = t->inputOf (c2);
                    if (in < 0) continue;
                    if (ch[(size_t) c2].first != 0.0f) sum[{ in, pair }] += ch[(size_t) c2].first;          // (negative: the polarity key)
                    if (ch[(size_t) c2].second != 0.0f) sum[{ in, pair + 1 }] += ch[(size_t) c2].second;
                }
        for (auto& [k, g] : sum) if (g != 0.0f) { juce::Array<juce::var> r; r.add (k.first); r.add (k.second); r.add (g); routes.add (juce::var (r)); }
    }
    // the talkback: the inputs marked TALK to the talkback outputs (each the first of a pair; the last output alone is mono)
    const int outs = model.outputNames.size();
    for (int in : project.crMicInputs)
        for (int o : project.tbOutputs)
            for (int k = 0; k < 2; ++k)
                if (o + k < outs && (k == 0 || o + 1 < outs))
                { juce::Array<juce::var> r; r.add (in); r.add (o + k); r.add (1.0); talk.add (juce::var (r)); }
    put (c, "routes", routes); put (c, "talk", talk);
    send (c);
    lastMixMs = juce::Time::getMillisecondCounter();
}

void PrimaryApp::setTalkback (bool open)
{
    auto c = cmd ("talkback"); put (c, "open", open); send (c);
    model.talkOpen = open;
    sendChangeMessage();
}

void PrimaryApp::setTalkInput (int input, bool on)
{
    auto& v = project.crMicInputs;
    v.erase (std::remove (v.begin(), v.end(), input), v.end());
    if (on) v.push_back (input);
    markDirty(); sendMix();
}

void PrimaryApp::setTalkOutput (int output, bool on)
{
    auto& v = project.tbOutputs;
    v.erase (std::remove (v.begin(), v.end(), output), v.end());
    if (on) { v.push_back (output); std::sort (v.begin(), v.end()); }
    markDirty(); sendMix();
}

// ---- playback -----------------------------------------------------------------------------------------------------------------------------------
void PrimaryApp::play (double from)
{
    if (player == nullptr) return;
    rebuildLayout();
    if (! mutedForPlayback) { mutedForPlayback = true; sendMix(); }           // (the live monitoring rests while a take is played)
    player->setGains (playbackGains());
    player->setOutputs (monitorPair(), monitorPair() >= 0 ? monitorPair() + 1 : -1);
    player->setLimit (-1.0);
    player->play (from);
    sendChangeMessage();
}
void PrimaryApp::stopPlayback()
{
    if (player == nullptr) return;
    player->stop();
    cursorSeconds = player->playheadSeconds();
    if (mutedForPlayback) { mutedForPlayback = false; sendMix(); }
    sendChangeMessage();
}
double PrimaryApp::playheadSeconds() const { return player != nullptr && player->isPlaying() ? player->playheadSeconds() : cursorSeconds; }

// ---- preamps ------------------------------------------------------------------------------------------------------------------------------------
void PrimaryApp::setPreampDevices (const std::vector<PreampDeviceCfg>& devs)
{
    project.preampDevices = devs;
    juce::Array<juce::var> arr; for (auto& d : devs) arr.add (d.toVar());
    settings->setValue ("preampDevices", juce::JSON::toString (juce::var (arr), true));
    project.preampDriver = std::make_shared<NullPreampDriver>();             // (the old connections go first)
    if (! devs.empty())
    {
        auto d = std::make_shared<RavennaPreampDriver> (devs);
        std::weak_ptr<bool> a = alive;
        d->onHardware = [this, a] (int input, const PreampSettings& s) { if (! a.expired()) project.adoptPreamp (input, s); };
        project.preampDriver = d;
    }
    if (hasProject()) markDirty();
}

void PrimaryApp::findPreampsWithAneman (std::function<void (const juce::String&)> done)
{
    std::weak_ptr<bool> a = alive;
    const auto preferred = settings->getValue ("anemanHost");
    juce::Thread::launch ([this, a, preferred, done]
    {
        auto plan = fetchAnemanPlan (preferred);
        juce::MessageManager::callAsync ([this, a, plan, done]
        {
            if (a.expired()) return;
            if (! plan.ok) { if (done) done ("ANEMAN did not answer: " + plan.error); return; }
            settings->setValue ("anemanHost", plan.source);
            setPreampDevices (plan.devices);
            juce::String text = "Found " + juce::String ((int) plan.devices.size()) + " device(s) with ANEMAN (" + plan.source + "):\n" + plan.summary.joinIntoString ("\n");
            if (! plan.warnings.isEmpty()) text << "\n\n" << plan.warnings.joinIntoString ("\n");
            if (done) done (text);
        });
    });
}

// ---- the Fixer ----------------------------------------------------------------------------------------------------------------------------------
void PrimaryApp::startFixer()
{
    // (a Fixer already open says so itself and closes again: one Fixer per PC. startProgram hands it none of this window's open handles)
    const auto now = juce::Time::getMillisecondCounter();
    if (lastFixerStartMs != 0 && now - lastFixerStartMs < 10000) { addLog ("The Fixer is starting: its window comes up in a moment"); return; }
    lastFixerStartMs = now;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::String error;
    if (startProgram ({ exe.getFullPathName(), "--fixer", "--port", juce::String (port) }, error)) addLog ("The Fixer is starting (its own window; close it any time - the recording goes on)");
    else addLog ("The Fixer could not be started: " + error);
}

// ---- closing (0.3.1) ----------------------------------------------------------------------------------------------------------------------------
PrimaryApp::StillGoing PrimaryApp::stillGoing() const
{
    StillGoing g;
    if (! model.connected) return g;                            // (nothing is known: the recorder closes by itself if it is left with nothing to do)
    g.session = model.sessionOn;
    int unmerged = 0;                                           // (a take just stopped is in the session's list before the merge says it is queued)
    for (auto& t : model.takes) if (t.end >= 0 && ! t.merged && t.mergeError.isEmpty()) ++unmerged;
    g.takesToMerge = juce::jmax (unmerged, model.mergeQueued + (model.mergeActive ? 1 : 0));
    return g;
}

bool PrimaryApp::closeEverything()
{
    closingDown = true;
    stopPlayback();
    if (dirty) saveNow();
    const auto g = stillGoing();
    link.stopLaunching();                                       // (the recorder told to close must not be replaced by a new one when it has gone)
    auto c = cmd ("quit"); put (c, "whenDone", true); send (c);
    if (! model.connected)
    {
        addLog ("Closing: the recorder is not answering. It closes by itself a minute after the last window has gone, unless a take records or the pre-take runs");
        return false;
    }
    if (g.takesToMerge == 0)
    {
        addLog (g.session ? "Closing: the session ends, the audio device is let go and the recorder closes" : "Closing: the recorder lets go of the audio device and closes");
        return false;
    }
    closingWait = true;
    closingMergeSeenMs = juce::Time::getMillisecondCounter();
    addLog ("Closing: " + juce::String (g.session ? "the session ends and " : "") + "the audio device is let go now; fermata:Live closes once the merge has finished and its take is in the project");
    return true;
}

void PrimaryApp::closeWindowOnly()
{
    closingDown = true;
    stopPlayback();
    if (dirty) saveNow();
    if (isRecording()) addLog ("A take is recording: the recorder is left running and is found again when fermata:Live starts");
    else if (model.sessionOn) addLog ("The session is left running (the pre-take): the recorder is found again when fermata:Live starts");
}

void PrimaryApp::finalSave()
{
    stopTimer();
    if (dirty) saveNow();
}

// ---- events -------------------------------------------------------------------------------------------------------------------------------------
void PrimaryApp::onConnection (bool on)
{
    if (! on && closingWait)
    {   // 0.3.1, Close everything: the recorder has finished its merges (their takes are in the project) and closed - fermata:Live closes too
        closingWait = false;
        model.setConnected (false);
        addLog ("The recorder has closed");
        if (onRecorderClosed) onRecorderClosed();
        return;
    }
    const bool hadBefore = wasConnected;
    model.setConnected (on);
    if (! on) sessionStarting = 0;                              // (the answer will not come: the restore after reconnecting starts it again)
    if (on) wasConnected = true;
    addLog (on ? "Connected to the recorder" + juce::String (link.startedTheRecorder() ? " (started by this window)" : " (it was already running)") : "The recorder is not answering - trying again");
    if (on)
    {
        askForDevices();
        restoreAfterStatus = hadBefore && (lastOpen.isObject() || lastSession.isObject());
        if (! hadBefore && ! settings->getValue ("deviceName").isEmpty() && ! lastOpen.isObject())
        {   // the device used last time, opened again at once (unless the recorder already has one: it is still running from before)
            juce::Component::SafePointer<juce::Component> none;
            std::weak_ptr<bool> a = alive;
            juce::Timer::callAfterDelay (900, [this, a]
            {
                if (a.expired() || model.deviceRunning || closingDown) return;     // (0.3.1: not when closing - it would call off Close everything)
                openDevice (settings->getValue ("deviceType"), settings->getValue ("deviceName"), settings->getDoubleValue ("deviceRate", 48000.0), settings->getIntValue ("deviceBuffer", 0));
            });
        }
        mixChanged();
    }
}

void PrimaryApp::onEvent (const juce::var& e)
{
    const auto ev = e["ev"].toString();
    auto warn = [this] (const juce::String& t) { lastWarning = t; lastWarningMs = juce::Time::getMillisecondCounter(); addLog (t); };
    if (ev == "log") { addLog ("[recorder] " + e["text"].toString()); return; }
    if (ev == "hello") { if (e["version"].toString() != version) warn ("The recorder is version " + e["version"].toString() + ", this window " + version + ": restart fermata:Live"); return; }
    if (ev == "devices") { devicesInfo = e; sendChangeMessage(); return; }
    if (ev == "device")
    {
        if ((bool) e["ok"])
        {
            juce::StringArray ins, outs;
            if (auto* a = e["inputs"].getArray()) for (auto& n : *a) ins.add (n.toString());
            if (auto* a = e["outputs"].getArray()) for (auto& n : *a) outs.add (n.toString());
            model.inputNames = ins; model.outputNames = outs; model.deviceRunning = true; model.deviceRate = (double) e["rate"];
            if (hasProject())
            {
                project.syncWithDevice (ins, outs);
                if (project.sampleRate <= 0.0) project.sampleRate = (double) e["rate"];
                markDirty();
            }
            addLog ("Audio device open: " + e["name"].toString() + ", " + juce::String (ins.size()) + " inputs, " + juce::String (outs.size()) + " outputs at " + e["rate"].toString() + " Hz");
            mixChanged();
        }
        else warn ("The audio device was not opened: " + e["error"].toString());
        return;
    }
    if (ev == "session")
    {
        sessionStarting = 0;
        if ((bool) e["ended"])
        {   // (0.3.0: however it ended - here, or in the recorder - it is not carried on if the recorder ever has to be started again)
            addLog ("Session ended"); recordWhenSessionStarts = false; layoutDirty = true;
            lastSession = juce::var();
        }
        else if ((bool) e["ok"])
        {
            addLog ("Session on (the pre-take is being written): " + e["folder"].toString());
            layoutDirty = true;
            const int goesOn = (int) e["recordingTake"], ended = (int) e["endedAtCut"];
            if ((bool) e["resumed"])
            {
                if (goesOn > 0) warn ("RECORDING AGAIN: take " + juce::String (goesOn) + " goes on - the time it was not recording is marked as a gap in it.");
                if (ended > 0) warn ("Take " + juce::String (ended) + " was recording when the session was cut off: it ends where its audio stops, and is being merged into the project. REC records the next take.");
            }
            if (recordWhenSessionStarts)
            {
                recordWhenSessionStarts = false;
                if (goesOn == 0)
                {   // (the status with a carried-on session's takes comes after this event: the number goes past them here)
                    const int number = juce::jmax (nextTakeNumber(), (int) e["lastTake"] + 1);
                    auto c = cmd ("record"); put (c, "number", number); put (c, "label", takeLabel.trim()); send (c);
                    addLog ("REC (take " + juce::String (number) + ")");
                }
            }
        }
        else if (carryingOnCutOff)
        {   // the recording comes first: a cut-off session that cannot be carried on is left on the disk as it is, and a new one starts at once
            carryingOnCutOff = false; ignoreCutOff = cutOffFolder;
            warn ("The last session could not be carried on (" + e["error"].toString() + "): a NEW session starts. The cut-off one is left on the disk as it was: " + cutOffFolder.getFullPathName());
            startSession (false);
        }
        else { recordWhenSessionStarts = false; warn ("The session did not start: " + e["error"].toString()); }
        return;
    }
    if (ev == "take")
    {
        const auto state = e["state"].toString();
        if (state == "refused") warn ("Record refused: " + e["error"].toString());
        else if (state == "recording") addLog ("RECORDING take " + e["number"].toString());
        else if (state == "stopped") addLog ("Take " + e["number"].toString() + " stopped: it is being merged");
        layoutDirty = true;
        return;
    }
    if (ev == "audio")
    {   // 0.3.0: the recorder's watch on the audio (the status line and the recorder box keep showing it while it lasts: LiveMainWindow)
        const auto why = e["why"].toString();
        const bool inSession = (bool) e["session"];
        const double s = (double) e["seconds"];
        const auto secs = s < 10.0 ? juce::String (s, 1) + " s" : juce::String (juce::roundToInt (s)) + " s";
        if (e["state"].toString() == "missing")
        {
            if (inSession)
                warn ("NO AUDIO from the audio device (" + why + "): NOTHING IS BEING RECORDED. The session and its takes are kept. If it does not come back: "
                      "STOP TAKE, end the session (the Pre-take square), open the device again in Audio settings, then REC.");
            else warn ("The audio device gives no audio (" + why + ")");
        }
        else if (inSession) warn ("The audio came back after " + secs + " (" + why + "). That time is not in the recording: a flag marks the place.");
        else addLog ("The audio device gives audio again");
        sendChangeMessage();
        return;
    }
    if (ev == "device-list")
    {   // 0.3.0: a device plugged in or out. Noted only: fermata:Live never changes the device by itself.
        juce::StringArray added, gone;
        if (auto* a = e["added"].getArray()) for (auto& x : *a) added.add (x.toString());
        if (auto* a = e["gone"].getArray()) for (auto& x : *a) gone.add (x.toString());
        const auto current = e["current"].toString();
        if (! added.isEmpty()) addLog ("Audio device plugged in: " + added.joinIntoString (", ") + (current.isNotEmpty() ? ". fermata:Live keeps using " + current + " (change it in Audio settings)." : juce::String()));
        if (! gone.isEmpty()) addLog ("Audio device gone: " + gone.joinIntoString (", "));
        if ((bool) e["currentGone"]) warn ("THE AUDIO DEVICE IN USE HAS GONE: " + current + ". fermata:Live does not change to another device by itself.");
        return;
    }
    if (ev == "drive") { warn ("A DRIVE WAS DROPPED: " + e["folder"].toString() + " - " + e["why"].toString() + ". The recording goes on on the other drives."); return; }
    if (ev == "gap") { warn ("GAP at " + e["time"].toString() + " on " + e["folder"].toString() + ": " + e["why"].toString()); return; }
    if (ev == "merge")
    {
        const auto state = e["state"].toString();
        if (state == "done") { if (! (bool) e["backup"]) registerTake (e); else addLog ("Backup merged into " + e["outputFolder"].toString()); }
        else if (state == "failed") warn ("The merge of take " + e["take"].toString() + " FAILED: " + e["error"].toString() + " (the chunks are kept; it is tried again when the session is carried on)");
        else if (state == "refused") warn ("Merge refused: " + e["error"].toString());
        return;
    }
    if (ev == "flag" || ev == "fix" || ev == "notes") { syncNotesIntoProject(); layoutDirty = true; return; }
    if (ev == "refused") { warn ("The recorder refused \"" + e["cmd"].toString() + "\": " + e["error"].toString()); return; }
    if (ev == "status")
    {
        if (restoreAfterStatus && closingDown) restoreAfterStatus = false;   // (0.3.1: closing - nothing is put back)
        if (restoreAfterStatus)
        {   // the recorder had to be started again (it died): it has no device and no session - they are put back without waiting for anyone
            restoreAfterStatus = false;
            if (! model.deviceRunning && lastOpen.isObject())
            {
                addLog ("The recorder had to be started again: opening the device again" + juce::String (lastSession.isObject() ? " and carrying on the session" : ""));
                send (lastOpen);
                if (lastSession.isObject()) { auto c = lastSession; put (c, "resume", true); send (c); }
            }
            else if (model.deviceRunning && ! model.sessionOn && lastSession.isObject())
            {
                addLog ("The recorder lost the session: carrying it on");
                auto c = lastSession; put (c, "resume", true); send (c);
            }
        }
        askForUnregistered();
        offerToCarryOn();
        if (model.sessionOn && ! lastSession.isObject() && hasProject())
        {   // a session that was running before this window started: what to send again if the recorder ever has to be restarted
            auto c = cmd ("session");
            put (c, "projectName", model.projectName); put (c, "projectFolder", project.projectFolder().getFullPathName()); put (c, "window", model.windowName);
            juce::Array<juce::var> ts;
            for (auto& t : model.tracks)
            {
                auto x = obj(); put (x, "id", t.id.isNull() ? juce::String() : t.id.toString()); put (x, "name", t.name); put (x, "channels", t.channels);
                juce::Array<juce::var> ins; for (auto i : t.inputs) ins.add (i); put (x, "inputs", ins);
                ts.add (x);
            }
            put (c, "tracks", ts);
            juce::Array<juce::var> bs; for (auto& b : backupFolders()) bs.add (b);
            put (c, "backups", bs); put (c, "preTakeSeconds", model.preTakeSeconds); put (c, "bufferSeconds", model.bufferSeconds);
            lastSession = c;
        }
        if (model.deviceRunning && ! lastOpen.isObject())
        {
            auto c = cmd ("open"); put (c, "type", model.deviceType); put (c, "device", model.deviceName); put (c, "rate", model.deviceRate); put (c, "buffer", model.deviceBuffer);
            lastOpen = c;
        }
        layoutDirty = true;
        return;
    }
}

void PrimaryApp::registerTake (const juce::var& e)
{
    if (! hasProject()) { addLog ("Take " + e["take"].toString() + " merged, but no project is open to put it in (open the project and it is put in)"); return; }
    if (! juce::File (e["outputFolder"].toString()).isAChildOf (project.projectFolder()))
    { addLog ("Take " + e["take"].toString() + " belongs to another project (" + e["projectName"].toString() + "): not put into this one"); return; }
    SessionIndex idx;
    idx.windowName = e["window"].toString(); idx.sampleRate = (double) e["rate"]; idx.projectName = e["projectName"].toString();
    if (auto* ts = e["tracks"].getArray())
        for (auto& x : *ts)
        {
            TrackSpec tr; const auto id = x["id"].toString(); tr.id = id.isEmpty() ? juce::Uuid::null() : juce::Uuid (id);
            tr.name = x["name"].toString(); tr.channels = juce::jlimit (1, (int) kMaxTrackChannels, (int) x["channels"]); tr.stem = x["stem"].toString();
            if (auto* ins = x["inputs"].getArray()) for (auto& i : *ins) tr.inputs.push_back ((int) i);
            idx.tracks.push_back (tr);
        }
    if (idx.windowName.isEmpty() || idx.sampleRate <= 0.0 || idx.tracks.empty()) { addLog ("Take " + e["take"].toString() + ": the merge event is incomplete - not put into the project"); return; }
    TakeRec t; t.number = (int) e["take"]; t.label = e["label"].toString(); t.start = (juce::int64) e["take_start"]; t.end = (juce::int64) e["take_end"];
    t.recordedAt = juce::Time::fromISO8601 (e["recordedAt"].toString()); t.merged = true;
    MergeResult res; res.ok = true; res.lengthSamples = (juce::int64) e["lengthSamples"]; res.lostSamples = (juce::int64) e["lostSamples"];
    if (auto* fs = e["files"].getArray()) for (auto& f : *fs) res.files.add (f.toString());
    if (auto* gs = e["gaps"].getArray()) for (auto& g : *gs) res.gaps.push_back ({ (juce::int64) g["first"], (juce::int64) g["samples"], g["time"].toString(), g["why"].toString() });
    auto& w = TakeRegister::window (project, idx.windowName);
    TakeRegister::add (project, w, idx, t, res);
    registeredTakes.insert (t.number);
    {   // its flags and fix versions go in with it - from the merge event, which carries the session's notes (the session may have ended meanwhile:
        // then they are nowhere else), and the running session's own after that (they may be newer)
        std::vector<FlagRec> fl; std::vector<juce::var> fx;
        if (auto* a = e["flags"].getArray()) for (auto& x : *a) { auto f = FlagRec::fromVar (x); if (f.id.isNotEmpty()) fl.push_back (f); }
        if (auto* a = e["fixes"].getArray()) for (auto& x : *a) if (x.isObject()) fx.push_back (x);
        StationTake st; st.number = t.number; st.start = t.start; st.end = t.end; st.merged = true;
        syncNotes (idx.windowName, idx.sampleRate, fl, fx, { st });
    }
    syncNotesIntoProject();
    juce::String err;
    if (! saveNow (&err)) { addLog ("Take " + juce::String (t.number) + " merged, but the project could not be saved: " + err); return; }
    auto c = cmd ("registered"); put (c, "number", t.number); put (c, "folder", e["folder"].toString()); send (c);
    addLog ("Take " + juce::String (t.number) + " is in the project (" + juce::String (res.files.size()) + " files" + (res.gaps.empty() ? ")" : ", " + juce::String ((int) res.gaps.size()) + " gap(s) marked)"));
    layoutDirty = true;
}

bool PrimaryApp::findCutOffSession (CutOffSession& out)
{
    auto* w = hasProject() ? pieceWindow() : nullptr;
    if (w == nullptr) return false;
    const auto found = LiveRecorder::newestSession (LiveRecorder::liveChunksFolder (project.projectFolder(), w->name));
    if (found == juce::File() || found == ignoreCutOff) return false;
    SessionIndex idx;
    if (! idx.load (found.getChildFile (kIndexFileName)) || idx.state != "writing") return false;      // (read only: it may be a live drive's index)
    out = {};
    out.folder = found; out.rate = idx.sampleRate;
    juce::int64 end = idx.lastSample;                           // (the index is written at each chunk's end: the chunk the stop left open says how far it got)
    for (auto& c : idx.chunks)
        if (c.samples < 0 && ! idx.tracks.empty())
        {
            juce::int64 len = -1;
            for (auto& t : idx.tracks) { const auto n = Merge::chunkLength (SessionIndex::chunkFile (found, t, c)); len = len < 0 ? n : juce::jmin (len, n); }
            if (len > 0) end = juce::jmax (end, c.firstSample + len);
        }
    out.lastWritten = idx.origin + juce::RelativeTime::seconds ((double) end / juce::jmax (1.0, idx.sampleRate));
    for (auto& t : idx.takes)
    {
        if (t.end < 0) out.openTake = t.number;
        else if (! t.merged || ! t.registered) ++out.unmergedTakes;
    }
    return out.openTake > 0 || out.unmergedTakes > 0;           // (a session that only wrote its pre-take has nothing to keep: a new one starts)
}

void PrimaryApp::offerToCarryOn()
{
    // once per project, as soon as the device runs and no session is on: a session that was cut off is offered to be carried on
    if (cutOffAsked || ! hasProject() || ! model.connected || ! model.deviceRunning || model.sessionOn) return;
    if (lastSession.isObject()) return;                         // (this window's own session, being carried on after the recorder was started again)
    cutOffAsked = true;
    CutOffSession cut;
    if (! findCutOffSession (cut)) return;
    auto* w = pieceWindow();
    juce::String what = "The session of \"" + w->name + "\" stopped at " + cut.lastWritten.toString (true, true, true, true) + " without being ended: fermata:Live, or the PC, stopped while it was writing.";
    if (cut.openTake > 0) what << "\n\nTake " << cut.openTake << " was recording.";
    if (cut.unmergedTakes > 0) what << "\n\n" << cut.unmergedTakes << " take(s) of it are not in the project yet.";
    const double ago = (juce::Time::getCurrentTime() - cut.lastWritten).inSeconds();
    what << "\n\nCarrying it on keeps everything recorded in it and puts it into the project.";
    if (cut.openTake > 0)
        what << (ago < 540.0 ? " Take " + juce::String (cut.openTake) + " goes on recording at once (the time it missed is marked as a gap in it)."
                             : " Take " + juce::String (cut.openTake) + " ends where its audio stops and is put into the project; REC then records the next take.");
    what << "\n\n(Starting the session later - REC or Pre-take - carries it on too.)";
    addLog ("Found a session that was cut off: " + cut.folder.getFullPathName());
    std::weak_ptr<bool> life = alive;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("The last session was cut off")
                                      .withMessage (what).withButton ("Carry on the session").withButton ("Later"),
                                  [this, life] (int r) { if (life.expired() || r != 1) return; if (! model.sessionOn) startSession (true); });
}

void PrimaryApp::askForUnregistered()
{
    // a take merged while this window was away (or before a project was open here): its merge result is asked for again, once every few seconds
    if (hasProject() && model.connected && ! model.sessionOn && ! unregisteredScanned)
    {   // 0.3.0: no session on - a session that ended while its last take was still merging, with this window closed, may have a take that never
        // got into the project: the ended sessions of every take window are looked through once (per project opened) and those takes asked for
        unregisteredScanned = true;
        int asked = 0;
        for (auto& w : project.takeWindows)
            for (auto& folder : LiveRecorder::liveChunksFolder (project.projectFolder(), w->name).findChildFiles (juce::File::findDirectories, false))
            {
                SessionIndex idx;
                if (! idx.load (folder.getChildFile (kIndexFileName)) || idx.state != "closed" || idx.role != "primary") continue;
                for (auto& t : idx.takes)
                {
                    if (t.end >= 0 && ! t.merged && (t.mergeError.isEmpty() || t.mergeError == "Cancelled"))
                    {   // 0.3.1: its merge was cut short (the recorder or the PC stopped while it merged - after Close everything, say): the session
                        // has ended, so it is not carried on - its take is merged again now, and put into the project when that is done
                        auto c = cmd ("merge-take"); put (c, "number", t.number); put (c, "folder", folder.getFullPathName()); send (c);
                        ++asked;
                        continue;
                    }
                    if (t.merged && ! t.registered)
                    {
                        bool inProject = false;                                // (put in already - the index was not told): nothing to ask
                        for (auto& g : w->groups) if (g.number == t.number && ! t.mergedFiles.isEmpty() && ! g.files.empty() && g.files.front().file.getFullPathName() == t.mergedFiles[0]) inProject = true;
                        if (inProject) { auto c = cmd ("registered"); put (c, "number", t.number); put (c, "folder", folder.getFullPathName()); send (c); continue; }
                        auto c = cmd ("take-info"); put (c, "number", t.number); put (c, "folder", folder.getFullPathName()); send (c);
                        ++asked;
                    }
                }
            }
        if (asked > 0) addLog (juce::String (asked) + " take(s) of earlier sessions were not put into the project (merged but not put in, or their merge cut short): asked for again");
        return;
    }
    if (! hasProject() || ! model.sessionOn) return;
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastAskMs < 4000) return;
    for (auto& t : model.takes)
        if (t.merged && ! t.registered && registeredTakes.count (t.number) == 0)
        {
            lastAskMs = now;
            auto c = cmd ("take-info"); put (c, "number", t.number); send (c);
            return;
        }
}

void PrimaryApp::syncNotesIntoProject()
{
    if (! hasProject() || ! model.sessionOn) return;
    syncNotes (model.windowName, model.sessionRate, model.flags, model.fixes, model.takes);
}

void PrimaryApp::syncNotes (const juce::String& windowName, double sessionRate, const std::vector<FlagRec>& flags, const std::vector<juce::var>& fixes,
                            const std::vector<StationTake>& takes)
{
    if (! hasProject()) return;
    TakeWindowDef* w = nullptr;
    for (auto& x : project.takeWindows) if (x->name == windowName) w = x.get();
    if (w == nullptr) return;
    bool changed = false;
    const double rate = sessionRate > 0 ? sessionRate : 48000.0;
    // each flag of a take that is in the project becomes (or stays) one of the take window's markers, at its place in the take
    for (auto& f : flags)
    {
        if (f.isNote) continue;                                         // (0.5.0: the notes, below)
        const juce::Uuid id (f.id);
        const StationTake* take = nullptr;
        for (auto& t : takes) if (f.sample >= t.start && (t.end < 0 || f.sample < t.end)) take = &t;
        const TakeGroup* g = nullptr;
        if (take != nullptr) for (auto& x : w->groups) if (x.number == take->number) g = &x;
        auto* m = findMarker (w->markers, id);
        if (f.removed || g == nullptr)
        {
            if (m != nullptr && f.removed) { removeMarker (w->markers, id); changed = true; }
            continue;
        }
        Marker want;
        want.id = id; want.flag = true; want.note = f.note; want.pinnedAt = f.pinnedAt; want.by = f.by; want.station = f.station;
        want.seconds = g->startSeconds + (double) (f.sample - take->start) / rate;
        for (auto& r : f.replies) want.replies.push_back ({ r.text, r.by, r.at });
        want.name = flagName (want.note, want.replies);
        if (m == nullptr) { w->markers.push_back (want); changed = true; }
        else if (m->name != want.name || m->note != want.note || m->replies.size() != want.replies.size() || std::abs (m->seconds - want.seconds) > 1.0e-6 || m->by != want.by)
        { *m = want; changed = true; }
    }
    // 0.5.0: both stations' notes, the same way: a marker of the take window once their take is in the project (fermata shows them as markers)
    for (auto& f : flags)
    {
        if (! f.isNote) continue;
        const juce::Uuid id (f.id);
        const StationTake* take = nullptr;
        for (auto& t : takes) if (f.sample >= t.start && (t.end < 0 || f.sample < t.end)) take = &t;
        const TakeGroup* g = nullptr;
        if (take != nullptr) for (auto& x : w->groups) if (x.number == take->number) g = &x;
        auto* m = findMarker (w->markers, id);
        if (f.removed) { if (m != nullptr) { removeMarker (w->markers, id); changed = true; } continue; }
        if (g == nullptr) continue;
        Marker want;
        want.id = id; want.liveNote = true; want.note = f.note; want.pinnedAt = f.pinnedAt; want.by = f.by; want.station = f.station;
        want.seconds = g->startSeconds + (double) (f.sample - take->start) / rate;
        want.name = noteName (want.note, want.by);
        if (m == nullptr) { w->markers.push_back (want); changed = true; }
        else if (m->name != want.name || m->note != want.note || std::abs (m->seconds - want.seconds) > 1.0e-6 || m->by != want.by)
        { *m = want; changed = true; }
    }
    // the Fixer's fix versions: in the take window, by id (the take's id once it is in)
    for (auto& fx : fixes)
    {
        const juce::Uuid id (fx["id"].toString());
        FixVersion v;
        v.id = id; v.takeNumber = (int) fx["takeNumber"];
        v.startSample = (juce::int64) fx["from"]; v.endSample = (juce::int64) fx["to"]; v.sampleRate = (double) fx["rate"] > 0 ? (double) fx["rate"] : rate;
        v.what = fx["what"].toString(); v.note = fx["note"].toString(); v.by = fx["by"].toString(); v.madeAt = juce::Time ((juce::int64) fx["madeAt"]);
        v.versionNumber = juce::jmax (1, (int) fx["version"]);
        if (fx["flag"].toString().isNotEmpty()) v.flagId = juce::Uuid (fx["flag"].toString());
        if (fx["replaces"].toString().isNotEmpty()) v.replaces = juce::Uuid (fx["replaces"].toString());
        if (auto* fs = fx["files"].getArray())
            for (auto& f : *fs)
            {
                FixFile ff; ff.trackId = juce::Uuid (f["track"].toString()); ff.trackName = f["trackName"].toString();
                ff.file = juce::File (f["file"].toString()); ff.numChannels = juce::jmax (1, (int) f["channels"]);
                v.files.push_back (ff);
            }
        for (auto& g : w->groups) if (g.number == v.takeNumber) v.takeId = g.id;
        FixVersion* have = nullptr;
        for (auto& x : w->fixVersions) if (x.id == id) have = &x;
        if (have == nullptr) { w->fixVersions.push_back (v); changed = true; }
        else if (have->takeId != v.takeId || have->files.size() != v.files.size() || have->note != v.note)
        { v.state = have->state; v.reviewNote = have->reviewNote; *have = v; changed = true; }
    }
    if (changed) markDirty();
}

void PrimaryApp::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &model) { layoutDirty = true; sendChangeMessage(); }
    else if (b == &project) sendChangeMessage();
}

void PrimaryApp::timerCallback()
{
    if (layoutDirty || model.recordingTake != 0) { layoutDirty = false; rebuildLayout(); }
    if (player != nullptr)
    {
        player->setWrittenTo (model.writtenTo());
        if (player->isPlaying() && player->reachedEnd()) stopPlayback();
    }
    const auto now = juce::Time::currentTimeMillis();
    if (dirty && now - lastSaveMs > 1500 && ! isRecording()) saveNow();       // (while a take records only every 10 s: the disk is the recording's)
    if (dirty && now - lastSaveMs > 10000) saveNow();
    if (model.connected && juce::Time::getMillisecondCounter() - lastMixMs > 5000) sendMix();   // (a recorder that came back gets the mix again)
    if (closingWait)
    {   // (the recorder closes 1.5 s after its last merge: if it says no merge is going for 10 s and is still there, fermata:Live closes anyway)
        const auto ms = juce::Time::getMillisecondCounter();
        if (model.mergeActive || model.mergeQueued > 0) closingMergeSeenMs = ms;
        if (ms - closingMergeSeenMs > 10000 && ! isRecording())
        {
            closingWait = false;
            addLog ("Closing: the recorder says it has finished merging but has not closed - fermata:Live closes anyway");
            if (onRecorderClosed) onRecorderClosed();
        }
    }
}
} // namespace td::live
