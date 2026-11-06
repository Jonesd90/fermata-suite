#pragma once
#include "Common.h"
#include <algorithm>
#include <vector>

namespace td
{
/** fermata:Live (contract 3): an answer to a flag - the Fixer's reply to what the Primary flagged. */
struct FlagReply
{
    juce::String text;
    juce::String by;                  // who wrote it ("Fixer")
    juce::Time   at;                  // when it was sent (the time of day)
};

/** A quick marker on a take window's or an edit window's timeline (seconds on that window's own timeline). */
struct Marker
{
    juce::Uuid   id;
    juce::String name;
    double       seconds = 0.0;
    /** fermata:Live (contract 3): a FLAG - pinned by the Primary during a live recording at the moment of the music it is about, with the note
        typed then; the Fixer's replies are kept with it. fermata shows a flag as an ordinary marker (its name carries the note and the last
        reply: see flagName) and keeps these fields when it saves the project. */
    bool         flag = false;
    juce::String note;
    juce::Time   pinnedAt;            // when it was pinned (the time of day)
    std::vector<FlagReply> replies;
    /** fermata:Live 0.5.0 (still contract 3): a NOTE - a station's own jotting at a place in the take (fermata:Live's N key), its text in 'note'.
        Each station shows only its own notes; the recording's report lists them all. fermata shows a note as an ordinary marker (see noteName). */
    bool         liveNote = false;
    juce::String by;                  // a flag or a note: who wrote it (the name the station was given: "David", or "Primary" / "Fixer")
    juce::String station;             // a flag or a note: the station it came from ("Primary" or "Fixer"), whatever its name
};

/** The name a note shows as an ordinary marker: "Note (David): the cello's chair creaks", on one line. */
inline juce::String noteName (const juce::String& text, const juce::String& by)
{
    juce::String n = "Note";
    if (by.trim().isNotEmpty()) n << " (" << by.trim() << ")";
    const auto firstLine = text.trim().upToFirstOccurrenceOf ("\n", false, false).trim();
    if (firstLine.isNotEmpty()) n << ": " << firstLine;
    return n;
}

/** The name a flag shows as an ordinary marker: "Flag: horn crack bar 34", with the last reply after it: "... | Fixer: fixed, version 1". */
inline juce::String flagName (const juce::String& note, const std::vector<FlagReply>& replies)
{
    juce::String n = "Flag";
    if (note.trim().isNotEmpty()) n << ": " << note.trim();
    if (! replies.empty())
    {
        const auto& r = replies.back();
        n << " | " << (r.by.trim().isNotEmpty() ? r.by.trim() : juce::String ("Reply")) << ": " << r.text.trim();
    }
    return n;
}

/** The number a new marker gets when it is not named: one more than the biggest all-digit name already there (1, 2, 3 ...). */
inline int nextMarkerNumber (const std::vector<Marker>& list)
{
    int top = 0;
    for (auto& m : list)
        if (m.name.isNotEmpty() && m.name.containsOnly ("0123456789") && m.name.length() < 9) top = std::max (top, m.name.getIntValue());
    return top + 1;
}

/** Adds a marker at 'seconds' called by the next sequential number; returns its id. */
inline juce::Uuid addMarker (std::vector<Marker>& list, double seconds)
{
    Marker m; m.id = juce::Uuid(); m.seconds = std::max (0.0, seconds); m.name = juce::String (nextMarkerNumber (list));
    list.push_back (m);
    return m.id;
}

inline Marker* findMarker (std::vector<Marker>& list, const juce::Uuid& id)
{
    for (auto& m : list) if (m.id == id) return &m;
    return nullptr;
}

inline void removeMarker (std::vector<Marker>& list, const juce::Uuid& id)
{
    list.erase (std::remove_if (list.begin(), list.end(), [&] (const Marker& m) { return m.id == id; }), list.end());
}
} // namespace td
