#include "RecorderService.h"
#include "TestSignalDevice.h"
#include "../core/Project.h"
#include "../ui/ClosingLog.h"

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
/** A driver type's devices, inputs and outputs together (some types list them apart). */
static juce::StringArray namesOf (juce::AudioIODeviceType& t)
{
    auto a = t.getDeviceNames (true);
    if (t.hasSeparateInputsAndOutputs()) for (auto& n : t.getDeviceNames (false)) a.addIfNotAlreadyThere (n);
    return a;
}
static juce::String secondsText (double s)
{
    if (s < 10.0) return juce::String (s, 1) + " s";
    if (s < 120.0) return juce::String (juce::roundToInt (s)) + " s";
    const int m = (int) (s / 60.0);
    return juce::String (m) + " min " + juce::String (juce::roundToInt (s - 60.0 * m)) + " s";
}

/** 0.3.0: told by a driver type when its list of devices changes; the recorder looks at the lists a moment later (not from inside the type's
    own scan) and only notes what came and went - it never changes the device it uses. */
struct RecorderService::ListWatch final : juce::AudioIODeviceType::Listener
{
    explicit ListWatch (RecorderService& o) : owner (o) {}
    void audioDeviceListChanged() override
    {
        juce::WeakReference<ListWatch> w (this);
        juce::MessageManager::callAsync ([w] { if (auto* x = w.get()) x->owner.deviceListChanged(); });
    }
    RecorderService& owner;
    JUCE_DECLARE_WEAK_REFERENCEABLE (ListWatch)
};

RecorderService::RecorderService (int port, const juce::String& ver)
    : version (ver), hub ([this] (remote::Hub::Connection& conn, const juce::String& line)
      {
          // a station's request for waveform history is answered here, on its own connection's thread, to it alone (the history can be big, and
          // the other stations have no use for it); everything else goes to the message thread
          if (line.contains ("\"peaks-get\""))
          {
              const auto c = juce::JSON::parse (line);
              if (c["cmd"].toString() == "peaks-get") { answerPeaks (conn, c); return; }
          }
          juce::MessageManager::callAsync ([this, line] { handleLine (line); });
      }),
      controlPort (port)
{
    hub.log = [this] (const juce::String& t) { log (t); };
    streams.log = [this] (const juce::String& t) { juce::MessageManager::callAsync ([this, t] { log (t); }); };
    // the real drivers first, then "Test signal" beside them. (0.3.0: JUCE makes its own list of drivers only while the list is still empty, so
    // the pretend device added first meant it was the only one - on Windows no ASIO, no WASAPI.)
    {
        juce::OwnedArray<juce::AudioIODeviceType> types;
        devices.createAudioDeviceTypes (types);
        for (auto* t : types) devices.addAudioDeviceType (std::unique_ptr<juce::AudioIODeviceType> (t));
        types.clear (false);
    }
    devices.addAudioDeviceType (std::make_unique<TestSignalDeviceType>());
    listWatch = std::make_unique<ListWatch> (*this);
    for (auto* t : devices.getAvailableDeviceTypes())                    // (this scans every type once)
    {
        knownDevices[t->getTypeName()] = namesOf (*t);
        t->addListener (listWatch.get());
    }
    if (! hub.start (port)) log ("Cannot listen on port " + juce::String (port) + " (is another recorder running?)");
    else log ("fermata:Live recorder " + version + " listening on 127.0.0.1:" + juce::String (port));
    if (! streams.start (port + 1)) log ("Cannot listen for playback streams on port " + juce::String (port + 1) + ": the stations will not be heard");
    mergeRunner.startThread (juce::Thread::Priority::low);
    aloneSinceMs = juce::Time::currentTimeMillis();
    startTimer (50);
}

void RecorderService::closeForQuit()
{
    stopTimer();
    endSession();                                                   // (the drives finish their chunks, and the session's index says it ended)
}

RecorderService::~RecorderService()
{
    // 0.3.1: each step into the log with its time (fermata's closing log, src/ui/ClosingLog.h - written only while the application closes).
    // The device is closed here, after the closing safety net has started: a driver that hangs as it is closed cannot keep the recorder for good.
    closeForQuit();
    const bool hadDevice = device != nullptr;
    closeDevice();
    if (hadDevice) closing::step ("the audio device let go");
    mergeCancel = true;
    mergeRunner.stopThread (60000);
    closing::step ("the merge thread stopped");
    for (auto* t : devices.getAvailableDeviceTypes()) t->removeListener (listWatch.get());
    listWatch.reset();
    peakTap.stop();
    closing::step ("the waveform thread stopped");
    streams.stop();
    closing::step ("the stations' playback streams closed");
    hub.stop();
    closing::step ("the control port closed");
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
    { juce::Array<juce::var> in, out; for (auto& n : inputNames) in.add (n); for (auto& n : outputNames) out.add (n); put (d, "inputNames", in); put (d, "outputNames", out); }
    put (d, "talkback", monitor.isTalkOpen());
    if (auto* dev = device.get()) { put (d, "name", dev->getName()); put (d, "type", deviceTypeName); put (d, "latencyMs", 1000.0 * (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples()) / juce::jmax (1.0, dev->getCurrentSampleRate())); }
    {   // 0.3.0: the device has given no audio (no session), or the session has had none, for this long - and why, as far as can be told
        const auto now = juce::Time::currentTimeMillis();
        if (hole.on && hole.warned) { put (d, "audioMissing", (now - hole.sinceMs) / 1000.0); put (d, "audioWhy", hole.why); put (d, "audioSession", true); }
        else if (deviceSilentWarned) { put (d, "audioMissing", (now - lastCallbackMs) / 1000.0); put (d, "audioWhy", deviceRunning.load() ? "the driver gives no audio" : "the driver stopped the audio device"); put (d, "audioSession", false); }
    }
    put (e, "device", d);
    if (auto rec = session(); rec != nullptr && rec->isPrepared())
    {
        auto s = obj();
        put (s, "folder", rec->primaryFolder().getFullPathName()); put (s, "position", rec->position()); put (s, "rate", rec->sampleRate());
        put (s, "bufferSeconds", rec->settings().bufferSeconds); put (s, "preTakeSeconds", rec->settings().preTakeSeconds); put (s, "chunkSeconds", rec->settings().chunkSeconds);
        put (s, "recordingTake", rec->recordingTake()); put (s, "project", projectName); put (s, "window", windowName);
        put (s, "origin", rec->timelineOrigin().toISO8601 (true));
        {   // (0.3.0) the tracks, so the stations can find each one's channels in the peaks and its chunk files
            juce::Array<juce::var> tr;
            for (auto& t : rec->tracks())
            {
                auto x = obj(); put (x, "id", t.id.isNull() ? juce::String() : t.id.toString()); put (x, "name", t.name); put (x, "channels", t.channels);
                put (x, "ringFirst", t.ringFirst); put (x, "stem", t.stem);
                juce::Array<juce::var> ins; for (auto i : t.inputs) ins.add (i); put (x, "inputs", ins);
                tr.add (x);
            }
            put (s, "tracks", tr);
        }
        {
            auto pk = obj(); const auto fr = peakTap.fineRange(), cr = peakTap.coarseRange();
            put (pk, "spp", peakTap.samplesPerPoint()); put (pk, "coarse", PeakTap::kCoarse); put (pk, "channels", peakTap.channels());
            put (pk, "fineFirst", fr.first); put (pk, "fineEnd", fr.end); put (pk, "coarseFirst", cr.first); put (pk, "coarseEnd", cr.end);
            put (s, "peaks", pk);
        }
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
            put (x, "writtenTo", juce::jmax<juce::int64> (0, rec->position() - dr.lagSamples));      // (0.3.0: the timeline is on this drive up to here)
            ds.add (x);
        }
        put (s, "drives", ds);
        put (e, "session", s);
    }
    auto m = obj();
    { const std::lock_guard<std::mutex> l (mergeLock); put (m, "active", mergeActive.load()); put (m, "take", mergeTake); put (m, "what", mergeWhat); put (m, "queued", (int) mergeQueue.size()); put (m, "folder", mergeFolder.getFullPathName()); }
    put (m, "fraction", mergeFraction.load());
    put (e, "merge", m);
    {   // (0.3.0) the stations playing through this recorder
        juce::Array<juce::var> st;
        for (auto& i : streams.info())
            if (i.connected) { auto x = obj(); put (x, "name", i.name); put (x, "outL", i.outL); put (x, "outR", i.outR); put (x, "playing", i.playing); put (x, "underruns", (int) i.underruns); st.add (x); }
        put (e, "streams", st);
        put (e, "streamPort", controlPort + 1);
    }
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
        else if (cmd == "open")      { if (recording()) refuse ("a take is recording"); else { keepRunning(); openDevice (c); } }
        else if (cmd == "close-device") { if (recording()) refuse ("a take is recording"); else closeDevice(); }
        else if (cmd == "session")   { keepRunning(); startSession (c); }
        else if (cmd == "end-session") { if (recording()) refuse ("a take is recording: Stop first"); else endSession(); }
        else if (cmd == "record")    record (c);
        else if (cmd == "stop")      stop();
        else if (cmd == "registered") takeRegistered (c);
        else if (cmd == "take-info") takeInfo (c);
        else if (cmd == "merge-backup") mergeBackup (c);
        else if (cmd == "merge-take") mergeAgain (c);
        else if (cmd == "resume-drive") resumeDrive (c);
        else if (cmd == "mix")       setMix (c);
        else if (cmd == "talkback")  setTalkback (c);
        else if (cmd == "flag" || cmd == "flag-note" || cmd == "flag-remove") putFlag (c, cmd);
        else if (cmd == "reply")     reply (c);
        else if (cmd == "fix")       putFix (c);
        else if (cmd == "notes")     sendNotes();
        else if (cmd == "test-signal") testSignal (c);
        else if (cmd == "quit")
        {
            if (recording()) return refuse ("a take is recording");
            if ((bool) c["whenDone"])
            {   // 0.3.1: the window's Close everything - nothing is left running. With no merge to finish the recorder closes now (the session
                // ends; the device is closed as the program closes, under the closing safety net). With one, the session ends and the audio device is
                // let go now - fermata can open it meanwhile - and the recorder closes as soon as the merges have finished (their results sent, so
                // the window can put the takes into the project).
                if (! merging() && mergeEventsUnsent.load() == 0) { endSession(); quitNow ("Quit (Close everything)"); return; }
                const auto name = device != nullptr ? device->getName() : juce::String();
                endSession();
                if (device != nullptr) closeWithGuard ([this] { closeDevice(); });
                if (name.isNotEmpty()) log ("The audio device is let go (" + name + ")");
                quitWhenDone = true; quitReadyMs = 0;
                log ("Closing as soon as the merge has finished");
                return;
            }
            if (merging())   return refuse ("a merge is still running: it finishes first");
            quitNow ("Quit");
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
        knownDevices[t->getTypeName()] = namesOf (*t);                     // (what a later change of the list is measured against)
        auto x = obj(); put (x, "type", t->getTypeName());
        juce::Array<juce::var> names; for (auto& n : t->getDeviceNames (true)) names.add (n);
        put (x, "devices", names);
        types.add (x);
    }
    put (e, "types", types);
    send (e);
}

void RecorderService::deviceListChanged()
{
    juce::StringArray added, gone;
    for (auto* t : devices.getAvailableDeviceTypes())
    {
        const auto now = namesOf (*t);
        auto& before = knownDevices[t->getTypeName()];
        for (auto& n : now)    if (! before.contains (n)) added.add (n + " (" + t->getTypeName() + ")");
        for (auto& n : before) if (! now.contains (n))    gone.add (n + " (" + t->getTypeName() + ")");
        before = now;
    }
    if (added.isEmpty() && gone.isEmpty()) return;
    const auto current = device != nullptr ? device->getName() + " (" + deviceTypeName + ")" : juce::String();
    const bool currentGone = current.isNotEmpty() && gone.contains (current);
    if (! added.isEmpty())
        log ("Audio device plugged in: " + added.joinIntoString (", ")
             + (current.isNotEmpty() ? ". The recorder keeps using " + current + " - it never changes device by itself" : juce::String()));
    if (! gone.isEmpty())
        log ("Audio device gone: " + gone.joinIntoString (", ") + (currentGone ? " - THE ONE IN USE (the recorder does not open another one in its place)" : ""));
    auto e = obj(); put (e, "ev", "device-list");
    { juce::Array<juce::var> a, g; for (auto& n : added) a.add (n); for (auto& n : gone) g.add (n); put (e, "added", a); put (e, "gone", g); }
    put (e, "current", current); put (e, "currentGone", currentGone);
    send (e);
    sendDevices();                                                      // (the window's list of devices, up to date)
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
    found->scanForDevices();
    knownDevices[type] = namesOf (*found);
    // 0.3.0: made, opened and started here by the recorder itself, and only ever closed by it - not through JUCE's device manager, which closed
    // a device that left its driver's list and opened another in its place by itself. (One initialisation of the driver: it is made, its
    // channels and buffer sizes are read, and it is opened.)
    std::unique_ptr<juce::AudioIODevice> dev (found->createDevice (name, name));
    if (dev == nullptr) return fail ("no device called " + name);
    const auto ins = dev->getInputChannelNames(), outs = dev->getOutputChannelNames();
    // the safest buffering the driver allows: the largest buffer it offers, unless one was asked for
    int useBuffer = buffer;
    if (useBuffer <= 0) for (auto b : dev->getAvailableBufferSizes()) useBuffer = juce::jmax (useBuffer, b);
    juce::BigInteger inMask, outMask;
    inMask.setRange (0, juce::jmax (1, ins.size()), true); outMask.setRange (0, juce::jmax (1, outs.size()), true);
    ownStartStop = true;
    const auto err = dev->open (inMask, outMask, rate, useBuffer);
    ownStartStop = false;
    if (err.isNotEmpty()) return fail (err);
    if (! dev->isOpen()) return fail ("the driver did not open the device" + (dev->getLastError().isNotEmpty() ? ": " + dev->getLastError() : juce::String()));
    if (const double got = dev->getCurrentSampleRate(); std::abs (got - rate) > 0.5)
    {
        ownStartStop = true; dev->close(); ownStartStop = false;
        return fail ("the driver stayed at " + juce::String (got, 0) + " Hz (set it in its own panel)");
    }
    inputNames = dev->getInputChannelNames(); outputNames = dev->getOutputChannelNames();
    deviceRate = dev->getCurrentSampleRate(); deviceBuffer = dev->getCurrentBufferSizeSamples();
    rateNow = deviceRate;
    peaks = std::vector<std::atomic<float>> ((size_t) inputNames.size());
    for (auto& p : peaks) p.store (0.0f);
    outPeaks = std::vector<std::atomic<float>> ((size_t) outputNames.size());
    for (auto& p : outPeaks) p.store (0.0f);
    streams.setSampleRate (deviceRate); streams.setBlockSize (deviceBuffer);
    device = std::move (dev); deviceTypeName = type;
    lastCallbackMs = juce::Time::currentTimeMillis(); lastCallbacks = callbacks.load(); deviceSilentWarned = false;
    ownStartStop = true;
    device->start (this);
    ownStartStop = false;
    auto e = obj(); put (e, "ev", "device"); put (e, "ok", true); put (e, "type", type); put (e, "name", name); put (e, "rate", deviceRate); put (e, "buffer", deviceBuffer);
    juce::Array<juce::var> in, out; for (auto& n : inputNames) in.add (n); for (auto& n : outputNames) out.add (n);
    put (e, "inputs", in); put (e, "outputs", out);
    put (e, "latencyMs", 1000.0 * (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()) / deviceRate);
    send (e);
    log ("Device " + name + " (" + type + ") open: " + juce::String (inputNames.size()) + " inputs, " + juce::String (outputNames.size()) + " outputs, " + juce::String (deviceRate, 0) + " Hz, buffer " + juce::String (deviceBuffer));
}

void RecorderService::closeDevice()
{
    if (session() != nullptr) endSession();
    if (device != nullptr)
    {
        ownStartStop = true;
        device->stop();
        device->close();
        device.reset();
        ownStartStop = false;
    }
    deviceTypeName.clear();
    deviceSilentWarned = false;
    deviceRunning = false;
    monitor.setAudioRunning (false);
    monitor.clear();                                                // (the next device's channels may be others: the window sends its mix again)
    inputNames.clear(); outputNames.clear(); deviceRate = 0.0; deviceBuffer = 0;
}

void RecorderService::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    const double rate = dev->getCurrentSampleRate(); const int block = dev->getCurrentBufferSizeSamples();
    rateNow = rate;
    // 0.3.0: a device the driver started again at another rate is never fed into the session (the session's files are at its own rate)
    const auto* r = live.load (std::memory_order_acquire);
    rateWrong = r != nullptr && std::abs (rate - r->sampleRate()) > 0.5;
    if (rateWrong.load()) { wrongRate = rate; rateWasWrong = true; }
    deviceRunning = true;
    monitor.setAudioRunning (true);
    streams.setSampleRate (rate); streams.setBlockSize (block);
    const bool byDriver = ! ownStartStop.load();
    const bool wrong = rateWrong.load();
    const double sessionRate = r != nullptr ? r->sampleRate() : 0.0;
    const auto text = (byDriver ? "The driver started the audio device again by itself: " : "Device started: ") + dev->getName() + " at " + juce::String (rate, 0) + " Hz, "
                      + juce::String (block) + " samples"
                      + (wrong ? " - NOT the session's " + juce::String (sessionRate, 0) + " Hz: nothing is recorded until it runs at that rate again" : juce::String());
    juce::MessageManager::callAsync ([this, text, rate, block, byDriver]
    {
        if (byDriver && device != nullptr) { deviceRate = rate; deviceBuffer = block; }     // (what it runs at now, for the status and a next session)
        log (text);
    });
}
void RecorderService::audioDeviceStopped()
{
    deviceRunning = false;
    monitor.setAudioRunning (false);
    const bool byDriver = ! ownStartStop.load();
    if (byDriver) driverStops.fetch_add (1);
    juce::MessageManager::callAsync ([this, byDriver]
    {
        log (byDriver ? "The driver stopped the audio device by itself" + juce::String (session() != nullptr ? ": the session gets no audio until it starts again" : "")
                      : juce::String ("Device stopped"));
    });
}
void RecorderService::audioDeviceError (const juce::String& message)
{
    juce::MessageManager::callAsync ([this, message] { log ("Device error: " + message); });
}

void RecorderService::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int n, const juce::AudioIODeviceCallbackContext&)
{
    static thread_local bool named = false;                         // 0.3.1: the driver's thread named for the log, once (nothing allocated), as fermata's
    if (! named) { named = true; crashreport::nameThisThreadW (L"fermata:Live audio callback (the driver's thread)"); }
    for (int o = 0; o < numOutputs; ++o) if (outputs[o] != nullptr) juce::FloatVectorOperations::clear (outputs[o], n);
    callbacks.fetch_add (1, std::memory_order_relaxed);
    // first the recording: the block goes into the ring before anything else is done with it (the monitoring below only reads the inputs);
    // never a block at a rate other than the session's (0.3.0: the driver may start the device again at another one)
    if (auto* r = live.load (std::memory_order_acquire); r != nullptr && ! rateWrong.load (std::memory_order_relaxed))
    {
        // (0.3.0) a stretch with no audio for the session before this block: noted for the timer (where, how long) - it gets a flag
        const auto t = juce::Time::getHighResolutionTicks();
        if (const int ser = sessionSerial.load (std::memory_order_acquire); ser != fedSerial) { fedSerial = ser; lastFedTicks = 0; }
        if (lastFedTicks != 0)
        {
            const double blockMs = 1000.0 * n / juce::jmax (1.0, r->sampleRate());
            const double gapMs = 1000.0 * juce::Time::highResolutionTicksToSeconds (t - lastFedTicks) - blockMs;
            if (gapMs > juce::jmax (500.0, 4.0 * blockMs))
            {
                holePos.store (r->position(), std::memory_order_relaxed);
                holeMs.store ((juce::int64) gapMs, std::memory_order_relaxed);
                holeSeq.fetch_add (1, std::memory_order_release);
            }
        }
        lastFedTicks = t;
        r->process (inputs, numInputs, n);
        fed.fetch_add (1, std::memory_order_relaxed);
    }
    const int m = juce::jmin (numInputs, (int) peaks.size());
    for (int i = 0; i < m; ++i)
        if (inputs[i] != nullptr)
        {
            const auto mm = juce::FloatVectorOperations::findMinAndMax (inputs[i], n);
            const float pk = juce::jmax (std::abs (mm.getStart()), std::abs (mm.getEnd()));
            if (pk > peaks[(size_t) i].load (std::memory_order_relaxed)) peaks[(size_t) i].store (pk, std::memory_order_relaxed);
        }
    // (0.3.0) then what is heard: the live monitoring and the talkback from the inputs, and the stations' playback
    monitor.process (inputs, numInputs, outputs, numOutputs, n);
    streams.mix (outputs, numOutputs, n);
    streams.blockDone();
    const int mo = juce::jmin (numOutputs, (int) outPeaks.size());
    for (int o = 0; o < mo; ++o)
        if (outputs[o] != nullptr)
        {
            const auto mm = juce::FloatVectorOperations::findMinAndMax (outputs[o], n);
            const float pk = juce::jmax (std::abs (mm.getStart()), std::abs (mm.getEnd()));
            if (pk > outPeaks[(size_t) o].load (std::memory_order_relaxed)) outPeaks[(size_t) o].store (pk, std::memory_order_relaxed);
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
    rateWrong = false; rateWasWrong = false;                        // (the session's rate is the device's)
    hole = {}; lastFed = fed.load(); lastFedMs = juce::Time::currentTimeMillis();
    holeSeqSeen = holeSeq.load(); stopsSeen = driverStops.load();
    sessionSerial.fetch_add (1, std::memory_order_release);         // (before the session is fed: the audio thread measures it afresh)
    live.store (r.get(), std::memory_order_release);
    peakTap.start (r);                                              // (0.3.0: the waveform for the stations, from the ring like a drive)
    auto e = obj(); put (e, "ev", "session"); put (e, "ok", true); put (e, "folder", r->primaryFolder().getFullPathName());
    put (e, "bufferSeconds", r->settings().bufferSeconds); put (e, "resumed", s.resume); put (e, "recordingTake", r->recordingTake());
    put (e, "endedAtCut", r->takeEndedWhenCarriedOn());                 // (0.3.0: a take the long stop ended - it is merged now, below)
    { int last = 0; for (auto& t : r->takesSoFar()) last = juce::jmax (last, t.number); put (e, "lastTake", last); }   // (a carried-on session's takes: the next number is past them)
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
    rateWrong = false; hole = {};
    // a callback that was inside process() has left it once the next block has run (or the device has stopped): wait for that, at most 300 ms
    const int seen = callbacks.load();
    for (int i = 0; i < 30 && deviceRunning.load() && callbacks.load() == seen; ++i) juce::Thread::sleep (10);
    juce::Thread::sleep (5);
    peakTap.stop();
    rec->close();                                                   // (the merge thread may still hold the session: it stays valid, closed)
    if (rec->leftWritersBehind())
    {   // a drive's writer is stuck inside a disk call and still uses this session's memory: the session object is kept for good (a small leak)
        static auto* keptForGood = new std::vector<std::shared_ptr<LiveRecorder>>();
        keptForGood->push_back (rec);
    }
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
    const juce::File folder (fullPath (c["folder"].toString()) ? c["folder"].toString() : juce::String());
    if (rec != nullptr && (folder == juce::File() || folder == rec->primaryFolder()))
    {
        for (auto t : rec->takesSoFar())
            if (t.number == (int) c["number"]) { t.registered = true; rec->updateTake (t); sendTake (t, "registered"); }
        return;
    }
    // 0.3.0: a take of a session that has ended (merged after it, put into the project when the window came back): its own index says so, so it is
    // not asked for again - that session has no writer any more, so the index is written here (as the merge of a backup after its session is)
    if (folder == juce::File()) return;
    if (merging()) return;                                          // (a merge may be writing that index: the window asks again next time)
    SessionIndex idx;
    if (! idx.load (folder.getChildFile (kIndexFileName)) || idx.state != "closed") return;
    if (auto* t = idx.findTake ((int) c["number"]); t != nullptr && t->merged && ! t->registered)
    {
        t->registered = true;
        if (idx.save (folder.getChildFile (kIndexFileName))) log ("Take " + juce::String (t->number) + " of the ended session " + folder.getFileName() + " is in the project");
    }
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
    // 0.3.0: the session's flags and fix versions go with it, so the window can put them in with the take even when the session has ended meanwhile
    juce::Array<juce::var> fl, fx;
    for (auto& f : idx.flags) fl.add (f.toVar());
    for (auto& v : idx.fixes) fx.add (v);
    put (e, "flags", fl); put (e, "fixes", fx);
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
    // (0.3.1: counted until sent - closing waits for the last of them, so the window hears that its take is merged before the recorder goes)
    auto post = [this] (juce::var e) { ++mergeEventsUnsent; juce::MessageManager::callAsync ([this, e] { send (e); --mergeEventsUnsent; }); };
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
    { SessionIndex now; bool live = false; if (rec != nullptr && rec->driveIndex (job.sessionFolder, now, live)) { idx.flags = now.flags; idx.fixes = now.fixes; } }   // (the notes as they are now: the merge took a while)
    post (mergeDoneEvent (t, idx, job.sessionFolder, job.outputFolder, job.backup));
    logger (t.merged ? "Take " + juce::String (t.number) + " merged: " + juce::String (t.mergedFiles.size()) + " files" + (job.backup ? " (backup)" : "")
                     : "Take " + juce::String (t.number) + " NOT merged: " + t.mergeError);
}

// ---- 0.3.0: the monitoring, the flags and fix versions, the waveform peaks ------------------------------------------------------------------------
void RecorderService::setMix (const juce::var& c)
{
    // {"routes": [[in, out, gain], ...], "talk": [[in, out, gain], ...]}: everything the Primary should hear from the inputs (gains linear)
    std::map<MonitorMix::Key, float> want;
    auto read = [&want] (const juce::var& list, int group)
    {
        if (auto* a = list.getArray())
            for (auto& r : *a)
                if (auto* x = r.getArray(); x != nullptr && x->size() >= 3)
                {
                    const int in = (int) (*x)[0], out = (int) (*x)[1];
                    const float g = (float) (double) (*x)[2];
                    if (in < 0 || in >= 4096 || out < 0 || out >= 4096 || ! std::isfinite (g) || g == 0.0f) continue;
                    want[{ group, { in, out } }] = juce::jlimit (-16.0f, 16.0f, g);            // (0.3.2: negative = the strip's polarity key)
                }
    };
    read (c["routes"], 0);
    read (c["talk"], 1);
    monitor.setRoutes (std::move (want));
}

void RecorderService::setTalkback (const juce::var& c)
{
    const bool open = (bool) c["open"];
    if (open != monitor.isTalkOpen()) log (open ? "Talkback open" : "Talkback closed");
    monitor.setTalkOpen (open);
    auto e = obj(); put (e, "ev", "talkback"); put (e, "open", open); send (e);
}

void RecorderService::putFlag (const juce::var& c, const juce::String& cmd)
{
    auto refuse = [&] (const juce::String& why) { auto e = obj(); put (e, "ev", "refused"); put (e, "cmd", cmd); put (e, "error", why); put (e, "id", c["id"].toString()); send (e); };
    auto rec = session();
    if (rec == nullptr) return refuse ("a flag needs a session (the recording running)");
    const auto id = c["id"].toString();
    if (id.isEmpty()) return refuse ("a flag needs an id");
    FlagRec f; bool known = false;
    for (auto& x : rec->flags()) if (x.id == id) { f = x; known = true; }
    if (cmd == "flag")
    {
        if (! known)
        {
            f.id = id;
            // where: the moment the Primary heard (the sound they flagged left the inputs a device latency ago), unless the station says where
            juce::int64 lat = 0;
            if (auto* dev = device.get()) lat = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
            f.sample = c.hasProperty ("sample") ? juce::jmax ((juce::int64) 0, (juce::int64) c["sample"]) : juce::jmax ((juce::int64) 0, rec->position() - lat);
            f.pinnedAt = juce::Time::getCurrentTime();
            f.by = c["by"].toString().isNotEmpty() ? c["by"].toString() : juce::String ("Primary");
        }
        if (c.hasProperty ("note")) f.note = c["note"].toString();
    }
    else if (! known) return refuse ("no such flag");
    else if (cmd == "flag-note") f.note = c["note"].toString();
    else if (cmd == "flag-remove") f.removed = true;
    rec->putFlag (f);
    auto e = obj(); put (e, "ev", "flag"); put (e, "flag", f.toVar()); send (e);
}

void RecorderService::reply (const juce::var& c)
{
    auto rec = session();
    if (rec == nullptr) { auto e = obj(); put (e, "ev", "refused"); put (e, "cmd", "reply"); put (e, "error", "no session"); send (e); return; }
    ReplyRec r; r.text = c["text"].toString(); r.by = c["by"].toString().isNotEmpty() ? c["by"].toString() : juce::String ("Fixer"); r.at = juce::Time::getCurrentTime();
    FlagRec updated;
    if (r.text.trim().isEmpty() || ! rec->addReply (c["flag"].toString(), r, updated))
    {
        auto e = obj(); put (e, "ev", "refused"); put (e, "cmd", "reply"); put (e, "error", "no such flag, or no text"); send (e); return;
    }
    auto e = obj(); put (e, "ev", "flag"); put (e, "flag", updated.toVar()); send (e);
}

void RecorderService::putFix (const juce::var& c)
{
    auto rec = session();
    const auto fix = c["fix"];
    if (rec == nullptr || ! fix.isObject() || fix["id"].toString().isEmpty())
    {
        auto e = obj(); put (e, "ev", "refused"); put (e, "cmd", "fix"); put (e, "error", rec == nullptr ? "no session" : "not a fix version"); send (e); return;
    }
    rec->putFix (fix);
    log ("Fix version " + fix["version"].toString() + " of take " + fix["takeNumber"].toString() + " from " + fix["by"].toString() + ": " + fix["what"].toString());
    auto e = obj(); put (e, "ev", "fix"); put (e, "fix", fix); send (e);
}

void RecorderService::sendNotes()
{
    auto e = obj(); put (e, "ev", "notes");
    juce::Array<juce::var> fl, fx;
    if (auto rec = session())
    {
        for (auto& f : rec->flags()) fl.add (f.toVar());
        for (auto& f : rec->fixes()) fx.add (f);
        put (e, "folder", rec->primaryFolder().getFullPathName());
    }
    put (e, "flags", fl); put (e, "fixes", fx);
    send (e);
}

static juce::var peaksVar (const std::vector<std::vector<juce::int8>>& data)
{
    juce::Array<juce::var> ch;
    for (auto& d : data) ch.add (juce::Base64::toBase64 (d.data(), d.size()));
    return ch;
}

void RecorderService::sendPeaks()
{
    if (! peakTap.isRunning() || hub.connections() == 0) return;
    juce::int64 first = 0; int n = 0;
    std::vector<std::vector<juce::int8>> data;
    for (int i = 0; i < 4 && peakTap.takeNew (first, n, data); ++i)
    {
        auto e = obj(); put (e, "ev", "peaks"); put (e, "first", first); put (e, "n", n); put (e, "spp", peakTap.samplesPerPoint());
        put (e, "ch", peaksVar (data));
        send (e);
    }
}

void RecorderService::answerPeaks (remote::Hub::Connection& conn, const juce::var& c)
{
    // {"cmd": "peaks-get", "id": n, "level": "fine" | "coarse", "from": point, "count": points}: on the station's connection thread
    const bool coarseLevel = c["level"].toString() == "coarse";
    const int chans = juce::jmax (1, peakTap.channels());
    const int most = juce::jmax (16, (512 * 1024) / (2 * chans));            // (a reply of at most about 0.7 MB)
    const int count = juce::jlimit (0, most, (int) c["count"]);
    const juce::int64 from = juce::jmax ((juce::int64) 0, (juce::int64) c["from"]);
    std::vector<std::vector<juce::int8>> data;
    peakTap.get (coarseLevel, from, count, data);
    auto e = obj(); put (e, "ev", "peaks-data"); put (e, "id", c["id"]); put (e, "level", coarseLevel ? "coarse" : "fine");
    put (e, "from", from); put (e, "count", count); put (e, "spp", peakTap.samplesPerPoint());
    put (e, "ch", peaksVar (data));
    std::string bytes = juce::JSON::toString (e, true).toStdString();
    for (auto& ch : bytes) if (ch == '\n' || ch == '\r') ch = ' ';
    bytes += '\n';
    conn.queue (bytes);
}

void RecorderService::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    if (hub.takeNewConnection()) { keepRunning(); send (statusEvent()); }      // (0.3.1: a window that connects wants this recorder: no Close everything)
    monitor.service();
    if (deviceRunning.load() && now - lastMeterMs >= 50 && hub.connections() > 0)
    {
        lastMeterMs = now;
        auto e = obj(); put (e, "ev", "meters");
        juce::Array<juce::var> pk, op;
        for (auto& p : peaks) pk.add ((double) p.exchange (0.0f));
        for (auto& p : outPeaks) op.add ((double) p.exchange (0.0f));
        put (e, "peak", pk); put (e, "out", op);
        send (e);
    }
    if (now - lastPeaksMs >= 100) { lastPeaksMs = now; sendPeaks(); }
    watchAudio (now);
    watchClosing (now);
    if (now - lastStatusMs >= 1000)
    {
        lastStatusMs = now;
        if (hub.connections() > 0) send (statusEvent());
    }
}

// ---- closing (0.3.1) ---------------------------------------------------------------------------------------------------------------------------
/** Closing the audio device while the recorder goes on merging (Close everything with a merge to finish): outside the closing safety net, which
    starts only when the recorder closes. A driver that hangs as its device is closed would hold the recorder for good - so if 'close' has not
    returned in 20 s, where every part of the recorder is goes into its log and it ends itself. (The merge it was doing is done again the next time
    fermata:Live opens the project: "merge-take".) Message thread. */
void RecorderService::closeWithGuard (std::function<void()> close)
{
    struct State { std::mutex m; std::condition_variable cv; bool done = false; };
    auto st = std::make_shared<State>();                            // (shared with the guard's thread, which is let go of: it may outlive this object)
    juce::File logFile;
    if (auto* fl = dynamic_cast<juce::FileLogger*> (juce::Logger::getCurrentLogger())) logFile = fl->getLogFile();
    const auto mainId = (unsigned long) (juce::pointer_sized_int) juce::Thread::getCurrentThreadId();
    std::thread ([st, logFile, mainId]
    {
        crashreport::nameThisThreadW (L"fermata:Live device close guard");
        std::unique_lock<std::mutex> lk (st->m);
        if (st->cv.wait_for (lk, std::chrono::seconds (20), [&st] { return st->done; })) return;
        if (logFile != juce::File())
            logFile.appendText ("\nThe audio driver has not let go of the device 20 s after it was asked to (it hangs as it is closed): the recorder ends itself now. "
                                "A take it was merging is merged again the next time fermata:Live opens the project. Where every part of it was (innermost first):\n"
                                + crashreport::describeThreads (mainId, 48, 24) + "\n");
        crashreport::endThisProcess();
    }).detach();
    close();
    { const std::lock_guard<std::mutex> l (st->m); st->done = true; }
    st->cv.notify_all();
}

void RecorderService::mergeAgain (const juce::var& c)
{
    // 0.3.1: a take of an ENDED session whose merge never finished - the recorder or the PC stopped while it merged (after Close everything,
    // say). Its index says it ended, so it is not carried on: the window finds the take in the index and asks for it here, and it is merged as
    // the session's own merge would have been, into the project (the result is put in by the window, as any merge's)
    if (! fullPath (c["folder"].toString())) return log ("merge-take: not a full path");
    const juce::File folder (c["folder"].toString());
    const int number = (int) c["number"];
    if (mergeQueuedFor (folder, number)) return log ("Take " + juce::String (number) + " of " + folder.getFileName() + " is being merged already");
    if (auto rec = session(); rec != nullptr && rec->primaryFolder() == folder) return;     // (the running session merges its own takes)
    SessionIndex idx;
    if (! idx.load (folder.getChildFile (kIndexFileName))) return log ("merge-take: no session index in " + folder.getFullPathName());
    const auto* t = idx.findTake (number);
    if (t == nullptr || t->end < 0 || t->merged) return;
    TakeRec take = *t;
    take.registered = false; take.mergeError.clear();
    log ("Take " + juce::String (number) + " of the ended session " + folder.getFileName() + " was never merged (it was cut short): merging it now");
    queueMerge ({ nullptr, folder, number, folder.getParentDirectory().getParentDirectory(), false, take, idx.projectName });
}

void RecorderService::quitNow (const juce::String& why)
{
    if (quitting) return;
    quitting = true;
    log (why);
    if (onQuit) onQuit();
}

void RecorderService::keepRunning()
{
    aloneSinceMs = juce::Time::currentTimeMillis();
    if (! quitWhenDone) return;
    quitWhenDone = false; quitReadyMs = 0;
    log ("Not closing after all: a window wants the recorder again");
}

void RecorderService::watchClosing (juce::int64 now)
{
    if (quitting) return;
    const bool busy = recording() || merging() || mergeEventsUnsent.load() > 0;
    if (quitWhenDone)
    {   // Close everything: once the merges have finished and their results are sent, 1.5 s more (the window puts the take into the project and
        // says so - "registered" - which goes into the session's index here), then the recorder closes
        if (busy || session() != nullptr) quitReadyMs = 0;
        else if (quitReadyMs == 0) quitReadyMs = now;
        else if (now - quitReadyMs >= 1500) { quitNow ("Quit (Close everything: nothing is left running)"); return; }
    }
    // left alone: no window for a minute and nothing running - the window died, or its "quit" never came (never with the pre-take or a take)
    if (hub.connections() > 0 || session() != nullptr || busy) aloneSinceMs = now;
    else if (now - aloneSinceMs >= kAloneQuitMs)
        quitNow ("No fermata:Live window has been connected for " + juce::String (kAloneQuitMs / 1000) + " s, and nothing is recording, merging or running: "
                 "the recorder closes" + juce::String (device != nullptr ? " and lets go of the audio device" : ""));
}

void RecorderService::watchAudio (juce::int64 now)
{
    const int c = callbacks.load (std::memory_order_relaxed), f = fed.load (std::memory_order_relaxed);
    if (c != lastCallbacks) { lastCallbacks = c; lastCallbackMs = now; }
    auto event = [this] (const juce::String& state, double seconds, const juce::String& why, bool inSession, const juce::String& flagId)
    {
        auto e = obj(); put (e, "ev", "audio"); put (e, "state", state); put (e, "seconds", seconds); put (e, "why", why); put (e, "session", inSession);
        if (flagId.isNotEmpty()) put (e, "flag", flagId);
        send (e);
    };
    if (device == nullptr) { hole = {}; deviceSilentWarned = false; lastFed = f; lastFedMs = now; lastCallbackMs = now; return; }
    auto rec = session();
    if (rec == nullptr)
    {   // no session: nothing is recorded, but the meters and what is heard stop - said once it has lasted 2 s, and again when it is back
        hole = {}; lastFed = f; lastFedMs = now;
        const bool silent = now - lastCallbackMs >= 2000;
        if (silent && ! deviceSilentWarned)
        {
            deviceSilentWarned = true;
            const juce::String why = deviceRunning.load() ? "the driver gives no audio" : "the driver stopped the audio device";
            log ("The audio device has given no audio for " + secondsText ((now - lastCallbackMs) / 1000.0) + " (" + why + ")");
            event ("missing", (now - lastCallbackMs) / 1000.0, why, false, {});
        }
        else if (! silent && deviceSilentWarned)
        {
            deviceSilentWarned = false;
            log ("The audio device gives audio again");
            event ("back", 0.0, {}, false, {});
        }
        return;
    }
    deviceSilentWarned = false;
    // why the session gets (or got) no audio, as far as can be told; 'rank': the stronger reason wins while a stretch lasts
    auto reason = [&] (int& rank) -> juce::String
    {
        if (rateWrong.load() || rateWasWrong.load())
        { rank = 3; return "the device ran at " + juce::String (wrongRate.load(), 0) + " Hz, not the session's " + juce::String (rec->sampleRate(), 0) + " Hz"; }
        if (! deviceRunning.load()) { rank = 2; return "the driver stopped the audio device"; }
        if (driverStops.load() != stopsSeen) { rank = 2; return "the driver stopped the audio device and started it again"; }
        rank = 1; return "the driver gave no audio";
    };
    // a stretch without audio that has ended (the audio thread measured it): a flag where it is - so the take shows it, in the Fixer and in fermata
    if (const int seq = holeSeq.load (std::memory_order_acquire); seq != holeSeqSeen)
    {
        holeSeqSeen = seq;
        const double secs = (double) holeMs.load (std::memory_order_relaxed) / 1000.0;
        int rank = 0; auto why = reason (rank);
        if (hole.on && hole.rank > rank) why = hole.why;
        FlagRec fl;
        fl.id = juce::Uuid().toString(); fl.sample = holePos.load (std::memory_order_relaxed); fl.pinnedAt = juce::Time::getCurrentTime(); fl.by = "Recorder";
        fl.note = "NO AUDIO for " + secondsText (secs) + " here (" + why + "): that time is not in the recording.";
        rec->putFlag (fl);
        auto fe = obj(); put (fe, "ev", "flag"); put (fe, "flag", fl.toVar()); send (fe);
        log ("The session had no audio for " + secondsText (secs) + " (" + why + "): a flag marks the place on the timeline");
        event ("back", secs, why, true, fl.id);
        stopsSeen = driverStops.load(); rateWasWrong = rateWrong.load();
        hole = {}; lastFed = f; lastFedMs = now;
        return;
    }
    if (f != lastFed) { lastFed = f; lastFedMs = now; hole = {}; return; }
    // no block for the session since the last look: a stretch without audio once it is longer than a few of the device's blocks (at least half
    // a second) - said once it has lasted 2 s, and shown in the status while it lasts
    const double blockMs = deviceRate > 0.0 && deviceBuffer > 0 ? 1000.0 * deviceBuffer / deviceRate : 50.0;
    if (! hole.on && (double) (now - lastFedMs) > juce::jmax (500.0, 4.0 * blockMs)) { hole.on = true; hole.sinceMs = lastFedMs; }
    if (! hole.on) return;
    {
        int rank = 0; const auto why = reason (rank);
        if (rank >= hole.rank) { hole.rank = rank; hole.why = why; }
    }
    if (! hole.warned && now - hole.sinceMs >= 2000)
    {
        hole.warned = true;
        log ("THE SESSION HAS HAD NO AUDIO FOR " + secondsText ((now - hole.sinceMs) / 1000.0) + " (" + hole.why + "): nothing is being recorded");
        event ("missing", (now - hole.sinceMs) / 1000.0, hole.why, true, {});
    }
}

void RecorderService::testSignal (const juce::var& c)
{
    const auto action = c["action"].toString();
    if (action == "plug" || action == "unplug")
    {
        for (auto* t : devices.getAvailableDeviceTypes())
            if (auto* ts = dynamic_cast<TestSignalDeviceType*> (t)) ts->plugSecond (action == "plug");
        return;
    }
    auto* ts = dynamic_cast<TestSignalDevice*> (device.get());
    if (ts == nullptr) { log ("test-signal " + action + ": only for the Test signal device, and it is not the one open"); return; }
    if (action == "pause")        ts->pauseFor (juce::roundToInt (1000.0 * (double) c["seconds"]));
    else if (action == "restart") ts->restartItself ((double) c["rate"]);
    else if (action == "die")     ts->dieForTests();
    else if (action == "hang-on-close") ts->hangWhenClosed (juce::roundToInt (1000.0 * (double) c["seconds"]));
    else log ("test-signal: no action called " + action);
}
} // namespace td::live
