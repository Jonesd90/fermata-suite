#include "RecorderWindow.h"
#include "RecorderService.h"
#include "TakeRegister.h"
#include "LiveRecorder.h"

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static juce::String timeOf (juce::int64 samples, double rate)
{
    const int s = rate > 0 ? (int) (samples / rate) : 0;
    return juce::String (s / 3600).paddedLeft ('0', 2) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

void RecorderPanel::Meters::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15181b));
    if (levels == nullptr || levels->empty()) { g.setColour (juce::Colours::grey); g.setFont (juce::FontOptions (12.0f)); g.drawText ("(no device)", getLocalBounds(), juce::Justification::centred); return; }
    const int n = (int) levels->size();
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float lv = (*levels)[(size_t) i];
        const float db = lv > 1.0e-6f ? 20.0f * std::log10 (lv) : -100.0f;
        const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) (getHeight() - 14);
        auto r = juce::Rectangle<float> ((float) i * w + 1.0f, (float) (getHeight() - 14) - h, juce::jmax (1.0f, w - 2.0f), h);
        g.setColour (db > -1.0f ? juce::Colours::red : db > -12.0f ? juce::Colour (0xffe0b040) : juce::Colour (0xff3aa860));
        g.fillRect (r);
        if (w >= 14.0f) { g.setColour (juce::Colours::grey); g.setFont (juce::FontOptions (9.0f)); g.drawText (juce::String (i + 1), juce::Rectangle<int> ((int) (i * w), getHeight() - 13, (int) w, 12), juce::Justification::centred); }
    }
}

RecorderPanel::RecorderPanel (const juce::String& ver, int port) : version (ver), link (port)
{
    auto label = [this] (juce::Label& l, const juce::String& t, float size = 13.0f, bool bold = false)
    {
        l.setText (t, juce::dontSendNotification); l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffe4e8ec)); addAndMakeVisible (l);
    };
    auto editor = [this] (juce::TextEditor& e, const juce::String& text, const juce::String& tip, bool multi = false)
    {
        e.setMultiLine (multi); e.setReturnKeyStartsNewLine (multi); e.setText (text); e.setTooltip (tip); addAndMakeVisible (e);
    };
    label (title, "fermata:Live recorder test bench", 20.0f, true);
    label (versionLabel, "Version " + version + ". Records on the MAD / ASIO driver through the recorder process; the take goes into the project for fermata.", 12.0f);
    versionLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8f9aa5));
    label (projectLine, "Project: none. Make a new one or open a fermata project.");
    label (deviceStatus, "Device: not open.");
    label (sessionStatus, "Session: none."); label (takeStatus, ""); label (drivesStatus, ""); label (mergeStatus, "");
    takeStatus.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    for (auto* b : { &newProject, &openProject, &openDev, &closeDev, &addBackup, &clearBackups, &startBtn, &resumeBtn, &recordBtn, &stopBtn, &endBtn, &mergeBackupBtn }) addAndMakeVisible (b);
    recordBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8c2c2c));
    for (auto* c : { &typeBox, &deviceBox, &rateBox, &bufferBox }) addAndMakeVisible (c);
    editor (windowName, "Live", "The take window (the piece or half) the takes go into");
    editor (backupsBox, "", "Backup drives: one folder per line (e.g. E:\\Recordings). The primary is the project's own drive.", true);
    editor (inputsToRecord, "8", "How many of the driver's inputs to record, each as a mono track (when the project has no tracks of its own)");
    editor (preTake, "120", "Pre-take: seconds before Record that the take includes (0 - 300)");
    editor (bufferSeconds, "60", "The memory buffer: how long a drive may stall before it gets a gap (30 - 60 s)");
    editor (takeLabel, "", "The take's label (the piece): empty = the take window's name");
    log.setMultiLine (true); log.setReadOnly (true); log.setCaretVisible (false); log.setFont (juce::FontOptions (12.0f)); addAndMakeVisible (log);
    meters.levels = &shown; meterView.setViewedComponent (&meters, false); meterView.setScrollBarsShown (false, true); addAndMakeVisible (meterView);

    newProject.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("New project: choose where, and type its name", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fermata");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) setProject (fc.getResult().withFileExtension (".fermata"), true); });
    };
    openProject.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a fermata project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fermata");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) setProject (fc.getResult(), false); });
    };
    addBackup.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder on a backup drive", juce::File());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) backupsBox.setText ((backupsBox.getText().trim() + "\n" + fc.getResult().getFullPathName()).trim()); });
    };
    clearBackups.onClick = [this] { backupsBox.clear(); };
    typeBox.onChange = [this] { fillDeviceBoxes(); };
    openDev.onClick = [this] { openDevice(); };
    closeDev.onClick = [this] { sendCommand ("close-device"); };
    startBtn.onClick = [this] { startSession (false); };
    resumeBtn.onClick = [this] { startSession (true); };
    recordBtn.onClick = [this]
    {
        int number = 1;
        if (project != nullptr) { auto& w = TakeRegister::window (*project, windowName.getText().trim().isEmpty() ? "Live" : windowName.getText().trim()); number = juce::jmax (1, w.nextNumber); }
        if (lastStatus.isObject() && lastStatus["session"].isObject())
            if (auto* ts = lastStatus["session"]["takes"].getArray()) for (auto& t : *ts) number = juce::jmax (number, (int) t["number"] + 1);
        sendCommand ("record", [&] (juce::var& c) { put (c, "number", number); put (c, "label", takeLabel.getText().trim()); });
    };
    stopBtn.onClick = [this] { sendCommand ("stop"); };
    endBtn.onClick = [this] { sendCommand ("end-session"); };
    mergeBackupBtn.onClick = [this]
    {
        if (lastTakeNumber == 0 || lastBackupFolder.isEmpty()) { addLog ("No finished take and backup drive to merge from yet"); return; }
        sendCommand ("merge-backup", [&] (juce::var& c) { put (c, "folder", lastBackupFolder); put (c, "number", lastTakeNumber); });
    };

    link.onEvent = [this] (const juce::var& e) { onEvent (e); };
    link.onConnection = [this] (bool on) { onConnection (on); };
    link.start (true);
    updateButtons();
    startTimer (40);
    setSize (1060, 820);
}

RecorderPanel::~RecorderPanel() { stopTimer(); link.stop(); }

void RecorderPanel::windowClosing()
{
    const bool mergeGoing = lastStatus.isObject() && lastStatus["merge"].isObject() && ((bool) lastStatus["merge"]["active"] || (int) lastStatus["merge"]["queued"] > 0);
    if (recordingTake != 0) addLog ("A take is recording: the recorder is left running and will be found again when fermata:Live starts");
    else if (mergeGoing) addLog ("A merge is still running: the recorder is left running to finish it, and will be found again when fermata:Live starts");
    else sendCommand ("quit");
}

void RecorderPanel::sendCommand (const juce::String& cmd, std::function<void (juce::var&)> fill)
{
    auto c = obj(); put (c, "cmd", cmd);
    if (fill) fill (c);
    link.send (c);
}

void RecorderPanel::addLog (const juce::String& text)
{
    log.moveCaretToEnd();
    log.insertTextAtCaret (juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + text + "\n");
    juce::Logger::writeToLog (text);
}

void RecorderPanel::setProject (const juce::File& file, bool create)
{
    auto p = std::make_unique<Project>();
    juce::String err;
    if (create)
    {
        const auto folder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension());
        if (! folder.createDirectory()) { addLog ("Cannot make " + folder.getFullPathName()); return; }
        p->projectFile = folder.getChildFile (file.getFileName());
        p->name = file.getFileNameWithoutExtension();
        p->createFolders();
        if (deviceOpen) { p->syncWithDevice (inputNames, outputNames); p->sampleRate = deviceRate; }
        if (! p->save (err)) { addLog ("Cannot save the new project: " + err); return; }
    }
    else if (! p->load (file, err)) { addLog ("Cannot open " + file.getFullPathName() + ": " + err); return; }
    project = std::move (p);
    projectFile = project->projectFile;
    if (! project->takeWindows.empty() && windowName.getText().trim() == "Live" && project->takeWindows.front()->name.isNotEmpty()) windowName.setText (project->takeWindows.front()->name);
    refreshProjectLine();
    addLog ((create ? "New project " : "Opened project ") + project->name + " in " + project->projectFolder().getFullPathName() + " (" + juce::String (project->tracks.size()) + " tracks)");
    updateButtons();
}

void RecorderPanel::refreshProjectLine()
{
    if (project == nullptr) { projectLine.setText ("Project: none. Make a new one or open a fermata project.", juce::dontSendNotification); return; }
    juce::String takes;
    for (auto& w : project->takeWindows) takes << (takes.isEmpty() ? "" : ", ") << w->name << " (" << (int) w->groups.size() << " takes)";
    projectLine.setText ("Project: " + project->name + " - " + project->projectFolder().getFullPathName() + " - " + juce::String (project->tracks.size()) + " tracks"
                         + (project->tracks.empty() ? " (every input will be recorded as a mono track)" : "") + (takes.isEmpty() ? juce::String() : " - " + takes), juce::dontSendNotification);
}

void RecorderPanel::fillDeviceBoxes()
{
    deviceBox.clear (juce::dontSendNotification);
    if (! devicesInfo.isObject()) return;
    if (auto* types = devicesInfo["types"].getArray())
        for (auto& t : *types)
            if (t["type"].toString() == typeBox.getText())
                if (auto* names = t["devices"].getArray()) { int id = 1; for (auto& n : *names) deviceBox.addItem (n.toString(), id++); }
    if (deviceBox.getNumItems() > 0 && deviceBox.getSelectedId() == 0) deviceBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void RecorderPanel::openDevice()
{
    if (deviceBox.getText().isEmpty()) { addLog ("Choose a device first"); return; }
    const double rate = rateBox.getText().getDoubleValue();
    const int buffer = bufferBox.getSelectedId() == 1 ? 0 : bufferBox.getText().getIntValue();
    auto c = obj(); put (c, "cmd", "open"); put (c, "type", typeBox.getText()); put (c, "device", deviceBox.getText()); put (c, "rate", rate); put (c, "buffer", buffer);
    lastOpen = c;
    link.send (c);
}

void RecorderPanel::startSession (bool resume)
{
    if (project == nullptr) { addLog ("Make or open a project first"); return; }
    if (! deviceOpen) { addLog ("Open the audio device first"); return; }
    const auto wName = windowName.getText().trim().isEmpty() ? juce::String ("Live") : windowName.getText().trim();
    // the tracks: the project's own, or one mono track per input (made in the project now, so the takes have tracks to land on)
    if (project->tracks.empty())
    {
        const int n = juce::jlimit (1, inputNames.size(), inputsToRecord.getText().getIntValue());
        for (int i = 0; i < n; ++i) project->addTrack (project->inputLabel (i).isNotEmpty() ? project->inputLabel (i) : "Input " + juce::String (i + 1), TrackFormat::Mono, i);
        juce::String err; if (! project->save (err)) { addLog ("Cannot save the project: " + err); return; }
        addLog (juce::String (n) + " mono tracks made in the project, one per input");
        refreshProjectLine();
    }
    TakeRegister::window (*project, wName);
    { juce::String err; project->save (err); }
    auto c = obj(); put (c, "cmd", "session");
    {
        put (c, "projectName", project->name); put (c, "projectFolder", project->projectFolder().getFullPathName()); put (c, "window", wName);
        juce::Array<juce::var> ts;
        for (auto& t : project->tracks)
        {
            auto x = obj(); put (x, "id", t.id.toString()); put (x, "name", t.name); put (x, "channels", t.channelCount());
            juce::Array<juce::var> ins; for (int ch = 0; ch < t.channelCount(); ++ch) ins.add (t.inputOf (ch)); put (x, "inputs", ins);
            ts.add (x);
        }
        put (c, "tracks", ts);
        juce::Array<juce::var> bs;
        juce::StringArray lines; lines.addLines (backupsBox.getText());
        for (auto& l : lines) if (l.trim().isNotEmpty()) bs.add (l.trim());
        put (c, "backups", bs);
        put (c, "preTakeSeconds", juce::jlimit (0, 300, preTake.getText().getIntValue()));
        put (c, "bufferSeconds", juce::jlimit (10, 60, bufferSeconds.getText().getIntValue()));
        put (c, "resume", resume);
    }
    lastSession = c;
    link.send (c);
}

void RecorderPanel::registerTake (const juce::var& e)
{
    if (project == nullptr) { addLog ("Take " + e["take"].toString() + " merged, but no project is open here to put it in (open the project and it will be put in)"); return; }
    // everything comes with the event: the take window, the rate, the tracks (ids, names, widths), the files, the gaps
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
    if (idx.windowName.isEmpty() || idx.sampleRate <= 0.0 || idx.tracks.empty()) { addLog ("Take " + e["take"].toString() + ": the merge event is incomplete, the take is not put into the project"); return; }
    TakeRec t; t.number = (int) e["take"]; t.label = e["label"].toString(); t.start = (juce::int64) e["take_start"]; t.end = (juce::int64) e["take_end"];
    t.recordedAt = juce::Time::fromISO8601 (e["recordedAt"].toString()); t.merged = true;
    MergeResult res; res.ok = true; res.lengthSamples = (juce::int64) e["lengthSamples"]; res.lostSamples = (juce::int64) e["lostSamples"];
    if (auto* fs = e["files"].getArray()) for (auto& f : *fs) res.files.add (f.toString());
    if (auto* gs = e["gaps"].getArray()) for (auto& g : *gs) res.gaps.push_back ({ (juce::int64) g["first"], (juce::int64) g["samples"], g["time"].toString(), g["why"].toString() });
    auto& w = TakeRegister::window (*project, idx.windowName);
    TakeRegister::add (*project, w, idx, t, res);
    juce::String err;
    if (! project->save (err)) { addLog ("Take " + juce::String (t.number) + " merged but the project could not be saved: " + err); return; }
    sendCommand ("registered", [&] (juce::var& c) { put (c, "number", t.number); });
    registeredTakes.insert (t.number);
    refreshProjectLine();
    addLog ("Take " + juce::String (t.number) + " is in the project (" + juce::String (res.files.size()) + " files, " + timeOf (res.lengthSamples, idx.sampleRate) + (res.gaps.empty() ? ")" : ", " + juce::String (res.gaps.size()) + " gap(s) marked)") + ": open it in fermata");
}

void RecorderPanel::askForUnregistered (const juce::var& status)
{
    // a take merged while this window was away (or before a project was open here): ask for its merge result again, once every few seconds
    if (project == nullptr || ! status["session"].isObject()) return;
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastAskMs < 5000) return;
    if (auto* ts = status["session"]["takes"].getArray())
        for (auto& t : *ts)
            if ((bool) t["merged"] && ! (bool) t["registered"] && registeredTakes.count ((int) t["number"]) == 0)
            {
                lastAskMs = now;
                sendCommand ("take-info", [&] (juce::var& c) { put (c, "number", (int) t["number"]); });
                return;
            }
}

void RecorderPanel::onConnection (bool on)
{
    const bool hadBefore = wasConnected;
    connected = on;
    if (on) wasConnected = true;
    addLog (on ? "Connected to the recorder process" + juce::String (link.startedTheRecorder() ? " (started by this window)" : " (it was already running)") : "The recorder process is not answering - trying again");
    if (on)
    {
        sendCommand ("devices");
        restoreAfterStatus = hadBefore && (deviceOpen || sessionOn);     // (the first status says whether the recorder still has them, or is a new one)
    }
    updateButtons();
}

void RecorderPanel::onEvent (const juce::var& e)
{
    const auto ev = e["ev"].toString();
    if (ev == "meters")
    {
        if (auto* a = e["peak"].getArray())
        {
            if (peaks.size() != (size_t) a->size()) { peaks.assign ((size_t) a->size(), 0.0f); shown.assign ((size_t) a->size(), 0.0f); meters.setSize (juce::jmax (meterView.getWidth(), (int) a->size() * 16), meterView.getHeight()); }
            for (int i = 0; i < a->size(); ++i) peaks[(size_t) i] = (float) (double) (*a)[i];
        }
        return;
    }
    if (ev == "log") { addLog ("[recorder] " + e["text"].toString()); return; }
    if (ev == "hello") { addLog ("Recorder " + e["version"].toString() + (e["version"].toString() == version ? "" : " (NOT this window's version " + version + ")")); return; }
    if (ev == "devices")
    {
        devicesInfo = e;
        typeBox.clear (juce::dontSendNotification);
        if (auto* types = e["types"].getArray()) { int id = 1; for (auto& t : *types) typeBox.addItem (t["type"].toString(), id++); }
        for (int i = 0; i < typeBox.getNumItems(); ++i) if (typeBox.getItemText (i) == "ASIO") typeBox.setSelectedItemIndex (i, juce::dontSendNotification);
        if (typeBox.getSelectedId() == 0 && typeBox.getNumItems() > 0) typeBox.setSelectedItemIndex (0, juce::dontSendNotification);
        fillDeviceBoxes();
        if (rateBox.getNumItems() == 0) { int id = 1; for (auto r : Project::kProjectRates) rateBox.addItem (juce::String ((int) r) + " Hz", id++); rateBox.setSelectedItemIndex (1); }
        if (bufferBox.getNumItems() == 0) { bufferBox.addItem ("Largest the driver offers (safest)", 1); for (int b : { 256, 512, 1024, 2048, 4096 }) bufferBox.addItem (juce::String (b), b); bufferBox.setSelectedId (1); }
        return;
    }
    if (ev == "device")
    {
        deviceOpen = (bool) e["ok"];
        if (deviceOpen)
        {
            deviceRate = (double) e["rate"]; inputNames.clear(); outputNames.clear();
            if (auto* a = e["inputs"].getArray()) for (auto& n : *a) inputNames.add (n.toString());
            if (auto* a = e["outputs"].getArray()) for (auto& n : *a) outputNames.add (n.toString());
            deviceStatus.setText ("Device: " + e["name"].toString() + " (" + e["type"].toString() + ") at " + juce::String (deviceRate, 0) + " Hz, buffer " + e["buffer"].toString() + " samples, "
                                  + juce::String (inputNames.size()) + " inputs, " + juce::String (outputNames.size()) + " outputs, latency " + juce::String ((double) e["latencyMs"], 1) + " ms", juce::dontSendNotification);
            if (inputsToRecord.getText().getIntValue() > inputNames.size() || inputsToRecord.getText().getIntValue() <= 0) inputsToRecord.setText (juce::String (inputNames.size()));
            if (project != nullptr) { project->syncWithDevice (inputNames, outputNames); if (project->sampleRate <= 0.0) project->sampleRate = deviceRate; juce::String err; project->save (err); }
        }
        else deviceStatus.setText ("Device: not open" + (e["error"].toString().isEmpty() ? juce::String (".") : " - " + e["error"].toString()), juce::dontSendNotification);
        updateButtons();
        return;
    }
    if (ev == "session")
    {
        if ((bool) e["ended"]) { sessionOn = false; recordingTake = 0; sessionStatus.setText ("Session: ended.", juce::dontSendNotification); }
        else if ((bool) e["ok"]) { sessionOn = true; addLog ("Session on: chunks in " + e["folder"].toString() + ", buffer " + e["bufferSeconds"].toString() + " s" + ((bool) e["resumed"] ? " (carried on)" : "")); }
        else addLog ("Session not started: " + e["error"].toString());
        updateButtons();
        return;
    }
    if (ev == "take")
    {
        const auto state = e["state"].toString();
        if (state == "refused") addLog ("Record refused: " + e["error"].toString());
        else if (state == "recording") { recordingTake = (int) e["number"]; addLog ("Recording take " + e["number"].toString()); }
        else if (state == "stopped") { recordingTake = 0; lastTakeNumber = (int) e["number"]; addLog ("Take " + e["number"].toString() + " stopped: the merge follows"); }
        updateButtons();
        return;
    }
    if (ev == "drive") { addLog ("DRIVE DROPPED: " + e["folder"].toString() + " - " + e["why"].toString() + (sessionOn ? " (recording continues on the other drives)" : "")); return; }
    if (ev == "gap") { addLog ("GAP on " + juce::File (e["folder"].toString()).getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getFullPathName() + " at " + e["time"].toString() + ": " + e["why"].toString()); return; }
    if (ev == "merge")
    {
        const auto state = e["state"].toString();
        if (state == "progress") mergeStatus.setText ("Merge: take " + e["take"].toString() + " " + juce::String (100.0 * (double) e["fraction"], 0) + "% - " + e["what"].toString(), juce::dontSendNotification);
        else if (state == "done")
        {
            mergeStatus.setText ("Merge: take " + e["take"].toString() + " done.", juce::dontSendNotification);
            if (! (bool) e["backup"]) registerTake (e);
            else addLog ("Backup merged into " + e["outputFolder"].toString());
        }
        else if (state == "failed") { mergeStatus.setText ("Merge of take " + e["take"].toString() + " FAILED: " + e["error"].toString(), juce::dontSendNotification); addLog ("Merge failed: " + e["error"].toString() + " (the chunks are kept)"); }
        else if (state == "refused") addLog ("Merge refused: " + e["error"].toString());
        return;
    }
    if (ev == "refused") { addLog ("The recorder refused \"" + e["cmd"].toString() + "\": " + e["error"].toString()); return; }
    if (ev == "status")
    {
        lastStatus = e;
        if (restoreAfterStatus)
        {   // the connection came back: a recorder that died and was started afresh has no device and no session - they are put back without waiting for anyone
            restoreAfterStatus = false;
            const bool devRunning = e["device"].isObject() && (bool) e["device"]["running"];
            const bool sessionThere = e["session"].isObject();
            if (! devRunning && lastOpen.isObject()) { addLog ("The recorder had to be started again: opening the device again" + juce::String (lastSession.isObject() ? " and carrying on the session" : "")); link.send (lastOpen); if (lastSession.isObject()) { auto c = lastSession; put (c, "resume", true); link.send (c); } deviceOpen = false; sessionOn = false; recordingTake = 0; }
            else if (devRunning && ! sessionThere && sessionOn && lastSession.isObject()) { addLog ("The recorder lost the session: carrying it on"); auto c = lastSession; put (c, "resume", true); link.send (c); sessionOn = false; recordingTake = 0; }
        }
        askForUnregistered (e);
        if (e["session"].isObject())
        {
            auto s = e["session"];
            sessionOn = true; recordingTake = (int) s["recordingTake"]; sessionPosition = (juce::int64) s["position"];
            const double rate = (double) s["rate"];
            sessionStatus.setText ("Session: " + timeOf (sessionPosition, rate) + " on the timeline, pre-take " + s["preTakeSeconds"].toString() + " s, buffer " + s["bufferSeconds"].toString() + " s, chunks of "
                                   + s["chunkSeconds"].toString() + " s. " + juce::File (s["folder"].toString()).getFullPathName(), juce::dontSendNotification);
            juce::String d;
            if (auto* ds = s["drives"].getArray())
                for (auto& x : *ds)
                {
                    d << (d.isEmpty() ? "" : "\n") << (x["role"].toString() == "primary" ? "PRIMARY " : "backup  ");
                    if ((bool) x["dropped"]) d << "DROPPED (" << x["why"].toString() << ") ";
                    else d << "ok  behind " << juce::String ((double) x["lagSeconds"], 1) << " s  longest write " << juce::String ((double) x["longestWriteMs"], 0) << " ms  chunks " << x["chunks"].toString() << "  gaps " << x["gaps"].toString() << "  ";
                    d << juce::File (x["folder"].toString()).getFullPathName();
                    if (x["role"].toString() == "backup" && ! (bool) x["dropped"]) lastBackupFolder = x["folder"].toString();
                }
            drivesStatus.setText (d, juce::dontSendNotification);
            if (recordingTake != 0)
            {
                juce::int64 start = 0; juce::String label;
                if (auto* ts = s["takes"].getArray()) for (auto& t : *ts) if ((int) t["number"] == recordingTake) { start = (juce::int64) t["recordPoint"]; label = t["label"].toString(); }
                takeStatus.setText ("RECORDING take " + juce::String (recordingTake) + (label.isEmpty() ? "" : " - " + label) + "   " + timeOf (sessionPosition - start, rate), juce::dontSendNotification);
                takeStatus.setColour (juce::Label::textColourId, juce::Colour (0xffe05050));
            }
            else { takeStatus.setText ("Armed (pre-take running). Press RECORD.", juce::dontSendNotification); takeStatus.setColour (juce::Label::textColourId, juce::Colour (0xffe0b040)); }
        }
        else { sessionOn = false; recordingTake = 0; takeStatus.setText ("", juce::dontSendNotification); drivesStatus.setText ("", juce::dontSendNotification); if (sessionStatus.getText().startsWith ("Session: 0")) sessionStatus.setText ("Session: none.", juce::dontSendNotification); }
        if (e["device"].isObject() && ! (bool) e["device"]["running"] && deviceOpen) { deviceOpen = false; deviceStatus.setText ("Device: not running.", juce::dontSendNotification); }
        if (e["device"].isObject() && (bool) e["device"]["running"] && ! deviceOpen)
        {   // the recorder was already running with its device when this window connected
            deviceOpen = true; deviceRate = (double) e["device"]["rate"];
            deviceStatus.setText ("Device: " + e["device"]["name"].toString() + " at " + juce::String (deviceRate, 0) + " Hz, buffer " + e["device"]["buffer"].toString() + " (open before this window started)", juce::dontSendNotification);
        }
        if (auto m = e["merge"]; m.isObject() && (bool) m["active"]) mergeStatus.setText ("Merge: take " + m["take"].toString() + " " + juce::String (100.0 * (double) m["fraction"], 0) + "% - " + m["what"].toString(), juce::dontSendNotification);
        updateButtons();
    }
}

void RecorderPanel::updateButtons()
{
    openDev.setEnabled (connected && ! sessionOn); closeDev.setEnabled (connected && deviceOpen && ! sessionOn);
    startBtn.setEnabled (connected && deviceOpen && ! sessionOn && project != nullptr); resumeBtn.setEnabled (connected && deviceOpen && ! sessionOn && project != nullptr);
    recordBtn.setEnabled (connected && sessionOn && recordingTake == 0); stopBtn.setEnabled (connected && recordingTake != 0);
    endBtn.setEnabled (connected && sessionOn && recordingTake == 0); mergeBackupBtn.setEnabled (connected && lastTakeNumber != 0 && recordingTake == 0);
    newProject.setEnabled (! sessionOn); openProject.setEnabled (! sessionOn);
}

void RecorderPanel::timerCallback()
{
    bool changed = false;
    for (size_t i = 0; i < shown.size() && i < peaks.size(); ++i)
    {
        const float target = peaks[i];
        const float next = target > shown[i] ? target : shown[i] * 0.85f;
        if (std::abs (next - shown[i]) > 1.0e-4f) { shown[i] = next; changed = true; }
        peaks[i] = 0.0f;
    }
    if (changed) meters.repaint();
}

void RecorderPanel::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff1b1f23)); }

void RecorderPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    title.setBounds (r.removeFromTop (28)); versionLabel.setBounds (r.removeFromTop (18)); r.removeFromTop (6);
    auto row = r.removeFromTop (26); newProject.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6); openProject.setBounds (row.removeFromLeft (120)); row.removeFromLeft (10); projectLine.setBounds (row);
    r.removeFromTop (4);
    row = r.removeFromTop (26);
    typeBox.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6); deviceBox.setBounds (row.removeFromLeft (300)); row.removeFromLeft (6);
    rateBox.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6); bufferBox.setBounds (row.removeFromLeft (210)); row.removeFromLeft (6);
    openDev.setBounds (row.removeFromLeft (100)); row.removeFromLeft (6); closeDev.setBounds (row.removeFromLeft (100));
    r.removeFromTop (2); deviceStatus.setBounds (r.removeFromTop (20)); r.removeFromTop (4);
    row = r.removeFromTop (78);
    auto left = row.removeFromLeft (520);
    auto l1 = left.removeFromTop (24);
    auto cap = [&] (juce::Label& l, const juce::String& t, juce::Rectangle<int> b) { if (l.getParentComponent() != this) { addAndMakeVisible (l); l.setColour (juce::Label::textColourId, juce::Colour (0xff8f9aa5)); l.setFont (juce::FontOptions (12.0f)); } l.setText (t, juce::dontSendNotification); l.setBounds (b); };
    cap (lWin, "Take window", l1.removeFromLeft (80)); windowName.setBounds (l1.removeFromLeft (150)); l1.removeFromLeft (10); cap (lLabel, "Take label", l1.removeFromLeft (70)); takeLabel.setBounds (l1.removeFromLeft (200));
    left.removeFromTop (3); auto l2 = left.removeFromTop (24);
    cap (lInputs, "Inputs to record", l2.removeFromLeft (100)); inputsToRecord.setBounds (l2.removeFromLeft (50)); l2.removeFromLeft (10);
    cap (lPre, "Pre-take s", l2.removeFromLeft (70)); preTake.setBounds (l2.removeFromLeft (50)); l2.removeFromLeft (10); cap (lBuf, "Buffer s", l2.removeFromLeft (60)); bufferSeconds.setBounds (l2.removeFromLeft (50));
    left.removeFromTop (3); auto l3 = left.removeFromTop (24); startBtn.setBounds (l3.removeFromLeft (180)); l3.removeFromLeft (6); resumeBtn.setBounds (l3.removeFromLeft (160)); l3.removeFromLeft (6); endBtn.setBounds (l3.removeFromLeft (110));
    row.removeFromLeft (10);
    auto bcol = row; auto b1 = bcol.removeFromTop (24); cap (lBackups, "Backup drives (one per line)", b1.removeFromLeft (180)); addBackup.setBounds (b1.removeFromLeft (150)); b1.removeFromLeft (6); clearBackups.setBounds (b1.removeFromLeft (110));
    bcol.removeFromTop (3); backupsBox.setBounds (bcol);
    r.removeFromTop (8);
    row = r.removeFromTop (44); recordBtn.setBounds (row.removeFromLeft (160)); row.removeFromLeft (8); stopBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (14); takeStatus.setBounds (row.removeFromLeft (420)); mergeBackupBtn.setBounds (row.removeFromRight (220).withHeight (26));
    r.removeFromTop (6);
    meterView.setBounds (r.removeFromTop (90)); meters.setSize (juce::jmax (meterView.getWidth(), (int) shown.size() * 16), meterView.getHeight());
    r.removeFromTop (6);
    sessionStatus.setBounds (r.removeFromTop (20)); drivesStatus.setBounds (r.removeFromTop (54)); mergeStatus.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    log.setBounds (r);
}

RecorderWindow::RecorderWindow (const juce::String& version, int port)
    : juce::DocumentWindow ("fermata:Live " + version + " - recorder test bench", juce::Colour (0xff1b1f23), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    panel = new RecorderPanel (version, port);
    setContentOwned (panel, true);
    setResizable (true, false);
    setResizeLimits (900, 600, 4000, 3000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void RecorderWindow::closeButtonPressed()
{
    if (panel != nullptr) panel->windowClosing();
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace td::live
