#include "StationModel.h"

namespace td::live
{
static std::vector<int> intList (const juce::var& v)
{
    std::vector<int> out;
    if (auto* a = v.getArray()) for (auto& x : *a) out.push_back ((int) x);
    return out;
}

void StationModel::handle (const juce::var& e)
{
    const auto ev = e["ev"].toString();
    bool changed = true;
    if (ev == "hello") recorderVersion = e["version"].toString();
    else if (ev == "status") readStatus (e);
    else if (ev == "audio")                                      // (0.3.0: at once, not at the next status)
    {
        const bool missing = e["state"].toString() == "missing";
        audioMissing = missing ? juce::jmax (0.1, (double) e["seconds"]) : 0.0;
        audioWhy = missing ? e["why"].toString() : juce::String(); audioMissingInSession = missing && (bool) e["session"];
    }
    else if (ev == "meters")
    {
        changed = false;
        auto read = [] (const juce::var& v, std::vector<float>& out)
        {
            out.clear();
            if (auto* a = v.getArray()) for (auto& x : *a) out.push_back ((float) (double) x);
        };
        read (e["peak"], inPeaks); read (e["out"], outPeaks);
    }
    else if (ev == "peaks") { changed = false; readPeaks (e, false); }
    else if (ev == "peaks-data")
    {
        changed = false;
        readPeaks (e, e["level"].toString() == "coarse");
        if ((int) e["id"] == historyId) { pendingHistory = 0; askHistory(); }
    }
    else if (ev == "talkback") talkOpen = (bool) e["open"];
    else if (ev == "session")
    {
        if ((bool) e["ended"]) { sessionOn = false; tracks.clear(); takes.clear(); drives.clear(); flags.clear(); fixes.clear(); recordingTake = 0; }
    }
    else if (ev == "take")
    {
        const int n = (int) e["number"];
        const auto state = e["state"].toString();
        if (n > 0 && state != "refused")
        {
            StationTake* t = nullptr;
            for (auto& k : takes) if (k.number == n) t = &k;
            if (t == nullptr) { takes.push_back ({}); t = &takes.back(); t->number = n; }
            t->label = e["label"].toString(); t->start = (juce::int64) e["start"]; t->end = (juce::int64) e["end"]; t->recordPoint = (juce::int64) e["recordPoint"];
            t->merged = (bool) e["merged"]; t->registered = (bool) e["registered"];
            recordingTake = state == "recording" ? n : (recordingTake == n ? 0 : recordingTake);
        }
    }
    else if (ev == "flag")
    {
        auto f = FlagRec::fromVar (e["flag"]);
        bool found = false;
        for (auto& k : flags) if (k.id == f.id) { k = f; found = true; }
        if (! found && f.id.isNotEmpty()) flags.push_back (f);
    }
    else if (ev == "fix")
    {
        const auto fx = e["fix"]; const auto id = fx["id"].toString();
        bool found = false;
        for (auto& k : fixes) if (k["id"].toString() == id) { k = fx; found = true; }
        if (! found && id.isNotEmpty()) fixes.push_back (fx);
    }
    else if (ev == "notes")
    {
        flags.clear(); fixes.clear();
        if (auto* a = e["flags"].getArray()) for (auto& x : *a) { auto f = FlagRec::fromVar (x); if (f.id.isNotEmpty()) flags.push_back (f); }
        if (auto* a = e["fixes"].getArray()) for (auto& x : *a) fixes.push_back (x);
    }
    else if (ev == "merge")
    {   // 0.3.1: at once, not a second later with the next status - closing asks only while a merge is really going (the status puts the rest right)
        const auto state = e["state"].toString();
        if (state == "started") { mergeActive = true; mergeTake = (int) e["take"]; }
        else if (state == "done" || state == "failed")
        {
            mergeActive = false; mergeTake = 0; mergeFraction = 0.0;
            if (state == "done" && ! (bool) e["backup"] && e["folder"].toString() == sessionFolder)
                for (auto& t : takes) if (t.number == (int) e["take"]) t.merged = true;
        }
    }
    else if (ev == "device" || ev == "drive" || ev == "gap" || ev == "refused" || ev == "log") {}
    else changed = false;
    if (onEvent) onEvent (e);
    if (changed) sendChangeMessage();
}

void StationModel::readStatus (const juce::var& e)
{
    recorderVersion = e["version"].toString();
    const auto d = e["device"];
    deviceRunning = (bool) d["running"]; deviceRate = (double) d["rate"]; deviceBuffer = (int) d["buffer"]; latencyMs = (double) d["latencyMs"];
    deviceName = d["name"].toString(); deviceType = d["type"].toString(); talkOpen = (bool) d["talkback"];
    audioMissing = (double) d["audioMissing"]; audioWhy = d["audioWhy"].toString(); audioMissingInSession = (bool) d["audioSession"];
    inputNames.clear(); outputNames.clear();
    if (auto* a = d["inputNames"].getArray()) for (auto& x : *a) inputNames.add (x.toString());
    if (auto* a = d["outputNames"].getArray()) for (auto& x : *a) outputNames.add (x.toString());
    const auto s = e["session"];
    const bool wasOn = sessionOn;
    sessionOn = s.isObject();
    if (sessionOn)
    {
        const auto folder = s["folder"].toString();
        sessionFolder = folder; projectName = s["project"].toString(); windowName = s["window"].toString();
        position = (juce::int64) s["position"]; positionAtMs = juce::Time::getMillisecondCounterHiRes();
        sessionRate = (double) s["rate"]; recordingTake = (int) s["recordingTake"];
        preTakeSeconds = (int) s["preTakeSeconds"]; bufferSeconds = (int) s["bufferSeconds"]; chunkSeconds = (int) s["chunkSeconds"];
        origin = juce::Time::fromISO8601 (s["origin"].toString());
        tracks.clear();
        if (auto* a = s["tracks"].getArray())
            for (auto& x : *a)
            {
                StationTrack t;
                const auto id = x["id"].toString(); t.id = id.isEmpty() ? juce::Uuid::null() : juce::Uuid (id);
                t.name = x["name"].toString(); t.channels = juce::jmax (1, (int) x["channels"]); t.ringFirst = (int) x["ringFirst"]; t.stem = x["stem"].toString();
                t.inputs = intList (x["inputs"]);
                tracks.push_back (t);
            }
        takes.clear();
        if (auto* a = s["takes"].getArray())
            for (auto& x : *a)
            {
                StationTake t;
                t.number = (int) x["number"]; t.label = x["label"].toString(); t.start = (juce::int64) x["start"]; t.end = (juce::int64) x["end"];
                t.recordPoint = (juce::int64) x["recordPoint"]; t.merged = (bool) x["merged"]; t.registered = (bool) x["registered"]; t.mergeError = x["mergeError"].toString();
                takes.push_back (t);
            }
        drives.clear();
        if (auto* a = s["drives"].getArray())
            for (auto& x : *a)
            {
                StationDrive dr;
                dr.folder = x["folder"].toString(); dr.role = x["role"].toString(); dr.dropped = (bool) x["dropped"]; dr.why = x["why"].toString();
                dr.lagSeconds = (double) x["lagSeconds"]; dr.stalledSeconds = (double) x["stalledSeconds"]; dr.chunks = (int) x["chunks"]; dr.gaps = (int) x["gaps"];
                dr.indexFailures = (int) x["indexFailures"]; dr.writtenTo = (juce::int64) x["writtenTo"];
                drives.push_back (dr);
            }
        const auto pk = s["peaks"];
        if (pk.isObject())
        {
            const int ch = (int) pk["channels"], spp = (int) pk["spp"], k = juce::jmax (1, (int) pk["coarse"]);
            if (peaksSession != folder || peaks.channels() != ch || peaks.samplesPerPoint() != spp)
            {
                peaks.reset (ch, spp, k); peaksSession = folder; historyAsked = false; pendingHistory = 0;
            }
            fineFirst = (juce::int64) pk["fineFirst"]; fineEndKnown = (juce::int64) pk["fineEnd"];
            coarseFirst = (juce::int64) pk["coarseFirst"]; coarseEndKnown = (juce::int64) pk["coarseEnd"];
            if (! historyAsked && pendingHistory == 0) { historyAsked = true; askHistory(); }
        }
        if (! wasOn && send) send (cmd ("notes"));                     // (a session that has just come: its flags)
    }
    else
    {
        tracks.clear(); takes.clear(); drives.clear(); recordingTake = 0;
    }
    const auto m = e["merge"];
    mergeActive = (bool) m["active"]; mergeTake = (int) m["take"]; mergeWhat = m["what"].toString(); mergeQueued = (int) m["queued"]; mergeFraction = (double) m["fraction"];
    streams.clear();
    if (auto* a = e["streams"].getArray())
        for (auto& x : *a) streams.push_back ({ x["name"].toString(), (int) x["outL"], (int) x["outR"], (bool) x["playing"], (int) x["underruns"] });
    streamPort = (int) e["streamPort"];
}

void StationModel::readPeaks (const juce::var& e, bool coarseLevel)
{
    const int n = (int) e[coarseLevel || e.hasProperty ("count") ? "count" : "n"];
    const juce::int64 first = (juce::int64) e[e.hasProperty ("from") ? "from" : "first"];
    if (n <= 0 || peaks.channels() == 0) return;
    std::vector<juce::MemoryBlock> data;
    if (auto* a = e["ch"].getArray())
        for (auto& x : *a)
        {
            juce::MemoryOutputStream mo;
            juce::Base64::convertFromBase64 (mo, x.toString());
            data.push_back (mo.getMemoryBlock());
        }
    if ((int) data.size() < peaks.channels()) return;
    peaks.add (coarseLevel, first, n, data);
}

void StationModel::askHistory()
{
    if (! connected || ! send || pendingHistory != 0 || peaks.channels() == 0) return;
    const int most = juce::jmax (16, (512 * 1024) / (2 * peaks.channels()));
    auto ask = [this, most] (bool coarseLevel, juce::int64 from, juce::int64 count)
    {
        auto c = cmd ("peaks-get");
        c.getDynamicObject()->setProperty ("id", ++historyId);
        c.getDynamicObject()->setProperty ("level", coarseLevel ? "coarse" : "fine");
        c.getDynamicObject()->setProperty ("from", from);
        c.getDynamicObject()->setProperty ("count", (int) juce::jmin<juce::int64> (most, count));
        pendingHistory = historyId;
        send (c);
    };
    // the overview first (the coarse level, oldest first), then the fine level, newest first (the part near the live end fills first)
    const auto c = peaks.firstMissingCoarse (coarseFirst, coarseEndKnown);
    if (c < coarseEndKnown) { ask (true, c, coarseEndKnown - c); return; }
    const auto k = peaks.lastMissingFine (fineFirst, fineEndKnown);
    if (k >= 0) { const juce::int64 from = juce::jmax (fineFirst, k - most + 1); ask (false, from, k + 1 - from); return; }
}
} // namespace td::live
