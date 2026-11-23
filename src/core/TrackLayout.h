#pragma once
#include "ImportPlan.h"
#include "Project.h"
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace td
{
/** 3.57.0 (asked for on 22 November 2026: "import the take into the EXISTING audio tracks ... where I have created stereo versions of particular
    tracks ... copied the naming I have given from the first takes import"): an import laid out like a take window that is already set up - every
    microphone's file on the track that mic went on there, whatever that track is called now, a left and a right file joined into the stereo track
    they were made into, no new tracks; and a take window's takes moved onto another window's tracks the same way.

    Which mic went where is read from what the project already keeps: every imported take file remembers its original file name
    (TakeFile::sourceName, and sourceName2 for the right half of a stereo track made from two mono files - also after "Make a stereo track"), so
    the window's original names are read again the way an import reads names (buildImportPlan) and each name is tied to the track (and the side)
    its audio is on now. Added to that: each track's own name, and the mics remembered on the track (TrackDef::mics, micsLeft, micsRight - what
    was chosen by hand in an earlier import). The new files' names, read the same way, are then matched to those names: the same name, the name
    of a stereo track with L / R after it, the same name without the numbers at its end, or the end of the name. What cannot be matched for sure
    is left for the person to choose in the list.

    A header only: pure logic over the project and an import plan (no files are read); the import window and the take window use it. */
namespace tracklayout
{
// ======================================================================================================== names
inline bool isSeparator (juce::juce_wchar c) noexcept
{
    return c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')' || c == '[' || c == ']' || c == '+' || c == ',' || c == '\t';
}
/** The words of a name, without capitals ("Rorate Caeli_SPOT4-19" -> rorate / caeli / spot4 / 19). */
inline juce::StringArray wordsOf (const juce::String& name)
{
    juce::StringArray w; juce::String cur;
    for (auto c : name)
    {
        if (isSeparator (c)) { if (cur.isNotEmpty()) w.add (cur); cur.clear(); }
        else cur << juce::String::charToString (juce::CharacterFunctions::toLowerCase (c));
    }
    if (cur.isNotEmpty()) w.add (cur);
    return w;
}
/** A name as names are compared here: its words, without capitals, joined by single spaces ("Omni-L" -> "omni l"). */
inline juce::String canon (const juce::String& name) { return wordsOf (name).joinIntoString (" "); }
inline bool isSideWord (const juce::String& w) noexcept { return w == "l" || w == "r" || w == "left" || w == "right"; }
inline bool isNumberWord (const juce::String& w) { return w.isNotEmpty() && w.containsOnly ("0123456789"); }
/** Does a run of words say something of its own (not only numbers and sides)? */
inline bool meaningful (const juce::StringArray& w, int from, int to)
{
    for (int i = juce::jmax (0, from); i < juce::jmin (w.size(), to); ++i) if (! isNumberWord (w[i]) && ! isSideWord (w[i])) return true;
    return false;
}

/** The side a name says: 0 left, 1 right, -1 none; 'base' gets the rest, canonical ("Omni L" / "Omni-Left" -> "omni", 0). A last word L / R /
    Left / Right, or (in a name that still has its capitals) an L or R stuck to the end after a small letter or a digit ("Piano4099L"). */
inline int sideOf (const juce::String& name, juce::String& base)
{
    const auto w = wordsOf (name);
    if (w.size() >= 2 && isSideWord (w[w.size() - 1]))
    {
        juce::StringArray b = w; b.remove (b.size() - 1);
        base = b.joinIntoString (" ");
        return w[w.size() - 1][0] == 'l' ? 0 : 1;
    }
    const auto t = name.trim();
    if (t.length() >= 2)
    {
        const auto c = t[t.length() - 1], prev = t[t.length() - 2];
        if ((c == 'L' || c == 'R') && (juce::CharacterFunctions::isLowerCase (prev) || juce::CharacterFunctions::isDigit (prev)))
        {
            base = canon (t.dropLastCharacters (1));
            return base.isNotEmpty() ? (c == 'L' ? 0 : 1) : -1;
        }
    }
    return -1;
}

/** A canonical name without the number words at its end that look like take or clip numbers (two digits or more: "spot4 19 18" -> "spot4";
    "mic 1" keeps its 1, which is more likely the mic's own number). Empty when nothing would be left that says something. */
inline juce::String withoutTakeNumbers (const juce::String& canonical)
{
    juce::StringArray w; w.addTokens (canonical, " ", ""); w.removeEmptyStrings();
    while (w.size() > 1 && isNumberWord (w[w.size() - 1]) && w[w.size() - 1].length() >= 2) w.remove (w.size() - 1);
    return meaningful (w, 0, w.size()) ? w.joinIntoString (" ") : juce::String();
}

/** The names of one import (or of one take window's original files), with the words that every one of them starts with, and those every one ends
    with, taken off ("gloria spot4", "gloria omni l" ... -> "spot4", "omni l"): what is left names the microphone, the same from piece to piece.
    Only with four names or more (fewer say too little), only while every name keeps a word that says something, and only if they all stay
    different. canonical name -> its reduced form (the same name when nothing comes off). */
inline std::map<juce::String, juce::String> reducedNames (const juce::StringArray& canonicalNames)
{
    std::map<juce::String, juce::String> out;
    juce::StringArray distinct;
    for (auto& n : canonicalNames) if (n.isNotEmpty()) distinct.addIfNotAlreadyThere (n);
    for (auto& n : distinct) out[n] = n;
    if (distinct.size() < 4) return out;
    std::vector<juce::StringArray> words;
    for (auto& n : distinct) { juce::StringArray w; w.addTokens (n, " ", ""); w.removeEmptyStrings(); words.push_back (w); }
    int lead = 0, trail = 0;
    for (;;)
    {
        bool same = true, keeps = true;
        for (auto& w : words)
        {
            if (w.size() - lead - trail <= 1 || w[lead] != words.front()[lead]) { same = false; break; }
            if (! meaningful (w, lead + 1, w.size() - trail)) keeps = false;
        }
        if (! same || ! keeps) break;
        ++lead;
    }
    for (;;)
    {
        bool same = true, keeps = true;
        const auto& f = words.front();
        for (auto& w : words)
        {
            if (w.size() - lead - trail <= 1 || w[w.size() - 1 - trail] != f[f.size() - 1 - trail]) { same = false; break; }
            if (! meaningful (w, lead, w.size() - trail - 1)) keeps = false;
        }
        if (! same || ! keeps) break;
        ++trail;
    }
    if (lead == 0 && trail == 0) return out;
    std::set<juce::String> seen; juce::StringArray red;
    for (auto& w : words)
    {
        juce::StringArray r; for (int i = lead; i < w.size() - trail; ++i) r.add (w[i]);
        red.add (r.joinIntoString (" "));
        if (! seen.insert (red[red.size() - 1]).second) return out;           // (two would become one: nothing comes off)
    }
    for (int i = 0; i < distinct.size(); ++i) out[distinct[i]] = red[i];
    return out;
}
/** A name's forms as compared here: canonical, and the reduced form when there is one (see reducedNames). */
inline juce::StringArray formsOf (const juce::String& name, const std::map<juce::String, juce::String>& reduced)
{
    juce::StringArray f; const auto c = canon (name);
    if (c.isNotEmpty()) f.add (c);
    if (auto it = reduced.find (c); it != reduced.end() && it->second.isNotEmpty()) f.addIfNotAlreadyThere (it->second);
    return f;
}

// ======================================================================================================== a take window's layout
/** Where audio can go: a track whole (side -1: a mono file on a mono track, a stereo file on a stereo track), or one side of a stereo track
    (0 left, 1 right: a mono file). */
struct Slot
{
    juce::Uuid track = juce::Uuid::null();
    int side = -1;
    bool isNull() const noexcept { return track.isNull(); }
    bool operator== (const Slot& o) const noexcept { return track == o.track && side == o.side; }
    bool operator!= (const Slot& o) const noexcept { return ! operator== (o); }
    bool operator< (const Slot& o) const noexcept { return track != o.track ? track < o.track : side < o.side; }
};
/** One place, and the names of the microphones whose audio went there ('channels': of a file that goes there - 1 for a side). */
struct Entry { Slot slot; int channels = 1; juce::StringArray names; };
struct Layout
{
    juce::Uuid window = juce::Uuid::null();
    std::vector<juce::Uuid> tracks;           // the tracks the window has audio on (not the LTC track), in the project's order
    std::vector<Entry> entries;
    const Entry* find (const Slot& s) const { for (auto& e : entries) if (e.slot == s) return &e; return nullptr; }
};

inline const TrackDef* trackIn (const Project& p, const juce::Uuid& id) { for (auto& t : p.tracks) if (t.id == id) return &t; return nullptr; }

/** Reads take window 'w': which mic's audio is on which of its tracks (and sides). 'naming': how the original file names are read (the import
    window's "Recorded with"). */
inline Layout layoutOf (const Project& p, const TakeWindowDef& w, ImportNaming naming = ImportNaming::Auto)
{
    Layout lay; lay.window = w.id;
    std::set<juce::Uuid> used;
    for (auto& g : w.groups) for (auto& f : g.files) used.insert (f.trackId);
    for (auto& t : p.tracks) if (used.count (t.id) != 0 && ! t.isLtc && (t.channelCount() == 1 || t.channelCount() == 2)) lay.tracks.push_back (t.id);
    auto entryFor = [&lay] (const Slot& s, int ch) -> Entry&
    {
        for (auto& e : lay.entries) if (e.slot == s) return e;
        lay.entries.push_back ({ s, ch, {} });
        return lay.entries.back();
    };
    auto add = [] (Entry& e, const juce::String& n) { if (n.isNotEmpty()) e.names.addIfNotAlreadyThere (n); };
    auto isUsed = [&lay] (const juce::Uuid& id) { return std::find (lay.tracks.begin(), lay.tracks.end(), id) != lay.tracks.end(); };
    // the tracks' own names, and the mics remembered on them
    for (auto& id : lay.tracks)
    {
        const auto* t = trackIn (p, id);
        const int ch = t->channelCount();
        add (entryFor ({ id, -1 }, ch), canon (t->name));
        for (auto& m : t->mics) add (entryFor ({ id, -1 }, ch), m);
        if (ch == 2)
        {
            for (auto& m : t->micsLeft)  add (entryFor ({ id, 0 }, 1), m);
            for (auto& m : t->micsRight) add (entryFor ({ id, 1 }, 1), m);
        }
    }
    // the takes' original file names, read again the way an import reads them, each tied to where its audio is now
    std::vector<ImportSource> src; std::vector<Slot> slotOf;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);      // (only for the names: no file is looked at)
    for (auto& g : w.groups)
    {
        std::map<juce::String, int> count;                                 // a name more than once in a take: the channels of one multichannel file - its name says nothing about which
        for (auto& f : g.files) { if (f.sourceName.isNotEmpty()) count[f.sourceName.toLowerCase()]++; if (f.sourceName2.isNotEmpty()) count[f.sourceName2.toLowerCase()]++; }
        for (auto& f : g.files)
        {
            if (! isUsed (f.trackId)) continue;
            auto one = [&] (const juce::String& name, int side, int ch)
            {
                if (name.isEmpty() || count[name.toLowerCase()] != 1) return;
                ImportSource s; s.file = dir.getChildFile (name); s.channels = ch; s.length = g.lengthSamples; s.sampleRate = g.sampleRate; s.modified = g.recordedAt;
                src.push_back (s); slotOf.push_back ({ f.trackId, side });
            };
            const int n = juce::jmax (1, f.numChannels);
            if (n == 2 && f.sourceName2.isNotEmpty()) { one (f.sourceName, 0, 1); one (f.sourceName2, 1, 1); }
            else if (n <= 2) one (f.sourceName, -1, n);
        }
    }
    if (! src.empty())
    {
        const auto plan = buildImportPlan (src, naming);
        juce::StringArray all; for (auto& t : plan.tracks) all.addIfNotAlreadyThere (canon (t.name));
        const auto reduced = reducedNames (all);
        for (auto& tk : plan.takes)
            for (auto& part : tk.parts)
            {
                if (! juce::isPositiveAndBelow (part.source, (int) slotOf.size())) continue;
                const auto sl = slotOf[(size_t) part.source];
                const auto* t = trackIn (p, sl.track);
                if (t == nullptr) continue;
                auto& e = entryFor (sl, sl.side >= 0 ? 1 : t->channelCount());
                for (auto& f : formsOf (part.trackName, reduced)) add (e, f);
            }
    }
    return lay;
}

// ======================================================================================================== matching
/** One thing to place: a track of an import (its name as the import read it) or a track of a take window that is being laid out again. */
struct Item
{
    juce::String key;                         // the caller's own
    juce::StringArray names;                  // its canonical forms (formsOf; for a take window's track: its layout entry's names)
    juce::String original;                    // the name as it is, capitals and all (for an L / R stuck to the end)
    int channels = 1;
};
enum class How { none, sameName, sideOfStereo, numbersOff, endOfName };
struct Found { Slot slot; How how = How::none; };

/** Words of 'a' end with all the words of 'b' (b says something of its own). */
inline bool endsWithWords (const juce::String& a, const juce::String& b)
{
    juce::StringArray wa, wb; wa.addTokens (a, " ", ""); wb.addTokens (b, " ", ""); wa.removeEmptyStrings(); wb.removeEmptyStrings();
    if (wb.isEmpty() || wb.size() > wa.size() || ! meaningful (wb, 0, wb.size())) return false;
    for (int i = 1; i <= wb.size(); ++i) if (wa[wa.size() - i] != wb[wb.size() - i]) return false;
    return true;
}

/** Where each item goes in the layout (Slot null and How::none where it cannot be said for sure: no match, or more than one place matches). */
inline std::vector<Found> match (const Layout& lay, const std::vector<Item>& items)
{
    std::vector<Found> out (items.size());
    auto fits = [] (const Entry& e, int channels) { return channels == 1 ? (e.slot.side >= 0 || e.channels == 1) : (e.slot.side < 0 && e.channels == 2); };
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        auto decide = [&] (const std::set<Slot>& hits, How how) { if (hits.size() != 1) return false; out[i] = { *hits.begin(), how }; return true; };
        // 1. the same name
        {
            std::set<Slot> hits;
            for (auto& e : lay.entries) if (fits (e, it.channels)) for (auto& n : it.names) if (e.names.contains (n)) hits.insert (e.slot);
            if (decide (hits, How::sameName)) continue;
            if (hits.size() > 1) continue;                                 // (the same name in two places: the person says which)
        }
        // 2. a mono file whose name is a stereo track's with L / R after it ("Omni L" -> the left of "Omni"), or a stereo file whose name is the
        //    two sides' without their L / R ("Omni" -> the stereo track made from "Omni L" and "Omni R")
        {
            std::set<Slot> hits;
            if (it.channels == 1)
            {
                juce::StringArray tries = it.names; tries.addIfNotAlreadyThere (it.original);
                for (auto& n : tries)
                {
                    juce::String base; const int side = sideOf (n, base);
                    if (side < 0 || base.isEmpty()) continue;
                    for (auto& e : lay.entries) if (e.slot.side < 0 && e.channels == 2 && e.names.contains (base)) hits.insert ({ e.slot.track, side });
                }
            }
            else
                for (auto& id : lay.tracks)
                {
                    const auto* l = lay.find ({ id, 0 }); const auto* r = lay.find ({ id, 1 });
                    if (l == nullptr || r == nullptr) continue;
                    auto hasBase = [&] (const Entry& e, int side)
                    {
                        for (auto& en : e.names) { juce::String b; if (sideOf (en, b) == side && it.names.contains (b)) return true; }
                        return false;
                    };
                    if (hasBase (*l, 0) && hasBase (*r, 1)) hits.insert ({ id, -1 });
                }
            if (decide (hits, How::sideOfStereo)) continue;
            if (hits.size() > 1) continue;
        }
        // 3. the same name without the take numbers at its end
        {
            std::set<Slot> hits;
            for (auto& n : it.names)
            {
                const auto a = withoutTakeNumbers (n);
                if (a.isEmpty()) continue;
                for (auto& e : lay.entries) if (fits (e, it.channels)) for (auto& en : e.names) if (withoutTakeNumbers (en) == a) hits.insert (e.slot);
            }
            if (decide (hits, How::numbersOff)) continue;
            if (hits.size() > 1) continue;
        }
        // 4. one name ends with the other ("gloria spot4" and "spot4"): the longest such ending, if only one place has it
        {
            std::map<Slot, int> best; int longest = 0;
            for (auto& n : it.names)
                for (auto& e : lay.entries)
                    if (fits (e, it.channels))
                        for (auto& en : e.names)
                        {
                            const bool a = endsWithWords (n, en), b = endsWithWords (en, n);
                            if (! a && ! b) continue;
                            juce::StringArray w; w.addTokens (a ? en : n, " ", ""); w.removeEmptyStrings();
                            best[e.slot] = juce::jmax (best[e.slot], w.size()); longest = juce::jmax (longest, w.size());
                        }
            std::set<Slot> hits; for (auto& kv : best) if (kv.second == longest) hits.insert (kv.first);
            decide (hits, How::endOfName);
        }
    }
    return out;
}

// ======================================================================================================== the import, laid out
/** What happens to one track of the import. */
struct Choice
{
    enum Kind { ownTrack, leaveOut, slot };
    Kind kind = ownTrack;
    Slot to;
};
inline juce::String keyFor (const juce::Uuid& track) { return "@" + track.toString(); }
inline juce::Uuid trackOfKey (const juce::String& key) { return key.startsWithChar ('@') ? juce::Uuid (key.substring (1)) : juce::Uuid::null(); }

struct Built
{
    /** The plan to import: a file that goes on a track of the layout has the key keyFor (that track) and the track's name; the left and the right
        file of one stereo track are one part (source + source2, or kOneSide when the take has only one of them). The rest as the import read it. */
    ImportPlan plan;
    struct Mic { juce::String key; int side = -1; juce::StringArray names; };
    std::vector<Mic> mics;                    // what to remember on the tracks afterwards (by the plan's keys): the names of what went there
    juce::StringArray problems;               // to be said in the import window
    std::set<juce::String> clashing;          // import tracks that go on the same place as another one in a take (the import waits until they are changed)
};

/** Applies the choices to the import as it was read ('base': no pairs joined). 'namesOf': each import track's canonical forms (for remembering);
    'trackName': a track's name now. */
inline Built build (const ImportPlan& base, const std::map<juce::String, Choice>& choices, const std::map<juce::String, juce::StringArray>& namesOf,
                    const std::function<juce::String (const juce::Uuid&)>& trackName)
{
    Built b; b.plan = base; b.plan.takes.clear(); b.plan.tracks.clear();
    std::map<std::pair<juce::String, int>, juce::StringArray> mics;
    auto remember = [&] (const juce::String& key, int side, const juce::String& importKey)
    {
        auto& m = mics[{ key, side }];
        if (auto it = namesOf.find (importKey); it != namesOf.end()) for (auto& n : it->second) m.addIfNotAlreadyThere (n);
    };
    std::map<juce::String, int> oneSided;                                  // "Omni L|right" -> takes with only one side of a stereo track
    for (auto& tk : base.takes)
    {
        ImportTake out = tk; out.parts.clear();
        std::map<Slot, std::vector<size_t>> at;
        for (size_t pi = 0; pi < tk.parts.size(); ++pi)
        {
            const auto& part = tk.parts[pi];
            const auto it = choices.find (part.trackKey);
            const Choice c = it != choices.end() ? it->second : Choice();
            if (c.kind == Choice::leaveOut) continue;
            if (c.kind == Choice::ownTrack || c.to.isNull()) { out.parts.push_back (part); remember (part.trackKey, -1, part.trackKey); continue; }
            at[c.to].push_back (pi);
        }
        // two files of one take on one place, or on a whole stereo track and one of its sides: the person has to change one of them
        for (auto& kv : at)
        {
            bool clash = kv.second.size() > 1;
            if (kv.first.side >= 0) clash = clash || at.count ({ kv.first.track, -1 }) != 0;
            else clash = clash || at.count ({ kv.first.track, 0 }) != 0 || at.count ({ kv.first.track, 1 }) != 0;
            if (clash) for (auto pi : kv.second) b.clashing.insert (tk.parts[pi].trackKey);
        }
        std::set<juce::Uuid> stereoDone;
        for (auto& kv : at)
        {
            const auto& sl = kv.first;
            if (sl.side < 0)
            {
                if (kv.second.size() != 1 || b.clashing.count (tk.parts[kv.second.front()].trackKey) != 0) continue;
                ImportPart q = tk.parts[kv.second.front()];
                const auto importKey = q.trackKey;
                q.trackKey = keyFor (sl.track); q.trackName = trackName (sl.track);
                out.parts.push_back (q); remember (q.trackKey, -1, importKey);
                continue;
            }
            if (! stereoDone.insert (sl.track).second) continue;
            const ImportPart* side[2] = { nullptr, nullptr };
            for (int s = 0; s < 2; ++s)
                if (auto f = at.find ({ sl.track, s }); f != at.end() && f->second.size() == 1 && b.clashing.count (tk.parts[f->second.front()].trackKey) == 0)
                    side[s] = &tk.parts[f->second.front()];
            auto monoFile = [&] (const ImportPart* x) { return x != nullptr && x->channel < 0 && ! x->joined() && base.sources[(size_t) x->source].channels == 1; };
            const juce::String name = trackName (sl.track);
            if (side[0] != nullptr && side[1] != nullptr)
            {
                if (monoFile (side[0]) && monoFile (side[1])
                    && std::abs (base.sources[(size_t) side[0]->source].sampleRate - base.sources[(size_t) side[1]->source].sampleRate) < 0.5)
                {
                    ImportPart q = *side[0]; q.source2 = side[1]->source; q.trackChannels = 2; q.trackKey = keyFor (sl.track); q.trackName = name;
                    out.parts.push_back (q); remember (q.trackKey, 0, side[0]->trackKey); remember (q.trackKey, 1, side[1]->trackKey);
                }
                else
                {
                    b.problems.addIfNotAlreadyThere (side[0]->trackName + " and " + side[1]->trackName + " cannot be joined into " + name
                                                     + " (a channel of a multichannel file, or different sample rates): each goes on a track of its own.");
                    out.parts.push_back (*side[0]); out.parts.push_back (*side[1]);
                    remember (side[0]->trackKey, -1, side[0]->trackKey); remember (side[1]->trackKey, -1, side[1]->trackKey);
                }
                continue;
            }
            for (int s = 0; s < 2; ++s)
            {
                if (side[s] == nullptr) continue;
                if (! monoFile (side[s]))
                {
                    b.problems.addIfNotAlreadyThere (side[s]->trackName + " cannot go on " + name + " (a channel of a multichannel file): it goes on a track of its own.");
                    out.parts.push_back (*side[s]); remember (side[s]->trackKey, -1, side[s]->trackKey);
                    continue;
                }
                ImportPart q = *side[s]; q.source2 = ImportPart::kOneSide; q.sourceRight = s == 1; q.trackChannels = 2; q.trackKey = keyFor (sl.track); q.trackName = name;
                out.parts.push_back (q); remember (q.trackKey, s, side[s]->trackKey);
                oneSided[side[s]->trackName + "|" + name + "|" + (s == 0 ? "right" : "left")]++;
            }
        }
        if (! out.parts.empty()) b.plan.takes.push_back (std::move (out));
    }
    for (auto& kv : oneSided)
    {
        juce::StringArray bits; bits.addTokens (kv.first, "|", "");
        b.problems.add (bits[0] + " goes on " + bits[1] + ", but " + juce::String (kv.second) + (kv.second == 1 ? " take has" : " takes have")
                        + " nothing for its " + bits[2] + " side: that side is silent there.");
    }
    std::set<juce::String> seen;
    for (auto& t : b.plan.takes)
        for (auto& p : t.parts)
            if (seen.insert (p.trackKey).second) b.plan.tracks.push_back ({ p.trackKey, p.trackName, p.trackChannels });
    for (auto& kv : mics) if (! kv.second.isEmpty()) b.mics.push_back ({ kv.first.first, kv.first.second, kv.second });
    return b;
}

/** Remembers on a track the names of mics whose audio went on it (side -1: whole; 0 / 1: the left / right of a stereo track). At most 32 each,
    the newest kept. */
inline void remember (TrackDef& t, int side, const juce::StringArray& names)
{
    auto& list = side < 0 ? t.mics : side == 0 ? t.micsLeft : t.micsRight;
    if (side >= 0 && t.channelCount() != 2) return;
    for (auto& n : names) if (n.isNotEmpty() && ! n.containsChar ('|') && ! list.contains (n)) list.add (n);
    while (list.size() > 32) list.remove (0);
}
} // namespace tracklayout
} // namespace td
