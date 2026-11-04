#pragma once
/** fermata:Live 0.3.0: what a station's take window shows - the takes of the window on its timeline (seconds, as fermata's take window has them)
    and where each one's audio is: the project's files (a take recorded before, or merged and put in already), and/or the session's timeline (a
    take of the session that is running: its chunks, and the live waveform). A take the session has recorded but the project does not have yet
    goes where TakeRegister will put it (after the window's last take, 2 s on), so it does not move when it is put in. Built on the message thread
    from the project and the station's model; handed to the player as a whole (shared_ptr). */
#include "StationModel.h"
#include "../core/Project.h"

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
    double lengthSeconds() const noexcept   { return rate > 0.0 ? (double) lengthSamples / rate : 0.0; }
    double end() const noexcept             { return start + lengthSeconds(); }
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

    double endSeconds() const { double e = 0.0; for (auto& t : takes) e = juce::jmax (e, t.end()); return e; }
    const ShownTake* takeAt (double seconds) const
    {
        for (auto& t : takes) if (seconds >= t.start && seconds < t.end()) return &t;
        return nullptr;
    }
    const ShownTake* findTake (int number) const { for (auto& t : takes) if (t.number == number) return &t; return nullptr; }
    const LayoutTrack* findTrack (const juce::Uuid& id) const { for (auto& t : tracks) if (t.id == id) return &t; return nullptr; }
    /** Where a sample of the session's timeline is on the window's timeline (seconds), or -1 when no shown take holds it. */
    double windowSecondsOf (juce::int64 sessionSample) const
    {
        for (auto& t : takes)
            if (t.session && sessionSample >= t.sessionStart && sessionSample < t.sessionStart + juce::jmax<juce::int64> (1, t.lengthSamples) + (t.recording ? (juce::int64) (rate * 3600) : 0))
                return t.start + (double) (sessionSample - t.sessionStart) / rate;
        return -1.0;
    }
    /** The other way: the session's sample at a place on the window's timeline (-1: not a session take there). */
    juce::int64 sessionSampleAt (double seconds) const
    {
        if (auto* t = takeAt (seconds)) if (t->session) return t->sessionStart + (juce::int64) std::llround ((seconds - t->start) * rate);
        return -1;
    }

    /** The layout of window 'w': its takes as the project has them, the session's (when the session records this window) on top. */
    static TakeLayout build (const Project& p, const TakeWindowDef& w, const StationModel& m)
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
        return L;
    }
};
} // namespace td::live
