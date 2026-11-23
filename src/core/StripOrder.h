#pragma once
#include "Project.h"
#include <map>

namespace td
{
/** 3.58.0: the order of the strips in a mixer's window, with Int Buses, Ext Buses that are not a main output and VCAs moved sideways among them
    (MixerState::stripPlaces). The audio tracks keep the project's order; a moved strip sits right after the strip it was dropped behind, and
    follows it when that strip moves (a track dragged to another place, say).

    The places are kept so that no two strips sit right after the same one: dropping a strip between A and B (B placed after A) makes it
    "after A" and B "after it". The strips that were right after the moved strip follow its old left-hand neighbour instead, so nothing else
    moves on the screen. A strip whose place names a strip that has gone (its track deleted) goes back to its usual place; Project::forgetStrips
    gives such places the gone strip's left-hand neighbour first, so it usually stays where it was. */
namespace striporder
{
/** One strip in its usual order: audio tracks (hidden ones too, shown false, so a strip placed after one keeps its spot), then Int Buses, Ext
    Buses, VCAs (those not in this mixer: shown false). movable: an Int Bus, an Ext Bus that is not a main output of this mixer, a VCA. */
struct Item { juce::Uuid id = juce::Uuid::null(); NodeKind kind = NodeKind::None; bool shown = true, movable = false; };
using Place = MixerState::StripPlace;

/** Every strip of mixer m in its usual order. */
inline std::vector<Item> itemsOf (const Project& p, const MixerState& m)
{
    std::vector<Item> v;
    for (auto& t : p.tracks) v.push_back ({ t.id, NodeKind::Track, ! m.isStripHidden (t.id), false });
    for (int pass = 0; pass < 2; ++pass)                                // Int Buses first, then Ext Buses
        for (auto& b : p.buses)
            if (b.external == (pass == 1))
            {
                bool isMain = false;
                if (b.external) for (auto& s : m.busPool) if (s->busId == b.id) isMain = s->isMain.get();
                v.push_back ({ b.id, b.external ? NodeKind::ExtBus : NodeKind::IntBus, true, ! isMain });
            }
    for (auto& d : p.vcas)
        v.push_back ({ d.id, NodeKind::Vca, d.number >= 1 && d.number <= kMaxVcas && (m.vcaShown.load() & (1u << (d.number - 1))) != 0, true });
    return v;
}

inline int indexOf (const std::vector<Item>& v, const juce::Uuid& id)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].id == id) return (int) i;
    return -1;
}

/** The strips in the order the window shows them (shown false ones included: the window leaves them out). A place counts when its strip is here
    and movable and the strip it names is here (or it is null); the first one for a strip counts. Several after one strip keep the order of
    'places'; places that go round in a circle (A after B, B after A) are ignored. */
inline std::vector<Item> order (const std::vector<Item>& items, const std::vector<Place>& places)
{
    std::vector<Place> valid;
    for (auto& p : places)
    {
        const int s = indexOf (items, p.strip);
        if (s < 0 || ! items[(size_t) s].movable || p.strip == p.after) continue;
        if (! p.after.isNull() && indexOf (items, p.after) < 0) continue;
        bool dup = false;
        for (auto& q : valid) dup = dup || q.strip == p.strip;
        if (! dup) valid.push_back (p);
    }
    auto isPlaced = [&valid] (const juce::Uuid& id) { for (auto& q : valid) if (q.strip == id) return true; return false; };
    std::vector<Item> out;
    for (auto& i : items) if (! isPlaced (i.id)) out.push_back (i);
    std::vector<bool> done (valid.size(), false);
    std::map<juce::String, juce::Uuid> lastAfter;                       // anchor -> the strip put in after it last (by its text: Uuid has no ordering)
    for (bool progress = true; progress;)
    {
        progress = false;
        for (size_t k = 0; k < valid.size(); ++k)
        {
            if (done[k]) continue;
            const auto& anchor = valid[k].after;
            int at = -1;                                                // insert after index 'at' (-1: first)
            if (! anchor.isNull())
            {
                at = indexOf (out, anchor);
                if (at < 0) continue;                                   // its anchor is not in yet (a placed strip itself): a later pass
            }
            const auto key = anchor.toString();
            if (auto it = lastAfter.find (key); it != lastAfter.end()) at = juce::jmax (at, indexOf (out, it->second));
            out.insert (out.begin() + (at + 1), items[(size_t) indexOf (items, valid[k].strip)]);
            lastAfter[key] = valid[k].strip;
            done[k] = progress = true;
        }
    }
    for (size_t k = 0; k < valid.size(); ++k)                           // (a circle: those go back to their usual spot, the end of the list)
        if (! done[k]) out.push_back (items[(size_t) indexOf (items, valid[k].strip)]);
    return out;
}

/** Moves strip 'id' to right after 'after' (null: first), as a drag that ends between two strips does. False when nothing changes (it is
    already there, it cannot be moved, or 'after' is not a strip of this mixer). */
inline bool moveAfter (std::vector<Place>& places, const std::vector<Item>& items, const juce::Uuid& id, const juce::Uuid& after)
{
    const auto cur = order (items, places);
    const int at = indexOf (cur, id);
    if (at < 0 || ! cur[(size_t) at].movable || id == after) return false;
    if (! after.isNull() && indexOf (cur, after) < 0) return false;
    const auto left = at > 0 ? cur[(size_t) at - 1].id : juce::Uuid::null();
    if (left == after) return false;
    // drop the strip's stale entries (a place that did not count) so the one written below is the one that counts
    places.erase (std::remove_if (places.begin(), places.end(), [&id] (const Place& p) { return p.strip == id; }), places.end());
    for (auto& p : places) if (p.after == id) p.after = left;          // those right after it stay where they are on the screen
    for (auto& p : places) if (p.after == after) p.after = id;         // the one right after 'after' now follows it
    places.push_back ({ id, after });
    return true;
}

/** Strip 'id' back to its usual place (those right after it stay where they are on the screen). False when it was not moved. */
inline bool backToUsual (std::vector<Place>& places, const std::vector<Item>& items, const juce::Uuid& id)
{
    bool had = false;
    for (auto& p : places) had = had || p.strip == id;
    if (! had) return false;
    const auto cur = order (items, places);
    const int at = indexOf (cur, id);
    const auto left = at > 0 ? cur[(size_t) at - 1].id : juce::Uuid::null();
    places.erase (std::remove_if (places.begin(), places.end(), [&id] (const Place& p) { return p.strip == id; }), places.end());
    for (auto& p : places) if (p.after == id) p.after = left;
    return true;
}

/** Strips about to go (deleted tracks, a bus removed): a place right after one of them takes the nearest strip before it that stays (or the
    start), and the places OF those strips go. Call it before they go, with the items as they are now. */
inline void forget (std::vector<Place>& places, const std::vector<Item>& items, const std::vector<juce::Uuid>& going)
{
    if (places.empty() || going.empty()) return;
    auto gone = [&going] (const juce::Uuid& id) { return std::find (going.begin(), going.end(), id) != going.end(); };
    const auto cur = order (items, places);
    for (auto& p : places)
    {
        if (p.after.isNull() || ! gone (p.after)) continue;
        int i = indexOf (cur, p.after);
        if (i < 0) continue;                                            // (a place that did not count anyway: left as it is)
        juce::Uuid left = juce::Uuid::null();
        while (--i >= 0) if (! gone (cur[(size_t) i].id) && cur[(size_t) i].id != p.strip) { left = cur[(size_t) i].id; break; }
        p.after = left;
    }
    places.erase (std::remove_if (places.begin(), places.end(), [&gone] (const Place& p) { return gone (p.strip); }), places.end());
}
} // namespace striporder
} // namespace td
