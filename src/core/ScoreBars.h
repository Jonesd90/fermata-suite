#pragma once
#include <juce_core/juce_core.h>
#include "TakeMatch.h"
#include "ScoreDigits.h"
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

namespace td
{
/** 3.54.0 (asked for on 11 November 2026: "get fermata to analyse the score and see where the bars are ... take note of what bar numbers the
    operator puts in for each take, and annotate the score"): where the bars of a score are on its pages, and where the takes' brackets go.

    The parts, none of which draws or reads a file of music:
      - analysePage (findSystems): from a grey PICTURE of one page (the score window's PDF reader draws it), the systems, their staves and
        their barlines, the numbers printed above the bars' starts (ScoreDigits.h), and the paper above each system (Band). A page from a
        notation program is used exactly as it comes; a scanned or photographed one (3.55.0) is first turned upright by its barlines
        and stems and each of its staves laid level.
      - Map: every page's systems, the bars counted through the whole score (1, 2, 3 ... with the user's corrections: "this bar is number N",
        a barline taken off, a barline added), kept in a small file beside the PDF.
      - brackets: for the takes of an edit (the bars the operator typed; a missing one guessed from the sound, guessBars), ONE start bracket and
        ONE end bracket at each barline where takes begin or stop, with the takes' numbers in order.
      - pieceLength / pieceBarsToGive: how many bars the piece has, for the take window's "bars in the piece" (the user's own number wins).

    The bars are counted, and the count is put right by the bar numbers PRINTED in the score where they could be read and make sense
    one after another (3.55.0: Map::bars - a pick-up bar and a movement that starts again at 1 are found that way), and by the numbers
    the user gives ("this bar's number is N"), which always win.
      - placeAbove: where a take's bracket can stand above its system without covering the music.

    A header only (fermata's score window is the one user; fermata:Live does not build it). Everything is a fraction of the page: x of its
    width, y and 'space' of its height, so the marks land in the same place at any zoom. */
namespace scorebars
{
constexpr int kAnalysisVersion = 2;                // (raised when findSystems changes what it finds: saved maps of an older one are made again)

struct Staff { double top = 0.0, bottom = 0.0; };  // its top and bottom lines

/** What is on the paper ABOVE a system, where the takes' marks go: a grid of small cells, each clear paper (0), ink that is not the
    music - words, numbers, a title (1) - or music (2): the notes, stems, beams and slurs that stand up out of the top staff, and what
    hangs down from the system above. A mark may never lie on a 2, and keeps off the 1s where it can (placeAbove). */
struct Band
{
    int w = 0, h = 0;                              // cells across and down
    double x0 = 0.0, y0 = 0.0, cx = 0.0, cy = 0.0; // its top left corner and a cell's size, as parts of the page's width and height
    std::vector<unsigned char> cells;              // w * h of them, row by row
    bool under = false;                            // its top edge is the bottom line of the system above (false: the page goes on clear above it, as far as is known)
    bool empty() const noexcept { return w <= 0 || h <= 0 || cells.size() != (size_t) w * (size_t) h; }
    int at (int x, int y) const noexcept { return x < 0 || y < 0 || x >= w || y >= h ? -1 : cells[(size_t) y * (size_t) w + (size_t) x]; }
};
struct System
{
    double top = 0.0, bottom = 0.0;                // the top line of its first staff, the bottom line of its last
    double space = 0.0;                            // the gap between two staff lines
    double x0 = 0.0, x1 = 0.0;                     // where the staves begin and end
    std::vector<Staff> staves;
    std::vector<double> lines;                     // the bars' edges in order: front() is where the first bar begins, back() where the last ends
    Band above;                                    // the paper above it
    int bars() const noexcept { return juce::jmax (0, (int) lines.size() - 1); }
};
/** A number seen printed above the start of a bar (ScoreDigits.h reads its digits): which bar of which system of the page, the number,
    and how well its digits lay over known ones (0..1). It is only what was SEEN: whether it is a bar number is decided over the whole
    score (Map::bars) - a tempo's "88" and a page number are seen too. */
struct Read { int system = 0, bar = 0, value = 0; float sure = 0.0f; };
struct Page { bool done = false; std::vector<System> systems; std::vector<Read> reads; };

// ================================================================================================ the picture of a page
namespace detail
{
struct HLine { double y = 0.0; int thick = 1, a = 0, b = 0; };
struct PxStaff { double top = 0.0, bottom = 0.0, d = 0.0; int a = 0, b = 0, thick = 1; };
struct Stroke { int xl = 0, xr = 0; bool through = false, clear = false; int parts = 1; };

struct Ink
{
    Ink (int width, int height) : w (width), h (height), bits ((size_t) width * (size_t) height, 0) {}
    Ink (const unsigned char* grey, int width, int height, int stride) : w (width), h (height), bits ((size_t) width * (size_t) height, 0)
    {
        // Ink is anything clearly darker than the PAPER ROUND ABOUT IT: the page is cut into squares, each square's paper is its own light
        // grey (the grey nine tenths of its dots are darker than or equal to), and a dot is ink below 0.78 of the paper there (worked out
        // between the squares' middles). On a drawn page the paper is white everywhere and this is one threshold; on a photograph the
        // shadow of a hand or the fall-off of a lamp no longer turns half the page to ink. A thin line that falls between two rows of
        // the picture is drawn as two pale rows, and those still count.
        const int cell = juce::jmax (24, w / 48), gw = (w + cell - 1) / cell, gh = (h + cell - 1) / cell;
        std::vector<float> paper ((size_t) gw * (size_t) gh, -1.0f);
        std::vector<int> hist (256);
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx)
            {
                std::fill (hist.begin(), hist.end(), 0); int n = 0;
                for (int y = gy * cell; y < juce::jmin (h, (gy + 1) * cell); y += 2)
                { const auto* row = grey + (size_t) y * (size_t) stride; for (int x = gx * cell; x < juce::jmin (w, (gx + 1) * cell); x += 2) { ++hist[row[x]]; ++n; } }
                int seen = 0, v = 255;
                for (; v > 0; --v) { seen += hist[(size_t) v]; if (seen * 10 >= n) break; }
                if (v >= 96) paper[(size_t) gy * (size_t) gw + (size_t) gx] = (float) v;          // (a square that is nearly all ink - a title's fat letter - says nothing of the paper)
            }
        float usual = 255.0f;
        { std::vector<float> known; for (float v : paper) if (v >= 0.0f) known.push_back (v); if (! known.empty()) { std::sort (known.begin(), known.end()); usual = known[known.size() / 2]; } }
        for (int pass = 0; pass < 3; ++pass)                                                    // squares that said nothing take their neighbours' paper
            for (int gy = 0; gy < gh; ++gy)
                for (int gx = 0; gx < gw; ++gx)
                {
                    auto& v = paper[(size_t) gy * (size_t) gw + (size_t) gx]; if (v >= 0.0f) continue;
                    float sum = 0.0f; int n = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                    { const int x = gx + dx, y = gy + dy; if (x < 0 || y < 0 || x >= gw || y >= gh) continue; const float q = paper[(size_t) y * (size_t) gw + (size_t) x]; if (q >= 0.0f) { sum += q; ++n; } }
                    if (n > 0) v = sum / (float) n; else if (pass == 2) v = usual;
                }
        std::vector<float> rowPaper ((size_t) gw);
        for (int y = 0; y < h; ++y)
        {
            const float fy = juce::jlimit (0.0f, (float) (gh - 1), ((float) y + 0.5f) / (float) cell - 0.5f);
            const int y0 = (int) fy, y1 = juce::jmin (gh - 1, y0 + 1); const float ty = fy - (float) y0;
            for (int gx = 0; gx < gw; ++gx) rowPaper[(size_t) gx] = paper[(size_t) y0 * (size_t) gw + (size_t) gx] * (1.0f - ty) + paper[(size_t) y1 * (size_t) gw + (size_t) gx] * ty;
            const auto* row = grey + (size_t) y * (size_t) stride; auto* out = bits.data() + (size_t) y * (size_t) w;
            for (int x = 0; x < w; ++x)
            {
                const float fx = juce::jlimit (0.0f, (float) (gw - 1), ((float) x + 0.5f) / (float) cell - 0.5f);
                const int x0 = (int) fx, x1 = juce::jmin (gw - 1, x0 + 1); const float tx = fx - (float) x0;
                out[x] = (float) row[x] < 0.78f * (rowPaper[(size_t) x0] * (1.0f - tx) + rowPaper[(size_t) x1] * tx) ? 1 : 0;
            }
        }
    }
    bool at (int x, int y) const noexcept { return x >= 0 && y >= 0 && x < w && y < h && bits[(size_t) y * (size_t) w + (size_t) x] != 0; }
    /** Ink at x or one pixel either side (an upright line that is a hair off upright, or drawn between two columns). */
    bool about (int x, int y) const noexcept { return at (x, y) || at (x - 1, y) || at (x + 1, y); }
    int w, h;
    std::vector<unsigned char> bits;
};

/** A turn about the page's middle: where a dot of the LEVELLED page is on the page as it came. */
struct Turn
{
    double cx = 0.0, cy = 0.0, c = 1.0, s = 0.0;
    Turn() = default;
    Turn (int w, int h, double degrees) : cx (w * 0.5), cy (h * 0.5), c (std::cos (degrees * 3.14159265358979 / 180.0)), s (std::sin (degrees * 3.14159265358979 / 180.0)) {}
    void toPage (double x, double y, double& px, double& py) const noexcept { px = cx + (x - cx) * c - (y - cy) * s; py = cy + (x - cx) * s + (y - cy) * c; }
    void toLevel (double px, double py, double& x, double& y) const noexcept { x = cx + (px - cx) * c + (py - cy) * s; y = cy - (px - cx) * s + (py - cy) * c; }
};

/** How far the page is turned (degrees): the turn at which its UPRIGHT strokes - barlines, stems - are most sharply upright, each falling
    into a single column. 0 for a drawn page. Up to 4 degrees either way is looked for. The uprights are used and not the staff lines: a
    page that is bent (a book on the glass) has staff lines that climb without its barlines leaning, and turning it by the staff lines'
    climb would make every barline lean; the staff lines are laid level afterwards, each staff by itself (Warp). */
inline double tiltOf (const Ink& ink)
{
    std::vector<std::pair<int, int>> dots;                                  // a sample of the ink: every third row, the middle of the page
    for (int y = ink.h / 20; y < ink.h - ink.h / 20; y += 3)
    { const auto* row = ink.bits.data() + (size_t) y * (size_t) ink.w; for (int x = ink.w / 20; x < ink.w - ink.w / 20; ++x) if (row[x] != 0) dots.push_back ({ x, y }); }
    if (dots.size() < 500) return 0.0;
    std::vector<int> cols ((size_t) ink.w + 600);
    auto sharpness = [&] (double degrees)
    {
        std::fill (cols.begin(), cols.end(), 0);
        const double t = std::tan (degrees * 3.14159265358979 / 180.0), cy = ink.h * 0.5;
        for (auto& d : dots) { const int c = (int) std::lround (d.first + (d.second - cy) * t) + 300; if (c >= 0 && c < (int) cols.size()) ++cols[(size_t) c]; }
        double sum = 0.0; for (int v : cols) sum += (double) v * (double) v;
        return sum;
    };
    const double level = sharpness (0.0);
    double best = 0.0, bestV = level;
    for (double a = -4.0; a <= 4.0001; a += 0.1) { const double v = sharpness (a); if (v > bestV) { bestV = v; best = a; } }
    const double mid = best;
    for (double a = mid - 0.1; a <= mid + 0.1001; a += 0.01) { const double v = sharpness (a); if (v > bestV) { bestV = v; best = a; } }
    return (std::abs (best) < 0.05 || bestV < level * 1.03) ? 0.0 : best;
}

/** The page's ink turned level. */
inline Ink levelled (const Ink& in, const Turn& t)
{
    Ink out (in.w, in.h);
    for (int y = 0; y < in.h; ++y)
    {
        auto* row = out.bits.data() + (size_t) y * (size_t) in.w;
        for (int x = 0; x < in.w; ++x)
        {
            double px, py; t.toPage (x, y, px, py);
            row[x] = in.at ((int) std::lround (px), (int) std::lround (py)) ? 1 : 0;
        }
    }
    return out;
}

/** A staff followed across the page in upright strips: where its middle is in each strip (a scanned page's staves climb, fall and bend). */
struct StaffTrack { std::vector<std::pair<double, double>> pts; double d = 0.0, ref = 0.0; int lastStrip = 0; };

inline std::vector<StaffTrack> trackStaves (const Ink& ink)
{
    std::vector<StaffTrack> open, done;
    const int strip = juce::jmax (40, ink.w / 28), step = strip / 2;
    std::vector<int> prof ((size_t) ink.h);
    int stripNo = 0;
    for (int x0 = 0; x0 + strip <= ink.w; x0 += step, ++stripNo)
    {
        for (int y = 0; y < ink.h; ++y)
        {
            const auto* row = ink.bits.data() + (size_t) y * (size_t) ink.w + (size_t) x0;
            int n = 0; for (int x = 0; x < strip; ++x) n += row[x];
            prof[(size_t) y] = n;
        }
        // the strip's level lines: rows that are ink for most of its width
        std::vector<std::pair<double, int>> lines;                              // middle row, thickness
        for (int y = 0; y < ink.h;)
        {
            if (prof[(size_t) y] * 10 < strip * 6) { ++y; continue; }
            int y1 = y; while (y1 + 1 < ink.h && prof[(size_t) y1 + 1] * 10 >= strip * 6) ++y1;
            lines.push_back ({ (y + y1) * 0.5, y1 - y + 1 });
            y = y1 + 1;
        }
        // five the same distance apart are a staff here. Every such five is looked at, and the staves already being followed take theirs
        // first: a hairpin's line one staff-space above a staff makes a second five with the staff's top four lines, and only the staff as
        // it was in the strip before says which five is the staff.
        struct Five { size_t first; double mid, d; };
        std::vector<Five> fives;
        for (size_t i = 0; i + 4 < lines.size(); ++i)
        {
            const double d = (lines[i + 4].first - lines[i].first) / 4.0;
            bool ok = d >= 4.0;
            const double tol = juce::jmax (1.5, 0.15 * d);
            for (size_t k = 0; ok && k < 4; ++k) ok = std::abs ((lines[i + k + 1].first - lines[i + k].first) - d) <= tol;
            for (size_t k = 0; ok && k < 5; ++k) ok = lines[i + k].second <= juce::jmax (3.0, 0.6 * d);
            if (! ok) continue;
            // where the staff's middle is: by its five lines each, the middle one of the five answers - a tie or a beam lying along one
            // line makes that "line" fatter and its middle wrong by a dot, and must not move the staff
            double mids[5]; for (size_t k = 0; k < 5; ++k) mids[k] = lines[i + k].first - ((double) k - 2.0) * d;
            std::sort (mids, mids + 5);
            fives.push_back ({ i, mids[2], d });
        }
        std::vector<char> used (lines.size(), 0);
        auto isFree = [&] (const Five& f) { for (size_t k = 0; k < 5; ++k) if (used[f.first + k] != 0) return false; return true; };
        auto take = [&] (const Five& f) { for (size_t k = 0; k < 5; ++k) used[f.first + k] = 1; };
        const double cx = x0 + strip * 0.5;
        for (auto& t : open)
        {
            const int behind = stripNo - t.lastStrip;
            if (behind < 1 || behind > 4) continue;
            const Five* best = nullptr; double bestGap = 1.0e9;
            for (auto& f : fives)
            {
                if (! isFree (f) || std::abs (t.d - f.d) > 0.15 * f.d) continue;
                const double gap = std::abs (t.pts.back().second - f.mid);
                if (gap <= (0.2 + 0.2 * behind) * f.d && gap < bestGap) { best = &f; bestGap = gap; }
            }
            if (best == nullptr) continue;
            take (*best);
            t.pts.push_back ({ cx, best->mid }); t.lastStrip = stripNo; t.d = t.d * 0.8 + best->d * 0.2;
        }
        for (auto& f : fives)
            if (isFree (f))
            {
                take (f);
                open.push_back ({}); open.back().d = f.d; open.back().pts.push_back ({ cx, f.mid }); open.back().lastStrip = stripNo;
            }
    }
    // a staff lost for a stretch (something printed over its lines, or a bend too sharp for a strip) was picked up again as a new one:
    // two that follow one another along the page at the same height are the same staff
    std::sort (open.begin(), open.end(), [] (const StaffTrack& a, const StaffTrack& b) { return a.pts.front().first < b.pts.front().first; });
    for (size_t i = 0; i < open.size(); ++i)
        for (size_t j = i + 1; j < open.size();)
        {
            auto& a = open[i]; auto& b = open[j];
            // (where the first would be by then, going on as it was going: five hairpins one above the other can look like a staff in a
            //  strip, and must not be joined to the real one beside them)
            const size_t n = a.pts.size(), back = juce::jmin ((size_t) 5, n - 1);
            const double slope = back > 0 ? (a.pts[n - 1].second - a.pts[n - 1 - back].second) / juce::jmax (1.0, a.pts[n - 1].first - a.pts[n - 1 - back].first) : 0.0;
            const double dx = b.pts.front().first - a.pts.back().first, dy = std::abs (b.pts.front().second - (a.pts.back().second + juce::jlimit (-0.05, 0.05, slope) * dx));
            if (dx > 0.0 && std::abs (a.d - b.d) <= 0.15 * a.d && dy <= 0.5 * a.d + 0.01 * dx && dy < 1.2 * a.d)
            {
                a.pts.insert (a.pts.end(), b.pts.begin(), b.pts.end());
                open.erase (open.begin() + (long) j);
            }
            else ++j;
        }
    for (size_t i = 0; i < open.size(); ++i)                                    // of two over the same stretch of the page at nearly the same height, the longer is the staff
        for (size_t j = 0; j < open.size(); ++j)
        {
            if (i == j || open[i].pts.empty() || open[j].pts.empty() || open[j].pts.size() > open[i].pts.size()) continue;
            const auto& a = open[i]; const auto& b = open[j];
            const bool overlap = b.pts.front().first <= a.pts.back().first && b.pts.back().first >= a.pts.front().first;
            if (overlap && std::abs (a.pts[a.pts.size() / 2].second - b.pts[b.pts.size() / 2].second) < 3.0 * a.d) open[j].pts.clear();
        }
    for (auto& t : open)
        if (t.pts.size() >= 6)
        {
            // one strip's reading that stands apart from its neighbours is a misreading: each takes the middle of itself and the two either side
            { auto smooth = t.pts;
              for (size_t k = 0; k < t.pts.size(); ++k)
              {
                  double v[5]; int n = 0;
                  for (int o = -2; o <= 2; ++o) { const long q = (long) k + o; if (q >= 0 && q < (long) t.pts.size()) v[n++] = t.pts[(size_t) q].second; }
                  std::sort (v, v + n); smooth[k].second = v[n / 2];
              }
              t.pts = std::move (smooth); }
            std::vector<double> ys; for (auto& pt : t.pts) ys.push_back (pt.second);
            std::sort (ys.begin(), ys.end()); t.ref = ys[ys.size() / 2];
            done.push_back (std::move (t));
        }
    std::sort (done.begin(), done.end(), [] (const StaffTrack& a, const StaffTrack& b) { return a.ref < b.ref; });
    return done;
}

/** A drawn page (a notation program's export): it has staves, and every one of them is level - or most are and the rest were misread
    (a scanned page has hardly a level staff on it). Such a page is not turned and not flattened: nothing of it is touched. */
inline bool isDrawnPage (const std::vector<StaffTrack>& tracks)
{
    int level = 0;
    for (auto& t : tracks) { double lo = 1.0e9, hi = -1.0e9; for (auto& pt : t.pts) { lo = juce::jmin (lo, pt.second); hi = juce::jmax (hi, pt.second); } if (hi - lo < 0.75) ++level; }
    return ! tracks.empty() && level * 10 >= (int) tracks.size() * 6;
}

/** How far up or down each dot of a page has to be moved for every staff to lie level: each followed staff is brought to its own middle
    height, and the paper between two staves moves with them. Only up and down: the barlines stay where they are across the page. */
struct Warp
{
    Warp() = default;
    Warp (const std::vector<StaffTrack>& tracks, int width) : w (width)
    {
        double most = 0.0;
        for (auto& t : tracks)
        {
            std::vector<float> col ((size_t) w, 0.0f);
            // (before its first strip's middle and after its last the staff goes on as it was going there - a little way: its two ends)
            const size_t n = t.pts.size(), back = juce::jmin ((size_t) 4, n - 1);
            const double slopeIn  = back > 0 ? juce::jlimit (-0.08, 0.08, (t.pts[back].second - t.pts[0].second) / juce::jmax (1.0, t.pts[back].first - t.pts[0].first)) : 0.0;
            const double slopeOut = back > 0 ? juce::jlimit (-0.08, 0.08, (t.pts[n - 1].second - t.pts[n - 1 - back].second) / juce::jmax (1.0, t.pts[n - 1].first - t.pts[n - 1 - back].first)) : 0.0;
            const double reach = 6.0 * t.d + (back > 0 ? t.pts[1].first - t.pts[0].first : 0.0) * 2.0;
            size_t k = 0;
            for (int x = 0; x < w; ++x)
            {
                while (k + 1 < t.pts.size() && t.pts[k + 1].first <= x) ++k;
                double y = t.pts[k].second;
                if (x > t.pts[k].first && k + 1 < t.pts.size()) { const double f = (x - t.pts[k].first) / (t.pts[k + 1].first - t.pts[k].first); y = t.pts[k].second * (1.0 - f) + t.pts[k + 1].second * f; }
                else if (x < t.pts[0].first) y = t.pts[0].second - slopeIn * juce::jmin (reach, t.pts[0].first - x);
                else if (x > t.pts[n - 1].first) y = t.pts[n - 1].second + slopeOut * juce::jmin (reach, x - t.pts[n - 1].first);
                col[(size_t) x] = (float) (y - t.ref); most = juce::jmax (most, std::abs (y - t.ref));
            }
            refs.push_back (t.ref); shift.push_back (std::move (col));
        }
        if (most < 0.75 || isDrawnPage (tracks)) { refs.clear(); shift.clear(); }      // (nothing to do, and nothing is touched)
    }
    bool none() const noexcept { return refs.empty(); }
    /** The dot of the flattened page at (x, y) is this far further down on the page as it was. */
    double dyAt (int x, double y) const noexcept
    {
        if (refs.empty()) return 0.0;
        x = juce::jlimit (0, w - 1, x);
        if (y <= refs.front()) return shift.front()[(size_t) x];
        if (y >= refs.back()) return shift.back()[(size_t) x];
        size_t i = 0; while (i + 2 < refs.size() && refs[i + 1] <= y) ++i;
        const double t = (y - refs[i]) / juce::jmax (1.0, refs[i + 1] - refs[i]);
        return shift[i][(size_t) x] * (1.0 - t) + shift[i + 1][(size_t) x] * t;
    }
    int w = 0;
    std::vector<double> refs;
    std::vector<std::vector<float>> shift;
};

inline Ink flattened (const Ink& in, const Warp& warp)
{
    Ink out (in.w, in.h);
    for (int y = 0; y < in.h; ++y)
    {
        auto* row = out.bits.data() + (size_t) y * (size_t) in.w;
        for (int x = 0; x < in.w; ++x) row[x] = in.at (x, (int) std::lround (y + warp.dyAt (x, y))) ? 1 : 0;
    }
    return out;
}

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
    const int reach = (int) std::ceil (2.5 * up.d);
    for (int x = up.a - reach; x <= up.a + reach; ++x)
    {
        // the line is followed down from row to row: on a scanned page it leans a dot or two on its way, and has a row missing here and there
        int miss = 0, at = x; const int allowed = juce::jmax (2, (y1 - y0) / 12);
        for (int y = y0; y <= y1 && miss <= allowed; ++y)
        {
            if (ink.at (at, y)) continue;
            if (ink.at (at - 1, y)) --at; else if (ink.at (at + 1, y)) ++at; else ++miss;
            if (std::abs (at - x) > juce::jmax (3, (y1 - y0) / 25)) miss = allowed + 1;
        }
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
inline std::vector<Stroke> strokesOf (const Ink& ink, const PxStaff& st, const PxStaff* above, const PxStaff* below, bool flattened = false)
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
        // (the stroke's skirt: on a scanned page its edges are a dot or two of grey, and those are not "ink beside it")
        int skirtL = s.xl, skirtR = s.xr;
        {
            auto inkIn = [&] (int x) { int n = 0; for (int y = y0; y <= y1; ++y) if (ink.at (x, y)) ++n; return n; };
            const int third = juce::jmax (1, rows * 3 / 10);
            while (s.xl - skirtL < 3 && inkIn (skirtL - 1) >= third) --skirtL;
            while (skirtR - s.xr < 3 && inkIn (skirtR + 1) >= third) ++skirtR;
        }
        // (on a page that was laid level the staff's lines still step a row here and there: each side of the stroke is looked at from where
        //  the five lines really are just there, a row or two up or down)
        auto linesAt = [&] (bool leftSide)
        {
            if (! flattened) return 0;
            int best = 0, bestInk = -1;
            for (int o : { 0, -1, 1, -2, 2 })
            {
                int n = 0;
                for (int l = 0; l < 5; ++l)
                {
                    const int y = (int) std::lround (st.top + l * st.d) + o;
                    for (int dx = 2; dx <= k + 2; ++dx) n += ink.at (leftSide ? skirtL - dx : skirtR + dx, y) ? 1 : 0;
                }
                if (n > bestInk) { bestInk = n; best = o; }
            }
            return best;
        };
        const int offL = linesAt (true), offR = linesAt (false);
        int oneSided = 0, crossed = 0;
        for (int sp = 0; sp < 4; ++sp)
        {
            const int ya = (int) std::lround (st.top + sp * st.d) + edge, yb = (int) std::lround (st.top + (sp + 1) * st.d) - edge;
            int left = 0, right = 0;                                             // rows of the space with ink just to the left, just to the right
            for (int y = ya; y <= yb; ++y)
            {
                bool l = false, r = false;
                for (int dx = 2; dx <= k + 2; ++dx) { l = l || ink.at (skirtL - dx, y + offL); r = r || ink.at (skirtR + dx, y + offR); }
                left += l ? 1 : 0; right += r ? 1 : 0;
            }
            const int heavy = juce::jmax (3, ((yb - ya + 1) * 8 + 9) / 10);     // a note's head fills the space from line to line; a tie or a slur is a line through it (and its end, beside a barline, climbs through half of it)
            // an OPEN head (a minim, a semibreve) on a blurred page shows only as its two arcs, hard against the lines above and below it:
            // ink in the row next to the top line AND in the row next to the bottom line, along most of the way out from the stroke
            auto arcs = [&] (bool leftSide)
            {
                int top = 0, bottom = 0;
                for (int dx = 2; dx <= k + 2; ++dx)
                {
                    const int x = leftSide ? skirtL - dx : skirtR + dx, o = leftSide ? offL : offR;
                    top += ink.at (x, ya - 1 + o) ? 1 : 0; bottom += ink.at (x, yb + 1 + o) ? 1 : 0;
                }
                return top * 2 >= k + 1 && bottom * 2 >= k + 1;
            };
            const bool l = left > 0 || arcs (true), r = right > 0 || arcs (false);
            if (l != r) ++oneSided;
            else if (l && r) { if ((left >= heavy) != (right >= heavy)) ++oneSided; else ++crossed; }
        }
        // ... and a time signature's "4 over 4" is a stroke the whole height of the staff too (the two 4s' uprights, one above the other),
        // with ink on BOTH sides of it in every space - no tie does that - and it is fat: a barline on its own is thin.
        const bool fat = s.parts == 1 && (s.xr - s.xl + 1) > juce::jmax (3, (int) (0.45 * st.d));
        s.clear = (oneSided == 0 && crossed <= 2 && ! fat) || s.parts >= 2;
    }
    return out;
}
/** The paper above a system (Band): from 'ceiling' (the bottom line of the system above, or the page's top) down to its top line, but
    no more than 16 staff-spaces of it. Music is what stands up out of the top staff - followed upward in each column of dots from the
    top line until there is clear paper for more than a staff-space and a bit - and what hangs down from the system above, followed
    down from its bottom line the same way. Everything else that is ink is "other ink". */
inline Band bandAbove (const Ink& ink, const PxStaff& topStaff, int a, int b, double d, int ceiling, bool hasSystemAbove)
{
    Band band;
    const int cell = juce::jmax (3, (int) std::lround (d / 3.0));
    const int X0 = juce::jmax (0, a - (int) (6.0 * d)), X1 = juce::jmin (ink.w - 1, b + (int) (2.0 * d));
    const int Y1 = (int) topStaff.top - 1, Y0 = juce::jmax (ceiling + 1, Y1 - (int) (16.0 * d), 0);
    if (X1 - X0 < cell * 4 || Y1 - Y0 < cell * 2) return band;
    band.w = (X1 - X0 + cell) / cell; band.h = (Y1 - Y0 + cell) / cell;
    band.cells.assign ((size_t) band.w * (size_t) band.h, 0);
    const int gapOk = (int) (1.25 * d);
    for (int x = X0; x <= X1; ++x)
    {
        // how far the music stands up in this column, and how far it hangs down from above
        int up = Y1 + 1, gap = 0;
        for (int y = Y1; y >= Y0; --y) { if (ink.at (x, y)) { up = y; gap = 0; } else if (++gap > gapOk) break; }
        int down = Y0 - 1;
        if (hasSystemAbove) { gap = 0; for (int y = Y0; y <= Y1; ++y) { if (ink.at (x, y)) { down = y; gap = 0; } else if (++gap > gapOk) break; } }
        const int cxi = (x - X0) / cell;
        for (int y = Y0; y <= Y1; ++y)
        {
            auto& c = band.cells[(size_t) ((y - Y0) / cell) * (size_t) band.w + (size_t) cxi];
            if (y >= up || y <= down) c = 2;
            else if (c == 0 && ink.at (x, y)) c = 1;
        }
    }
    // a cell either side of music is kept clear too (a note's head is wider than its stem's column says)
    auto wide = band.cells;
    for (int y = 0; y < band.h; ++y)
        for (int x = 0; x < band.w; ++x)
            if (band.cells[(size_t) y * (size_t) band.w + (size_t) x] == 2)
                for (int dx : { -1, 1 }) if (x + dx >= 0 && x + dx < band.w) wide[(size_t) y * (size_t) band.w + (size_t) (x + dx)] = 2;
    band.cells = std::move (wide);
    band.x0 = (double) X0 / ink.w; band.y0 = (double) Y0 / ink.h; band.cx = (double) cell / ink.w; band.cy = (double) cell / ink.h;
    band.under = hasSystemAbove && Y0 == ceiling + 1;
    return band;
}

/** The numbers printed above a system's top staff: small shapes of a digit's size standing alone or two to four in a row, with nothing
    else of their size beside them (a word has letters either side; a boxed number has its box). Each is given to the bar whose start it
    stands over. 'ceiling': the row above which the page belongs to something else (the system above; the page's top). */
inline std::vector<Read> readNumbers (const Ink& ink, const PxStaff& topStaff, const std::vector<double>& edges, double d, int ceiling, int systemNo)
{
    std::vector<Read> out;
    if (edges.size() < 2) return out;
    const int X0 = juce::jmax (0, (int) (edges.front() - 5.0 * d)), X1 = juce::jmin (ink.w - 1, (int) (edges.back() + 1.5 * d));
    const int Y1 = (int) (topStaff.top - juce::jmax (3.0, 0.35 * d)), Y0 = juce::jmax (ceiling + 1, (int) (topStaff.top - 9.0 * d), 0);
    const int rw = X1 - X0 + 1, rh = Y1 - Y0 + 1;
    if (rw < 8 || rh < 8) return out;
    // every separate shape in that band of the page
    struct Blob { int x0, y0, x1, y1, area; bool cut; };
    std::vector<int> label ((size_t) rw * (size_t) rh, 0);
    std::vector<Blob> blobs;
    std::vector<int> stack;
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x)
        {
            if (label[(size_t) y * (size_t) rw + (size_t) x] != 0 || ! ink.at (X0 + x, Y0 + y)) continue;
            const int id = (int) blobs.size() + 1;
            Blob b { x, y, x, y, 0, false };
            stack.clear(); stack.push_back (y * rw + x); label[(size_t) y * (size_t) rw + (size_t) x] = id;
            while (! stack.empty())
            {
                const int at = stack.back(); stack.pop_back();
                const int px = at % rw, py = at / rw;
                b.x0 = juce::jmin (b.x0, px); b.x1 = juce::jmax (b.x1, px); b.y0 = juce::jmin (b.y0, py); b.y1 = juce::jmax (b.y1, py); ++b.area;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= rw || ny >= rh || label[(size_t) ny * (size_t) rw + (size_t) nx] != 0 || ! ink.at (X0 + nx, Y0 + ny)) continue;
                        label[(size_t) ny * (size_t) rw + (size_t) nx] = id; stack.push_back (ny * rw + nx);
                    }
            }
            b.cut = b.y1 >= rh - 1 || b.y0 <= 0;                               // (it runs on out of the band: part of something bigger)
            blobs.push_back (b);
        }
    auto hOf = [] (const Blob& b) { return b.y1 - b.y0 + 1; };
    auto wOf = [] (const Blob& b) { return b.x1 - b.x0 + 1; };
    std::vector<int> digits;                                                    // the blobs of a digit's size, left to right
    for (int i = 0; i < (int) blobs.size(); ++i)
    {
        const auto& b = blobs[(size_t) i];
        const int h = hOf (b), w = wOf (b);
        if (b.cut || h < juce::jmax (8, (int) (0.8 * d)) || h > (int) (3.4 * d) || w > (int) (1.15 * h) || w * 12 < h || b.area * 100 < w * h * 12) continue;
        digits.push_back (i);
    }
    std::sort (digits.begin(), digits.end(), [&] (int p, int q) { return blobs[(size_t) p].x0 < blobs[(size_t) q].x0; });
    std::vector<char> grouped (blobs.size(), 0);
    for (size_t gi = 0; gi < digits.size(); ++gi)
    {
        if (grouped[(size_t) digits[gi]] != 0) continue;
        std::vector<int> group { digits[gi] };
        int H = hOf (blobs[(size_t) digits[gi]]);
        for (size_t gj = gi + 1; gj < digits.size(); ++gj)
        {
            const auto& last = blobs[(size_t) group.back()]; const auto& next = blobs[(size_t) digits[gj]];
            if (grouped[(size_t) digits[gj]] != 0) continue;
            if (next.x0 - last.x1 > (int) (0.45 * H)) break;
            const double midLast = (last.y0 + last.y1) * 0.5, midNext = (next.y0 + next.y1) * 0.5;
            if (std::abs (hOf (next) - H) > 0.28 * H || std::abs (midNext - midLast) > 0.3 * H) continue;
            group.push_back (digits[gj]); H = juce::jmax (H, hOf (next));
        }
        for (int g : group) grouped[(size_t) g] = 1;
        if (group.size() > 4) continue;
        const int gx0 = blobs[(size_t) group.front()].x0, gx1 = blobs[(size_t) group.back()].x1;
        int gy0 = 1 << 30, gy1 = -1; for (int g : group) { gy0 = juce::jmin (gy0, blobs[(size_t) g].y0); gy1 = juce::jmax (gy1, blobs[(size_t) g].y1); }
        // anything of a letter's size right beside it, on its line: it is part of a word (or boxed)
        bool alone = true;
        for (int i = 0; i < (int) blobs.size() && alone; ++i)
        {
            if (std::find (group.begin(), group.end(), i) != group.end()) continue;
            const auto& b = blobs[(size_t) i];
            if (hOf (b) < (int) (0.3 * H) && wOf (b) < (int) (0.3 * H)) continue;                      // (a dot, a speck)
            const int overlap = juce::jmin (b.y1, gy1) - juce::jmax (b.y0, gy0) + 1;
            if (overlap * 10 < juce::jmin (hOf (b), H) * 3) continue;
            const int gap = b.x0 > gx1 ? b.x0 - gx1 : (b.x1 < gx0 ? gx0 - b.x1 : 0);
            if (gap < (int) (0.7 * H)) alone = false;
        }
        if (! alone) continue;
        int value = 0; float sure = 1.0f; bool ok = true;
        for (size_t k = 0; k < group.size() && ok; ++k)
        {
            const auto& b = blobs[(size_t) group[k]];
            const int w = wOf (b), h = hOf (b);
            std::vector<unsigned char> own ((size_t) w * (size_t) h);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) own[(size_t) y * (size_t) w + (size_t) x] = label[(size_t) (b.y0 + y) * (size_t) rw + (size_t) (b.x0 + x)] == group[k] + 1 ? 1 : 0;
            const auto guess = scoredigits::classify (scoredigits::shapeOf (ink.bits.data(), ink.w, X0 + b.x0, Y0 + b.y0, w, h, own.data()));
            ok = guess.digit >= 0 && guess.score >= 0.5f && ! (k == 0 && guess.digit == 0 && group.size() > 1);
            value = value * 10 + juce::jmax (0, guess.digit); sure = juce::jmin (sure, guess.score);
        }
        if (! ok || value <= 0) continue;
        // the bar it stands over: the one whose start it is nearest (a number sits over the barline, or just to its right)
        const double cx = X0 + (gx0 + gx1) * 0.5;
        int bar = -1; double best = 1.0e9;
        for (int e = 0; e + 1 < (int) edges.size(); ++e)
        {
            const double off = cx - edges[(size_t) e];
            if (off < (e == 0 ? -5.0 : -3.5) * d || off > (e == 0 ? 6.0 : 5.0) * d) continue;
            if (std::abs (off - d) < best) { best = std::abs (off - d); bar = e; }
        }
        if (bar >= 0) out.push_back ({ systemNo, bar, value, sure });
    }
    return out;
}
} // namespace detail

/** The systems of one page, top to bottom, from a grey picture of it (0 = black ... 255 = white; 'stride' bytes from one row to the next).
    About 250 dots to the inch is right (analysisSize): a staff's lines must be at least four dots apart. */
struct PageFound { std::vector<System> systems; std::vector<Read> reads; double tilt = 0.0; bool levelled = false; };
inline PageFound analysePage (const unsigned char* grey, int w, int h, int stride)
{
    PageFound result;
    auto& out = result.systems;
    double* const tiltFound = &result.tilt;
    if (grey == nullptr || w < 100 || h < 100) return result;
    // a scanned or photographed page is first turned level (the systems found are then put back where they are on the page as it came)
    // - but first its staves are followed as it is: a page whose staves are level is a drawn one, and is left exactly as it came
    const detail::Ink raw (grey, w, h, stride);
    const auto asItCame = detail::trackStaves (raw);
    const double tilt = detail::isDrawnPage (asItCame) ? 0.0 : detail::tiltOf (raw);
    const detail::Turn turn (w, h, tilt);
    const detail::Ink turned = tilt != 0.0 ? detail::levelled (raw, turn) : detail::Ink (1, 1);
    const detail::Ink& upright = tilt != 0.0 ? turned : raw;
    // ... and its staves, which may still climb, fall or bend (a book's page on the glass), are each laid level
    const detail::Warp warp (tilt != 0.0 ? detail::trackStaves (upright) : asItCame, w);
    const detail::Ink flat = ! warp.none() ? detail::flattened (upright, warp) : detail::Ink (1, 1);
    const detail::Ink& ink = ! warp.none() ? flat : upright;
    *tiltFound = tilt;
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
        // (staves: the staves that have the stroke AND clear paper beside it or the stroke running on to the next staff. A "4 over 4" time
        //  signature is a stroke the whole height of every staff at one place - the two 4s' uprights, one above the other - and only the
        //  ink beside it, on one side, tells it from a barline: found on the first real score tried, 12 November 2026.)
        std::vector<Found> found;
        for (size_t k = i; k <= j; ++k)
            for (auto& s : detail::strokesOf (ink, staves[k], k > i ? &staves[k - 1] : nullptr, k < j ? &staves[k + 1] : nullptr, tilt != 0.0 || ! warp.none()))
            {
                const double c = (s.xl + s.xr) * 0.5;
                Found* same = nullptr;
                for (auto& f : found) if (c >= f.xl - 0.7 * d && c <= f.xr + 0.7 * d) { same = &f; break; }
                const int vote = (s.clear || s.through) ? 1 : 0;
                if (same == nullptr) { found.push_back ({ (double) s.xl, (double) s.xr, vote, s.through, s.clear, vote != 0 ? (int) k : -1 }); continue; }
                same->xl = juce::jmin (same->xl, (double) s.xl); same->xr = juce::jmax (same->xr, (double) s.xr);
                if (vote != 0 && same->lastStaff != (int) k) { ++same->staves; same->lastStaff = (int) k; }
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
        const double midX = (a + b) * 0.5, midY = (staves[i].top + staves[j].bottom) * 0.5;
        auto pageX = [&] (double x) { double px, py; turn.toPage (x, midY + warp.dyAt ((int) x, midY), px, py); return px / w; };        // (a drawn page: the same dot)
        auto pageY = [&] (double y) { double px, py; turn.toPage (midX, y + warp.dyAt ((int) midX, y), px, py); return py / h; };
        sys.top = pageY (staves[i].top); sys.bottom = pageY (staves[j].bottom); sys.space = d / h;
        sys.x0 = pageX ((double) a); sys.x1 = pageX ((double) b);
        for (size_t k = i; k <= j; ++k) sys.staves.push_back ({ pageY (staves[k].top), pageY (staves[k].bottom) });
        for (double x : edges) sys.lines.push_back (pageX (x));
        for (auto& r : detail::readNumbers (ink, staves[i], edges, d, i > 0 ? (int) staves[i - 1].bottom : 0, (int) out.size())) result.reads.push_back (r);
        sys.above = detail::bandAbove (ink, staves[i], a, b, d, i > 0 ? (int) staves[i - 1].bottom : 0, i > 0);
        // (on a page that was laid level the band is moved as its system's top left corner was; it stays square to the page)
        if (! sys.above.empty()) { sys.above.x0 += sys.x0 - (double) a / w; sys.above.y0 += sys.top - staves[i].top / h; }
        out.push_back (std::move (sys));
        i = j + 1;
    }
    result.levelled = tilt != 0.0 || ! warp.none();
    return result;
}
/** The systems alone (the first build's way in; the tests' made-up pages use it). */
inline std::vector<System> findSystems (const unsigned char* grey, int w, int h, int stride, double* tiltFound = nullptr)
{
    auto found = analysePage (grey, w, h, stride);
    if (tiltFound != nullptr) *tiltFound = found.tilt;
    return std::move (found.systems);
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
/** How the user has set one take bracket apart from where and how fermata would draw it: the bracket of the bar at 'at' (its start
    bracket or its end bracket), moved to stand 'dx', 'dy' from the top of that bar's barline (parts of the page's width and height),
    and / or drawn 'scale' times the usual size. */
struct MarkLook { Anchor at; bool start = true; bool moved = false; double dx = 0.0, dy = 0.0, scale = 1.0; };
/** One bar: where it is, and its number (counted from 1, put right by the user's NumberFixes). */
struct Bar
{
    int page = 0, system = 0, index = 0, number = 0; double x0 = 0.0, x1 = 0.0;
    bool read = false;       // its number was seen printed there, and agrees
    bool jump = false;       // the print and the count parted company on the way to this bar (a barline too many or too few before it, or something the count cannot know): worth a look
};

struct Map
{
    std::vector<Page> pages;
    std::vector<NumberFix> numbers;                 // "this bar is number N": the count goes on from there
    std::map<juce::String, Anchor> editStarts;      // by edit id: the bar this edit's music begins at (its takes' bar numbers are looked for from there on)
    std::map<juce::String, int> piecesGiven;        // by take window id: the "bars in the piece" fermata last gave that window from this score (see pieceBarsToGive)
    bool useReads = true;                           // the printed bar numbers put the count right (off: counted, and the user's numbers only)
    std::vector<MarkLook> looks;                    // the brackets the user has moved or resized
    double markScale = 1.0;                         // the size of every bracket (1 = the usual size)

    /** The look set for a bar's start or end bracket (null: none - it is where and as fermata draws it). */
    MarkLook* lookOf (int page, int system, int index, bool start) noexcept
    {
        const auto* s = systemAt (page, system); if (s == nullptr) return nullptr;
        for (auto& l : looks) if (l.start == start && l.at.page == page && l.at.system == system && indexAt (*s, l.at.x) == index) return &l;
        return nullptr;
    }
    const MarkLook* lookOf (int page, int system, int index, bool start) const noexcept { return const_cast<Map*> (this)->lookOf (page, system, index, start); }
    MarkLook& lookToSet (int page, int system, int index, bool start)
    {
        if (auto* l = lookOf (page, system, index, start)) return *l;
        looks.push_back ({ anchorOf (page, system, index), start, false, 0.0, 0.0, 1.0 });
        return looks.back();
    }
    void forgetLook (int page, int system, int index, bool start)
    {
        const auto* s = systemAt (page, system); if (s == nullptr) return;
        looks.erase (std::remove_if (looks.begin(), looks.end(), [&] (const MarkLook& l) { return l.start == start && l.at.page == page && l.at.system == system && indexAt (*s, l.at.x) == index; }), looks.end());
    }

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
    /** Every bar in order, numbered: counted from 1, put right by the numbers the user gave ("this bar is number N") and - unless
        useReads is off - by the numbers PRINTED in the score (Page::reads).

        A printed number is believed when it is what the count says there anyway, or when it makes sense with another printed number
        near it: two numbers seen k bars apart that differ by k are bar numbers (a tuplet's 3, a tempo's 88 and a page number never
        are, and a misread digit is not). Where a believed number differs from the count:
          - it is lower, and counting back from it reaches 1 exactly at the start of a system: a new movement begins there (its first
            line prints no number), and the bars from there are numbered 1, 2, 3 ... up to it;
          - the same, but 1 falls on the SECOND bar of a system: that system begins with a pick-up bar, which gets 0;
          - otherwise the count simply takes the printed number from there on, and the bar is marked (Bar::jump): a barline was missed
            or a stroke taken for one somewhere before it, or the score counts in a way no count can know (a repeat's bars counted
            twice, a multi-bar rest). The user's own numbers always win. */
    std::vector<Bar> bars() const
    {
        std::vector<Bar> out;
        std::vector<std::vector<int>> first (pages.size());                    // by page, by system: the place of its first bar in 'out'
        for (int p = 0; p < (int) pages.size(); ++p)
            for (int s = 0; s < (int) pages[(size_t) p].systems.size(); ++s)
            {
                const auto& sys = pages[(size_t) p].systems[(size_t) s];
                first[(size_t) p].push_back ((int) out.size());
                for (int i = 0; i < sys.bars(); ++i) out.push_back ({ p, s, i, 0, sys.lines[(size_t) i], sys.lines[(size_t) i + 1], false, false });
            }
        const int total = (int) out.size();
        auto placeOf = [&] (int page, int system, int index) { return page >= 0 && page < (int) first.size() && system >= 0 && system < (int) first[(size_t) page].size() && index >= 0 && index < pages[(size_t) page].systems[(size_t) system].bars() ? first[(size_t) page][(size_t) system] + index : -1; };
        std::vector<int> given ((size_t) total, -1);                           // the user's numbers
        for (auto& f : numbers)
            if (const auto* sys = systemAt (f.at.page, f.at.system))
                if (const int at = placeOf (f.at.page, f.at.system, indexAt (*sys, f.at.x)); at >= 0) given[(size_t) at] = f.number;
        struct Seen { int at, value; };
        std::vector<Seen> seen;
        if (useReads)
            for (int p = 0; p < (int) pages.size(); ++p)
                for (auto& r : pages[(size_t) p].reads)
                    if (const int at = placeOf (p, r.system, r.bar); at >= 0) seen.push_back ({ at, r.value });
        std::vector<int> printed ((size_t) total, -1);                         // a number seen at a bar that makes sense with another near it
        for (size_t i = 0; i < seen.size(); ++i)
            for (size_t j = 0; j < seen.size(); ++j)
            {
                const int apart = seen[j].at - seen[i].at;
                if (apart != 0 && std::abs (apart) <= 80 && seen[j].value - seen[i].value == apart) { printed[(size_t) seen[i].at] = seen[i].value; break; }
            }
        int n = 1, lastSure = -1;                                               // lastSure: the last bar whose number was given or seen
        for (int k = 0; k < total; ++k)
        {
            if (given[(size_t) k] >= 0) { n = given[(size_t) k]; lastSure = k; }
            else if (printed[(size_t) k] >= 0)
            {
                const int v = printed[(size_t) k];
                if (v != n)
                {
                    const int one = k - (v - 1);                               // where bar 1 would be, counting back
                    if (v < n && one > lastSure && one >= 0 && out[(size_t) one].index == 0)
                        for (int q = one; q < k; ++q) out[(size_t) q].number = q - one + 1;
                    else if (v < n && one - 1 > lastSure && one >= 1 && out[(size_t) one].index == 1)
                        for (int q = one - 1; q < k; ++q) out[(size_t) q].number = q - one + 1;
                    else out[(size_t) k].jump = true;
                    n = v;
                }
                out[(size_t) k].read = true; lastSure = k;
            }
            else
                for (auto& sn : seen) if (sn.at == k && sn.value == n) { out[(size_t) k].read = true; lastSure = k; break; }
            out[(size_t) k].number = n;
            ++n;
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

// ---- where a mark can go ----
/** Where a take bracket's block (its upright, arm and numbers: 'w' wide and 'h' tall, as parts of the page's width and height, its left
    edge at 'left') can stand above a system: the height of its TOP edge. It is put as low as it can go - just above the music at that
    place - where it covers no music, none of the blocks already 'taken', and as little other ink as can be had without climbing far (a
    cell of other ink costs as much as six cells of climbing: two staff-spaces). Never on the staves or the notes ("it needs to be clear of the stave and
    notes"); over words only when there is no clear paper within reach. When nothing at all is free (a crowded page, two systems hard
    against each other) it goes where it covers least music. Without a band (an old map; a page not looked at) it sits just above the
    top line, as in the first build. */
struct PageRect { double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
                  bool meets (const PageRect& o) const noexcept { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; } };
inline double placeAbove (const System& sys, double left, double w, double h, const std::vector<PageRect>& taken)
{
    const auto& band = sys.above;
    const double rest = sys.top - 1.6 * sys.space - h;                          // (no band: where the first build put it)
    if (band.empty() || band.cx <= 0.0 || band.cy <= 0.0) return rest;
    const int cx0 = (int) std::floor ((left - band.x0) / band.cx), cx1 = (int) std::floor ((left + w - band.x0) / band.cx);
    const int tall = juce::jmax (1, (int) std::ceil (h / band.cy));
    double best = rest; long bestCost = -1;
    const int lowest = band.h - tall - (int) std::ceil (1.2 * sys.space / band.cy), highest = lowest - (int) std::ceil (11.0 * sys.space / band.cy);
    for (int top = lowest; top >= highest; --top)
    {
        long music = 0, other = 0;
        for (int y = top; y < top + tall; ++y)
            for (int x = cx0; x <= cx1; ++x)
            {
                const int c = band.at (x, y);
                if (c == 2 || (c < 0 && y < 0 && band.under)) ++music;         // (above the band's top edge is the system above)
                else if (c == 1) ++other;
            }
        const PageRect here { left, band.y0 + top * band.cy, w, h };
        bool free = here.y >= 0.0; for (auto& t : taken) if (t.meets (here)) { free = false; break; }
        const long cost = music * 1000 + (free ? 0 : 400) + other * 6 + (lowest - top);
        if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = here.y; }
        if (cost == 0) break;
    }
    return best;
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
            if (! s.above.empty())
            {
                // the band: its place and size, and its cells packed small (they are mostly long runs of the same figure)
                auto* bo = new juce::DynamicObject();
                bo->setProperty ("w", s.above.w); bo->setProperty ("h", s.above.h); bo->setProperty ("under", s.above.under);
                bo->setProperty ("x0", num (s.above.x0)); bo->setProperty ("y0", num (s.above.y0)); bo->setProperty ("cx", juce::var (s.above.cx)); bo->setProperty ("cy", juce::var (s.above.cy));
                juce::MemoryOutputStream packed;
                { juce::GZIPCompressorOutputStream z (packed, 6); z.write (s.above.cells.data(), s.above.cells.size()); }
                bo->setProperty ("cells", juce::Base64::toBase64 (packed.getData(), packed.getDataSize()));
                so->setProperty ("above", juce::var (bo));
            }
            systems.add (juce::var (so));
        }
        po->setProperty ("systems", systems);
        juce::Array<juce::var> reads;                                          // four figures a number: system, bar, the number, how sure (in hundredths)
        for (auto& r : p.reads) { reads.add (r.system); reads.add (r.bar); reads.add (r.value); reads.add ((int) std::lround (r.sure * 100.0f)); }
        if (! reads.isEmpty()) po->setProperty ("reads", reads);
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
    root->setProperty ("useReads", m.useReads);
    root->setProperty ("markScale", juce::var (m.markScale));
    juce::Array<juce::var> looks;
    for (auto& l : m.looks)
    {
        auto v = anchor (l.at); auto* o = v.getDynamicObject();
        o->setProperty ("start", l.start); o->setProperty ("moved", l.moved); o->setProperty ("dx", num (l.dx)); o->setProperty ("dy", num (l.dy)); o->setProperty ("scale", juce::var (l.scale));
        looks.add (v);
    }
    root->setProperty ("looks", looks);
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
                if (const auto bv = sv["above"]; bv.isObject())
                {
                    Band band; band.w = bv["w"]; band.h = bv["h"]; band.x0 = bv["x0"]; band.y0 = bv["y0"]; band.cx = bv["cx"]; band.cy = bv["cy"]; band.under = (bool) bv["under"];
                    juce::MemoryOutputStream packed;
                    if (band.w > 0 && band.h > 0 && band.w * band.h < 4000000 && juce::Base64::convertFromBase64 (packed, bv["cells"].toString()))
                    {
                        juce::MemoryInputStream in (packed.getData(), packed.getDataSize(), false);
                        juce::GZIPDecompressorInputStream z (in);
                        band.cells.resize ((size_t) band.w * (size_t) band.h);
                        if (z.read (band.cells.data(), (int) band.cells.size()) == (int) band.cells.size()) s.above = std::move (band);
                    }
                }
                if (s.lines.size() >= 2 && s.space > 0.0) p.systems.push_back (std::move (s));
            }
        if (const auto* reads = pv["reads"].getArray())
            for (int i = 0; i + 3 < reads->size(); i += 4) p.reads.push_back ({ (int) (*reads)[i], (int) (*reads)[i + 1], (int) (*reads)[i + 2], (float) (int) (*reads)[i + 3] / 100.0f });
        out.pages.push_back (std::move (p));
    }
    if (const auto* fixes = v["numbers"].getArray()) for (auto& f : *fixes) out.numbers.push_back ({ anchor (f), (int) f["number"] });
    if (const auto* starts = v["editStarts"].getArray()) for (auto& e : *starts) out.editStarts[e["edit"].toString()] = anchor (e);
    if (const auto* given = v["piecesGiven"].getArray()) for (auto& g : *given) out.piecesGiven[g["window"].toString()] = (int) g["bars"];
    out.useReads = ! v.hasProperty ("useReads") || (bool) v["useReads"];
    out.markScale = v.hasProperty ("markScale") ? juce::jlimit (0.4, 3.0, (double) v["markScale"]) : 1.0;
    if (const auto* looks = v["looks"].getArray())
        for (auto& l : *looks) out.looks.push_back ({ anchor (l), (bool) l["start"], (bool) l["moved"], (double) l["dx"], (double) l["dy"], juce::jlimit (0.3, 4.0, l.hasProperty ("scale") ? (double) l["scale"] : 1.0) });
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
