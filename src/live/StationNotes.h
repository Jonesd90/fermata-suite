#pragma once
/** fermata:Live 0.5.0: the NOTES (N), as both stations see them. A note is a station's own jotting at a place in a take: the session's notes travel
    through the recorder exactly as flags do (FlagRec with isNote: kept in every drive's index, handed to both stations), and the Primary's window
    puts each into the project once its take is there (a take window marker with liveNote); the Primary's notes on a take from an earlier session
    go straight into the project. Each station shows only its own ('station': "Primary" or "Fixer"); the report lists everyone's.

    Also the project's ordinary markers (the gaps of a recording, markers made in fermata), drawn grey in the notes row and never changed here.
    Message thread only. */
#include "LiveTakeView.h"
#include <set>

namespace td::live::notes
{
/** One note, wherever it is kept. */
struct Info
{
    juce::String id, text, by, station;
    juce::Time at;
    double seconds = -1.0;                   // on the timeline shown (-1: not on this window's timeline)
    juce::int64 sample = -1;                 // the session's: its place on the session's timeline
    bool inSession = false;                  // the running session keeps it (changed through the recorder)
    bool inProject = false;                  // the project has it (a marker)
};

/** The notes of the window: the running session's (when it records this window) and the project's markers with liveNote, once each; 'station'
    empty = everyone's. */
inline std::vector<Info> collect (const StationModel& m, const TakeWindowDef* w, const TakeLayout* L, const juce::String& station)
{
    std::vector<Info> out;
    std::set<juce::String> have;
    if (m.sessionOn && (w == nullptr || m.windowName == w->name))
        for (auto& f : m.flags)
        {
            if (! f.isNote) continue;
            have.insert (f.id);
            if (f.removed || (station.isNotEmpty() && f.station != station)) continue;
            Info x; x.id = f.id; x.text = f.note; x.by = f.by; x.station = f.station; x.at = f.pinnedAt; x.sample = f.sample; x.inSession = true;
            x.seconds = L != nullptr ? L->windowSecondsOf (f.sample) : -1.0;
            out.push_back (x);
        }
    if (w != nullptr)
        for (auto& mk : w->markers)
        {
            if (! mk.liveNote) continue;
            const auto id = mk.id.toString();
            if (have.count (id) != 0) { for (auto& x : out) if (x.id == id) x.inProject = true; continue; }
            if (station.isNotEmpty() && mk.station != station) continue;
            Info x; x.id = id; x.text = mk.note; x.by = mk.by; x.station = mk.station; x.at = mk.pinnedAt; x.inProject = true;
            x.seconds = L != nullptr ? L->shownOfWindow (mk.seconds) : mk.seconds;
            out.push_back (x);
        }
    std::sort (out.begin(), out.end(), [] (const Info& a, const Info& b) { return a.seconds < b.seconds; });
    return out;
}

inline const Info* find (const std::vector<Info>& list, const juce::String& id) { for (auto& x : list) if (x.id == id) return &x; return nullptr; }

/** What the view draws in its notes row: the station's notes, and the project's ordinary markers (grey, read only). */
inline std::vector<LiveTakeView::NoteShown> forView (const std::vector<Info>& list, const TakeWindowDef* w, const TakeLayout* L)
{
    std::vector<LiveTakeView::NoteShown> v;
    for (auto& x : list)
        if (x.seconds >= 0.0) { LiveTakeView::NoteShown n; n.id = x.id; n.seconds = x.seconds; n.text = x.text; n.by = x.by; n.at = x.at; v.push_back (n); }
    if (w != nullptr)
        for (auto& mk : w->markers)
            if (! mk.flag && ! mk.liveNote)
            {
                LiveTakeView::NoteShown n; n.id = mk.id.toString(); n.plain = true; n.editable = false; n.text = mk.name;
                n.seconds = L != nullptr ? L->shownOfWindow (mk.seconds) : mk.seconds;
                v.push_back (n);
            }
    return v;
}

/** The panel's heading for a note: where it is and who wrote it ("Note - take 5 at 00:12:34 (19:44:44) - by David"). */
inline juce::String heading (const Info& x, const TakeLayout* L)
{
    juce::String h = "NOTE";
    if (L != nullptr && x.seconds >= 0.0)
    {
        if (auto* t = L->takeAt (x.seconds)) h << " - take " << t->number << " at " << TakeLayout::hmsText (x.seconds - t->start, false);
        const auto clock = L->clockMsAt (x.seconds);
        if (clock != 0) h << " (" << juce::Time (clock).formatted ("%H:%M:%S") << ")";
    }
    if (x.by.isNotEmpty()) h << " - by " << x.by;
    return h;
}

/** A recorder command for a note: "flag" with the kind, or "flag-note" / "flag-remove" for one the session has. */
inline juce::var command (const juce::String& cmd, const juce::String& id)
{
    auto* o = new juce::DynamicObject(); juce::var c (o);
    o->setProperty ("cmd", cmd); o->setProperty ("id", id);
    if (cmd == "flag") o->setProperty ("kind", "note");
    return c;
}
} // namespace td::live::notes
