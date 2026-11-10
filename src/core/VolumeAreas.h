#pragma once
/** 3.50.0: the volume changes made before there were volume areas (VolumeArea, Edit.h), turned into areas.

    Until 3.50.0 a volume change was written INTO the pieces, as an absolute level for every file: "from here to next edit" as one change in one
    piece, "between the 1/2 marks" as a group of them (GainChange::group / role). Those stay in old edits and play exactly as they did. When one
    is opened and kept in 3.50.0 it becomes an area: the functions here work out what the old change did - which tracks, how many dB, from where
    to where, with what fades - and take its points out of the pieces, leaving each piece the level it has of its own (the Trim window's).

    A change on its own had no end: it stopped at the join. Where the level was nevertheless carried on - the next piece starts at the same
    raise (the program gave a piece cut out of a raised one, or put in its place, that level as a loose copy) - and where it was brought back by
    hand with a second change, the whole run is read as ONE area: that is what was meant, and what can now be said. Nothing here guesses beyond
    that: a piece that does not carry the raise ends the area at the join.

    Header-only, no windows: tested in tests/core_test.cpp. */
#include "Edit.h"

namespace td::volumeareas
{
namespace detail
{
inline float dbOf (float linear) noexcept { return juce::jmax (-100.0f, juce::Decibels::gainToDecibels (linear, -100.0f)); }
/** The level the files that the change does NOT move have in common (their median): what a moved file's level is measured against. 0 when every file is moved. */
inline float common (const std::vector<float>& db, const std::vector<bool>& moved)
{
    std::vector<float> v;
    for (size_t k = 0; k < db.size(); ++k) if (k >= moved.size() || ! moved[k]) v.push_back (juce::jmax (-100.0f, db[k]));
    if (v.empty()) return 0.0f;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}
inline juce::int64 tolOf (const EditRegion& r) noexcept { return (juce::int64) (0.001 * r.sampleRate); }
/** The change at the very start of a piece that is the piece's own level (not part of a group, not the normalise glide); -1: none. */
inline int startLevelOf (const EditRegion& r)
{
    for (size_t i = 0; i < r.gains.size(); ++i)
        if (! r.gains[i].autoNorm && r.gains[i].group.isEmpty() && r.gains[i].at <= tolOf (r) && r.gains[i].ramp <= 0.0) return (int) i;
    return -1;
}
}

/** A "between the 1/2 marks" change, by its group: what it did, as an area (a new id), and its points taken out of the pieces. A point at the
    very start of a piece - where the area carried on into it - also held that piece's own level: that stays, less the area's part. False when
    there is no such group (nothing is changed). */
inline bool takeOutGroup (EditDef& e, const juce::String& group, VolumeArea& area)
{
    if (group.isEmpty()) return false;
    const double rate = juce::jmax (1.0, e.sampleRate);
    bool any = false, haveStart = false, haveEnd = false;
    juce::int64 t0 = std::numeric_limits<juce::int64>::max(), t1 = 0, lastPieceEnd = 0;
    double fin = 0.0, fout = 0.0;
    std::vector<std::pair<juce::Uuid, float>> delta;                           // track -> what the change does to it
    std::vector<juce::Uuid> known;                                             // the tracks whose part has been read (also those it leaves alone)
    auto has = [&] (const juce::Uuid& t) { return std::find (known.begin(), known.end(), t) != known.end(); };
    // What it does to a track is read where the area BEGAN inside a piece (role 1): there the level just before it is the piece's own, so the
    // difference is the change. A point at the start of a piece the area carried on into (role 2) holds that piece's own level and the change
    // in one figure - it only says anything about a track the first piece did not have. So: the role 1 points first, then the rest.
    for (int pass = 0; pass < 2; ++pass)
    for (auto& r : e.regions)
        for (size_t i = 0; i < r.gains.size(); ++i)
        {
            const auto& g = r.gains[i];
            if (g.group != group) continue;
            any = true;
            if ((pass == 0) != (g.role == 1)) continue;
            const auto rampS = (juce::int64) std::llround (g.ramp * rate);
            if (g.role == 3) { t1 = juce::jmax (t1, r.startSample + g.at + rampS); if (! haveEnd) { fout = g.ramp; haveEnd = true; } continue; }
            t0 = juce::jmin (t0, r.startSample + g.at);
            if (g.role == 1 && ! haveStart) { fin = g.ramp; haveStart = true; }
            lastPieceEnd = juce::jmax (lastPieceEnd, r.endSample());
            for (size_t k = 0; k < r.files.size() && k < g.db.size(); ++k)     // its level less the one just before it
            {
                if (has (r.files[k].trackId)) continue;
                known.push_back (r.files[k].trackId);
                const float before = detail::dbOf (r.levelAfter (k, i, g.at));
                const float d = g.db[k] <= -99.9f ? VolumeArea::kOff : g.db[k] - before;
                if (std::abs (d) > 0.05f) delta.push_back ({ r.files[k].trackId, d });
            }
        }
    if (! any) return false;
    if (! haveEnd) t1 = juce::jmax (t1, lastPieceEnd);                         // (no glide back: it lasted to the end of the last piece it was in)
    auto deltaOf = [&] (const juce::Uuid& t, float& d) { for (auto& x : delta) if (x.first == t) { d = x.second; return true; } return false; };
    for (auto& r : e.regions)
        for (size_t i = r.gains.size(); i-- > 0;)
        {
            auto& g = r.gains[i];
            if (g.group != group) continue;
            if (g.role == 2 && g.at <= detail::tolOf (r))                      // the area carried on into this piece: what is left is the piece's own level
            {
                bool allZero = true;
                for (size_t k = 0; k < r.files.size() && k < g.db.size(); ++k)
                {
                    float d = 0.0f;
                    if (deltaOf (r.files[k].trackId, d)) g.db[k] = d <= VolumeArea::kOff ? 0.0f : g.db[k] - d;     // (a track the area silenced: its own level is not known - 0 dB)
                    allZero = allZero && std::abs (g.db[k]) < 0.05f;
                }
                if (! allZero) { g.group = {}; g.role = 0; g.ramp = 0.0; continue; }
            }
            r.gains.erase (r.gains.begin() + (long) i);
        }
    area = VolumeArea(); area.id = juce::Uuid();
    area.from = t0 == std::numeric_limits<juce::int64>::max() ? 0 : t0; area.to = t1; area.fadeIn = fin; area.fadeOut = fout;
    for (auto& d : delta) area.setDb (d.first, d.second);
    return true;
}

/** What takeOutLoneChange found beyond the change itself (for the box to say). */
struct LoneFound
{
    int carriedInto = 0;            // how many following pieces started at the same raise
    bool broughtBack = false;       // a later change brought the tracks back: the area ends with it, and its glide is the fade out
};

/** A change on its own ("from here to next edit"): the change of piece 'regionId' at 'at' (samples from the piece's start, within 1 ms). What it
    did, as an area (a new id), and its points taken out of the pieces:
    - it starts where the change did, with the change's glide as its fade in, and is on the tracks the change moved (0.05 dB or more), by what
      it moved them;
    - it ends where a later change in the piece moves one of those tracks again - if that change brings them back to where they were against
      the other tracks, its glide is the fade out (and it is taken out too, when it does nothing else); otherwise the area just stops there;
    - with no such change it runs to the end of the piece, and on through every following piece that STARTS at the same raise (that start
      level loses the raise: the area gives it now), to a change in one of them that moves the tracks again (as above) or to the last one's end.
    False - and nothing changed - for a piece of the overdub layer (an area is of the timeline: it would change what lies under the overdub
    too), for the level a piece starts with (that is the piece's own), for the normalise glide, for a member of a group, and for a change
    that moves nothing. */
inline bool takeOutLoneChange (EditDef& e, const juce::Uuid& regionId, juce::int64 at, VolumeArea& area, LoneFound* found = nullptr)
{
    const int ri = e.indexOf (regionId);
    if (ri < 0) return false;
    const double rate = juce::jmax (1.0, e.sampleRate);
    int gi = -1;
    {
        const auto& P = e.regions[(size_t) ri];
        for (size_t i = 0; i < P.gains.size(); ++i)
            if (std::abs (P.gains[i].at - at) <= detail::tolOf (P) && ! P.gains[i].autoNorm && P.gains[i].group.isEmpty()) { gi = (int) i; break; }
        if (gi < 0 || gi == detail::startLevelOf (P)) return false;
    }
    // what it moves, and where those tracks stood against the others just before it
    std::vector<std::pair<juce::Uuid, float>> delta;                           // track -> dB (kOff: silenced)
    std::vector<std::pair<juce::Uuid, float>> offsetBefore;                    // track -> its level less the others', before the change
    {
        const auto& P = e.regions[(size_t) ri]; const auto& g = P.gains[(size_t) gi];
        std::vector<float> before (P.files.size(), 0.0f); std::vector<bool> moved (P.files.size(), false);
        for (size_t k = 0; k < P.files.size(); ++k)
        {
            before[k] = detail::dbOf (P.levelAfter (k, (size_t) gi, g.at));
            const float after = k < g.db.size() ? juce::jmax (-100.0f, g.db[k]) : 0.0f;
            moved[k] = std::abs (after - before[k]) >= 0.05f;
        }
        const float com = detail::common (before, moved);
        for (size_t k = 0; k < P.files.size(); ++k)
            if (moved[k])
            {
                const float after = juce::jmax (-100.0f, g.db[k]);
                delta.push_back ({ P.files[k].trackId, after <= -99.9f ? VolumeArea::kOff : after - before[k] });
                offsetBefore.push_back ({ P.files[k].trackId, before[k] - com });
            }
        if (delta.empty()) return false;
        area = VolumeArea(); area.id = juce::Uuid();
        area.from = P.startSample + g.at; area.to = P.endSample(); area.fadeIn = g.ramp; area.fadeOut = 0.0;
        for (auto& d : delta) area.setDb (d.first, d.second);
    }
    auto pairOf = [] (const std::vector<std::pair<juce::Uuid, float>>& v, const juce::Uuid& t, float& out) { for (auto& x : v) if (x.first == t) { out = x.second; return true; } return false; };
    LoneFound f;
    bool ended = false;
    // a later change of piece r (from index 'first') that moves one of the area's tracks: the area ends with it. Returns true when one was found.
    auto endsIn = [&] (EditRegion& r, size_t first, int skip)
    {
        for (size_t j = first; j < r.gains.size(); ++j)
        {
            if ((int) j == skip || r.gains[j].autoNorm) continue;
            const auto gj = r.gains[j];
            std::vector<bool> ours (r.files.size(), false); bool movesOurs = false;
            for (size_t k = 0; k < r.files.size() && k < gj.db.size(); ++k)
            {
                float d = 0.0f;
                ours[k] = pairOf (delta, r.files[k].trackId, d);
                if (ours[k] && std::abs (juce::jmax (-100.0f, gj.db[k]) - detail::dbOf (r.levelAfter (k, j, gj.at))) >= 0.05f) movesOurs = true;
            }
            if (! movesOurs) continue;
            const float com = detail::common (gj.db, ours);
            bool back = true;                                                  // does it put our tracks back where they stood against the others?
            for (size_t k = 0; k < r.files.size() && k < gj.db.size(); ++k)
                if (ours[k]) { float off = 0.0f; pairOf (offsetBefore, r.files[k].trackId, off); back = back && std::abs ((juce::jmax (-100.0f, gj.db[k]) - com) - off) < 0.1f; }
            const auto rampS = (juce::int64) std::llround (gj.ramp * rate);
            if (! back) { area.to = r.startSample + gj.at; return true; }      // something else begins there: the area just stops
            area.to = r.startSample + gj.at + rampS; area.fadeOut = gj.ramp; f.broughtBack = true;
            // does it do anything besides? Without it, where would the other tracks be when its glide arrives?
            EditRegion without = r; without.gains.erase (without.gains.begin() + (long) j);
            bool only = true;
            for (size_t k = 0; k < r.files.size() && k < gj.db.size(); ++k)
                if (! ours[k]) only = only && std::abs (juce::jmax (-100.0f, gj.db[k]) - detail::dbOf (without.levelAt (k, gj.at + rampS))) < 0.05f;
            if (only) r.gains.erase (r.gains.begin() + (long) j);              // (it was only the way back: the area's fade out is that now)
            return true;
        }
        return false;
    };
    // in its own piece. (The later changes are read while the raise is still in the piece: the change that brings a track back moves it only as
    // long as it is up. The same below, in the pieces that carry it.)
    {
        auto& P = e.regions[(size_t) ri];
        ended = endsIn (P, (size_t) gi + 1, gi);
        P.gains.erase (P.gains.begin() + gi);
    }
    // on through the pieces that start at the same raise
    for (size_t qi = (size_t) ri + 1; ! ended && qi < e.regions.size(); ++qi)
    {
        auto& Q = e.regions[qi];
        const int si = detail::startLevelOf (Q);
        if (si < 0) break;
        auto& s = Q.gains[(size_t) si];
        std::vector<bool> ours (Q.files.size(), false); bool anyOurs = false;
        for (size_t k = 0; k < Q.files.size(); ++k) { float d = 0.0f; ours[k] = pairOf (delta, Q.files[k].trackId, d); anyOurs = anyOurs || ours[k]; }
        if (! anyOurs) break;
        const float com = detail::common (s.db, ours);
        bool carries = true;
        for (size_t k = 0; k < Q.files.size() && k < s.db.size(); ++k)
            if (ours[k])
            {
                float d = 0.0f, off = 0.0f; pairOf (delta, Q.files[k].trackId, d); pairOf (offsetBefore, Q.files[k].trackId, off);
                const float have = juce::jmax (-100.0f, s.db[k]);
                carries = carries && (d <= VolumeArea::kOff ? have <= -99.9f : std::abs ((have - com) - (off + d)) < 0.1f);
            }
        if (! carries) break;
        ++f.carriedInto;
        area.to = Q.endSample();
        ended = endsIn (Q, 0, si);
        const int si2 = detail::startLevelOf (Q);                              // (the same change: endsIn may have taken one out)
        if (si2 < 0) continue;
        auto& s2 = Q.gains[(size_t) si2];
        bool allZero = true;                                                   // the start level loses the raise: the area gives it now
        for (size_t k = 0; k < Q.files.size() && k < s2.db.size(); ++k)
        {
            if (ours[k]) { float d = 0.0f; pairOf (delta, Q.files[k].trackId, d); s2.db[k] = d <= VolumeArea::kOff ? com : s2.db[k] - d; }
            allZero = allZero && std::abs (s2.db[k]) < 0.05f;
        }
        if (allZero) Q.gains.erase (Q.gains.begin() + si2);
    }
    if (found != nullptr) *found = f;
    return true;
}
} // namespace td::volumeareas
