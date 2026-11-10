#pragma once
#include "Edit.h"
#include "LowCut.h"
#include <functional>
#include <map>

namespace td
{
/** 3.42.0: the versions of an edit - "Edit 1", "Edit 2" ... (EditVersion in Edit.h) - and what is done with them:
      - freeze: the live edit becomes a frozen version and the work goes on in the next number;
      - mapSeconds: a moment on one version found on another (a listener's comment on Edit 1 shown on Edit 3's timeline);
      - compare / putBack: what the newer version changed, piece by piece, and any one change put back from the older one;
      - compareMix / patchedMix: the same for the edit's mixer (every setting, down to 0.1 dB).
    Shared code: fermata:Live only carries the versions through (it saves and loads them); fermata makes and shows them. */
namespace editversions
{
/** Freezes 'live' as Edit N (N = live.versionNumber()) and makes it Edit N + 1: a copy of the whole edit, with the comments that were on it (they
    were about this version) and its subheading, goes into 'versions'; the live edit keeps everything else and starts with no comments and no
    subheading. 'mixer' is Project::mixSnapshot of the edit's own mixer (with inserts), or void. Returns the number frozen. */
inline int freeze (EditDef& live, const juce::var& mixer, juce::Time now = juce::Time::getCurrentTime())
{
    EditVersion v;
    v.number = live.versionNumber(); v.subheading = live.subheading.trim(); v.frozenAt = now; v.mixer = mixer;
    v.edit = std::make_shared<EditDef> (live);
    v.edit->versions.clear(); v.edit->subheading = v.subheading;
    live.versions.push_back (std::move (v));
    live.subheading.clear();
    live.feedback.clear();
    return live.versions.back().number;
}

namespace detail
{
/** The sample t of 'from' found in 'to' (see mapSeconds, which is this in seconds). False when the music is not in 'to' any more. Neither edit is empty. */
inline bool placeOf (const EditDef& from, const EditDef& to, juce::int64 t, juce::int64& out)
{
    // the piece of 'from' at t, or the one before it (and how far past its end)
    int i = from.regionAt (t); juce::int64 past = 0;
    if (i < 0)
    {
        for (int k = 0; k < (int) from.regions.size(); ++k)
            if (from.regions[(size_t) k].endSample() <= t && (i < 0 || from.regions[(size_t) k].endSample() > from.regions[(size_t) i].endSample())) i = k;
        if (i < 0) { i = 0; past = t - from.regions.front().startSample; }                  // (before the first piece: its distance before it)
        else past = t - from.regions[(size_t) i].endSample();
    }
    const auto& r = from.regions[(size_t) i];
    const juce::int64 src = past > 0 ? r.srcOut : past < 0 ? r.srcIn : r.srcIn + (t - r.startSample);
    // the same music in 'to': the same piece, or any piece of the same take holding that place (the nearest to where it was)
    const EditRegion* best = nullptr; juce::int64 bestDist = 0;
    for (auto& c : to.regions)
    {
        if (c.id != r.id && (c.takeId.isNull() || c.takeId != r.takeId)) continue;
        if (src < c.srcIn || src > c.srcOut) continue;
        const auto tt = c.startSample + (src - c.srcIn);
        const auto d = std::abs (tt - t);
        if (best == nullptr || c.id == r.id || d < bestDist) { best = &c; bestDist = d; if (c.id == r.id) break; }
    }
    if (best == nullptr) return false;
    out = juce::jmax ((juce::int64) 0, best->startSample + (src - best->srcIn) + past);
    return true;
}
}

/** A moment on version 'from' found on version 'to', by the music: the take and the place in it that was playing there. In a gap, or past the
    end, it keeps its distance from the piece before it. When the music is not in 'to' any more the time is kept and 'floating' is set. */
inline double mapSeconds (const EditDef& from, const EditDef& to, double seconds, bool* floating = nullptr)
{
    if (floating) *floating = false;
    if (&from == &to) return seconds;
    const double rate = from.sampleRate > 0.0 ? from.sampleRate : to.sampleRate;
    if (rate <= 0.0 || from.regions.empty() || to.regions.empty()) { if (floating) *floating = ! from.regions.empty() || ! to.regions.empty(); return seconds; }
    juce::int64 out = 0;
    if (! detail::placeOf (from, to, (juce::int64) std::llround (seconds * rate), out)) { if (floating) *floating = true; return seconds; }
    return (double) out / rate;
}

/** 3.47.0 (decided on 10 November 2026: "sliding pieces should also move markers and overdubs"): what lies over an edit's pieces - the overdubs,
    the markers, the comment flags, marks 1 / 2, the playhead and the automation points fixed in time - as it stood BEFORE pieces are slid,
    shuffled or put in another order, so that each can be put back on its music afterwards (carry). The rule is mapSeconds': a thing stays over
    the same place in the same piece; in a silence, or past the end, it stays with the piece before it; one whose music is no longer there (a
    piece slid under a join until that place was cut off) stays where it was. Putting a piece in, or taking one out, has its own way
    (EditDef::rippleAfter); this is for the operations that move pieces about.

    A drag makes ONE of these when it starts and carries from it at every move of the mouse: carried from the move before, a piece sliding
    over a marker in a silence would pick it up and take it along. */
class Riders
{
public:
    explicit Riders (const EditDef& e) : fixIn (e.fixIn), fixOut (e.fixOut), playhead (e.playheadSeconds)
    {
        was.sampleRate = e.sampleRate; was.regions = e.regions;
        for (auto& o : e.overdubs) overdubs.push_back ({ o.id.toString(), (double) o.startSample });
        for (auto& m : e.markers)  markers.push_back ({ m.id.toString(), m.seconds });
        for (auto& f : e.feedback) feedback.push_back ({ f.key, f.seconds });
        for (auto& l : e.lanes) for (auto& p : l.pts) if (p.region.isNull()) points[p.id] = p.time;
        for (auto& a : e.areas) areas[a.id] = { a.from, a.to };                           // 3.50.0: the volume areas
    }

    /** Puts everything where its music is now. What was not there when this was made (a marker added since) is left alone. */
    void carry (EditDef& e) const
    {
        if (was.sampleRate <= 0.0 || was.regions.empty() || e.regions.empty()) return;
        auto smp = [&] (juce::int64 t) { juce::int64 o = t; return detail::placeOf (was, e, t, o) ? o : t; };
        auto sec = [&] (double s) { return s < 0.0 ? s : mapSeconds (was, e, s); };
        auto then = [] (const std::vector<std::pair<juce::String, double>>& list, size_t i, const juce::String& id) -> const double*
        {
            if (i < list.size() && list[i].first == id) return &list[i].second;                 // (the usual case: the same list in the same order)
            for (auto& x : list) if (x.first == id) return &x.second;
            return nullptr;
        };
        for (size_t i = 0; i < e.overdubs.size(); ++i) if (auto* t = then (overdubs, i, e.overdubs[i].id.toString())) e.overdubs[i].startSample = smp ((juce::int64) *t);
        for (size_t i = 0; i < e.markers.size(); ++i)  if (auto* t = then (markers, i, e.markers[i].id.toString()))   e.markers[i].seconds = sec (*t);
        for (size_t i = 0; i < e.feedback.size(); ++i) if (auto* t = then (feedback, i, e.feedback[i].key))           e.feedback[i].seconds = sec (*t);
        e.fixIn = sec (fixIn); e.fixOut = sec (fixOut);
        if (e.fixIn >= 0.0 && e.fixOut >= 0.0 && e.fixOut <= e.fixIn) e.fixIn = e.fixOut = -1.0;      // (the marked music is in two places now, the wrong way round: nothing is marked)
        e.playheadSeconds = juce::jmax (0.0, sec (playhead));
        for (auto& l : e.lanes)
        {
            bool any = false;
            for (auto& p : l.pts)
                if (p.region.isNull())
                    if (const auto it = points.find (p.id); it != points.end()) { const auto t = smp (it->second); any = any || t != p.time; p.time = t; }
            if (any) l.sortPoints();
        }
        for (auto& a : e.areas)                                                // 3.50.0: each end of a volume area stays over its music (an area whose music is now
            if (const auto it = areas.find (a.id); it != areas.end())         //  the other way round - a shuffle took one end past the other - covers what lies between)
            {
                a.from = smp (it->second.first); a.to = smp (it->second.second);
                if (a.to < a.from) std::swap (a.from, a.to);
            }
    }

private:
    EditDef was;                                                           // (its pieces and its sample rate: all mapSeconds reads)
    std::vector<std::pair<juce::String, double>> overdubs, markers, feedback;  // id (a comment flag's key) -> where it was
    std::map<juce::Uuid, juce::int64> points;
    std::map<juce::Uuid, std::pair<juce::int64, juce::int64>> areas;
    double fixIn = -1.0, fixOut = -1.0, playhead = 0.0;
};

/** The last freeze undone: the newest frozen version comes off the list, its subheading comes back, and its comments come onto the live edit again
    (at the same music). False when there is none. */
inline bool unfreezeLast (EditDef& live)
{
    if (live.versions.empty()) return false;
    auto v = live.versions.back(); live.versions.pop_back();
    live.subheading = v.subheading;
    if (v.edit != nullptr)
    {
        auto notes = v.edit->feedback;
        for (auto& n : notes) n.seconds = mapSeconds (*v.edit, live, n.seconds);
        feedback::merge (live.feedback, notes);
    }
    return true;
}

// ----------------------------------------------------------------------------- what changed
/** One change the newer version (B) made to the older (A): a span of the main track (one piece, or a run of pieces changed together), an overdub, or
    an automation lane. 'label' is the word on the flag, 'text' the full account. The spans are samples on each version's timeline; an empty span
    (from == to) is where something taken out used to be, or where something new was put. */
struct Change
{
    enum class Kind { Piece, Added, Removed, Moved, Overdub, OverdubAdded, OverdubRemoved, Lane };
    Kind kind = Kind::Piece;
    juce::String label, text;
    juce::int64 bFrom = 0, bTo = 0, aFrom = 0, aTo = 0;
    juce::int64 bWas = 0;                              // Kind::Moved: where on B's timeline the piece used to be (putting it back takes it out at bFrom..bTo and puts it in here)
    juce::Uuid overdubId = juce::Uuid::null();         // the overdub (Overdub kinds)
    juce::Uuid laneTrack = juce::Uuid::null(), laneMixer = juce::Uuid::null(); juce::String laneParam;     // the lane (Kind::Lane)
    bool onMainTrack() const noexcept { return kind == Kind::Piece || kind == Kind::Added || kind == Kind::Removed || kind == Kind::Moved; }
};

namespace detail
{
inline juce::String secs (double s) { return juce::String (std::abs (s), std::abs (s) < 1.0 ? 3 : 2) + " s"; }
inline juce::String ms (double s) { return juce::String (juce::roundToInt (s * 1000.0)) + " ms"; }
inline juce::String db (float d) { return (d > 0.0f ? "+" : "") + juce::String (d, 1) + " dB"; }
inline juce::String minSec (double s)
{
    const int m = (int) (s / 60.0); const double r = s - m * 60.0;
    return juce::String (m) + ":" + (r < 9.995 ? "0" : "") + juce::String (r, 2);
}
inline bool sameFiles (const EditRegion& a, const EditRegion& b)
{
    if (a.files.size() != b.files.size()) return false;
    for (size_t i = 0; i < a.files.size(); ++i)
        if (a.files[i].file != b.files[i].file || a.files[i].fileStart != b.files[i].fileStart || a.files[i].trackId != b.files[i].trackId) return false;
    return a.waiting.active() == b.waiting.active();
}
inline bool sameGains (const EditRegion& a, const EditRegion& b)
{
    if (a.gains.size() != b.gains.size()) return false;
    for (size_t i = 0; i < a.gains.size(); ++i)
    {
        const auto& x = a.gains[i]; const auto& y = b.gains[i];
        if (x.at != y.at || std::abs (x.ramp - y.ramp) > 1.0e-6 || x.autoNorm != y.autoNorm || x.db.size() != y.db.size()) return false;
        for (size_t k = 0; k < x.db.size(); ++k) if (std::abs (x.db[k] - y.db[k]) > 0.005f) return false;
    }
    return true;
}
/** What differs between the same piece in A and in B (nothing about where it sits on the timeline). */
inline juce::StringArray pieceDifferences (const EditRegion& a, const EditRegion& b, juce::StringArray& labels)
{
    juce::StringArray out;
    const double rate = juce::jmax (1.0, b.sampleRate);
    auto add = [&] (const juce::String& label, const juce::String& text) { out.add (text); if (! labels.contains (label)) labels.add (label); };
    if (! sameFiles (a, b)) add ("fix", b.waiting.active() ? "waiting for corrected audio" : "its audio was replaced (a fix)");
    if (a.srcIn != b.srcIn)
    {
        const double d = (double) (b.srcIn - a.srcIn) / rate;
        add ("trim", "starts " + secs (d) + (d > 0 ? " later" : " earlier") + " in the take");
    }
    if (a.srcOut != b.srcOut)
    {
        const double d = (double) (b.srcOut - a.srcOut) / rate;
        add ("trim", "ends " + secs (d) + (d > 0 ? " later" : " earlier") + " in the take");
    }
    const double tol = 0.0005;
    if (std::abs (a.inStart - b.inStart) > tol || std::abs (a.inEnd - b.inEnd) > tol || a.curve != b.curve)
    {
        const double la = a.inEnd - a.inStart, lb = b.inEnd - b.inStart;
        juce::String t = "fade in";
        if (std::abs (la - lb) > tol) t += " " + ms (la) + " -> " + ms (lb);
        else if (std::abs (a.inStart - b.inStart) > tol) t += " moved " + secs (b.inStart - a.inStart) + (b.inStart > a.inStart ? " later" : " earlier");
        if (a.curve != b.curve) t += juce::String (t.endsWith ("fade in") ? " " : ", ") + "curve " + juce::String (fadeCurveName (a.curve)).upToFirstOccurrenceOf (" (", false, false) + " -> " + juce::String (fadeCurveName (b.curve)).upToFirstOccurrenceOf (" (", false, false);
        add ("fade", t);
    }
    if (std::abs (a.outStart - b.outStart) > tol || std::abs (a.outEnd - b.outEnd) > tol || a.outCurveValue() != b.outCurveValue())
    {
        const double la = a.outEnd - a.outStart, lb = b.outEnd - b.outStart;
        juce::String t = "fade out";
        if (std::abs (la - lb) > tol) t += " " + ms (la) + " -> " + ms (lb);
        else if (std::abs (a.outEnd - b.outEnd) > tol) t += " moved " + secs (b.outEnd - a.outEnd) + (b.outEnd > a.outEnd ? " later" : " earlier");
        if (a.outCurveValue() != b.outCurveValue()) t += juce::String (t.endsWith ("fade out") ? " " : ", ") + "curve " + juce::String (fadeCurveName (a.outCurveValue())).upToFirstOccurrenceOf (" (", false, false) + " -> " + juce::String (fadeCurveName (b.outCurveValue())).upToFirstOccurrenceOf (" (", false, false);
        add ("fade", t);
    }
    if (! sameGains (a, b))
    {
        if (std::abs (a.pieceLevelDb() - b.pieceLevelDb()) > 0.005f) add ("level", "level " + db (a.pieceLevelDb()) + " -> " + db (b.pieceLevelDb()));
        else add ("level", "the volume changes inside it changed");
    }
    if (a.normaliseIn != b.normaliseIn) add ("level", juce::String ("normalise over the next take ") + (b.normaliseIn ? "on" : "off"));
    return out;
}
inline juce::String pieceName (const EditRegion& r) { return r.takeName.isNotEmpty() ? r.takeName : juce::String ("a piece"); }
inline juce::String joinLabels (const juce::StringArray& l) { return l.joinIntoString (" "); }
} // namespace detail

/** What B (the newer version) changed since A (the older): one Change per piece or run of pieces, per overdub and per automation lane.
    The main track is compared piece by piece - the same piece (by id, or the same take's audio re-cut) in both - and the pieces only in one of
    them are "taken out" or "new". Where things moved on the timeline only because something before them got longer or shorter, nothing is
    reported: the music itself did not change there. */
inline std::vector<Change> compare (const EditDef& a, const EditDef& b)
{
    std::vector<Change> out;
    const double rate = b.sampleRate > 0.0 ? b.sampleRate : (a.sampleRate > 0.0 ? a.sampleRate : 48000.0);
    const int na = (int) a.regions.size(), nb = (int) b.regions.size();
    // ---- the main track: which piece of A each piece of B is
    std::vector<int> aOf ((size_t) nb, -1); std::vector<char> aUsed ((size_t) na, 0);
    for (int j = 0; j < nb; ++j)
        for (int i = 0; i < na; ++i)
            if (! aUsed[(size_t) i] && a.regions[(size_t) i].id == b.regions[(size_t) j].id) { aOf[(size_t) j] = i; aUsed[(size_t) i] = 1; break; }
    for (int j = 0; j < nb; ++j)                                               // (the same take's audio, re-cut: a snip, a split, a replaced passage)
    {
        if (aOf[(size_t) j] >= 0) continue;
        const auto& rb = b.regions[(size_t) j];
        if (rb.takeId.isNull()) continue;
        int best = -1; juce::int64 bestOverlap = 0;
        for (int i = 0; i < na; ++i)
        {
            const auto& ra = a.regions[(size_t) i];
            if (aUsed[(size_t) i] || ra.takeId != rb.takeId) continue;
            const auto ov = juce::jmin (ra.srcOut, rb.srcOut) - juce::jmax (ra.srcIn, rb.srcIn);
            if (ov > bestOverlap) { bestOverlap = ov; best = i; }
        }
        if (best >= 0) { aOf[(size_t) j] = best; aUsed[(size_t) best] = 1; }
    }
    // the matched pairs that are in the same order in both (the longest such run): the rest moved, and count as taken out there and new here
    std::vector<int> seq;                                                      // B indices with a match
    for (int j = 0; j < nb; ++j) if (aOf[(size_t) j] >= 0) seq.push_back (j);
    std::vector<int> lisLen (seq.size(), 1), lisPrev (seq.size(), -1);
    int bestEnd = -1;
    for (size_t k = 0; k < seq.size(); ++k)
    {
        for (size_t m = 0; m < k; ++m)
            if (aOf[(size_t) seq[m]] < aOf[(size_t) seq[k]] && lisLen[m] + 1 > lisLen[k]) { lisLen[k] = lisLen[m] + 1; lisPrev[k] = (int) m; }
        if (bestEnd < 0 || lisLen[k] > lisLen[(size_t) bestEnd]) bestEnd = (int) k;
    }
    std::vector<char> aligned ((size_t) nb, 0);
    for (int k = bestEnd; k >= 0; k = lisPrev[(size_t) k]) aligned[(size_t) seq[(size_t) k]] = 1;
    std::vector<int> bOfA ((size_t) na, -1);
    for (int j = 0; j < nb; ++j) if (aligned[(size_t) j]) bOfA[(size_t) aOf[(size_t) j]] = j;

    // where a piece of A that is gone would be on B: after the B piece of the nearest aligned A piece before it (else before the next one, else 0)
    auto bPositionOfA = [&] (int i) -> juce::int64
    {
        for (int k = i - 1; k >= 0; --k) if (bOfA[(size_t) k] >= 0) return b.regions[(size_t) bOfA[(size_t) k]].endSample();
        for (int k = i + 1; k < na; ++k) if (bOfA[(size_t) k] >= 0) return b.regions[(size_t) bOfA[(size_t) k]].startSample;
        return 0;
    };
    auto aPositionOfB = [&] (int j) -> juce::int64
    {
        for (int k = j - 1; k >= 0; --k) if (aligned[(size_t) k]) return a.regions[(size_t) aOf[(size_t) k]].endSample();
        for (int k = j + 1; k < nb; ++k) if (aligned[(size_t) k]) return a.regions[(size_t) aOf[(size_t) k]].startSample;
        return 0;
    };
    std::vector<Change> main;
    // A's pieces that are not in B: taken out (a piece that is in B somewhere else is "moved", reported once, where it is now)
    for (int i = 0; i < na; ++i)
        if (bOfA[(size_t) i] < 0)
        {
            bool moved = false; for (int j = 0; j < nb; ++j) if (aOf[(size_t) j] == i) moved = true;
            if (moved) continue;
            const auto& ra = a.regions[(size_t) i];
            Change c; c.kind = Change::Kind::Removed; c.label = "cut";
            c.text = detail::pieceName (ra) + " taken out (" + detail::secs ((double) ra.length() / rate) + ", was at " + detail::minSec ((double) ra.startSample / rate) + ")";
            c.aFrom = ra.startSample; c.aTo = ra.endSample();
            c.bFrom = c.bTo = bPositionOfA (i);
            main.push_back (c);
        }
    for (int j = 0; j < nb; ++j)
    {
        const auto& rb = b.regions[(size_t) j];
        if (! aligned[(size_t) j])
        {
            const bool moved = aOf[(size_t) j] >= 0;
            Change c; c.kind = moved ? Change::Kind::Moved : Change::Kind::Added; c.label = moved ? "moved" : "new";
            c.bFrom = rb.startSample; c.bTo = rb.endSample();
            if (moved)
            {
                const auto& ra = a.regions[(size_t) aOf[(size_t) j]];
                c.aFrom = ra.startSample; c.aTo = ra.endSample(); c.bWas = bPositionOfA (aOf[(size_t) j]);
                juce::StringArray labels; const auto diffs = detail::pieceDifferences (ra, rb, labels);
                c.text = detail::pieceName (rb) + " moved here from " + detail::minSec ((double) ra.startSample / rate) + " (" + detail::secs ((double) rb.length() / rate) + ")" + (diffs.isEmpty() ? juce::String() : "; " + diffs.joinIntoString ("; "));
                for (auto& l : labels) c.label += " " + l;
            }
            else
            {
                c.text = detail::pieceName (rb) + " new (" + detail::secs ((double) rb.length() / rate) + ")";
                c.aFrom = c.aTo = aPositionOfB (j);
            }
            main.push_back (c);
            continue;
        }
        const auto& ra = a.regions[(size_t) aOf[(size_t) j]];
        juce::StringArray labels;
        auto diffs = detail::pieceDifferences (ra, rb, labels);
        // the silence before it (against the previous piece, when that is the same piece in both)
        if (j > 0 && aligned[(size_t) j - 1] && aOf[(size_t) j] > 0 && aOf[(size_t) j - 1] == aOf[(size_t) j] - 1)
        {
            const auto gapA = ra.startSample - a.regions[(size_t) aOf[(size_t) j] - 1].endSample(), gapB = rb.startSample - b.regions[(size_t) j - 1].endSample();
            if (std::abs (gapA - gapB) > (juce::int64) (0.001 * rate) && (gapA > 0 || gapB > 0))
            {
                diffs.add ("gap before it " + detail::secs ((double) juce::jmax ((juce::int64) 0, gapA) / rate) + " -> " + detail::secs ((double) juce::jmax ((juce::int64) 0, gapB) / rate));
                if (! labels.contains ("gap")) labels.add ("gap");
            }
        }
        if (diffs.isEmpty()) continue;
        Change c; c.kind = Change::Kind::Piece; c.label = detail::joinLabels (labels);
        c.text = detail::pieceName (rb) + ": " + diffs.joinIntoString ("; ");
        c.bFrom = rb.startSample; c.bTo = rb.endSample(); c.aFrom = ra.startSample; c.aTo = ra.endSample();
        if (labels.contains ("gap")) { c.bFrom = b.regions[(size_t) j - 1].endSample(); c.aFrom = a.regions[(size_t) aOf[(size_t) j] - 1].endSample(); }
        main.push_back (c);
    }
    // in B's order; changes whose spans touch are one change (a run of pieces re-cut together is put back together)
    std::stable_sort (main.begin(), main.end(), [] (const Change& x, const Change& y) { return x.bFrom < y.bFrom || (x.bFrom == y.bFrom && x.aFrom < y.aFrom); });
    for (auto& c : main)
    {
        if (! out.empty() && out.back().onMainTrack() && out.back().kind != Change::Kind::Moved && c.kind != Change::Kind::Moved && c.bFrom <= out.back().bTo && c.aFrom <= out.back().aTo + 1)
        {
            auto& p = out.back();
            p.kind = Change::Kind::Piece;
            for (auto& w : juce::StringArray::fromTokens (c.label, " ", "")) if (! p.label.contains (w)) p.label += " " + w;
            p.text += "\n" + c.text;
            p.bTo = juce::jmax (p.bTo, c.bTo); p.aTo = juce::jmax (p.aTo, c.aTo);
            p.bFrom = juce::jmin (p.bFrom, c.bFrom); p.aFrom = juce::jmin (p.aFrom, c.aFrom);
        }
        else out.push_back (c);
    }
    // ---- the overdubs (by id)
    for (auto& ob : b.overdubs)
    {
        const EditRegion* oa = nullptr; for (auto& x : a.overdubs) if (x.id == ob.id) oa = &x;
        if (oa == nullptr)
        {
            Change c; c.kind = Change::Kind::OverdubAdded; c.label = "overdub"; c.overdubId = ob.id;
            c.text = "Overdub " + detail::pieceName (ob) + " new (" + detail::secs ((double) ob.length() / rate) + ")";
            c.bFrom = ob.startSample; c.bTo = ob.endSample(); c.aFrom = c.aTo = ob.startSample;
            out.push_back (c); continue;
        }
        juce::StringArray labels; auto diffs = detail::pieceDifferences (*oa, ob, labels);
        if (oa->startSample != ob.startSample)
        {
            const double d = (double) (ob.startSample - oa->startSample) / rate;
            diffs.insert (0, "moved " + detail::secs (d) + (d > 0 ? " later" : " earlier") + " (was at " + detail::minSec ((double) oa->startSample / rate) + ")");
            labels.insert (0, "moved");
        }
        if (diffs.isEmpty()) continue;
        Change c; c.kind = Change::Kind::Overdub; c.label = "overdub " + detail::joinLabels (labels); c.overdubId = ob.id;
        c.text = "Overdub " + detail::pieceName (ob) + ": " + diffs.joinIntoString ("; ");
        c.bFrom = ob.startSample; c.bTo = ob.endSample(); c.aFrom = oa->startSample; c.aTo = oa->endSample();
        out.push_back (c);
    }
    for (auto& oa : a.overdubs)
    {
        bool there = false; for (auto& x : b.overdubs) if (x.id == oa.id) there = true;
        if (there) continue;
        Change c; c.kind = Change::Kind::OverdubRemoved; c.label = "overdub cut"; c.overdubId = oa.id;
        c.text = "Overdub " + detail::pieceName (oa) + " taken out (" + detail::secs ((double) oa.length() / rate) + ", was at " + detail::minSec ((double) oa.startSample / rate) + ")";
        c.aFrom = oa.startSample; c.aTo = oa.endSample(); c.bFrom = c.bTo = (juce::int64) std::llround (mapSeconds (a, b, (double) oa.startSample / rate) * rate);
        out.push_back (c);
    }
    // ---- the automation lanes (by track, parameter and mixer): the points, as they lie on each timeline
    auto laneDiffers = [] (const AutoLane* la, const AutoLane* lb)
    {
        const size_t n1 = la ? la->pts.size() : 0, n2 = lb ? lb->pts.size() : 0;
        if (n1 != n2) return true;
        for (size_t k = 0; k < n1; ++k)
            if (la->pts[k].time != lb->pts[k].time || std::abs (la->pts[k].value - lb->pts[k].value) > 1.0e-4f) return true;
        return false;
    };
    std::vector<const AutoLane*> seenA;
    for (auto& lb : b.lanes)
    {
        const AutoLane* la = nullptr; for (auto& x : a.lanes) if (x.trackId == lb.trackId && x.param == lb.param && x.mixerId == lb.mixerId) la = &x;
        if (la != nullptr) seenA.push_back (la);
        if (! laneDiffers (la, &lb)) continue;
        Change c; c.kind = Change::Kind::Lane; c.label = "automation"; c.laneTrack = lb.trackId; c.laneParam = lb.param; c.laneMixer = lb.mixerId;
        juce::int64 lo = std::numeric_limits<juce::int64>::max(), hi = 0;
        for (auto* l : { la, (const AutoLane*) &lb }) if (l != nullptr) for (auto& p : l->pts) { lo = juce::jmin (lo, p.time); hi = juce::jmax (hi, p.time); }
        if (lo == std::numeric_limits<juce::int64>::max()) lo = 0;
        c.text = juce::String ("Automation (") + lb.param + "): " + (la == nullptr ? "new lane, " : "") + juce::String ((int) lb.pts.size()) + " point" + (lb.pts.size() == 1 ? "" : "s")
               + (la != nullptr ? " (was " + juce::String ((int) la->pts.size()) + ")" : juce::String());
        c.bFrom = lo; c.bTo = hi; c.aFrom = lo; c.aTo = hi;
        out.push_back (c);
    }
    for (auto& la : a.lanes)
    {
        if (la.pts.empty() || std::find (seenA.begin(), seenA.end(), &la) != seenA.end()) continue;
        Change c; c.kind = Change::Kind::Lane; c.label = "automation"; c.laneTrack = la.trackId; c.laneParam = la.param; c.laneMixer = la.mixerId;
        c.text = juce::String ("Automation (") + la.param + "): the lane's " + juce::String ((int) la.pts.size()) + " point" + (la.pts.size() == 1 ? "" : "s") + " taken away";
        c.aFrom = la.pts.front().time; c.aTo = la.pts.back().time; c.bFrom = c.bTo = (juce::int64) std::llround (mapSeconds (a, b, (double) c.aFrom / rate) * rate);
        out.push_back (c);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Change& x, const Change& y) { return x.bFrom < y.bFrom; });
    return out;
}

/** Puts one change back from A into B: the span's pieces as A had them (B's pieces there go; what comes after moves by the difference in length,
    as a replaced passage does), the overdub as A had it (or taken away again), the lane's points as A had them. Returns an error text, or "". */
inline juce::String putBack (EditDef& b, const EditDef& a, const Change& c)
{
    const double rate = b.sampleRate > 0.0 ? b.sampleRate : a.sampleRate;
    if (rate <= 0.0) return "The edit is empty.";
    if (c.kind == Change::Kind::Lane)
    {
        const AutoLane* la = nullptr; for (auto& x : a.lanes) if (x.trackId == c.laneTrack && x.param == c.laneParam && x.mixerId == c.laneMixer) la = &x;
        auto& lb = b.addLane (c.laneTrack, c.laneParam, c.laneMixer);
        lb.pts = la != nullptr ? la->pts : std::vector<AutoPoint>();
        for (auto& p : lb.pts) p.id = juce::Uuid();
        lb.sortPoints();
        b.resolveAutomation();
        return {};
    }
    if (c.kind == Change::Kind::Overdub || c.kind == Change::Kind::OverdubAdded || c.kind == Change::Kind::OverdubRemoved)
    {
        const EditRegion* oa = nullptr; for (auto& x : a.overdubs) if (x.id == c.overdubId) oa = &x;
        b.removeOverdub (c.overdubId);
        if (oa != nullptr) { b.overdubs.push_back (*oa); if (b.sampleRate <= 0.0) b.sampleRate = oa->sampleRate; }
        return {};
    }
    // ---- the main track
    if (c.kind == Change::Kind::Moved)
    {   // out where it is now, then in where it was (as A had it)
        Change out = c; out.kind = Change::Kind::Added; out.aFrom = out.aTo = c.aFrom;
        auto err = putBack (b, a, out);
        if (err.isNotEmpty()) return err;
        Change in = c; in.kind = Change::Kind::Removed;
        in.bFrom = in.bTo = c.bWas >= c.bTo ? c.bWas - (c.bTo - c.bFrom) : c.bWas;
        return putBack (b, a, in);
    }
    if (c.bFrom > c.bTo || c.aFrom > c.aTo) return "That change cannot be put back.";
    // A's pieces inside [aFrom, aTo), trimmed to it
    std::vector<EditRegion> put;
    for (auto& ra : a.regions)
    {
        if (ra.endSample() <= c.aFrom || ra.startSample >= c.aTo) continue;
        EditRegion r = ra;
        if (r.startSample < c.aFrom)
        {
            const auto d = c.aFrom - r.startSample;
            const EditRegion old = r;
            r.srcIn += d; r.startSample = c.aFrom; r.rebaseGains (old, d);
            r.inStart = r.inEnd = 0.0; r.fixIn = r.snipIn = false;
        }
        if (r.endSample() > c.aTo) { r.srcOut -= r.endSample() - c.aTo; r.outStart = r.outEnd = 0.0; r.fixOut = r.snipOut = false; }
        if (r.length() <= 0) continue;
        r.startSample += c.bFrom - c.aFrom;
        put.push_back (std::move (r));
    }
    // B: cut at the span's edges, take out what is inside
    for (auto cut : { c.bTo, c.bFrom })
        for (int i = 0; i < (int) b.regions.size(); ++i)
        {
            auto& x = b.regions[(size_t) i];
            if (cut > x.startSample && cut < x.endSample()) { b.splitRegion (i, cut - x.startSample); break; }
        }
    for (int i = 0; i < (int) b.regions.size();)
    {
        const auto& x = b.regions[(size_t) i];
        if (x.startSample >= c.bFrom && x.endSample() <= c.bTo && c.bTo > c.bFrom) b.regions.erase (b.regions.begin() + i); else ++i;
    }
    for (auto& r : put) if (b.find (r.id) != nullptr) r.id = juce::Uuid();        // (a piece that is still in B elsewhere: the copy is a new piece)
    const juce::int64 delta = (c.aTo - c.aFrom) - (c.bTo - c.bFrom);
    for (auto& x : b.regions) if (x.startSample >= c.bTo) x.startSample += delta;
    b.rippleAfter (c.bTo, delta);
    int at = (int) b.regions.size();
    for (int i = 0; i < (int) b.regions.size(); ++i) if (b.regions[(size_t) i].startSample >= c.bFrom) { at = i; break; }
    const int nPut = (int) put.size();
    b.regions.insert (b.regions.begin() + at, put.begin(), put.end());
    if (b.sampleRate <= 0.0 && ! put.empty()) b.sampleRate = put.front().sampleRate;
    b.insertIndex = -1;
    // the joins at the two edges: A's fades when the neighbour is the same piece as in A, else the default crossfade; the joins inside came with the pieces
    auto edgeJoin = [&] (int k, bool start)
    {
        if (k <= 0 || k >= (int) b.regions.size()) return;
        const auto& left = b.regions[(size_t) k - 1]; const auto& right = b.regions[(size_t) k];
        const EditRegion* la = nullptr; const EditRegion* ra = nullptr;
        for (auto& x : a.regions) { if (x.id == left.id) la = &x; if (x.id == right.id) ra = &x; }
        const bool sameAsA = la != nullptr && ra != nullptr && (la - &a.regions.front()) + 1 == (ra - &a.regions.front());
        if (sameAsA)
        {
            auto& l = b.regions[(size_t) k - 1]; auto& r = b.regions[(size_t) k];
            l.outStart = la->outStart; l.outEnd = la->outEnd; l.setOutCurve (la->outCurveValue()); l.fixOut = la->fixOut; l.snipOut = la->snipOut;
            r.inStart = ra->inStart; r.inEnd = ra->inEnd; r.setInCurve (ra->curve); r.fixIn = ra->fixIn; r.snipIn = ra->snipIn;
            b.clampFades (k - 1); b.clampFades (k);
        }
        else
        {
            juce::ignoreUnused (start);
            b.regions[(size_t) k - 1].fixOut = b.regions[(size_t) k - 1].snipOut = false; b.regions[(size_t) k].fixIn = b.regions[(size_t) k].snipIn = false;
            b.setDefaultJoinFades (k);
        }
    };
    edgeJoin (at, true);
    edgeJoin (at + nPut, false);
    if (nPut == 0 && at > 0 && at < (int) b.regions.size()) edgeJoin (at, true);
    b.hardenEnds();
    b.settleGapFades();
    b.clampAll();
    b.refreshNormalised();
    b.resolveAutomation();
    return {};
}

// ----------------------------------------------------------------------------- the mixer
/** One setting of the mixer that differs between two snapshots (Project::mixSnapshot). 'path' finds it again in a snapshot: the list's key
    ("strips" / "buses" / "vcaStates"), the strip's id (track, bus or VCA number), then the keys down to the leaf (a send: "sendList", its bus id, the
    key; a plug-in: "slots", its number). 'insert' marks a change of a plug-in (putting it back puts the plug-in's state back: the inserts are rebuilt). */
struct MixChange
{
    juce::StringArray path;
    juce::String text;
    juce::var from, to;                  // the older and the newer value (void = not there)
    bool insert = false;
};

namespace detail
{
inline juce::String panText (double p)
{
    const int v = juce::roundToInt (std::abs (p) * 100.0);
    return v == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (v);
}
inline juce::String onOff (const juce::var& v) { return (bool) v ? "on" : "off"; }
inline bool sameValue (const juce::var& x, const juce::var& y)
{
    if (x.isVoid() && y.isVoid()) return true;
    if ((x.isDouble() || x.isInt() || x.isInt64()) && (y.isDouble() || y.isInt() || y.isInt64())) return std::abs ((double) x - (double) y) < 1.0e-4;
    if (x.isBool() || y.isBool()) return (bool) x == (bool) y && x.isVoid() == y.isVoid();
    return x == y;
}
/** The value a snapshot means by a key it does not write (the writers leave the defaults out). */
inline juce::var defaultFor (const juce::String& key)
{
    if (key == "inGainDb" || key == "pan" || key == "panL" || key == "panR") return 0.0;
    if (key == "vcaMask") return (juce::int64) 0;
    if (key == "polarity" || key == "mute" || key == "solo" || key == "outOn" || key == "pre" || key == "bypass" || key == "linear" || key == "on") return false;
    if (key == "followMute") return true;
    if (key == "hz") return (double) lowcut::kDefaultHz;
    if (key == "slope") return lowcut::kDefaultSlope;
    if (key == "mutedGainDb" || key == "gainDb") return juce::var();
    return juce::var();
}
inline juce::var valueOr (const juce::var& o, const juce::String& key) { const auto v = o.getProperty (juce::Identifier (key), juce::var()); return v.isVoid() ? defaultFor (key) : v; }
inline juce::String describe (const juce::String& key, const juce::var& from, const juce::var& to)
{
    if (key == "gainDb" || key == "inGainDb" || key == "mutedGainDb")
    {
        auto d = [] (const juce::var& v) { if (v.isVoid()) return juce::String ("off"); const float f = (float) (double) v; return f <= -99.0f ? juce::String ("off") : db (f); };
        return juce::String (key == "inGainDb" ? "input gain " : "level ") + d (from) + " -> " + d (to);
    }
    if (key == "pan" || key == "panL" || key == "panR") return juce::String (key == "pan" ? "pan " : key == "panL" ? "pan L " : "pan R ") + panText ((double) from) + " -> " + panText ((double) to);
    if (key == "mute" || key == "solo" || key == "polarity" || key == "pre" || key == "bypass" || key == "linear" || key == "on" || key == "followMute")
        return juce::String (key == "pre" ? "pre-fader" : key == "on" ? "low cut" : key == "linear" ? "linear phase" : key == "followMute" ? "follows mute" : key == "polarity" ? "polarity flip" : key) + " " + onOff (from) + " -> " + onOff (to);
    if (key == "outOn") return "output " + onOff (from) + " -> " + onOff (to);
    if (key == "outDest") return "output goes somewhere else";
    if (key == "vcaMask") return "VCA assignment changed";
    if (key == "hz") return "low cut " + juce::String (juce::roundToInt ((double) from)) + " -> " + juce::String (juce::roundToInt ((double) to)) + " Hz";
    if (key == "slope") return "low cut slope changed";
    if (key == "outSurroundPan" || key == "surroundPan") return "surround pan changed";
    return key + " changed";
}
} // namespace detail

/** Every setting that differs between the older snapshot 'a' and the newer 'b' (both Project::mixSnapshot, with inserts), in plain words.
    'nameOf' gives the name of a track ("track", id), a bus ("bus", id) or a VCA ("vca", number) for the text. */
inline std::vector<MixChange> compareMix (const juce::var& a, const juce::var& b, const std::function<juce::String (const juce::String& kind, const juce::String& id)>& nameOf)
{
    std::vector<MixChange> out;
    auto leaf = [&] (juce::StringArray path, const juce::String& owner, const juce::String& key, const juce::var& from, const juce::var& to, const juce::String& textPrefix = {})
    {
        if (detail::sameValue (from, to)) return;
        MixChange c; path.add (key); c.path = path; c.from = from; c.to = to;
        auto what = detail::describe (key, from, to);
        if (key == "gainDb" && textPrefix.isEmpty()) what = "fader" + what.fromFirstOccurrenceOf ("level", false, false);     // (a strip's own fader; a send's stays "level")
        c.text = owner + ": " + textPrefix + what;
        out.push_back (c);
    };
    auto slots = [&] (juce::StringArray path, const juce::String& owner, const juce::var& sa, const juce::var& sb, const juce::String& where)
    {
        auto* xa = sa.getArray(); auto* xb = sb.getArray();
        const int n = juce::jmax (xa ? xa->size() : 0, xb ? xb->size() : 0);
        for (int i = 0; i < n; ++i)
        {
            const juce::var va = xa && i < xa->size() ? (*xa)[i] : juce::var(), vb = xb && i < xb->size() ? (*xb)[i] : juce::var();
            const auto da = va["desc"].toString(), dbs = vb["desc"].toString();
            const bool emptyA = da.isEmpty(), emptyB = dbs.isEmpty();
            if (emptyA && emptyB) continue;
            auto p = path; p.add (juce::String (i));
            MixChange c; c.path = p; c.from = va; c.to = vb; c.insert = true;
            const juce::String slotName = where + "insert " + juce::String (i + 1);
            if (emptyA) c.text = owner + ": " + slotName + ": " + dbs + " added";
            else if (emptyB) c.text = owner + ": " + slotName + ": " + da + " taken out";
            else if (da != dbs) c.text = owner + ": " + slotName + ": " + da + " -> " + dbs;
            else if ((bool) va["bypass"] != (bool) vb["bypass"]) c.text = owner + ": " + slotName + " (" + dbs + "): bypass " + detail::onOff (va["bypass"]) + " -> " + detail::onOff (vb["bypass"]);
            else if (va["state"].toString() != vb["state"].toString()) c.text = owner + ": " + slotName + " (" + dbs + "): settings changed";
            else continue;
            out.push_back (c);
        }
    };
    auto strip = [&] (juce::StringArray path, const juce::String& owner, const juce::var& sa, const juce::var& sb)
    {
        for (auto* k : { "gainDb", "inGainDb", "pan", "panL", "panR", "mute", "solo", "outOn", "outDest", "vcaMask", "polarity" })
            leaf (path, owner, k, detail::valueOr (sa, k), detail::valueOr (sb, k));
        if (juce::JSON::toString (sa["outSurroundPan"], true) != juce::JSON::toString (sb["outSurroundPan"], true)) leaf (path, owner, "outSurroundPan", sa["outSurroundPan"], sb["outSurroundPan"]);
        {   // the low cut: its keys, each against the default when the other side has no low cut
            auto p = path; p.add ("lowCut");
            const juce::var la = sa["lowCut"], lb = sb["lowCut"];
            for (auto* k : { "on", "hz", "slope", "linear" }) leaf (p, owner, k, detail::valueOr (la, k), detail::valueOr (lb, k));
        }
        {   // the sends, by their bus
            auto p = path; p.add ("sendList");
            juce::StringArray dests;
            for (auto* list : { &sa, &sb }) if (auto* arr = (*list)["sendList"].getArray()) for (auto& s : *arr) dests.addIfNotAlreadyThere (s["dest"].toString());
            for (auto& d : dests)
            {
                juce::var xa, xb;
                if (auto* arr = sa["sendList"].getArray()) for (auto& s : *arr) if (s["dest"].toString() == d) xa = s;
                if (auto* arr = sb["sendList"].getArray()) for (auto& s : *arr) if (s["dest"].toString() == d) xb = s;
                auto q = p; q.add (d);
                const juce::String sendName = "send to " + nameOf ("bus", d) + ": ";
                for (auto* k : { "gainDb", "pre", "followMute", "mute" }) leaf (q, owner, k, detail::valueOr (xa, k), detail::valueOr (xb, k), sendName);
                if (juce::JSON::toString (xa["surroundPan"], true) != juce::JSON::toString (xb["surroundPan"], true)) leaf (q, owner, "surroundPan", xa["surroundPan"], xb["surroundPan"], sendName);
                auto qs = q; qs.add ("slots"); slots (qs, owner, xa["slots"], xb["slots"], sendName);
            }
        }
        { auto p = path; p.add ("slots"); slots (p, owner, sa["slots"], sb["slots"], {}); }
        if (juce::JSON::toString (sa["postSlot"], true) != juce::JSON::toString (sb["postSlot"], true))
        { MixChange c; c.path = path; c.path.add ("postSlot"); c.from = sa["postSlot"]; c.to = sb["postSlot"]; c.insert = true; c.text = owner + ": the post-fader insert changed"; out.push_back (c); }
    };
    auto list = [&] (const char* key, const char* idKey, const char* kind)
    {
        juce::StringArray ids;
        for (auto* snap : { &a, &b }) if (auto* arr = (*snap)[key].getArray()) for (auto& s : *arr) ids.addIfNotAlreadyThere (s[idKey].toString());
        for (auto& id : ids)
        {
            juce::var xa, xb;
            if (auto* arr = a[key].getArray()) for (auto& s : *arr) if (s[idKey].toString() == id) xa = s;
            if (auto* arr = b[key].getArray()) for (auto& s : *arr) if (s[idKey].toString() == id) xb = s;
            if (xa.isVoid() || xb.isVoid()) continue;                                 // (a strip only one snapshot has: a track or bus added or removed since - nothing to put back)
            juce::StringArray path; path.add (key); path.add (id);
            const auto owner = nameOf (kind, id);
            if (juce::String (key) == "vcaStates") { for (auto* k : { "gainDb", "mute", "solo" }) leaf (path, owner, k, detail::valueOr (xa, k), detail::valueOr (xb, k)); }
            else strip (path, owner, xa, xb);
        }
    };
    list ("strips", "track", "track");
    list ("buses", "bus", "bus");
    list ("vcaStates", "number", "vca");
    return out;
}

/** A copy of the newer snapshot with that one setting as the older one had it (Project::applyMix puts it on the mixer). */
inline juce::var patchedMix (const juce::var& current, const MixChange& c)
{
    juce::var root = current.clone();
    if (c.path.size() < 3) return root;
    // the strip: the list's entry with that id
    auto* arr = root[juce::Identifier (c.path[0])].getArray();
    if (arr == nullptr) return root;
    const char* idKey = c.path[0] == "strips" ? "track" : c.path[0] == "buses" ? "bus" : "number";
    juce::var node;
    for (auto& s : *arr) if (s[idKey].toString() == c.path[1]) node = s;
    if (! node.isObject()) return root;
    auto setLeaf = [] (juce::var& obj, const juce::String& key, const juce::var& value)
    {
        if (auto* d = obj.getDynamicObject()) { if (value.isVoid()) d->removeProperty (juce::Identifier (key)); else d->setProperty (juce::Identifier (key), value); }
    };
    int k = 2;
    if (c.path[k] == "sendList")
    {
        if (k + 1 >= c.path.size()) return root;
        auto* sends = node["sendList"].getArray();
        if (sends == nullptr) { if (auto* d = node.getDynamicObject()) { d->setProperty ("sendList", juce::Array<juce::var>()); sends = node["sendList"].getArray(); } }
        if (sends == nullptr) return root;
        juce::var send;
        for (auto& s : *sends) if (s["dest"].toString() == c.path[k + 1]) send = s;
        if (! send.isObject()) { auto* o = new juce::DynamicObject(); o->setProperty ("dest", c.path[k + 1]); o->setProperty ("gainDb", -100.0); send = juce::var (o); sends->add (send); }
        k += 2;
        node = send;
    }
    if (k >= c.path.size()) return root;
    if (c.path[k] == "lowCut")
    {
        if (k + 1 >= c.path.size()) return root;
        juce::var lc = node["lowCut"];
        if (! lc.isObject()) { auto* o = new juce::DynamicObject(); o->setProperty ("on", false); o->setProperty ("hz", (double) lowcut::kDefaultHz); o->setProperty ("slope", lowcut::kDefaultSlope); lc = juce::var (o); setLeaf (node, "lowCut", lc); }
        setLeaf (lc, c.path[k + 1], c.from.isVoid() ? detail::defaultFor (c.path[k + 1]) : c.from);
        return root;
    }
    if (c.path[k] == "slots")
    {
        if (k + 1 >= c.path.size()) return root;
        const int i = c.path[k + 1].getIntValue();
        auto* sl = node["slots"].getArray();
        if (sl == nullptr) { if (auto* d = node.getDynamicObject()) { d->setProperty ("slots", juce::Array<juce::var>()); sl = node["slots"].getArray(); } }
        if (sl == nullptr || i < 0 || i > 64) return root;
        while (sl->size() <= i) { auto* o = new juce::DynamicObject(); o->setProperty ("desc", ""); sl->add (juce::var (o)); }
        if (c.from.isVoid() || c.from["desc"].toString().isEmpty()) { auto* o = new juce::DynamicObject(); o->setProperty ("desc", ""); sl->set (i, juce::var (o)); }
        else sl->set (i, c.from.clone());
        return root;
    }
    setLeaf (node, c.path[k], c.from.isVoid() && c.path[k] != "gainDb" ? detail::defaultFor (c.path[k]) : c.from);
    if (c.path[k] == "gainDb" && c.from.isVoid()) setLeaf (node, "gainDb", -100.0);
    return root;
}
} // namespace editversions
} // namespace td
