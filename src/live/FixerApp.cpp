#include "FixerApp.h"
#include "StationNotes.h"
#include "FixerWindow.h"
#include "LiveLook.h"
#include "../ui/ClosingLog.h"

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static float dbToGain (float db) { return db <= -89.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
static void panGains (float pan, float& l, float& r)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    l = std::cos (a); r = std::sin (a);
}

FixerApp::FixerApp (const juce::String& ver, int p) : version (ver), port (p), link (p)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "fermata-Live-Fixer"; o.filenameSuffix = ".settings"; o.folderName = "Fermata"; o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (o);
    look::install (*settings);                                                // 0.9.0: the look this station had (UI style, colour, dark or light), before its window is made
    delaySeconds = juce::jlimit (1.0, 30.0, settings->getDoubleValue ("delaySeconds", 3.0));
    outPair = settings->getIntValue ("outPair", 2);
    model.send = [this] (const juce::var& c) { link.send (c); };
    model.onEvent = [this] (const juce::var& e) { onEvent (e); };
    model.addChangeListener (this);
    link.onEvent = [this] (const juce::var& e) { model.handle (e); };
    link.onConnection = [this] (bool on)
    {
        model.setConnected (on);
        say (on ? "Connected to the recorder" : "The recorder is not answering: is fermata:Live (the Primary) running? Trying again...");
    };
    player = std::make_unique<Player> (port + 1, "Fixer", 1);
    link.start (false);                                                       // (the Fixer never starts a recorder: the Primary does)
    // with no session yet: the project the Primary used last, to look at its takes
    {
        juce::PropertiesFile::Options po = o; po.applicationName = "fermata-Live";
        juce::PropertiesFile primarySettings (po);
        const juce::File last (primarySettings.getValue ("lastProject"));
        if (last.existsAsFile()) { shownPiece = primarySettings.getValue ("piece"); loadProjectFor (last); }
    }
    window = std::make_unique<FixerWindow> (*this);
    window->onClose = [this] { if (onQuit) onQuit(); };
    startTimerHz (30);
}

void FixerApp::saveSettings()
{
    saveMix();
    settings->setValue ("delaySeconds", delaySeconds);
    settings->setValue ("outPair", outPair);
    settings->saveIfNeeded();
}

FixerApp::~FixerApp()
{
    // (0.3.1: each step into the log with its time - fermata's closing log, written only while the program closes)
    stopTimer();
    saveSettings();
    window.reset();
    look::uninstall();
    closing::step ("the Fixer's window closed");
    model.removeChangeListener (this);
    player.reset();
    closing::step ("playback stopped (its stream from the recorder closed)");
    link.stop();
    closing::step ("the connection to the recorder closed");
}

TakeWindowDef* FixerApp::pieceWindow()
{
    if (! hasProject()) return nullptr;
    const auto n = pieceName();
    for (auto& w : project.takeWindows) if (w->name == n) return w.get();
    return project.takeWindows.empty() ? nullptr : project.takeWindows.front().get();
}

void FixerApp::loadProjectFor (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    juce::String err;
    Project fresh;
    fresh.readOnlyLoads = true; project.readOnlyLoads = true;       // (the Primary writes the project: reading it must never mend anything on the disk)
    if (! fresh.load (f, err)) { say ("The project could not be read: " + err); return; }
    const bool first = project.projectFile != f;
    if (! project.load (f, err)) { say ("The project could not be read: " + err); return; }
    watchedProject = f; watchedTime = f.getLastModificationTime().toMilliseconds();
    if (first) { loadMix(); say ("Project " + project.name + " (read only: the Primary's window saves it)"); }
    layoutDirty = true;
    sendChangeMessage();
}

void FixerApp::checkProjectFile()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastProjectCheck < 1500) return;
    lastProjectCheck = now;
    // the session's project: <project folder>/Recorded Media/<window>/Live Chunks/<session> - the project file is in the folder four up
    if (model.sessionOn && model.sessionFolder.isNotEmpty())
    {
        const auto folder = juce::File (model.sessionFolder).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
        juce::File pf = folder.getChildFile (model.projectName + ".fermata");
        if (! pf.existsAsFile()) { auto found = folder.findChildFiles (juce::File::findFiles, false, "*.fermata"); if (! found.isEmpty()) pf = found[0]; }
        if (pf.existsAsFile() && pf != watchedProject) { loadProjectFor (pf); return; }
    }
    if (watchedProject.existsAsFile() && watchedProject.getLastModificationTime().toMilliseconds() != watchedTime) loadProjectFor (watchedProject);
}

void FixerApp::rebuildLayout()
{
    if (! hasProject()) { currentLayout.reset(); if (player) player->setLayout (nullptr); return; }
    auto* w = pieceWindow();
    if (w == nullptr) return;
    auto L = std::make_shared<TakeLayout> (TakeLayout::build (project, *w, model, timeOfDay()));
    currentLayout = L;
    if (player) player->setLayout (L);
}

double FixerApp::writtenEndSeconds() const
{
    if (currentLayout == nullptr || model.recordingTake == 0) return -1.0;
    for (auto& t : currentLayout->takes)
        if (t.recording)
        {
            const auto w = model.writtenTo();
            return t.start + juce::jmax (0.0, (double) (w - t.sessionStart) / juce::jmax (1.0, t.rate));
        }
    return -1.0;
}

double FixerApp::liveEndSeconds() const
{
    if (currentLayout == nullptr || model.recordingTake == 0) return -1.0;
    for (auto& t : currentLayout->takes) if (t.recording) return t.start + (double) (model.positionNow() - t.sessionStart) / juce::jmax (1.0, t.rate);
    return -1.0;
}

void FixerApp::setFollow (bool on)
{
    follow = on;
    if (! on) { if (player) player->setLimit (-1.0); }
    say (on ? "Following the Primary: the playhead stays " + juce::String (delaySeconds, 1) + " s behind the audio on the disk (untick Follow to move it)" : "Not following: move the playhead where you like");
}

void FixerApp::play (double from)
{
    if (player == nullptr) return;
    rebuildLayout();
    player->setGains (gains());
    const int pair = playPair();
    player->setOutputs (pair, pair >= 0 ? pair + 1 : -1);
    if (pair != outPair && pair >= 0 && ! pairNoteShown)
    {
        pairNoteShown = true;
        say ("Playing on outputs " + juce::String (pair + 1) + "-" + juce::String (pair + 2) + ": the device has no outputs " + juce::String (outPair + 1) + "-" + juce::String (outPair + 2) + " (the Fixer's mixer chooses)");
    }
    const double we = writtenEndSeconds();
    player->setLimit (we >= 0.0 ? we : -1.0);
    player->play (juce::jmax (0.0, from));
    sendChangeMessage();
}

void FixerApp::stop()
{
    if (player == nullptr) return;
    player->stop();
    cursorSeconds = player->playheadSeconds();
    sendChangeMessage();
}

double FixerApp::playheadSeconds() const { return isPlaying() ? player->playheadSeconds() : cursorSeconds; }

Player::Gains FixerApp::gains() const
{
    Player::Gains out;
    if (currentLayout == nullptr) return out;
    bool anySolo = false;
    for (auto& [k, m] : mix) anySolo = anySolo || m.solo;
    const float master = dbToGain (masterDb);
    for (auto& t : currentLayout->tracks)
    {
        MixTrack m;
        if (auto it = mix.find (t.id.toString()); it != mix.end()) m = it->second;
        const bool heard = ! m.mute && (! anySolo || m.solo);
        const float g = heard ? dbToGain (m.gainDb) * master : 0.0f;
        std::vector<std::pair<float, float>> ch;
        for (int c = 0; c < t.channels; ++c)
        {
            float l = 0.0f, r = 0.0f;
            if (t.channels == 1) panGains (m.pan, l, r);
            else if (t.channels == 2) panGains (c == 0 ? m.panL : m.panR, l, r);
            else panGains (-1.0f + 2.0f * (float) c / (float) (t.channels - 1), l, r);
            ch.push_back ({ g * l, g * r });
        }
        out[t.id] = ch;
    }
    return out;
}

void FixerApp::mixChanged()
{
    if (player != nullptr) { player->setGains (gains()); const int pair = playPair(); player->setOutputs (pair, pair >= 0 ? pair + 1 : -1); }
    saveMix();
    settings->setValue ("outPair", outPair);
    sendChangeMessage();
}

void FixerApp::loadMix()
{
    mix.clear();
    const auto v = juce::JSON::parse (settings->getValue ("mix:" + project.projectFile.getFullPathName()));
    if (v.getDynamicObject() != nullptr)
    {
        masterDb = (float) (double) v.getProperty ("master", 0.0);
        if (auto* ts = v["tracks"].getDynamicObject())
            for (auto& p : ts->getProperties())
            {
                MixTrack m; const auto& x = p.value;
                m.gainDb = (float) (double) x["gain"]; m.pan = (float) (double) x["pan"]; m.panL = (float) (double) x.getProperty ("panL", -1.0); m.panR = (float) (double) x.getProperty ("panR", 1.0);
                m.mute = (bool) x["mute"]; m.solo = (bool) x["solo"];
                mix[p.name.toString()] = m;
            }
    }
}

void FixerApp::saveMix()
{
    if (! hasProject()) return;
    auto root = obj(); put (root, "master", masterDb);
    auto ts = obj();
    for (auto& [k, m] : mix)
    {
        auto x = obj(); put (x, "gain", m.gainDb); put (x, "pan", m.pan); put (x, "panL", m.panL); put (x, "panR", m.panR); put (x, "mute", m.mute); put (x, "solo", m.solo);
        put (ts, juce::Identifier (k), x);
    }
    put (root, "tracks", ts);
    settings->setValue ("mix:" + project.projectFile.getFullPathName(), juce::JSON::toString (root, true));
}

void FixerApp::reply (const juce::String& flagId, const juce::String& text)
{
    if (text.trim().isEmpty()) return;
    auto c = obj(); put (c, "cmd", "reply"); put (c, "flag", flagId); put (c, "text", text.trim()); put (c, "by", stationName());
    link.send (c);
    say ("Reply sent: " + text.trim());
}

// ---- notes (0.5.0): through the recorder, which keeps them with the session; the Primary's window puts them into the project
juce::String FixerApp::pinNote (double seconds, juce::String& why)
{
    if (! model.sessionOn) { why = "A note from the Fixer is kept by the session: the Primary must start it first (Pre-take is enough)."; return {}; }
    const auto L = layout();
    const juce::int64 sample = L != nullptr ? L->sessionSampleAt (seconds) : -1;
    if (sample < 0) { why = "A note from the Fixer goes on a take of the session that is running (the earlier ones are in the project, which the Fixer does not change)."; return {}; }
    const auto id = juce::Uuid().toString();
    auto c = notes::command ("flag", id); put (c, "by", stationName()); put (c, "station", "Fixer"); put (c, "sample", sample);
    link.send (c);
    sentNotes.insert (id);
    say ("Note pinned");
    return id;
}
void FixerApp::setNoteText (const juce::String& id, const juce::String& text)
{
    if (! model.sessionOn || (model.findFlag (id) == nullptr && sentNotes.count (id) == 0)) return;
    auto c = notes::command ("flag-note", id); put (c, "note", text);
    link.send (c);
}
void FixerApp::removeNote (const juce::String& id)
{
    if (! model.sessionOn || (model.findFlag (id) == nullptr && sentNotes.count (id) == 0)) return;
    link.send (notes::command ("flag-remove", id));
    say ("Note taken away");
}

void FixerApp::announceFix (const juce::var& fix)
{
    auto c = obj(); put (c, "cmd", "fix"); put (c, "fix", fix);
    link.send (c);
}

void FixerApp::onEvent (const juce::var& e)
{
    const auto ev = e["ev"].toString();
    if (ev == "flag")
    {
        auto f = FlagRec::fromVar (e["flag"]);
        if (! f.isNote)                                                  // (0.5.0: a note is its station's own - the Primary's are not shown here)
        {
            if (! f.removed && f.replies.empty()) say ("FLAG from " + (f.by.isNotEmpty() ? f.by : juce::String ("the Primary")) + (f.note.isNotEmpty() ? ": " + f.note : juce::String (" (the note is coming)")));
            if (window != nullptr) window->flagArrived (f);
        }
    }
    else if (ev == "session" && (bool) e["ended"]) say ("The Primary ended the session");
    else if (ev == "refused") say ("The recorder refused \"" + e["cmd"].toString() + "\": " + e["error"].toString());
    else if (ev == "fix") say ("Fix version kept by the recorder (version " + e["fix"]["version"].toString() + " of take " + e["fix"]["takeNumber"].toString() + ")");
    layoutDirty = true;
}

void FixerApp::changeListenerCallback (juce::ChangeBroadcaster*) { layoutDirty = true; sendChangeMessage(); }

void FixerApp::timerCallback()
{
    checkProjectFile();
    if (layoutDirty || model.recordingTake != 0) { layoutDirty = false; rebuildLayout(); }
    if (player == nullptr) return;
    player->setWrittenTo (model.writtenTo());
    const double we = writtenEndSeconds();
    if (follow)
    {
        // a few seconds behind what is on the disk, playing on; a jump back when it has drifted (a stall on the drive, a new take)
        if (we >= 0.0)
        {
            player->setLimit (we);
            const double target = juce::jmax (0.0, we - delaySeconds);
            const auto now = juce::Time::getMillisecondCounter();
            if (! player->isPlaying()) { if (we > delaySeconds * 0.5) play (target); }
            else if (std::abs (player->playheadSeconds() - target) > juce::jmax (1.5, delaySeconds * 0.6) && now - lastFollowFix > 1500)
            {
                lastFollowFix = now;
                play (target);
            }
        }
        else if (player->isPlaying() && player->reachedEnd()) stop();
    }
    else
    {
        player->setLimit (we >= 0.0 ? we : -1.0);
        if (player->isPlaying() && player->reachedEnd()) stop();
    }
}

// ---- the Fixer's mixer model ------------------------------------------------------------------------------------------------------------------
std::vector<MixStrip> FixerMixModel::strips() const
{
    std::vector<MixStrip> out;
    const auto L = app.layout();
    if (L == nullptr) return out;
    for (auto& t : L->tracks)
    {
        MixStrip s; s.id = t.id; s.name = t.name; s.channels = t.channels;
        s.colour = app.hasProject() ? chan::of (app.project, t.id) : juce::Colour (0xff3f7fc4);
        if (auto it = app.mix.find (t.id.toString()); it != app.mix.end())
        { s.gainDb = it->second.gainDb; s.pan = it->second.pan; s.panL = it->second.panL; s.panR = it->second.panR; s.mute = it->second.mute; s.solo = it->second.solo; }
        out.push_back (s);
    }
    return out;
}
void FixerMixModel::setGain (const juce::Uuid& id, float db)                { app.mix[id.toString()].gainDb = db; app.mixChanged(); }
void FixerMixModel::setPan (const juce::Uuid& id, int which, float v)      { auto& m = app.mix[id.toString()]; (which == 0 ? m.pan : which == 1 ? m.panL : m.panR) = v; app.mixChanged(); }
void FixerMixModel::setMute (const juce::Uuid& id, bool b)                 { app.mix[id.toString()].mute = b; app.mixChanged(); }
void FixerMixModel::setSolo (const juce::Uuid& id, bool b)                 { app.mix[id.toString()].solo = b; app.mixChanged(); }
} // namespace td::live
