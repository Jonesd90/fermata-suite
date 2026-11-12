#pragma once
#include <juce_core/juce_core.h>
#include "TakeMatch.h"
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

namespace td
{
/** 3.54.0 (asked for on 11 November 2026: "get fermata to analyse the score and see where the bars are ... take note of what bar numbers the
    operator puts in for each take, and annotate the score"): where the bars of a score are on its pages, and where the takes' brackets go.

    Three parts, none of which draws or reads a file of music:
      - findSystems: from a grey PICTURE of one page (the score window's PDF reader draws it), the systems, their staves and their barlines.
        It is made for a score exported from a notation program: straight, level staff lines and upright barlines. A photographed or scanned
        page is not level and is not expected to work.
      - Map: every page's systems, the bars counted through the whole score (1, 2, 3 ... with the user's corrections: "this bar is number N",
        a barline taken off, a barline added), kept in a small file beside the PDF.
      - brackets: for the takes of an edit (the bars the operator typed; a missing one guessed from the sound, guessBars), ONE start bracket and
        ONE end bracket at each barline where takes begin or stop, with the takes' numbers in order.
      - pieceLength / pieceBarsToGive: how many bars the piece has, for the take window's "bars in the piece" (the user's own number wins).

    The printed bar numbers are NOT read (that needs the reading of print, which fermata has none of): the bars are counted, and the user puts
    the count right where the score's own numbering differs (a pick-up bar, a second movement that starts again at 1, a bar split over a line).

    A header only (fermata's score window is the one user; fermata:Live does not build it). Everything is a fraction of the page: x of its
    width, y and 'space' of its height, so the marks land in the same place at any zoom. */
namespace scorebars
{
constexpr int kAnalysisVersion = 1;                // (raised when findSystems changes what it finds: saved maps of an older one are made again)

struct Staff { double top = 0.0, bottom = 0.0; };  // its top and bottom lines
struct System
{
    double top = 0.0, bottom = 0.0;                // the top line of its first staff, the bottom line of its last
    double space = 0.0;                            // the gap between two staff lines
    double x0 = 0.0, x1 = 0.0;                     // where the staves begin and end
    std::vector<Staff> staves;
    std::vector<double> lines;                     // the bars' edges in order: front() is where the first bar begins, back() where the last ends
    int bars() const noexcept { return juce::jmax (0, (int) lines.size() - 1); }
};
struct Page { bool done = false; std::vector<System> systems; };

// ================================================================================================ the picture of a page
namespace detail
{
struct HLine { double y = 0.0; int thick = 1, a = 0, b = 0; };
struct PxStaff { double top = 0.0, bottom = 0.0, d = 0.0; int a = 0, b = 0, thick = 1; };
struct Stroke { int xl = 0, xr = 0; bool through = false, clear = false; int parts = 1; };

struct Ink
{
    Ink (const unsigned char* grey, int width, int height, int stride) : w (width), h (height), bits ((size_t) width * (size_t) height, 0)
    {
        // the paper's own grey (the most common light value): ink is anything clearly darker than it. A thin line that falls between two
        // rows of the picture is drawn as two pale rows, and those have to count.
        std::vector<int> hist (256, 0);
        for (int y = 0; y < h; y += 2) { const auto* row = grey + (size_t) y * (size_t) stride; for (int x = 0; x < w; x += 2) ++hist[row[x]]; }
        int paper = 255, most = -1;
        for (int v = 128; v < 256; ++v) if (hist[(size_t) v] > most) { most = hist[(size_t) v]; paper = v; }
        const int thr = (int) (paper * 0.78);
        for (int y = 0; y < h; ++y)
        {
            const auto* row = grey + (size_t) y * (size_t) stride; auto* out = bits.data() + (size_t) y * (size_t) w;
            for (int x = 0; x < w; ++x) out[x] = row[x] < thr ? 1 : 0;
        }
    }
    bool at (int x, int y) const noexcept { return x >= 0 && y >= 0 && x < w && y < h && bits[(size_t) y * (size_t) w + (size_t) x] != 0; }
    /** Ink at x or one pixel either side (an upright line that is a hair off upright, or drawn between two columns). */
    bool about (int x, int y) const noexcept { return at (x, y) || at (x - 1, y) || at (x + 1, y); }
    int w, h;
    std::vector<unsigned char> bits;
};

/** The long level lines of the page: rows where the long runs of ink add up to at least a fifth of the page's width (a staff line can be
    broken - a time signature or a bar number printed on a patch of white), neighbouring rows taken together. */
inline std::vector<HLine> levelLines (const Ink& ink)
{
    std::vector<HLine> out;
    const int need = juce::jmax (40, ink.w / 5), gapOk = juce::jmax (2, ink.w / 900), longRun = juce::jmax (12, ink.w / 80);
    int openFrom = -1, a = 0, b = 0;
    auto close = [&] (int yEnd)
    {
        if (openFrom < 0) return;
        out.push_back ({ (openFrom + yEnd - 1) * 0.5, yEnd - openFrom, a, b });
        openFrom = -1;
    };
    for (int y = 0; y < ink.h; ++y)
    {
        const auto* row = ink.bits.data() + (size_t) y * (size_t) ink.w;
        int total = 0, rowA = -1, rowB = -1, runA = -1, last = -1000;
        auto endRun = [&] { if (runA >= 0 && last - runA + 1 >= longRun) { total += last - runA + 1; if (rowA < 0) rowA = runA; rowB = last; } };
        for (int x = 0; x < ink.w; ++x)
        {
            if (row[x] == 0) continue;
            if (x - last > gapOk + 1) { endRun(); runA = x; }
            last = x;
        }
        endRun();
        if (total >= need)
        {
            if (openFrom < 0) { openFrom = y; a = rowA; b = rowB; }
            else { a = juce::jmin (a, rowA); b = juce::jmax (b, rowB); }
        }
        else close (y);
    }
    close (ink.h);
    return out;
}

/** Five level lines the same distance apart, one above the other, are a staff. */
inline std::vector<PxStaff> stavesOf (const std::vector<HLine>& lines)
{
    std::vector<PxStaff> out;
    for (size_t i = 0; i + 4 < lines.size();)
    {
        const double d = (lines[i + 4].y - lines[i].y) / 4.0;
        bool ok = d >= 4.0;
        const double tol = juce::jmax (1.5, 0.12 * d);
        for (size_t k = 0; ok && k < 4; ++k) ok = std::abs ((lines[i + k + 1].y - lines[i + k].y) - d) <= tol;
        int a = 0, b = 1 << 30, longest = 0;
        std::vector<int> as, bs, th;
        for (size_t k = 0; ok && k < 5; ++k)
        {
            a = juce::jmax (a, lines[i + k].a); b = juce::jmin (b, lines[i + k].b); longest = juce::jmax (longest, lines[i + k].b - lines[i + k].a);
            as.push_back (lines[i + k].a); bs.push_back (lines[i + k].b); th.push_back (lines[i + k].thick);
        }
        if (ok) ok = (b - a) >= (int) (0.8 * longest);                        // (they lie over each other)
        if (ok) { std::sort (as.begin(), as.end()); std::sort (bs.begin(), bs.end()); std::sort (th.begin(), th.end()); ok = (double) th[2] <= juce::jmax (3.0, 0.45 * d); }   // (lines, not bands)
        if (! ok) { ++i; continue; }
        out.push_back ({ lines[i].y, lines[i + 4].y, d, as[2], bs[2], juce::jmax (1, th[2]) });
        i += 5;
    }
    return out;
}

/** Is there an upright line near the staves' left edge that runs from the upper staff down to the lower one? (Then they are one system.) */
inline bool joinedAtLeft (const Ink& ink, const PxStaff& up, const PxStaff& low)
{
    if (std::abs (up.a - low.a) > (int) (2.0 * up.d)) return false;
    const int y0 = (int) std::ceil (up.bottom), y1 = (int) std::floor (low.top);
    if (y1 <= y0) return false;
    const int reach = (int) std::ceil (1.5 * up.d);
    for (int x = up.a - reach; x <= up.a + reach; ++x)
    {
        int miss = 0; const int allowed = juce::jmax (1, (y1 - y0) / 30);
        for (int y = y0; y <= y1 && miss <= allowed; ++y) if (! ink.about (x, y)) ++miss;
        if (miss <= allowed) return true;
    }
    return false;
}

/** How far the ink goes on above (dir -1) or below (dir +1) a stroke, from just outside the staff's outer line. */
inline int runBeyond (const Ink& ink, const Stroke& s, int yFrom, int dir, int most)
{
    int n = 0, gap = 0;
    for (int y = yFrom; n + gap < most && y >= 0 && y < ink.h; y += dir)
    {
        bool any = false;
        for (int x = s.xl - 1; x <= s.xr + 1 && ! any; ++x) any = ink.at (x, y);
        if (any) { n += 1 + gap; gap = 0; }
        else if (++gap > 1) break;
    }
    return n;
}

/** The upright strokes of one staff that could be barlines: ink the whole height of the staff, that stops at its outer lines (or runs on all
    the way to the next staff of the system - a barline through a piano's two staves, or an orchestra's group). A note's stem that crosses the
    whole staff nearly always sticks out of it; the ones that do not are left to the other tests. */
inline std::vector<Stroke> strokesOf (const Ink& ink, const PxStaff& st, const PxStaff* above, const PxStaff* below)
{
    std::vector<Stroke> raw;
    const int y0 = (int) std::floor (st.top), y1 = (int) std::ceil (st.bottom);
    const int rows = y1 - y0 + 1, allowed = juce::jmax (0, rows / 40);
    int open = -1;
    for (int x = st.a - 2; x <= st.b + 3; ++x)
    {
        int miss = 0;
        if (x <= st.b + 2) { for (int y = y0; y <= y1 && miss <= allowed; ++y) if (! ink.about (x, y)) ++miss; } else miss = allowed + 1;
        const bool full = miss <= allowed;
        if (full && open < 0) open = x;
        if (! full && open >= 0)
        {
            // exactly which columns the stroke is in: the ones with the most ink of their own (the test above also passes the column either side)
            std::vector<int> count; int most = 0;
            const int c0 = open - 3;
            for (int c = c0; c <= x + 2; ++c) { int n = 0; for (int y = y0; y <= y1; ++y) if (ink.at (c, y)) ++n; count.push_back (n); most = juce::jmax (most, n); }
            int xl = -1, xr = -1;
            for (int c = 0; c < (int) count.size(); ++c) if (count[(size_t) c] * 5 >= most * 4) { if (xl < 0) xl = c0 + c; xr = c0 + c; }
            raw.push_back ({ xl, xr, false, false, 1 });
            open = -1;
        }
    }
    std::vector<Stroke> kept;
    const int beyond = st.thick / 2 + 2, stick = juce::jmax (2, (int) (0.45 * st.d));
    for (auto s : raw)
    {
        const int gapUp = above != nullptr ? (int) (st.top - above->bottom) : 0, gapDown = below != nullptr ? (int) (below->top - st.bottom) : 0;
        const int up = runBeyond (ink, s, y0 - beyond, -1, juce::jmax (stick + 2, gapUp + 4));
        const int down = runBeyond (ink, s, y1 + beyond, +1, juce::jmax (stick + 2, gapDown + 4));
        const bool throughUp = above != nullptr && up >= gapUp - beyond - 3, throughDown = below != nullptr && down >= gapDown - beyond - 3;
        if ((up > stick && ! throughUp) || (down > stick && ! throughDown)) continue;
        s.through = throughUp || throughDown;
        kept.push_back (s);
    }
    // strokes close together are one barline (a double bar, a thin and a thick line at the end, a repeat)
    std::vector<Stroke> out;
    for (auto& s : kept)
    {
        if (! out.empty() && s.xl - out.back().xr <= (int) (1.0 * st.d)) { out.back().xr = s.xr; out.back().through = out.back().through || s.through; ++out.back().parts; }
        else out.push_back (s);
    }
    // Is it a stem? A note in the top space with its stem down ends exactly on the bottom line (as one in the bottom space with its stem up
    // ends on the top line), so such a stem fills the staff's height just as a barline does. What tells them apart is the note's head: ink
    // right beside the stroke, in one of the four spaces, on ONE side of it. (Ink on both sides is something that crosses the barline - a tie,
    // a slur - and says nothing.)
    const int k = juce::jmax (3, (int) (0.4 * st.d)), edge = st.thick / 2 + 2;
    for (auto& s : out)
    {
        int oneSided = 0;
        for (int sp = 0; sp < 4; ++sp)
        {
            const int ya = (int) std::lround (st.top + sp * st.d) + edge, yb = (int) std::lround (st.top + (sp + 1) * st.d) - edge;
            bool left = false, right = false;
            for (int y = ya; y <= yb; ++y)
                for (int dx = 2; dx <= k + 2; ++dx) { left = left || ink.at (s.xl - dx, y); right = right || ink.at (s.xr + dx, y); }
            if (left != right) ++oneSided;
        }
        s.clear = oneSided == 0 || s.parts >= 2;
    }
    return out;
}
} // namespace detail

/** The systems of one page, top to bottom, from a grey picture of it (0 = black ... 255 = white; 'stride' bytes from one row to the next).
    About 250 dots to the inch is right (analysisSize): a staff's lines must be at least four dots apart. */
inline std::vector<System> findSystems (const unsigned char* grey, int w, int h, int stride)
{
    std::vector<System> out;
    if (grey == nullptr || w < 100 || h < 100) return out;
    const detail::Ink ink (grey, w, h, stride);
    const auto staves = detail::stavesOf (detail::levelLines (ink));
    for (size_t i = 0; i < staves.size();)
    {
        size_t j = i;
        while (j + 1 < staves.size() && detail::joinedAtLeft (ink, staves[j], staves[j + 1])) ++j;
        const int n = (int) (j - i + 1);
        double d = 0.0; int a = 0, b = 0;
        { std::vector<int> as, bs; for (size_t k = i; k <= j; ++k) { d += staves[k].d; as.push_back (staves[k].a); bs.push_back (staves[k].b); }
          std::sort (as.begin(), as.end()); std::sort (bs.begin(), bs.end()); a = as[as.size() / 2]; b = bs[bs.size() / 2]; d /= (double) n; }

        // every staff's strokes; a barline is one that most of the system's staves have at the same place
        struct Found { double xl = 0.0, xr = 0.0; int staves = 0; bool through = false, clear = true; int lastStaff = -1; };
        std::vector<Found> found;
        for (size_t k = i; k <= j; ++k)
            for (auto& s : detail::strokesOf (ink, staves[k], k > i ? &staves[k - 1] : nullptr, k < j ? &staves[k + 1] : nullptr))
            {
                const double c = (s.xl + s.xr) * 0.5;
                Found* same = nullptr;
                for (auto& f : found) if (c >= f.xl - 0.7 * d && c <= f.xr + 0.7 * d) { same = &f; break; }
                if (same == nullptr) { found.push_back ({ (double) s.xl, (double) s.xr, 1, s.through, s.clear, (int) k }); continue; }
                same->xl = juce::jmin (same->xl, (double) s.xl); same->xr = juce::jmax (same->xr, (double) s.xr);
                if (same->lastStaff != (int) k) { ++same->staves; same->lastStaff = (int) k; }
                same->through = same->through || s.through; same->clear = same->clear && s.clear;
            }
        std::sort (found.begin(), found.end(), [] (const Found& p, const Found& q) { return p.xl < q.xl; });
        const int need = n <= 2 ? n : (int) std::ceil (0.6 * n);
        std::vector<double> xs;
        for (auto& f : found)
        {
            const bool ok = n == 1 ? f.clear : (f.staves >= need || (f.through && f.staves >= juce::jmax (1, need - 1)));
            if (ok) xs.push_back ((f.xl + f.xr) * 0.5);
        }
        // the bars' edges: the system's start, each barline, and its end
        std::vector<double> edges; edges.push_back ((double) a);
        for (double x : xs) if (x - a > 2.5 * d) edges.push_back (x);            // (a line at the very start is the system's own edge - or an alto clef's two bars)
        if ((double) b - edges.back() > 1.5 * d)
        {
            // the staff goes on after the last barline: a bar that is carried over the line end - unless it is only the room for the next
            // line's key or time signature (shorter than a real bar would be)
            std::vector<double> widths; for (size_t k = 1; k < edges.size(); ++k) widths.push_back (edges[k] - edges[k - 1]);
            std::sort (widths.begin(), widths.end());
            const double usual = widths.empty() ? 0.0 : widths[widths.size() / 2];
            if (edges.size() == 1 || (double) b - edges.back() >= juce::jmax (8.0 * d, 0.4 * usual)) edges.push_back ((double) b);
        }
        else if (edges.size() >= 2) edges.back() = juce::jmin (edges.back(), (double) b);
        if (edges.size() == 1) edges.push_back ((double) b);
        for (size_t k = 1; k < edges.size();) { if (edges[k] - edges[k - 1] < 1.2 * d && edges.size() > 2) edges.erase (edges.begin() + (long) (k + 1 < edges.size() ? k : k - 1)); else ++k; }

        System sys;
        sys.top = staves[i].top / h; sys.bottom = staves[j].bottom / h; sys.space = d / h;
        sys.x0 = (double) a / w; sys.x1 = (double) b / w;
        for (size_t k = i; k <= j; ++k) sys.staves.push_back ({ staves[k].top / h, staves[k].bottom / h });
        for (double x : edges) sys.lines.push_back (x / w);
        out.push_back (std::move (sys));
        i = j + 1;
    }
    return out;
}

/** The size, in dots, a page of w by h units (PdfPages.h: 1/96 inch) is drawn at to have its bars found. */
inline void analysisSize (double unitsW, double unitsH, int& w, int& h)
{
    double f = 250.0 / 96.0;
    const double longest = juce::jmax (unitsW, unitsH, 1.0);
    if (longest * f > 3600.0) f = 3600.0 / longest;
    w = juce::jmax (100, juce::roundToInt (unitsW * f)); h = juce::jmax (100, juce::roundToInt (unitsH * f));
}

// ================================================================================================ the whole score's bars
/** A place in the score given by where it is on the paper (so that it still means the same bar after a barline is added or taken off). */
struct Anchor { int page = -1, system = -1; double x = 0.0; bool valid() const noexcept { return page >= 0 && system >= 0; } };
struct NumberFix { Anchor at; int number = 1; };
/** One bar: where it is, and its number (counted from 1, put right by the user's NumberFixes). */
struct Bar { int page = 0, system = 0, index = 0, number = 0; double x0 = 0.0, x1 = 0.0; };

struct Map
{
    std::vector<Page> pages;
    std::vector<NumberFix> numbers;                 // "this bar is number N": the count goes on from there
    std::map<juce::String, Anchor> editStarts;      // by edit id: the bar this edit's music begins at (its takes' bar numbers are looked for from there on)
    std::map<juce::String, int> piecesGiven;        // by take window id: the "bars in the piece" fermata last gave that window from this score (see pieceBarsToGive)

    bool complete() const noexcept { if (pages.empty()) return false; for (auto& p : pages) if (! p.done) return false; return true; }
    int pagesDone() const noexcept { int n = 0; for (auto& p : pages) if (p.done) ++n; return n; }
    const System* systemAt (int page, int system) const noexcept
    {
        if (page < 0 || page >= (int) pages.size() || system < 0 || system >= (int) pages[(size_t) page].systems.size()) return nullptr;
        return &pages[(size_t) page].systems[(size_t) system];
    }
    System* systemAt (int page, int system) noexcept { return const_cast<System*> (static_cast<const Map&> (*this).systemAt (page, system)); }

    /** The bar of a system that holds x (-1: none). */
    static int indexAt (const System& s, double x) noexcept
    {
        if (s.bars() <= 0) return -1;
        for (int i = 0; i < s.bars(); ++i) if (x < s.lines[(size_t) i + 1]) return i;
        return s.bars() - 1;
    }
    /** Every bar in order, numbered. */
    std::vector<Bar> bars() const
    {
        std::vector<Bar> out;
        int n = 1;
        for (int p = 0; p < (int) pages.size(); ++p)
            for (int s = 0; s < (int) pages[(size_t) p].systems.size(); ++s)
            {
                const auto& sys = pages[(size_t) p].systems[(size_t) s];
                for (int i = 0; i < sys.bars(); ++i)
                {
                    for (auto& f : numbers) if (f.at.page == p && f.at.system == s && indexAt (sys, f.at.x) == i) n = f.number;
                    out.push_back ({ p, s, i, n, sys.lines[(size_t) i], sys.lines[(size_t) i + 1] });
                    ++n;
                }
            }
        return out;
    }
    /** The system of a page nearest to a height on it (-1: the page has none). */
    int systemNear (int page, double y) const noexcept
    {
        if (page < 0 || page >= (int) pages.size()) return -1;
        int best = -1; double bestD = 1.0e9;
        const auto& list = pages[(size_t) page].systems;
        for (int s = 0; s < (int) list.size(); ++s)
        {
            const double d = y < list[(size_t) s].top ? list[(size_t) s].top - y : (y > list[(size_t) s].bottom ? y - list[(size_t) s].bottom : 0.0);
            if (d < bestD) { bestD = d; best = s; }
        }
        return best;
    }
    Anchor anchorOf (int page, int system, int index) const noexcept
    {
        const auto* s = systemAt (page, system);
        if (s == nullptr || index < 0 || index >= s->bars()) return {};
        return { page, system, (s->lines[(size_t) index] + s->lines[(size_t) index + 1]) * 0.5 };
    }
    void forgetFixIn (int page, int system, int index)
    {
        const auto* s = systemAt (page, system); if (s == nullptr) return;
        numbers.erase (std::remove_if (numbers.begin(), numbers.end(), [&] (const NumberFix& f) { return f.at.page == page && f.at.system == system && indexAt (*s, f.at.x) == index; }), numbers.end());
    }
    /** "This bar is number N" (the bars after it count on from N). */
    void setNumber (int page, int system, int index, int number)
    {
        const auto a = anchorOf (page, system, index); if (! a.valid()) return;
        forgetFixIn (page, system, index);
        numbers.push_back ({ a, number });
    }
    /** The barline at the start of this bar is not one: the bar becomes part of the one before it (in the same system). */
    bool joinToBefore (int page, int system, int index)
    {
        auto* s = systemAt (page, system);
        if (s == nullptr || index <= 0 || index >= s->bars()) return false;
        forgetFixIn (page, system, index);
        s->lines.erase (s->lines.begin() + index);
        return true;
    }
    /** A barline the picture did not show: this bar is two, parted at x. */
    bool splitAt (int page, int system, double x)
    {
        auto* s = systemAt (page, system); if (s == nullptr || s->bars() <= 0) return false;
        const int i = indexAt (*s, x);
        if (i < 0 || x <= s->lines[(size_t) i] || x >= s->lines[(size_t) i + 1]) return false;
        s->lines.insert (s->lines.begin() + i + 1, x);
        return true;
    }
};

/** Where in 'bars' the edit's music begins (0 when nothing was said). */
inline int startOrdinal (const Map& m, const std::vector<Bar>& bars, const juce::String& editId)
{
    const auto it = m.editStarts.find (editId);
    if (it == m.editStarts.end() || ! it->second.valid()) return 0;
    const auto* s = m.systemAt (it->second.page, it->second.system); if (s == nullptr) return 0;
    const int index = Map::indexAt (*s, it->second.x);
    for (int i = 0; i < (int) bars.size(); ++i) if (bars[(size_t) i].page == it->second.page && bars[(size_t) i].system == it->second.system && bars[(size_t) i].index == index) return i;
    return 0;
}
/** How many bars the piece has: the number of the last bar of the music that begins at 'from' - it ends where the numbers start again
    (the next movement) or the score does. 0 when there are no bars. */
inline int pieceLength (const std::vector<Bar>& bars, int from = 0)
{
    if (from < 0 || from >= (int) bars.size()) return 0;
    int last = bars[(size_t) from].number;
    for (int i = from + 1; i < (int) bars.size(); ++i) { if (bars[(size_t) i].number < bars[(size_t) i - 1].number) break; last = bars[(size_t) i].number; }
    return juce::jmax (0, last);
}
/** What a take window's "bars in the piece" should become, given what the score says ('fromScore'), what the window has now ('has', 0 =
    none) and what fermata last gave it from this score ('given', 0 = never): the score's count when the window has none and was never
    given one, or still has exactly what it was given (the score's count has been put right since); otherwise what it has - a number the
    user typed there stays, and so does an emptied box. */
inline int pieceBarsToGive (int fromScore, int has, int given)
{
    if (fromScore <= 0) return has;
    if (given <= 0) return has <= 0 ? fromScore : has;
    return has == given ? fromScore : has;
}
/** The first bar with this number at or after 'from' (else the first anywhere); -1 when the score has no such bar. */
inline int findBar (const std::vector<Bar>& bars, int number, int from = 0)
{
    for (int i = juce::jmax (0, from); i < (int) bars.size(); ++i) if (bars[(size_t) i].number == number) return i;
    for (int i = 0; i < juce::jmin (from, (int) bars.size()); ++i) if (bars[(size_t) i].number == number) return i;
    return -1;
}
/** A bar carried over a line end is two pieces with one number: the last piece of the one that begins at i. */
inline int lastPiece (const std::vector<Bar>& bars, int i)
{
    while (i >= 0 && i + 1 < (int) bars.size() && bars[(size_t) i + 1].number == bars[(size_t) i].number) ++i;
    return i;
}

// ---- the file beside the PDF ----
inline juce::var toVar (const Map& m, juce::int64 pdfBytes)
{
    auto num = [] (double v) { return juce::var (std::round (v * 100000.0) / 100000.0); };
    auto anchor = [&] (const Anchor& a) { auto* o = new juce::DynamicObject(); o->setProperty ("page", a.page); o->setProperty ("system", a.system); o->setProperty ("x", num (a.x)); return juce::var (o); };
    auto* root = new juce::DynamicObject();
    root->setProperty ("fermata", "score bars"); root->setProperty ("finder", kAnalysisVersion); root->setProperty ("pdfBytes", pdfBytes);
    juce::Array<juce::var> pages;
    for (auto& p : m.pages)
    {
        auto* po = new juce::DynamicObject(); po->setProperty ("done", p.done);
        juce::Array<juce::var> systems;
        for (auto& s : p.systems)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("top", num (s.top)); so->setProperty ("bottom", num (s.bottom)); so->setProperty ("space", num (s.space));
            so->setProperty ("x0", num (s.x0)); so->setProperty ("x1", num (s.x1));
            juce::Array<juce::var> staves, lines;
            for (auto& st : s.staves) { staves.add (num (st.top)); staves.add (num (st.bottom)); }
            for (double x : s.lines) lines.add (num (x));
            so->setProperty ("staves", staves); so->setProperty ("lines", lines);
            systems.add (juce::var (so));
        }
        po->setProperty ("systems", systems);
        pages.add (juce::var (po));
    }
    root->setProperty ("pages", pages);
    juce::Array<juce::var> fixes;
    for (auto& f : m.numbers) { auto v = anchor (f.at); v.getDynamicObject()->setProperty ("number", f.number); fixes.add (v); }
    root->setProperty ("numbers", fixes);
    juce::Array<juce::var> starts;
    for (auto& kv : m.editStarts) { auto v = anchor (kv.second); v.getDynamicObject()->setProperty ("edit", kv.first); starts.add (v); }
    root->setProperty ("editStarts", starts);
    juce::Array<juce::var> given;
    for (auto& kv : m.piecesGiven) { auto* o = new juce::DynamicObject(); o->setProperty ("window", kv.first); o->setProperty ("bars", kv.second); given.add (juce::var (o)); }
    root->setProperty ("piecesGiven", given);
    return juce::var (root);
}
/** Reads a saved map. False when it is not one, was made by another finder, or is of another PDF (its size in bytes) or page count. */
inline bool fromVar (Map& m, const juce::var& v, juce::int64 pdfBytes, int pageCount)
{
    if (! v.isObject() || v["fermata"].toString() != "score bars" || (int) v["finder"] != kAnalysisVersion) return false;
    if ((juce::int64) v["pdfBytes"] != pdfBytes) return false;
    const auto* pages = v["pages"].getArray();
    if (pages == nullptr || pages->size() != pageCount) return false;
    auto anchor = [] (const juce::var& a) { return Anchor { (int) a["page"], (int) a["system"], (double) a["x"] }; };
    Map out;
    for (auto& pv : *pages)
    {
        Page p; p.done = (bool) pv["done"];
        if (const auto* systems = pv["systems"].getArray())
            for (auto& sv : *systems)
            {
                System s; s.top = sv["top"]; s.bottom = sv["bottom"]; s.space = sv["space"]; s.x0 = sv["x0"]; s.x1 = sv["x1"];
                if (const auto* st = sv["staves"].getArray()) for (int i = 0; i + 1 < st->size(); i += 2) s.staves.push_back ({ (double) (*st)[i], (double) (*st)[i + 1] });
                if (const auto* ln = sv["lines"].getArray()) for (auto& x : *ln) s.lines.push_back ((double) x);
                if (s.lines.size() >= 2 && s.space > 0.0) p.systems.push_back (std::move (s));
            }
        out.pages.push_back (std::move (p));
    }
    if (const auto* fixes = v["numbers"].getArray()) for (auto& f : *fixes) out.numbers.push_back ({ anchor (f), (int) f["number"] });
    if (const auto* starts = v["editStarts"].getArray()) for (auto& e : *starts) out.editStarts[e["edit"].toString()] = anchor (e);
    if (const auto* given = v["piecesGiven"].getArray()) for (auto& g : *given) out.piecesGiven[g["window"].toString()] = (int) g["bars"];
    m = std::move (out);
    return true;
}

// ================================================================================================ the takes' brackets
/** A take as the score sees it: its number and the bars it starts and ends in (0 = not known). */
struct TakeSpan
{
    juce::Uuid window, take;
    int number = 0;
    int barIn = 0, barOut = 0;
    bool inGuessed = false, outGuessed = false;     // from the sound, not typed by the operator
    bool dud = false;
};
/** One bracket: at the barline where the takes listed start (the bar going off to the right) or stop (to the left). */
struct Bracket
{
    bool start = true;
    int page = 0, system = 0;
    double x = 0.0;
    int bar = 0;                                    // start: the bar that begins here; end: the bar that finishes here
    int ordinal = 0;                                // that bar's place in Map::bars()
    std::vector<int> takes;                         // indexes into the spans given, in the order of the takes' numbers
};

/** ONE start bracket and ONE end bracket at each barline where takes begin or stop. A take's start is the barline before its first bar;
    its end is the barline after its last bar (a take logged "Bar 12 - 29" finished at the beginning of bar 30). Bars that were guessed
    from the sound join a bracket of typed bars that is within a bar of them, as the user's own pen does ("so long as the takes land within
    about a bar then it can go where the rest of them are"). A take whose bars the score does not have is left out. */
inline std::vector<Bracket> brackets (const std::vector<Bar>& bars, const std::vector<TakeSpan>& spans, int from = 0)
{
    std::vector<Bracket> out;
    auto bracketAt = [&] (bool start, int ordinal, bool nearToo) -> Bracket*
    {
        for (auto& b : out) if (b.start == start && b.ordinal == ordinal) return &b;
        if (nearToo) for (auto& b : out) if (b.start == start && std::abs (b.ordinal - ordinal) == 1) return &b;
        return nullptr;
    };
    auto put = [&] (bool start, int ordinal, int span, bool guessed)
    {
        auto* b = bracketAt (start, ordinal, guessed);
        if (b == nullptr)
        {
            const auto& bar = bars[(size_t) ordinal];
            out.push_back ({ start, bar.page, bar.system, start ? bar.x0 : bar.x1, bar.number, ordinal, {} });
            b = &out.back();
        }
        b->takes.push_back (span);
    };
    for (int pass = 0; pass < 2; ++pass)                                       // the typed bars first: the guessed ones join them
        for (int i = 0; i < (int) spans.size(); ++i)
        {
            const auto& t = spans[(size_t) i];
            const int a = t.barIn > 0 ? findBar (bars, t.barIn, from) : -1;
            if (a >= 0 && (int) t.inGuessed == pass) put (true, a, i, t.inGuessed);
            if (t.barOut > 0 && (int) t.outGuessed == pass)
            {
                int z = findBar (bars, t.barOut, a >= 0 ? a : from);
                if (z >= 0) put (false, lastPiece (bars, z), i, t.outGuessed);
            }
        }
    for (auto& b : out)
        std::stable_sort (b.takes.begin(), b.takes.end(), [&] (int p, int q) { return spans[(size_t) p].number < spans[(size_t) q].number; });
    std::sort (out.begin(), out.end(), [] (const Bracket& p, const Bracket& q) { return p.ordinal != q.ordinal ? p.ordinal < q.ordinal : (p.start && ! q.start); });   // (in the score's order: a bar's start bracket is before its end bracket)
    return out;
}

// ================================================================================================ bars from the sound
/** The bars of a take that has none typed, worked out from another take that has ('ref', bars refIn..refOut): the two are laid over each
    other by their sound (takematch::align (take, ref)), and the take's first and last music is read off the reference's bars (spread evenly
    over its music, takematch::BarMap - so "within about a bar", which is all the brackets need). 0 for an end that falls outside the
    reference's bars or is not found surely enough. */
struct Guess { int barIn = 0, barOut = 0; double confidence = 0.0; };
inline Guess guessBars (const takematch::Fingerprint& take, const takematch::Fingerprint& ref, int refIn, int refOut, const takematch::Alignment& takeToRef)
{
    Guess g;
    if (take.empty() || ref.empty() || takeToRef.empty() || refIn <= 0 || refOut < refIn) return g;
    const auto own = takematch::barMapOf (take, 1, 1, take.lengthSeconds());
    const auto map = takematch::barMapOf (ref, refIn, refOut, ref.lengthSeconds());
    if (! map.valid() || own.to - own.from < 2.0) return g;
    const double len = map.barLength(), in = juce::jmin (1.0, (own.to - own.from) * 0.25);
    const auto first = takematch::locate (takeToRef, own.from + in), last = takematch::locate (takeToRef, own.to - in);
    if (first.found)
    {
        const double line = (first.seconds - in - map.from) / len;               // in bars from the reference's first barline
        if (line > -0.75 && line < (double) map.bars() - 0.25) { g.barIn = juce::jlimit (refIn, refOut, refIn + (int) std::lround (line)); g.confidence = first.confidence; }
    }
    if (last.found)
    {
        const double line = (last.seconds + in - map.from) / len;
        if (line > 0.25 && line < (double) map.bars() + 0.75)
        {
            g.barOut = juce::jlimit (juce::jmax (refIn, g.barIn), refOut, refIn + (int) std::lround (line) - 1);
            g.confidence = g.barIn > 0 ? juce::jmin (g.confidence, last.confidence) : last.confidence;
        }
    }
    return g;
}
} // namespace scorebars
} // namespace td
