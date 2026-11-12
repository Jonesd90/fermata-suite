#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace td
{
/** 3.55.0 (asked for on 11 November 2026: "the program can interpret bar numbers ... see the numbers and guess that they are bars"):
    the ten digits as print, for reading a score's printed bar numbers from a picture of the page.

    fermata has no reader of print. What it has is this: a small shape cut from the page is brought to a 12 x 18 grid and laid over some
    two hundred drawn digits (ScoreDigitShapes.inc, made by tools/make_digit_shapes.py from free fonts that stand in for the ones
    notation programs print bar numbers in - upright, italic and bold); the digit whose shape it lies over best is the answer, with how
    well it lay and how much better than the next digit. That alone would be too trusting - a tuplet's 3, a fingering, a tempo's 88 are
    digits too - so nothing here decides what IS a bar number: ScoreBars.h does, by where the number stands and by whether the numbers
    of a score make sense one after another (Map::bars).

    A header only; fermata's score window is the one user. */
namespace scoredigits
{
constexpr int kW = 12, kH = 18;
struct Shape { unsigned char v[kW * kH] = {}; };          // each cell 0 (paper) .. 15 (ink)

namespace detail
{
static const char* const kShapes[] = {
#include "ScoreDigitShapes.inc"
};
struct Known { int digit = 0; Shape shape; int ink = 0; };
inline const std::vector<Known>& known()
{
    static const std::vector<Known> all = []
    {
        std::vector<Known> out;
        for (const char* s : kShapes)
        {
            Known k; k.digit = s[0] - '0';
            for (int i = 0; i < kW * kH && s[1 + i] != 0; ++i)
            {
                const char c = s[1 + i];
                k.shape.v[i] = (unsigned char) (c >= 'a' ? c - 'a' + 10 : c - '0'); k.ink += k.shape.v[i];
            }
            out.push_back (k);
        }
        return out;
    }();
    return all;
}
} // namespace detail

/** A shape cut from a page's ink (bits: 1 = ink, 'stride' to the next row; the w x h dots at x0, y0 - the shape's own edges), on the grid:
    its height fills the grid's; its width keeps its proportion and is centred (a 1 stays thin), or is squeezed if it is too wide.
    The same as to_grid in tools/make_digit_shapes.py. 'only': when given, a map the size of the cut-out saying which dots are this
    shape's (non-zero) - a neighbouring shape's dots inside the same rectangle are left out. */
inline Shape shapeOf (const unsigned char* bits, int stride, int x0, int y0, int w, int h, const unsigned char* only = nullptr)
{
    Shape out;
    if (w <= 0 || h <= 0) return out;
    const double outW = juce::jmin ((double) kW, juce::jmax (1.0, (double) w * kH / (double) h)), xOff = (kW - outW) * 0.5;
    double grid[kW * kH] = {};
    for (int y = 0; y < h; ++y)
    {
        const double gy0 = (double) y * kH / h, gy1 = (double) (y + 1) * kH / h;
        for (int x = 0; x < w; ++x)
        {
            if (bits[(size_t) (y0 + y) * (size_t) stride + (size_t) (x0 + x)] == 0) continue;
            if (only != nullptr && only[(size_t) y * (size_t) w + (size_t) x] == 0) continue;
            const double gx0 = xOff + (double) x * outW / w, gx1 = xOff + (double) (x + 1) * outW / w;
            for (int cy = (int) gy0; cy < juce::jmin (kH, (int) std::ceil (gy1)); ++cy)
            {
                const double oy = juce::jmin (gy1, cy + 1.0) - juce::jmax (gy0, (double) cy);
                if (oy <= 0.0) continue;
                for (int cx = (int) gx0; cx < juce::jmin (kW, (int) std::ceil (gx1)); ++cx)
                {
                    const double ox = juce::jmin (gx1, cx + 1.0) - juce::jmax (gx0, (double) cx);
                    if (ox > 0.0) grid[cy * kW + cx] += ox * oy;
                }
            }
        }
    }
    for (int i = 0; i < kW * kH; ++i) out.v[i] = (unsigned char) juce::jlimit (0, 15, (int) std::lround (grid[i] * 15.0));
    return out;
}

/** How alike two shapes are: the ink they share over the ink either has (1 = the same shape, 0 = nothing in common). */
inline float alike (const Shape& a, const Shape& b) noexcept
{
    int both = 0, either = 0;
    for (int i = 0; i < kW * kH; ++i) { both += juce::jmin (a.v[i], b.v[i]); either += juce::jmax (a.v[i], b.v[i]); }
    return either > 0 ? (float) both / (float) either : 0.0f;
}

/** The digit a shape is most like. 'score': how alike (above about 0.6 it is that digit in some known type; handwriting and things that
    are not digits come out lower). 'margin': how much more alike than to the best OTHER digit. digit -1: the shape is empty. */
struct Guess { int digit = -1; float score = 0.0f, margin = 0.0f; };
inline Guess classify (const Shape& s)
{
    float best[10] = {};
    bool any = false;
    for (int i = 0; i < kW * kH && ! any; ++i) any = s.v[i] != 0;
    if (! any) return {};
    for (auto& k : detail::known()) best[k.digit] = juce::jmax (best[k.digit], alike (s, k.shape));
    Guess g;
    float second = 0.0f;
    for (int d = 0; d < 10; ++d)
    {
        if (best[d] > g.score) { second = g.score; g.score = best[d]; g.digit = d; }
        else second = juce::jmax (second, best[d]);
    }
    g.margin = g.score - second;
    return g;
}
} // namespace scoredigits
} // namespace td
