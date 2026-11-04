#include "RecorderService.h"
#include "TestSignalDevice.h"
#include "../core/Project.h"

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static juce::var gapsVar (const std::vector<GapRec>& gaps)
{
    juce::Array<juce::var> gs;
    for (auto& g : gaps) { auto x = obj(); put (x, "first", g.firstSample); put (x, "samples", g.samples); put (x, "time", g.time); put (x, "why", g.why); gs.add (x); }
    return gs;
}
static bool fullPath (const juce::String& s) { return s.isNotEmpty() && juce::File::isAbsolutePath (s); }

RecorderService::RecorderService (int port, const juce::String& ver)
    : version (ver), hub ([this] (remote::Hub::Connection&, const juce::String& line) { juce::MessageManager::callAsync ([this, line] { handleLine (line); }); })
{
    hub.log = [this] (const juce::String& t) { log (t); };
    devices.addAudioDeviceType (std::make_unique<TestSignalDeviceType>());        // ("Test signal": the pretend device, beside the real drivers)
    if (! hub.start (port)) log ("Cannot listen on port " + juce::String (port) + " (is another recorder running?)");
    else log ("fermata:Live recorder " + version + " listening on 127.0.0.1:" + juce::String (port));
    mergeRunner.startThread (juce::Thread::Priority::low);
    startTimer (50);
}

RecorderService::~RecorderService()
{
    stopTimer();
    mergeCancel = true;
    mergeRunner.stopThread (60000);
    closeDevice();
    hub.stop();
}

void RecorderService::log (const juce::String& text)
{
    juce::Logger::writeToLog ("[recorder] " + text);
    auto e = obj(); put (e, "ev", "log"); put (e, "text", text); put (e, "time", juce::Time::getCurrentTime().formatted ("%H:%M:%S"));
    send (e);
}

void RecorderService::send (const juce::var& event)
{
    hub.broadcast (juce::JSON::toString (event, true));
}

void RecorderService::sendTake (const TakeRec& t, const juce::String& state)
{
    auto e = obj(); put (e, "ev", "take"); put (e, "state", state); put (e, "number", t.number); put (e, "label", t.label);
    put (e, "start", t.start); put (e, "end", t.end); put (e, "recordPoint", t.recordPoint); put (e, "recordedAt", t.recordedAt.toISO8601 (true));
    put (e, "merged", t.merged); put (e, "registered", t.registered);
    juce::Array<juce::var> fs; for (auto& f : t.mergedFiles) fs.add (f); put (e, "mergedFiles", fs);
    send (e);
}

juce::var RecorderService::statusEvent() const
{
    auto e = obj(); put (e, "ev", "status"); put (e, "version", version);
    auto d = obj();
    put (d, "running", deviceRunning.load()); put (d, "rate", deviceRate); put (d, "buffer", deviceBuffer);
    put (d, "inputs", inputNames.size()); put (d, "outputs", outputNames.size()); put (d, "callbacks", callbacks.load());
    if (auto* dev = devices.getCurrentAudioDevice()) { put (d, "name", dev->getName()); put (d, "type", dev->getTypeName()); put (d, "latencyMs", 1000.0 * (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples()) / juce::jmax (1.0, dev->getCurrentSampleRate())); }
    put (e, "device", d);
    if (auto rec = session(); rec != nullptr && rec->isPrepared())
    {
        auto s = obj();
        put (s, "folder", rec->primaryFolder().getFullPathName()); put (s, "position", rec->position()); put (s, "rate", rec->sampleRate());
        put (s, "bufferSeconds", rec->settings().bufferSeconds); put (s, "preTakeSeconds", rec->settings().preTakeSeconds); put (s, "chunkSeconds", rec->settings().chunkSeconds);
        put (s, "recordingTake", rec->recordingTake()); put (s, "project", projectName); put (s, "window", windowName);
        juce::Array<juce::var> ts;
        for (auto& t : rec->takesSoFar())
        {
            auto x = obj(); put (x, "number", t.number); put (x, "label", t.label); put (x, "start", t.start); put (x, "end", t.end); put (x, "recordPoint", t.recordPoint);
            put (x, "merged", t.merged); put (x, "registered", t.registered); put (x, "mergeError", t.mergeError); ts.add (x);
        }
        put (s, "takes", ts);
        juce::Array<juce::var> ds;
        for (auto& dr : rec->drives())
        {
            auto x = obj(); put (x, "folder", dr.folder.getFullPathName()); put (x, "role", dr.role); put (x, "dropped", dr.dropped); put (x, "why", dr.why);
            put (x, "lagSeconds", rec->sampleRate() > 0 ? (double) dr.lagSamples / rec->sampleRate() : 0.0); put (x, "longestWriteMs", dr.longestWriteMs);
            put (x, "stalledSeconds", dr.stalledMs / 1000.0); put (x, "indexFailures", dr.indexFailures);
            put (x, "chunks", dr.chunks); put (x, "gaps", dr.gaps); put (x, "written", dr.written);
            ds.add (x);
        }
        put (s, "drives", ds);
        put (e, "session", s);
    }
    auto m = obj();
    { const std::lock_guard<std::mutex> l (mergeLock); put (m, "active", mergeActive.load()); put (m, "take", mergeTake); put (m, "what", mergeWhat); put (m, "queued", (int) mergeQueue.size()); put (m, "folder", mergeFolder.getFullPathName()); }
    put (m, "fraction", mergeFraction.load());
    put (e, "merge", m);
    return e;
}

void RecorderService::handleLine (const juce::String& line)
{
    const auto c = juce::JSON::parse (line);
    if (! c.isObject()) return;
    const auto cmd = c["cmd"].toString();
    auto refuse = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "refused"); put (e, "cmd", cmd); put (e, "error", why); send (e); log (cmd + " refused: " + why); };
    try
    {
        if (cmd == "hello")          { auto e = obj(); put (e, "ev", "hello"); put (e, "version", version); send (e); send (statusEvent()); }
        else if (cmd == "status")    send (statusEvent());
        else if (cmd == "devices")   sendDevices();
        else if (cmd == "open")      { if (recording()) refuse ("a take is recording"); else openDevice (c); }
        else if (cmd == "close-device") { if (recording()) refuse ("a take is recording"); else closeDevice(); }
        else if (cmd == "session")   startSession (c);
        else if (cmd == "end-session") { if (recording()) refuse ("a take is recording: Stop first"); else endSession(); }
        else if (cmd == "record")    record (c);
        else if (cmd == "stop")      stop();
        else if (cmd == "registered") takeRegistered (c);
        else if (cmd == "take-info") takeInfo (c);
        else if (cmd == "merge-backup") mergeBackup (c);
        else if (cmd == "resume-drive") resumeDrive (c);
        else if (cmd == "quit")
        {
            if (recording()) return refuse ("a take is recording");
            if (merging())   return refuse ("a merge is still running: it finishes first");
            log ("Quit");
            if (onQuit) onQuit();
        }
        else log ("Unknown command: " + cmd);
    }
    catch (const std::exception& e) { log ("Command " + cmd + " failed: " + juce::String (e.what())); }
    catch (...)                     { log ("Command " + cmd + " failed"); }
}

void RecorderService::sendDevices()
{
    auto e = obj(); put (e, "ev", "devices");
    juce::Array<juce::var> types;
    for (auto* t : devices.getAvailableDeviceTypes())
    {
        t->scanForDevices();
        auto x = obj(); put (x, "type", t->getTypeName());
        juce::Array<juce::var> names; for (auto& n : t->getDeviceNames (true)) names.add (n);
        put (x, "devices", names);
        types.add (x);
    }
    put (e, "types", types);
    send (e);
}

void RecorderService::openDevice (const juce::var& c)
{
    closeDevice();
    const auto type = c["type"].toString(), name = c["device"].toString();
    const double rate = (double) c["rate"]; const int buffer = (int) c["buffer"];
    auto fail = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "device"); put (e, "ok", false); put (e, "error", why); send (e); log ("Device not opened: " + why); };
    juce::AudioIODeviceType* found = nullptr;
    for (auto* t : devices.getAvailableDeviceTypes()) if (t->getTypeName() == type) found = t;
    if (found == nullptr) return fail ("no driver type called " + type);
    devices.setCurrentAudioDeviceType (type, true);
    found->scanForDevices();
    // (the device is made once to read its channels and buffer sizes, then opened for real: two initialisations of the driver, which ASIO drivers take)
    std::unique_ptr<juce::AudioIODevice> probe (found->createDevice (name, name));
    if (probe == nullptr) return fail ("no device called " + name);
    const auto ins = probe->getInputChannelNames(), outs = probe->getOutputChannelNames();
    // the safest buffering the driver allows: the largest buffer it offers, unless one was asked for
    int useBuffer = buffer;
    if (useBuffer <= 0) for (auto b : probe->getAvailableBufferSizes()) useBuffer = juce::jmax (useBuffer, b);
    probe.reset();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = name; setup.outputDeviceName = name;
    setup.sampleRate = rate; setup.bufferSize = useBuffer;
    setup.useDefaultInputChannels = false; setup.useDefaultOutputChannels = false;
    setup.inputChannels.setRange (0, juce::jmax (1, ins.size()), true); setup.outputChannels.setRange (0, juce::jmax (1, outs.size()), true);
    const auto err = devices.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty()) return fail (err);
    auto* dev = devices.getCurrentAudioDevice();
    if (dev == nullptr) return fail ("the device did not start");
    if (std::abs (dev->getCurrentSampleRate() - rate) > 0.5) { closeDevice(); return fail ("the driver stayed at " + juce::String (dev->getCurrentSampleRate(), 0) + " Hz (set it in its own panel)"); }
    inputNames = dev->getInputChannelNames(); outputNames = dev->getOutputChannelNames();
    deviceRate = dev->getCurrentSampleRate(); deviceBuffer = dev->getCurrentBufferSizeSamples();
    peaks = std::vector<std::atomic<float>> ((size_t) inputNames.size());
    for (auto& p : peaks) p.store (0.0f);
    devices.addAudioCallback (this);
    auto e = obj(); put (e, "ev", "device"); put (e, "ok", true); put (e, "type", type); put (e, "name", name); put (e, "rate", deviceRate); put (e, "buffer", deviceBuffer);
    juce::Array<juce::var> in, out; for (auto& n : inputNames) in.add (n); for (auto& n : outputNames) out.add (n);
    put (e, "inputs", in); put (e, "outputs", out);
    put (e, "latencyMs", 1000.0 * (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples()) / deviceRate);
    send (e);
    log ("Device " + name + " (" + type + ") open: " + juce::String (inputNames.size()) + " inputs, " + juce::String (outputNames.size()) + " outputs, " + juce::String (deviceRate, 0) + " Hz, buffer " + juce::String (deviceBuffer));
}

void RecorderService::closeDevice()
{
    if (session() != nullptr) endSession();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    deviceRunning = false;
    inputNames.clear(); outputNames.clear(); deviceRate = 0.0; deviceBuffer = 0;
}

void RecorderService::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    deviceRunning = true;
    const auto text = "Device started: " + dev->getName() + " at " + juce::String (dev->getCurrentSampleRate(), 0) + " Hz, " + juce::String (dev->getCurrentBufferSizeSamples()) + " samples";
    juce::MessageManager::callAsync ([this, text] { log (text); });
}
void RecorderService::audioDeviceStopped()
{
    deviceRunning = false;
    juce::MessageManager::callAsync ([this] { log ("Device stopped"); });
}
void RecorderService::audioDeviceError (const juce::String& message)
{
    juce::MessageManager::callAsync ([this, message] { log ("Device error: " + message); });
}

void RecorderService::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int o = 0; o < numOutputs; ++o) if (outputs[o] != nullptr) juce::FloatVectorOperations::clear (outputs[o], n);
    callbacks.fetch_add (1, std::memory_order_relaxed);
    if (auto* r = live.load (std::memory_order_acquire)) r->process (inputs, numInputs, n);
    const int m = juce::jmin (numInputs, (int) peaks.size());
    for (int i = 0; i < m; ++i)
        if (inputs[i] != nullptr)
        {
            const auto mm = juce::FloatVectorOperations::findMinAndMax (inputs[i], n);
            const float pk = juce::jmax (std::abs (mm.getStart()), std::abs (mm.getEnd()));
            if (pk > peaks[(size_t) i].load (std::memory_order_relaxed)) peaks[(size_t) i].store (pk, std::memory_order_relaxed);
        }
}

void RecorderService::startSession (const juce::var& c)
{
    auto fail = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "session"); put (e, "ok", false); put (e, "error", why); send (e); log ("Session not started: " + why); };
    if (session() != nullptr) return fail ("a session is already on");
    if (! deviceRunning.load()) return fail ("the audio device is not running");
    LiveRecorder::Setup s;
    s.projectName = c["projectName"].toString(); s.windowName = c["window"].toString();
    if (! fullPath (c["projectFolder"].toString())) return fail ("the project folder is not a full path: " + c["projectFolder"].toString());
    s.projectFolder = juce::File (c["projectFolder"].toString());
    if (s.windowName.isEmpty()) s.windowName = "Live";
    if (auto* a = c["tracks"].getArray())
        for (auto& t : *a)
        {
            TrackSpec ts;
            const auto id = t["id"].toString(); ts.id = id.isEmpty() ? juce::Uuid::null() : juce::Uuid (id);
            ts.name = t["name"].toString(); ts.channels = juce::jlimit (1, (int) kMaxTrackChannels, (int) t["channels"]);
            if (auto* ins = t["inputs"].getArray()) for (auto& i : *ins) ts.inputs.push_back ((int) i);
            s.tracks.push_back (ts);
        }
    if (auto* b = c["backups"].getArray())
        for (auto& x : *b)
        {
            if (! fullPath (x.toString())) return fail ("a backup drive's folder is not a full path: " + x.toString());
            s.backupRoots.emplace_back (x.toString());
        }
    s.sampleRate = deviceRate;
    s.bufferSeconds = c.hasProperty ("bufferSeconds") ? (int) c["bufferSeconds"] : 60;
    s.chunkSeconds = c.hasProperty ("chunkSeconds") ? (int) c["chunkSeconds"] : kChunkSecondsDefault;
    s.preTakeSeconds = c.hasProperty ("preTakeSeconds") ? (int) c["preTakeSeconds"] : 120;
    s.resume = (bool) c["resume"];
    s.programName = "fermata:Live " + version;
    auto r = std::make_shared<LiveRecorder>();
    r->callbacks.log = [this] (const juce::String& t) { juce::MessageManager::callAsync ([this, t] { log (t); }); };
    r->callbacks.driveDropped = [this] (const juce::File& f, const juce::String& why)
    {
        juce::MessageManager::callAsync ([this, f, why] { auto e = obj(); put (e, "ev", "drive"); put (e, "folder", f.getFullPathName()); put (e, "dropped", true); put (e, "why", why); send (e); });
    };
    r->callbacks.gap = [this] (const juce::File& f, const GapRec& g)
    {
        juce::MessageManager::callAsync ([this, f, g] { auto e = obj(); put (e, "ev", "gap"); put (e, "folder", f.getFullPathName()); put (e, "first", g.firstSample); put (e, "samples", g.samples); put (e, "time", g.time); put (e, "why", g.why); send (e); });
    };
    const auto err = r->prepare (s);
    if (err.isNotEmpty()) return fail (err);
    projectName = s.projectName; windowName = s.windowName; projectFolder = s.projectFolder; backupRoots = s.backupRoots; preTakeSeconds = r->settings().preTakeSeconds;
    { const std::lock_guard<std::mutex> l (recorderLock); recorder = r; }
    live.store (r.get(), std::memory_order_release);
    auto e = obj(); put (e, "ev", "session"); put (e, "ok", true); put (e, "folder", r->primaryFolder().getFullPathName());
    put (e, "bufferSeconds", r->settings().bufferSeconds); put (e, "resumed", s.resume); put (e, "recordingTake", r->recordingTake());
    send (e);
    send (statusEvent());
    // a session carried on: its takes that ended but were never merged are merged now (unless a merge for one is already queued)
    if (s.resume)
        for (auto& t : r->takesSoFar())
            if (t.end >= 0 && ! t.merged && ! mergeQueuedFor (r->primaryFolder(), t.number))
                queueMerge ({ r, r->primaryFolder(), t.number, projectFolder.getChildFile ("Recorded Media").getChildFile (sanitiseForFile (windowName)), false, t, projectName });
}

void RecorderService::endSession()
{
    auto rec = session();
    if (rec == nullptr) return;
    if (rec->recordingTake() != 0) stop();
    live.store (nullptr, std::memory_order_release);
    // a callback that was inside process() has left it once the next block has run (or the device has stopped): wait for that, at most 300 ms
    const int seen = callbacks.load();
    for (int i = 0; i < 30 && deviceRunning.load() && callbacks.load() == seen; ++i) juce::Thread::sleep (10);
    juce::Thread::sleep (5);
    rec->close();                                                   // (the merge thread may still hold the session: it stays valid, closed)
    log ("Session ended: " + rec->primaryFolder().getFullPathName() + (merging() ? " (a merge is still running: it finishes in the background)" : ""));
    { const std::lock_guard<std::mutex> l (recorderLock); recorder.reset(); }
    auto e = obj(); put (e, "ev", "session"); put (e, "ok", true); put (e, "ended", true); send (e);
    send (statusEvent());
}

void RecorderService::record (const juce::var& c)
{
    auto fail = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "take"); put (e, "state", "refused"); put (e, "error", why); send (e); log ("Record refused: " + why); };
    auto rec = session();
    if (rec == nullptr) return fail ("no session");
    if (! deviceRunning.load()) return fail ("the audio device is not running");
    const int number = (int) c["number"];
    const auto err = rec->startTake (number > 0 ? number : 1, c["label"].toString());
    if (err.isNotEmpty()) return fail (err);
    for (auto& t : rec->takesSoFar()) if (t.end < 0) sendTake (t, "recording");
}

void RecorderService::stop()
{
    auto rec = session();
    if (rec == nullptr) return;
    const int n = rec->stopTake();
    if (n == 0) return;
    for (auto& t : rec->takesSoFar())
        if (t.number == n)
        {
            sendTake (t, "stopped");
            queueMerge ({ rec, rec->primaryFolder(), n, projectFolder.getChildFile ("Recorded Media").getChildFile (sanitiseForFile (windowName)), false, t, projectName });
        }
}

void RecorderService::takeRegistered (const juce::var& c)
{
    auto rec = session();
    if (rec == nullptr) return;
    for (auto t : rec->takesSoFar())
        if (t.number == (int) c["number"]) { t.registered = true; rec->updateTake (t); sendTake (t, "registered"); }
}

juce::var RecorderService::mergeDoneEvent (const TakeRec& t, const SessionIndex& idx, const juce::File& sessionFolder, const juce::File& outputFolder, bool backup) const
{
    auto e = obj(); put (e, "ev", "merge"); put (e, "state", t.merged ? "done" : "failed"); put (e, "take", t.number); put (e, "backup", backup); put (e, "folder", sessionFolder.getFullPathName());
    put (e, "error", t.mergeError); put (e, "lengthSamples", t.mergedSamples); put (e, "outputFolder", outputFolder.getFullPathName());
    juce::int64 lost = 0; for (auto& g : t.mergeGaps) lost += g.samples;
    put (e, "lostSamples", lost);
    juce::Array<juce::var> fs; for (auto& f : t.mergedFiles) fs.add (f); put (e, "files", fs);
    put (e, "gaps", gapsVar (t.mergeGaps));
    put (e, "take_start", t.start); put (e, "take_end", t.end); put (e, "recordPoint", t.recordPoint); put (e, "label", t.label); put (e, "recordedAt", t.recordedAt.toISO8601 (true));
    // what the window needs to put the take into the project without reading the index itself
    put (e, "window", idx.windowName); put (e, "rate", idx.sampleRate); put (e, "projectName", idx.projectName);
    juce::Array<juce::var> ts;
    for (auto& tr : idx.tracks)
    {
        auto x = obj(); put (x, "id", tr.id.isNull() ? juce::String() : tr.id.toString()); put (x, "name", tr.name); put (x, "channels", tr.channels); put (x, "stem", tr.stem);
        juce::Array<juce::var> ins; for (auto i : tr.inputs) ins.add (i); put (x, "inputs", ins);
        ts.add (x);
    }
    put (e, "tracks", ts);
    return e;
}

void RecorderService::takeInfo (const juce::var& c)
{
    // the window asks for a take's merge result again (it was away when the merge finished): the same "done" event, from the index
    auto rec = session();
    const int number = (int) c["number"];
    juce::File folder (fullPath (c["folder"].toString()) ? c["folder"].toString() : (rec != nullptr ? rec->primaryFolder().getFullPathName() : juce::String()));
    SessionIndex idx; bool liveDrive = false;
    if (rec == nullptr || ! rec->driveIndex (folder, idx, liveDrive))
    {
        if (folder == juce::File() || ! idx.load (folder.getChildFile (kIndexFileName))) { log ("take-info: no session index for " + folder.getFullPathName()); return; }
    }
    if (auto* t = idx.findTake (number))
    {
        if (t->merged) send (mergeDoneEvent (*t, idx, folder, projectFolder != juce::File() ? projectFolder.getChildFile ("Recorded Media").getChildFile (sanitiseForFile (idx.windowName)) : folder.getParentDirectory().getParentDirectory(), idx.role != "primary"));
        else log ("take-info: take " + juce::String (number) + " is not merged" + (t->mergeError.isEmpty() ? juce::String() : " (" + t->mergeError + ")"));
    }
}

bool RecorderService::mergeQueuedFor (const juce::File& sessionFolder, int take) const
{
    const std::lock_guard<std::mutex> l (mergeLock);
    if (mergeActive.load() && mergeFolder == sessionFolder && mergeTake == take) return true;
    for (auto& j : mergeQueue) if (j.sessionFolder == sessionFolder && j.take == take) return true;
    return false;
}

void RecorderService::mergeBackup (const juce::var& c)
{
    auto fail = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "merge"); put (e, "state", "refused"); put (e, "error", why); send (e); };
    if (! fullPath (c["folder"].toString())) return fail ("the backup's session folder is not a full path");
    const juce::File folder (c["folder"].toString());
    const int number = (int) c["number"];
    if (mergeQueuedFor (folder, number)) return fail ("that merge is already queued or running");
    auto rec = session();
    SessionIndex idx; bool liveDrive = false;
    const bool ours = rec != nullptr && rec->driveIndex (folder, idx, liveDrive);
    if (! ours && ! idx.load (folder.getChildFile (kIndexFileName))) return fail ("no session index in " + folder.getFullPathName());
    // the take's span from the recorder (a dropped drive's index may not have its end); the drive's OWN merged state (what it has merged so far)
    TakeRec take; bool have = false;
    if (ours) for (auto& t : rec->takesSoFar()) if (t.number == number) { take = t; have = true; }
    if (! have) { if (auto* t = idx.findTake (number)) { take = *t; have = true; } }
    if (! have || take.end < 0) return fail ("take " + juce::String (number) + " is not a finished take of that session");
    if (auto* own = idx.findTake (number)) { take.merged = own->merged; take.mergedFiles = own->mergedFiles; take.mergeError = own->mergeError; take.mergedSamples = own->mergedSamples; take.mergeGaps = own->mergeGaps; }
    else { take.merged = false; take.mergedFiles.clear(); take.mergeError.clear(); take.mergedSamples = 0; take.mergeGaps.clear(); }
    take.registered = false;
    if (take.merged) return fail ("take " + juce::String (number) + " has already been merged from that drive");
    // the backup's take files go beside its chunks' "Live Chunks" folder: <backup>/<project>/Recorded Media/<window>
    queueMerge ({ ours ? rec : nullptr, folder, number, folder.getParentDirectory().getParentDirectory(), true, take, idx.projectName });
}

void RecorderService::resumeDrive (const juce::var& c)
{
    auto rec = session();
    if (rec == nullptr) return;
    if (! fullPath (c["folder"].toString())) { log ("Drive not taken on: not a full path"); return; }
    const auto err = rec->resumeDrive (juce::File (c["folder"].toString()), c["role"].toString().isEmpty() ? "backup" : c["role"].toString());
    if (err.isNotEmpty()) log ("Drive not taken on: " + err);
    send (statusEvent());
}

void RecorderService::queueMerge (const MergeJob& j)
{
    { std::lock_guard<std::mutex> l (mergeLock); mergeQueue.push_back (j); }
    mergeRunner.notify();
    auto e = obj(); put (e, "ev", "merge"); put (e, "state", "queued"); put (e, "take", j.take); put (e, "backup", j.backup); put (e, "folder", j.sessionFolder.getFullPathName()); send (e);
}

void RecorderService::mergeThreadRun()
{
    crashreport::nameThisThread ("fermata:Live merge");
    while (! mergeRunner.threadShouldExit())
    {
        MergeJob job; bool have = false;
        { std::lock_guard<std::mutex> l (mergeLock); if (! mergeQueue.empty()) { job = mergeQueue.front(); mergeQueue.pop_front(); have = true; mergeTake = job.take; mergeFolder = job.sessionFolder; mergeWhat = "Waiting"; mergeActive = true; } }
        if (! have) { mergeRunner.wait (500); continue; }
        mergeFraction = 0.0;
        try { runMerge (job); }
        catch (...) { juce::MessageManager::callAsync ([this, take = job.take] { log ("The merge of take " + juce::String (take) + " failed with an exception"); }); }
        { std::lock_guard<std::mutex> l (mergeLock); mergeActive = false; mergeTake = 0; mergeFolder = juce::File(); mergeWhat.clear(); }
    }
}

void RecorderService::runMerge (MergeJob job)
{
    // Everything here talks to the session through its own thread-safe calls (no round trips to the message thread): the job holds the session
    // alive (shared_ptr), so it stays valid - closed, at worst - until the merge is over
    auto post = [this] (juce::var e) { juce::MessageManager::callAsync ([this, e] { send (e); }); };
    auto logger = [this] (const juce::String& t) { juce::MessageManager::callAsync ([this, t] { log (t); }); };
    { auto e = obj(); put (e, "ev", "merge"); put (e, "state", "started"); put (e, "take", job.take); put (e, "backup", job.backup); put (e, "folder", job.sessionFolder.getFullPathName()); post (e); }
    const auto rec = job.rec;
    Merge::Job mj;
    mj.sessionFolder = job.sessionFolder; mj.takeNumber = job.take; mj.outputFolder = job.outputFolder; mj.programName = "fermata:Live " + version; mj.projectName = job.projectName;
    mj.take = &job.takeRec;
    // the index as the drive holds it. A live drive (its writer running, not dropped) must have written the whole take before it is merged: wait for
    // that, as long as it takes (a take records meanwhile? the wait goes on; the drive being dropped meanwhile: merge what is there)
    SessionIndex idx; bool liveDrive = false; bool known = false;
    auto refresh = [&]
    {
        if (rec != nullptr && rec->driveIndex (job.sessionFolder, idx, liveDrive)) { known = true; return true; }
        liveDrive = false;
        known = idx.load (job.sessionFolder.getChildFile (kIndexFileName));
        return known;
    };
    if (! refresh())
    {
        auto t = job.takeRec; t.merged = false; t.mergeError = "cannot read the session index in " + job.sessionFolder.getFullPathName();
        post (mergeDoneEvent (t, idx, job.sessionFolder, job.outputFolder, job.backup)); logger ("Merge of take " + juce::String (job.take) + " stopped: " + t.mergeError);
        return;
    }
    bool said = false;
    for (;;)
    {
        if (mergeCancel.load() || mergeRunner.threadShouldExit()) return;
        bool ready = ! liveDrive;
        if (liveDrive)
        {
            ready = idx.lastSample >= job.takeRec.end;
            for (auto& c : idx.chunks) if (c.samples < 0 && c.firstSample < job.takeRec.end) ready = false;
        }
        if (ready) break;
        if (! said) { said = true; { std::lock_guard<std::mutex> l (mergeLock); mergeWhat = "Waiting for the drive to write the end of the take"; } auto e = obj(); put (e, "ev", "merge"); put (e, "state", "progress"); put (e, "take", job.take); put (e, "fraction", 0.0); put (e, "what", "Waiting for the drive to write the end of the take"); post (e); }
        mergeRunner.wait (500);
        refresh();
    }
    mj.index = &idx;
    mj.allowOpenChunks = ! liveDrive;
    auto paused = [this] { return recording(); };
    auto cancelled = [this] { return mergeCancel.load() || mergeRunner.threadShouldExit(); };
    auto progress = [this, post, &job] (double f, const juce::String& what)
    {
        mergeFraction = f;
        { std::lock_guard<std::mutex> l (mergeLock); mergeWhat = what; }
        auto e = obj(); put (e, "ev", "merge"); put (e, "state", "progress"); put (e, "take", job.take); put (e, "fraction", f); put (e, "what", what); post (e);
    };
    auto saveProgress = [&job, rec] (const TakeRec& t)
    {
        if (rec == nullptr) return;
        if (job.backup) rec->updateTakeOnDrive (job.sessionFolder, t); else rec->updateTake (t);
    };
    MergeResult res;
    try { res = Merge::run (mj, paused, cancelled, progress, logger, saveProgress); }
    catch (const std::exception& ex) { res.ok = false; res.error = juce::String ("the merge failed: ") + ex.what(); }
    catch (...) { res.ok = false; res.error = "the merge failed"; }
    auto t = job.takeRec;
    t.merged = res.ok; t.mergeError = res.error;
    t.mergedFiles = res.files;                                      // (also on failure: the tracks already done, so the next run carries on from there)
    if (res.ok) { t.mergedSamples = res.lengthSamples; t.mergeGaps = res.gaps; }
    if (rec != nullptr) { if (job.backup) rec->updateTakeOnDrive (job.sessionFolder, t); else rec->updateTake (t); }
    else
    {   // no session object (a backup merged after the session): the index on the drive gets the result
        if (auto* k = idx.findTake (t.number)) *k = t; else idx.takes.push_back (t);
        idx.save (job.sessionFolder.getChildFile (kIndexFileName));
    }
    post (mergeDoneEvent (t, idx, job.sessionFolder, job.outputFolder, job.backup));
    logger (t.merged ? "Take " + juce::String (t.number) + " merged: " + juce::String (t.mergedFiles.size()) + " files" + (job.backup ? " (backup)" : "")
                     : "Take " + juce::String (t.number) + " NOT merged: " + t.mergeError);
}

void RecorderService::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    if (hub.takeNewConnection()) send (statusEvent());
    if (deviceRunning.load() && now - lastMeterMs >= 50 && hub.connections() > 0)
    {
        lastMeterMs = now;
        auto e = obj(); put (e, "ev", "meters");
        juce::Array<juce::var> pk;
        for (auto& p : peaks) pk.add ((double) p.exchange (0.0f));
        put (e, "peak", pk);
        send (e);
    }
    if (now - lastStatusMs >= 1000)
    {
        lastStatusMs = now;
        if (hub.connections() > 0) send (statusEvent());
    }
}
} // namespace td::live
