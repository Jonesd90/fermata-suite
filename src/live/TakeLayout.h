#pragma once
/** fermata:Live 0.3.0: what a station's take window shows - the takes of the window on its timeline (seconds, as fermata's take window has them)
    and where each one's audio is: the project's files (a take recorded before, or merged and put in already), and/or the session's timeline (a
    take of the session that is running: its chunks, and the live waveform). A take the session has recorded but the project does not have yet
    goes where TakeRegister will put it (after the window's last take, 2 s on), so it does not move when it is put in. Built on the message thread
    from the project and the station's model; handed to the player as a whole (shared_ptr).

    0.5.0: the TIME OF DAY. A station can show its timeline as the clock instead of fermata's timeline: every take sits at the time of day it was
    recorded (its first sample: the session's clock, or the BWF time reference in its files), the gaps between takes included, and the timeline runs
    on past midnight into the next day. Seconds on that timeline count from the local midnight of the first take's day (originMs). Everything a
    station does with the layout (the player, the flags, the ReTouch selections, marks 1 and 2) works the same on either timeline, because it goes
    through the takes' 'start'; only what is kept in the project (fermata's timeline: windowStart) is converted (shownOfWindow / windowOfShown). */
#include "StationModel.h"
#include "../core/Project.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <mutex>

namespace td::live
{
struct ShownTake
{
    int          number = 0;
    juce::String label;
    double       start = 0.0;                // on the window's timeline (seconds)
    juce::int64  lengthSamples = 0;
    double       rate = 48000.0;
    bool         session = false;            // its audio is on the session's timeline: sessionStart is its first sample there
    juce::int64  sessionStart = 0, recordPoint = -1;
    bool         recording = false;          // still recording (its length is the session's position now less its start)
    bool         merged = false, inProject = false;
    juce::Uuid   groupId;                    // the project's take, once it is in
    std::vector<TakeFile> files;             // the project's files of the take
    juce::Time   recordedAt;
    double       windowStart = 0.0;          // 0.5.0: where the project's take window (fermata's timeline) has it - the same as 'start' unless the
                                             // layout shows the time of day
    juce::int64  clockMs = 0;                // 0.5.0: the time of day of its first sample (juce::Time milliseconds; 0: not known)
    double       cutAt = -1.0;               // 0.5.0, the time of day: the next take's pre-take reached back over this one's end - this one is shown and
                                             // played only up to there (it is the same recording; < 0: not cut)
    double lengthSeconds() const noexcept   { return rate > 0.0 ? (double) lengthSamples / rate : 0.0; }
    double end() const noexcept             { return start + lengthSeconds(); }
    double shownEnd() const noexcept        { return cutAt >= 0.0 ? juce::jmin (end(), cutAt) : end(); }
    juce::String title (const juce::String& windowName) const
    {
        return juce::String (number).paddedLeft ('0', 3) + " - " + (label.isNotEmpty() ? label : windowName);
    }
};

struct LayoutTrack
{
    juce::Uuid id; juce::String name; int channels = 1; juce::uint32 colour = 0;
    int sessionIndex = -1;                   // its place in the session's track list (-1: the session does not record it)
    int ringFirst = -1;                      // its first channel in the session's peaks (-1: none)
};

struct TakeLayout
{
    juce::String windowName;
    juce::Uuid windowId;
    double rate = 48000.0;
    std::vector<ShownTake> takes;
    std::vector<LayoutTrack> tracks;
    juce::File sessionFolder;
    bool timeOfDay = false;                  // 0.5.0: the timeline is the clock (see the top of this file)
    juce::int64 originMs = 0;                // the time of day: the moment second 0 of the timeline is (the local midnight of the first take's day)
    juce::int64 sessionOriginMs = 0;         // the session recording this window: the time of day of its sample 0 (0: no session here)

    double endSeconds() const { double e = 0.0; for (auto& t : takes) e = juce::jmax (e, t.end()); return e; }
    /** The first take's start (0 when there is none). */
    double firstSeconds() const { double s = -1.0; for (auto& t : takes) if (s < 0.0 || t.start < s) s = t.start; return juce::jmax (0.0, s); }
    /** The take at a place (the later one where a take's pre-take reached back over the one before: the same recording). */
    const ShownTake* takeAt (double seconds) const
    {
        const ShownTake* best = nullptr;
        for (auto& t : takes) if (seconds >= t.start && seconds < t.shownEnd() && (best == nullptr || t.start > best->start)) best = &t;
        if (best == nullptr) for (auto& t : takes) if (seconds >= t.start && seconds < t.end() && (best == nullptr || t.start > best->start)) best = &t;
        return best;
    }
    const ShownTake* findTake (int number) const { for (auto& t : takes) if (t.number == number) return &t; return nullptr; }
    const LayoutTrack* findTrack (const juce::Uuid& id) const { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }
    /** Where a sample of the session's timeline is on the timeline shown (seconds), or -1 when no shown take holds it. On the time of day every
        sample of the session has its place (a flag pinned between takes too). */
    double windowSecondsOf (juce::int64 sessionSample) const
    {
        for (auto& t : takes)
            if (t.session && sessionSample >= t.sessionStart && sessionSample < t.sessionStart + juce::jmax<juce::int64> (1, t.lengthSamples) + (t.recording ? (juce::int64) (rate * 3600) : 0))
                return t.start + (double) (sessionSample - t.sessionStart) / rate;
        if (timeOfDay && sessionOriginMs != 0 && sessionSample >= 0) return (double) (sessionOriginMs - originMs) / 1000.0 + (double) sessionSample / rate;
        return -1.0;
    }
    /** The other way: the session's sample at a place on the timeline shown (-1: not a session take there; on the time of day, any place since the
        session started). */
    juce::int64 sessionSampleAt (double seconds) const
    {
        if (auto* t = takeAt (seconds)) { if (t->session) return t->sessionStart + (juce::int64) std::llround ((seconds - t->start) * rate); return -1; }
        if (timeOfDay && sessionOriginMs != 0)
        {
            const auto s = (juce::int64) std::llround ((seconds - (double) (sessionOriginMs - originMs) / 1000.0) * rate);
            return s >= 0 ? s : -1;
        }
        return -1;
    }
    /** 0.5.0: a place on fermata's timeline (what the project keeps: markers) on the timeline shown. On the time of day it goes with its take;
        between takes it goes just after the take before it. */
    double shownOfWindow (double ws) const
    {
        if (! timeOfDay) return ws;
        const ShownTake* in = nullptr; const ShownTake* before = nullptr;
        for (auto& t : takes)
        {
            if (! t.inProject && ! t.session) continue;
            if (ws >= t.windowStart - 1.0e-9 && ws <= t.windowStart + t.lengthSeconds() + 1.0e-9) in = &t;
            if (t.windowStart <= ws && (before == nullptr || t.windowStart > before->windowStart)) before = &t;
        }
        if (in != nullptr) return in->start + (ws - in->windowStart);
        if (before != nullptr) return before->start + juce::jmin (ws - before->windowStart, before->lengthSeconds());
        return takes.empty() ? ws : firstSeconds();
    }
    /** The other way: a place on the timeline shown, on fermata's timeline (-1 on the time of day when no take is there: there is nothing in the
        project to pin it to). */
    double windowOfShown (double s) const
    {
        if (! timeOfDay) return s;
        if (auto* t = takeAt (s)) return t->windowStart + (s - t->start);
        return -1.0;
    }
    /** The time of day at a place (juce::Time milliseconds; 0: not known - on fermata's timeline between takes, or a take of unknown time). */
    juce::int64 clockMsAt (double s) const
    {
        if (timeOfDay) return originMs + (juce::int64) std::llround (s * 1000.0);
        if (auto* t = takeAt (s)) if (t->clockMs != 0) return t->clockMs + (juce::int64) std::llround ((s - t->start) * 1000.0);
        return 0;
    }
    /** A place as text: on the time of day the clock ("19:32:10.250"), on fermata's timeline hours, minutes and seconds from its start. */
    juce::String timeText (double s, bool ms = true) const
    {
        if (timeOfDay)
        {
            const juce::Time t (clockMsAt (s));
            juce::String x = t.formatted ("%H:%M:%S");
            if (ms) x << "." << juce::String (t.getMilliseconds()).paddedLeft ('0', 3);
            return x;
        }
        return hmsText (s, ms);
    }
    static juce::String hmsText (double seconds, bool ms = true)
    {
        if (seconds < 0.0) seconds = 0.0;
        const auto total = (juce::int64) std::floor (seconds);
        juce::String t = juce::String ((int) (total / 3600)).paddedLeft ('0', 2) + ":" + juce::String ((int) ((total / 60) % 60)).paddedLeft ('0', 2) + ":" + juce::String ((int) (total % 60)).paddedLeft ('0', 2);
        if (ms) t << "." << juce::String ((int) std::floor ((seconds - (double) total) * 1000.0)).paddedLeft ('0', 3);
        return t;
    }
    /** The local midnight of the day a moment is in. */
    static juce::int64 midnightOf (juce::int64 ms)
    {
        const juce::Time t (ms);
        return juce::Time (t.getYear(), t.getMonth(), t.getDayOfMonth(), 0, 0, 0, 0, true).toMilliseconds();
    }
    /** The time of day of a recorded file's first sample, from its BWF chunk (the origination date and the time reference: what fermata and
        fermata:Live write); 0 when it has none. Read once per file (kept by name). A time reference later in the day than the file was made
        belongs to the day before (a pre-take that reached back over midnight). */
    static juce::int64 clockOfFile (const juce::File& f)
    {
        static std::mutex lock;
        static std::map<juce::String, juce::int64> known;
        const auto key = f.getFullPathName();
        {
            const std::lock_guard<std::mutex> g (lock);
            if (auto it = known.find (key); it != known.end()) return it->second;
        }
        juce::int64 ms = 0;
        if (f.existsAsFile())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
            if (r != nullptr && r->sampleRate > 0.0 && r->metadataValues.containsKey (juce::WavAudioFormat::bwavTimeReference))
            {
                const auto date = r->metadataValues[juce::WavAudioFormat::bwavOriginationDate];          // "2026-11-06"
                const auto made = r->metadataValues[juce::WavAudioFormat::bwavOriginationTime];          // "19:32:10"
                const auto ref = r->metadataValues[juce::WavAudioFormat::bwavTimeReference].getLargeIntValue();
                if (date.length() >= 10 && ref >= 0)
                {
                    const int y = date.substring (0, 4).getIntValue(), mo = date.substring (5, 7).getIntValue(), d = date.substring (8, 10).getIntValue();
                    if (y > 1970 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31)
                    {
                        const auto midnight = juce::Time (y, mo - 1, d, 0, 0, 0, 0, true).toMilliseconds();
                        ms = midnight + (juce::int64) std::llround ((double) ref / r->sampleRate * 1000.0);
                        if (made.length() >= 8)
                        {
                            const auto madeMs = midnight + 1000 * (juce::int64) (made.substring (0, 2).getIntValue() * 3600 + made.substring (3, 5).getIntValue() * 60 + made.substring (6, 8).getIntValue());
                            if (ms > madeMs + 3600000) ms -= 86400000;
                        }
                    }
                }
            }
        }
        const std::lock_guard<std::mutex> g (lock);
        known[key] = ms;
        return ms;
    }

    /** The layout of window 'w': its takes as the project has them, the session's (when the session records this window) on top. 'timeOfDay': the
        timeline is the clock (0.5.0). */
    static TakeLayout build (const Project& p, const TakeWindowDef& w, const StationModel& m, bool timeOfDay = false)
    {
        TakeLayout L;
        L.windowName = w.name; L.windowId = w.id;
        L.rate = p.sampleRate > 0.0 ? p.sampleRate : (m.sessionRate > 0.0 ? m.sessionRate : 48000.0);
        const bool sessionHere = m.sessionOn && m.windowName == w.name;
        if (sessionHere) L.sessionFolder = juce::File (m.sessionFolder);
        for (auto& t : p.tracks)
        {
            if (t.isLtc || w.isHidden (t.id)) continue;
            LayoutTrack lt; lt.id = t.id; lt.name = t.name; lt.channels = t.channelCount(); lt.colour = t.colour;
            if (sessionHere)
                for (size_t i = 0; i < m.tracks.size(); ++i)
                    if ((! m.tracks[i].id.isNull() && m.tracks[i].id == t.id) || (m.tracks[i].id.isNull() && m.tracks[i].name == t.name))
                    { lt.sessionIndex = (int) i; lt.ringFirst = m.tracks[i].ringFirst; break; }
            L.tracks.push_back (lt);
        }
        if (sessionHere)                                                    // a session track the project has no track for (yet): shown too
            for (size_t i = 0; i < m.tracks.size(); ++i)
            {
                bool have = false;
                for (auto& lt : L.tracks) if (lt.sessionIndex == (int) i) have = true;
                if (have) continue;
                LayoutTrack lt; lt.id = m.tracks[i].id; lt.name = m.tracks[i].name; lt.channels = m.tracks[i].channels; lt.sessionIndex = (int) i; lt.ringFirst = m.tracks[i].ringFirst;
                L.tracks.push_back (lt);
            }
        auto groups = w.groups;
        std::stable_sort (groups.begin(), groups.end(), [] (const TakeGroup& a, const TakeGroup& b) { return a.startSeconds < b.startSeconds; });
        double lastEnd = -2.0; bool any = false;
        for (auto& g : groups)
        {
            ShownTake s;
            s.number = g.number; s.label = g.label; s.start = g.startSeconds; s.lengthSamples = g.lengthSamples; s.rate = g.sampleRate > 0 ? g.sampleRate : L.rate;
            s.groupId = g.id; s.files = g.files; s.inProject = true; s.merged = true; s.recordedAt = g.recordedAt;
            if (sessionHere)
                if (auto* st = m.findTake (g.number); st != nullptr && st->merged && st->end > st->start)
                { s.session = true; s.sessionStart = st->start; s.recordPoint = st->recordPoint; }
            L.takes.push_back (s);
            lastEnd = juce::jmax (lastEnd, s.end()); any = true;
        }
        if (sessionHere && m.origin.toMilliseconds() != 0) L.sessionOriginMs = m.origin.toMilliseconds();
        if (sessionHere)
        {
            const auto now = m.positionNow();
            auto sessionTakes = m.takes;
            std::sort (sessionTakes.begin(), sessionTakes.end(), [] (const StationTake& a, const StationTake& b) { return a.start < b.start; });
            for (auto& st : sessionTakes)
            {
                if (L.findTake (st.number) != nullptr) continue;           // (in the project already)
                ShownTake s;
                s.number = st.number; s.label = st.label; s.rate = m.sessionRate > 0 ? m.sessionRate : L.rate;
                s.session = true; s.sessionStart = st.start; s.recordPoint = st.recordPoint; s.recording = st.end < 0; s.merged = st.merged;
                s.lengthSamples = juce::jmax<juce::int64> (0, (st.end < 0 ? now : st.end) - st.start);
                s.start = any ? lastEnd + 2.0 : 0.0;                        // (where TakeRegister puts it)
                L.takes.push_back (s);
                lastEnd = s.end(); any = true;
            }
        }
        // 0.5.0: each take's time of day, and (on the time of day) its place on the clock
        for (auto& t : L.takes)
        {
            t.windowStart = t.start;
            if (t.session && L.sessionOriginMs != 0) t.clockMs = L.sessionOriginMs + (juce::int64) std::llround ((double) t.sessionStart / juce::jmax (1.0, t.rate) * 1000.0);
            if (t.clockMs == 0 && ! t.files.empty()) t.clockMs = clockOfFile (t.files.front().file);
            if (t.clockMs == 0 && t.recordedAt.toMilliseconds() != 0) t.clockMs = t.recordedAt.toMilliseconds();
        }
        if (timeOfDay) L.placeOnTheClock();
        return L;
    }

    /** The time of day: every take at its clock time, counted from the local midnight of the first one's day; a take whose time is not known goes
        2 s after the one before it; a take whose pre-take reached back over the one before cuts that one short where it starts. */
    void placeOnTheClock()
    {
        timeOfDay = true;
        juce::int64 first = 0;
        for (auto& t : takes) if (t.clockMs != 0 && (first == 0 || t.clockMs < first)) first = t.clockMs;
        if (first == 0) first = sessionOriginMs != 0 ? sessionOriginMs : juce::Time::currentTimeMillis();
        originMs = midnightOf (first);
        std::stable_sort (takes.begin(), takes.end(), [] (const ShownTake& a, const ShownTake& b) { return a.windowStart < b.windowStart; });
        double prevEnd = -2.0;
        for (auto& t : takes)
        {
            t.start = t.clockMs != 0 ? (double) (t.clockMs - originMs) / 1000.0 : juce::jmax (0.0, prevEnd + 2.0);
            prevEnd = t.end();
        }
        std::stable_sort (takes.begin(), takes.end(), [] (const ShownTake& a, const ShownTake& b) { return a.start < b.start; });
        for (size_t i = 0; i + 1 < takes.size(); ++i)
            if (takes[i].end() > takes[i + 1].start + 1.0e-6 && ! takes[i].recording) takes[i].cutAt = takes[i + 1].start;
    }
};
} // namespace td::live
