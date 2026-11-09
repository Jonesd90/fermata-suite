#pragma once
#include "Dsp.h"
#include "Denoise.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <set>
#include <vector>

namespace td::dehum
{
/** 3.44.0: the De-Hummer (the De-hum tab of Spectral Repair and Noise mode). Two halves:

    FINDING. A hum is a tone that never changes: the same pitch, about the same level, in every frame of the box. The box is cut into long frames
    (a power of two near 1.5 Hz per bin: 65536 samples at 96 kHz), at most 64 of them spread evenly over the box, and each bin's level is taken
    as the 10th-percentile over the frames - what is there ALL the time. A note that sounds through part of the box drops out of that; a note
    held right through it cannot be told from a hum (the user unticks it). A tone is then a narrow peak of that steady spectrum standing 12 dB
    or more above the floor around it (the running median over +-40 Hz), whose level and pitch hardly move from frame to frame (its exact pitch
    is followed from frame to frame: how far it wanders decides how wide the notch must be). The tones are then grouped:
    - a SERIES: tones at whole-number multiples (up to the 60th) of one fundamental between 20 Hz and 2 kHz - a motor, a transformer, a fan: the
      user's 30.18 Hz buzz with harmonics 7, 11, 12, 15, 16, 20, 21, 28 and 42 - 49 (211, 332, 362, 453, 483, 604, 634, 845, 1268 - 1479 Hz).
      Tones land near a multiple of a low fundamental by chance quite often (each has a 2 x tol / f0 chance), so a series must have more members
      than chance would give it (4 + the number expected), three of them up to the 30th harmonic, where a real buzz is thick with them;
    - a ROW: 4 or more tones evenly spaced (10 - 400 Hz) high up with no fundamental among them (a switching supply's 55.5 Hz sidebands at 15 kHz);
    - the rest stay single tones (the mains' 100 Hz, a 1440 Hz whistle).
    A hum below -126 dBFS is not listed (nothing at that level is heard; 24-bit converters leave tones like that), nor anything above 20 kHz.

    REMOVING. Each tone is taken out by heterodyning: the sound is shifted down so the tone sits at 0 Hz, a zero-phase (forward then backward)
    fourth-order Butterworth low-pass a few hertz wide keeps only what sits at that exact pitch, and the result, shifted back up, is subtracted
    from the sound - a steep notch that follows the hum's level from moment to moment and nothing else. The notch is 1.5 - 4 Hz (Soft .. Hard)
    plus twice the tone's measured wander (so a wandering whistle stays in the flat middle of it), and Soft .. Hard also sets how much of the tone
    goes: 6 dB at Soft, 40 dB (all of it) at Hard. The
    subtraction fades in and out over 50 ms at the box's edges; outside the box nothing changes. Two seconds of sound either side of the box
    are used to settle the filter, so a box that starts in the middle of a hum is clean right from its first sample; a long box is worked in
    one-minute stretches, each with its two seconds of the ORIGINAL sound either side, so an hour needs a minute's memory and the joins are exact.
    The reference (Python, checked on the user's 17 example files) is in docs/DEVELOPMENT-NOTES.md, "De-Hummer". */

/** One hum, as found or as the user set it. */
struct Hum
{
    enum class Kind { Tone, Series, Row };
    Kind kind = Kind::Tone;
    double hz = 0.0;                 // Tone: its pitch. Series: the fundamental. Row: the lowest tone.
    double spacing = 0.0;            // Row: the distance between its tones (Hz)
    std::vector<int> harmonics;      // Series: the harmonic numbers to take out (the fundamental is the 1st), lowest first. Row: 0 .. count - 1.
    double levelDb = -200.0;         // the loudest of its tones (dBFS, as a sine's level)
    double aboveDb = 0.0;            // ... how far above the floor around it (dB); 0 = set by the user, nothing measured there
    double wanderHz = 0.0;           // how far its pitch moves over the box (Hz; the median over a series' or row's tones)
    bool on = true;                  // ticked: it is taken out
    bool user = false;               // typed in by the user (Add), or its pitch changed by hand

    /** Every pitch this hum takes out, lowest first. */
    std::vector<double> frequencies() const
    {
        std::vector<double> v;
        if (kind == Kind::Tone) { v.push_back (hz); return v; }
        for (int k : harmonics) v.push_back (kind == Kind::Series ? hz * (double) k : hz + spacing * (double) k);
        return v;
    }
    double lowest() const { auto v = frequencies(); return v.empty() ? hz : v.front(); }
    double highest() const { auto v = frequencies(); return v.empty() ? hz : v.back(); }
    /** The pitch the user may change: a tone's own, a series' fundamental, a row's lowest tone (the others follow). */
    double editable() const noexcept { return hz; }
    void setEditable (double newHz) { if (newHz > 0.0) { hz = newHz; user = true; } }
    /** In words, for the list: "1439.0 Hz" / "Buzz at 30.18 Hz: 15 harmonics, 211 - 1751 Hz" / "17 tones 55.6 Hz apart, 14944 - 15833 Hz". */
    juce::String text() const
    {
        if (kind == Kind::Tone) return juce::String (hz, 1) + " Hz";
        if (kind == Kind::Series) return "Buzz at " + juce::String (hz, 2) + " Hz: " + juce::String ((int) harmonics.size()) + (harmonics.size() == 1 ? " harmonic" : " harmonics") + ", "
                                         + juce::String ((int) std::lround (lowest())) + " - " + juce::String ((int) std::lround (highest())) + " Hz";
        return juce::String ((int) harmonics.size()) + " tones " + juce::String (spacing, 1) + " Hz apart, " + juce::String ((int) std::lround (lowest())) + " - " + juce::String ((int) std::lround (highest())) + " Hz";
    }
    /** How loud, in words: "27 dB above the room" or, for one typed in, "not measured". */
    juce::String loudness() const
    {
        if (aboveDb <= 0.0) return "nothing steady measured there";
        return juce::String ((int) std::lround (aboveDb)) + " dB above the room" + (wanderHz >= 1.5 ? ", wanders " + juce::String (wanderHz, 1) + " Hz" : juce::String());
    }
};

/** The settings of a De-hum fix. */
struct Params
{
    std::vector<Hum> hums;
    double hardness = 0.7;           // 0 = Soft (6 dB off, a 1.5 Hz notch) .. 1 = Hard (40 dB off, a 4 Hz notch), plus twice each tone's own wander
    /** How much of each tone goes (dB) and how wide the notch is (Hz, before twice the tone's wander is added). */
    double reductionDb() const noexcept { return 6.0 + 34.0 * juce::jlimit (0.0, 1.0, hardness); }
    double notchHz() const noexcept { return 1.5 + 2.5 * juce::jlimit (0.0, 1.0, hardness); }
    /** The pitches to take out (the ticked hums'), each once, with its notch width. */
    std::vector<std::pair<double, double>> notches() const
    {
        std::vector<std::pair<double, double>> v;
        for (auto& h : hums)
        {
            if (! h.on) continue;
            for (double hz : h.frequencies())
            {
                if (hz <= 1.0) continue;
                bool dup = false;
                for (auto& n : v) if (std::abs (n.first - hz) < 0.5) { dup = true; n.second = juce::jmax (n.second, notchHz() + 2.0 * h.wanderHz); }
                if (! dup) v.push_back ({ hz, notchHz() + 2.0 * h.wanderHz });
            }
        }
        std::sort (v.begin(), v.end());
        return v;
    }
    juce::String summary() const
    {
        int n = 0; for (auto& h : hums) if (h.on) ++n;
        return juce::String (n) + (n == 1 ? " hum" : " hums") + " (" + juce::String ((int) notches().size()) + (notches().size() == 1 ? " tone" : " tones") + "), " + juce::String ((int) std::lround (reductionDb())) + " dB off";
    }
};

/** Where the finder reads the box's audio: 'channels' mono channels (one per track is enough: a hum sits in every channel of a track at the same
    pitch), each read(ch, at, n, dst) filling n samples from 'at' (samples of the box's own time: 0 = the box's start; beyond its end: silence). */
struct Source
{
    int channels = 0;
    juce::int64 length = 0;                                                 // the box's length in samples
    std::function<bool (int ch, juce::int64 at, int n, float* dst)> read;
    /** A source over audio already in memory: each vector one channel, the box [a, b) of it. */
    static Source ofVectors (const std::vector<const std::vector<float>*>& chans, juce::int64 a, juce::int64 b)
    {
        Source s; s.channels = (int) chans.size(); s.length = juce::jmax ((juce::int64) 0, b - a);
        s.read = [chans, a] (int ch, juce::int64 at, int n, float* dst)
        {
            if (ch < 0 || ch >= (int) chans.size()) return false;
            const auto& x = *chans[(size_t) ch];
            for (int i = 0; i < n; ++i) { const juce::int64 j = a + at + i; dst[i] = j >= 0 && j < (juce::int64) x.size() ? x[(size_t) j] : 0.0f; }
            return true;
        };
        return s;
    }
};

struct Found
{
    std::vector<Hum> hums;                                                  // loudest first
    double binHz = 0.0; int frames = 0;
    juce::String note;                                                      // what to tell the user when nothing could be measured ("" = fine)
};

namespace detail
{
/** The frame length: a power of two near 1.5 Hz per bin, but no longer than a third of the box (so there are a few frames to compare), never under 2048. */
inline int frameSize (double sr, juce::int64 length)
{
    int n = 2048;
    while ((double) n * 2.0 <= sr / 1.4) n *= 2;
    while (n > 2048 && (juce::int64) n > length / 3) n /= 2;
    return n;
}

/** The dB spectra of up to 64 frames of one channel, spread evenly over the box: spectra[frame][bin], as a sine's level in dBFS. */
struct Frames
{
    int n = 0, bins = 0, count = 0; double binHz = 0.0;
    std::vector<float> db;                                                  // count x bins
    const float* row (int t) const { return db.data() + (size_t) t * (size_t) bins; }
};
inline bool analyse (const Source& src, int ch, double sr, Frames& out, const std::function<bool()>& cancelled)
{
    const int n = frameSize (sr, src.length), hop = n / 4;
    const juce::int64 starts = juce::jmax ((juce::int64) 1, (src.length - n) / hop + 1);
    const int count = (int) juce::jmin ((juce::int64) 64, starts);
    out.n = n; out.bins = n / 2 + 1; out.count = count; out.binHz = sr / (double) n;
    out.db.assign ((size_t) count * (size_t) out.bins, -200.0f);
    const auto win = dsp::hann (n);
    double wsum = 0.0; for (double w : win) wsum += w;
    const double scale = 2.0 / wsum;                                        // a full-scale sine reads 0 dBFS
    std::vector<float> buf ((size_t) n); std::vector<double> a ((size_t) n); std::vector<std::complex<double>> spec ((size_t) out.bins);
    for (int t = 0; t < count; ++t)
    {
        if (cancelled && cancelled()) return false;
        const juce::int64 at = count > 1 ? (juce::int64) std::llround ((double) (starts - 1) * (double) t / (double) (count - 1)) * hop : 0;
        if (! src.read (ch, at, n, buf.data())) return false;
        for (int i = 0; i < n; ++i) a[(size_t) i] = (double) buf[(size_t) i] * win[(size_t) i];
        denoise::detail::realForward (a.data(), n, spec.data());
        float* row = out.db.data() + (size_t) t * (size_t) out.bins;
        for (int k = 0; k < out.bins; ++k) row[(size_t) k] = (float) (20.0 * std::log10 (std::abs (spec[(size_t) k]) * scale + 1e-30));
    }
    return true;
}

/** The p-th percentile (0 - 100) of a small set, by sorting a copy. */
inline float percentile (std::vector<float> v, double p)
{
    if (v.empty()) return -200.0f;
    std::sort (v.begin(), v.end());
    const double x = juce::jlimit (0.0, 1.0, p / 100.0) * (double) (v.size() - 1);
    const size_t i = (size_t) x; const double fr = x - (double) i;
    return i + 1 < v.size() ? (float) ((1.0 - fr) * v[i] + fr * v[i + 1]) : v[i];
}

/** The steady spectrum of one channel: each bin's 10th-percentile level over the frames (the lowest with fewer than 10 frames). */
inline std::vector<float> steadySpectrum (const Frames& fr)
{
    std::vector<float> low ((size_t) fr.bins), col ((size_t) fr.count);
    for (int k = 0; k < fr.bins; ++k)
    {
        for (int t = 0; t < fr.count; ++t) col[(size_t) t] = fr.row (t)[k];
        low[(size_t) k] = fr.count >= 10 ? percentile (col, 10.0) : *std::min_element (col.begin(), col.end());
    }
    return low;
}

/** The running median over +-half bins (the floor the tones stand on). */
inline std::vector<float> runningMedian (const std::vector<float>& v, int half)
{
    std::vector<float> out (v.size()), w;
    const int n = (int) v.size();
    for (int i = 0; i < n; ++i)
    {
        const int lo = juce::jmax (0, i - half), hi = juce::jmin (n, i + half + 1);
        w.assign (v.begin() + lo, v.begin() + hi);
        auto mid = w.begin() + (w.size() / 2);
        std::nth_element (w.begin(), mid, w.end());
        out[(size_t) i] = *mid;
    }
    return out;
}

/** The exact pitch of a peak at bin i: a parabola through the three bins round the top (clamped to half a bin). */
inline double peakHz (const float* row, int i, int bins, double binHz)
{
    if (i <= 0 || i >= bins - 1) return i * binHz;
    const double y0 = row[i - 1], y1 = row[i], y2 = row[i + 1], d = y0 - 2.0 * y1 + y2;
    const double off = d != 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / d) : 0.0;
    return ((double) i + off) * binHz;
}

/** A tone followed from frame to frame (the peak within 2 bins of bin i): how far its pitch wanders (the 15th - 85th percentile range, Hz), its
    steady level (the 10th percentile of its per-frame levels, the lowest with fewer than 10 frames) and how far the 80th percentile rises above that
    (a note that sounds for a few frames near the tone lifts those frames only; the percentiles leave them out). */
struct Track { double wanderHz = 0.0, steadyDb = -200.0, spreadDb = 0.0; };
inline Track trackTone (const Frames& fr, int i)
{
    std::vector<float> hz, lv;
    for (int t = 0; t < fr.count; ++t)
    {
        const float* row = fr.row (t);
        const int lo = juce::jmax (1, i - 2), hi = juce::jmin (fr.bins - 2, i + 2);
        int j = lo; for (int k = lo + 1; k <= hi; ++k) if (row[k] > row[j]) j = k;
        hz.push_back ((float) peakHz (row, j, fr.bins, fr.binHz)); lv.push_back (row[j]);
    }
    Track tr;
    tr.wanderHz = hz.size() > 1 ? (double) (percentile (hz, 85.0) - percentile (hz, 15.0)) : 0.0;
    tr.steadyDb = fr.count >= 10 ? percentile (lv, 10.0) : *std::min_element (lv.begin(), lv.end());
    tr.spreadDb = (fr.count >= 10 ? percentile (lv, 80.0) : *std::max_element (lv.begin(), lv.end())) - tr.steadyDb;
    return tr;
}

/** A tone found in one channel's steady spectrum. */
struct Tone { double hz = 0.0, levelDb = -200.0, aboveDb = 0.0, wanderHz = 0.0, spreadDb = 0.0; int bin = 0, channel = 0; };

/** The tones of one channel: narrow steady peaks 'minAbove' dB or more above the floor, between f0 and f1. 'low' and 'prom' come back for the grouping. */
inline std::vector<Tone> tonesOf (const Frames& fr, int ch, double f0, double f1, double minAbove, double minLevelDb, std::vector<float>& low, std::vector<float>& prom)
{
    low = steadySpectrum (fr);
    const auto floor = runningMedian (low, juce::jmax (1, (int) std::lround (40.0 / fr.binHz)));
    prom.resize (low.size()); for (size_t k = 0; k < low.size(); ++k) prom[k] = low[k] - floor[k];
    std::vector<Tone> tones;
    for (int i = 1; i < fr.bins - 1;)
    {
        const double hz = i * fr.binHz;
        if (hz >= f0 && hz <= f1 && prom[(size_t) i] >= minAbove && low[(size_t) i] >= low[(size_t) i - 1] && low[(size_t) i] > low[(size_t) i + 1])
        {
            // narrow: it falls at least 6 dB on each side, and is at most 10 bins wide where it has fallen 20 dB (or most of its height: a very loud tone's
            // skirt runs on for many bins), as a tone does; a hump of noise, or a band of them, does not. A hum right beside a note (the user's 211 Hz hum,
            // 5 Hz under a 216 Hz note): the fall stops at the saddle between them, 6 dB or more down, and the tone is still narrow.
            const float depth = juce::jmin (20.0f, 0.7f * prom[(size_t) i]);
            int lo = i; while (lo > 0 && low[(size_t) lo - 1] < low[(size_t) lo] && low[(size_t) i] - low[(size_t) lo] < depth) --lo;
            int hi = i; while (hi < fr.bins - 1 && low[(size_t) hi + 1] < low[(size_t) hi] && low[(size_t) i] - low[(size_t) hi] < depth) ++hi;
            const bool narrow = low[(size_t) i] - low[(size_t) lo] >= 6.0f && low[(size_t) i] - low[(size_t) hi] >= 6.0f && hi - lo <= 10;
            while (hi < fr.bins - 1 && low[(size_t) hi + 1] < low[(size_t) hi]) ++hi;          // (then on down the whole skirt, so the next search starts past it)
            if (narrow && low[(size_t) i] >= (float) minLevelDb)
            {
                const auto tr = trackTone (fr, i);
                if (tr.wanderHz <= 6.0 && tr.spreadDb <= 12.0)
                {
                    Tone t; t.hz = peakHz (low.data(), i, fr.bins, fr.binHz); t.bin = i; t.levelDb = low[(size_t) i]; t.aboveDb = prom[(size_t) i];
                    t.wanderHz = tr.wanderHz; t.spreadDb = tr.spreadDb; t.channel = ch;
                    tones.push_back (t);
                }
            }
            i = hi + 1;
        }
        else ++i;
    }
    return tones;
}

/** A narrow peak at 'hz' in the steady spectrum, 'minDb' or more above the floor: its exact pitch, or 0. */
inline double faintAt (double hz, const std::vector<float>& low, const std::vector<float>& prom, double binHz, double minDb, double& above)
{
    const int i = (int) std::lround (hz / binHz), n = (int) prom.size();
    if (i < 2 || i > n - 3) return 0.0;
    int j = i - 1; for (int k = i; k <= i + 1; ++k) if (prom[(size_t) k] > prom[(size_t) j]) j = k;
    if (prom[(size_t) j] < (float) minDb || prom[(size_t) j] < prom[(size_t) j - 1] || prom[(size_t) j] < prom[(size_t) j + 1]) return 0.0;
    above = prom[(size_t) j];
    return peakHz (low.data(), j, n, binHz);
}

inline double median (std::vector<double> v) { if (v.empty()) return 0.0; std::sort (v.begin(), v.end()); return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]); }

/** The members of the best harmonic series among the free tones (see the top of the file), with its fundamental; empty = none. */
inline std::vector<int> bestSeries (const std::vector<Tone>& tones, const std::vector<int>& free, double& fundamental)
{
    auto members = [&] (double f0, double tolBase, double tolPerHz)
    {
        std::vector<int> out;
        for (int k : free)
        {
            const int h = (int) std::lround (tones[(size_t) k].hz / f0);
            if (h >= 1 && h <= 60 && std::abs (tones[(size_t) k].hz - h * f0) <= juce::jmin (1.5, tolBase + tolPerHz * tones[(size_t) k].hz)) out.push_back (k);
        }
        return out;
    };
    // a real buzz has its harmonics thick on the ground low down: three or more of them up to the 30th, or it is a coincidence
    auto lowMembers = [&] (const std::vector<int>& mem, double f0) { int n = 0; for (int k : mem) if (std::lround (tones[(size_t) k].hz / f0) <= 30) ++n; return n; };
    double bestScore = -1.0, bestF0 = 0.0; std::vector<int> best;
    for (int i : free)
    {
        const double t = tones[(size_t) i].hz;
        if (t > 2000.0) continue;
        for (int m = 1; m <= juce::jmin (60, (int) (t / 20.0)); ++m)
        {
            const double f0 = t / m;
            const auto mem = members (f0, 2.0, 0.0);
            if (mem.size() < 3) continue;
            double num = 0.0, den = 0.0;                                       // the fundamental fitted through the origin (least squares)
            for (int k : mem) { const double h = std::round (tones[(size_t) k].hz / f0); num += h * tones[(size_t) k].hz; den += h * h; }
            const double f0b = num / den;
            const auto mem2 = members (f0b, 0.5, 0.001);
            const int need = 4 + (int) std::ceil ((double) free.size() * 2.4 / f0b);
            if ((int) mem2.size() < need || lowMembers (mem2, f0b) < 3) continue;
            double score = 0.0; for (int k : mem2) score += tones[(size_t) k].aboveDb;
            if (score > bestScore + 1e-9 || (std::abs (score - bestScore) <= 1e-9 && f0b > bestF0)) { bestScore = score; bestF0 = f0b; best = mem2; }
        }
    }
    fundamental = bestF0;
    return best;
}

/** The best row of 4 or more free tones evenly spaced (10 - 400 Hz), one missing tone allowed at a time; its spacing and lowest member come back. Empty = none. */
inline std::vector<int> bestRow (const std::vector<Tone>& tones, const std::vector<int>& free, double binHz, double& spacing, double& base, std::vector<int>& indices)
{
    const double tol = juce::jmax (1.5 * binHz, 1.0);
    double bestScore = -1.0; std::vector<int> best; double bestSp = 0.0, bestBase = 0.0; std::vector<int> bestKs;
    for (int i : free)
        for (int j : free)
        {
            const double d = tones[(size_t) j].hz - tones[(size_t) i].hz;
            if (d < 10.0 || d > 400.0) continue;
            std::vector<int> mem { i, j };
            for (int dir : { 1, -1 })
            {
                double nxt = (dir > 0 ? tones[(size_t) j].hz : tones[(size_t) i].hz) + dir * d;
                int gaps = 0;
                for (;;)
                {
                    int pick = -1;
                    for (int k : free) if (std::find (mem.begin(), mem.end(), k) == mem.end() && std::abs (tones[(size_t) k].hz - nxt) <= tol && (pick < 0 || std::abs (tones[(size_t) k].hz - nxt) < std::abs (tones[(size_t) pick].hz - nxt))) pick = k;
                    if (pick < 0) { if (++gaps > 1) break; nxt += dir * d; continue; }
                    gaps = 0; mem.push_back (pick); nxt = tones[(size_t) pick].hz + dir * d;
                }
            }
            if (mem.size() < 4) continue;
            std::sort (mem.begin(), mem.end(), [&] (int a, int b) { return tones[(size_t) a].hz < tones[(size_t) b].hz; });
            // the spacing and the base by least squares: hz = base + k * spacing
            const double h0 = tones[(size_t) mem[0]].hz;
            double sk = 0.0, sh = 0.0, skk = 0.0, skh = 0.0; const double n = (double) mem.size();
            std::vector<int> ks;
            for (int k : mem) { const double kk = std::round ((tones[(size_t) k].hz - h0) / d); ks.push_back ((int) kk); sk += kk; sh += tones[(size_t) k].hz; skk += kk * kk; skh += kk * tones[(size_t) k].hz; }
            const double den = n * skk - sk * sk;
            const double sp = den != 0.0 ? (n * skh - sk * sh) / den : d, b0 = (sh - sp * sk) / n;
            double score = 0.0; for (int k : mem) score += tones[(size_t) k].aboveDb;
            if (score > bestScore) { bestScore = score; best = mem; bestSp = sp; bestBase = b0; bestKs = ks; }
        }
    spacing = bestSp; base = bestBase; indices = bestKs;
    return best;
}
} // namespace detail

/** Finds the hums in the box between f0 and f1 Hz (loudest first). 'cancelled' may stop it early (an empty result with a note). */
inline Found find (const Source& src, double sr, double f0, double f1, const std::function<bool()>& cancelled = {})
{
    using namespace detail;
    Found out;
    if (src.channels <= 0 || src.length < 2048 || sr <= 0.0) { out.note = "The box is too short to look for hums in: make it at least a tenth of a second long (two seconds or more is better)."; return out; }
    f0 = juce::jmax (f0, 20.0); f1 = juce::jmin (f1, 20000.0, sr * 0.5 - 10.0);
    constexpr double kMinAbove = 12.0, kMinLevel = -126.0, kFaintDb = 7.0;
    // the tones of every channel; where channels hear the same tone, the one that hears it best speaks for it
    std::vector<Tone> tones; std::vector<float> low, prom; double binHz = 0.0; int frames = 0;
    std::vector<std::vector<float>> lows, proms;                             // per channel, for the faint members of the groups
    for (int ch = 0; ch < src.channels; ++ch)
    {
        Frames fr;
        if (! analyse (src, ch, sr, fr, cancelled)) { out.note = cancelled && cancelled() ? "Cancelled." : "The audio could not be read."; return out; }
        binHz = fr.binHz; frames = fr.count;
        auto t = tonesOf (fr, ch, f0, f1, kMinAbove, kMinLevel, low, prom);
        lows.push_back (low); proms.push_back (prom);
        for (auto& x : t)
        {
            bool merged = false;
            for (auto& y : tones) if (std::abs (y.hz - x.hz) <= juce::jmax (1.0, 1.5 * binHz)) { merged = true; if (x.aboveDb > y.aboveDb) y = x; break; }
            if (! merged) tones.push_back (x);
        }
    }
    out.binHz = binHz; out.frames = frames;
    if (tones.size() > 200)                                                   // (a box full of steady partials: the 200 most prominent are enough to group)
    {
        std::sort (tones.begin(), tones.end(), [] (const Tone& a, const Tone& b) { return a.aboveDb > b.aboveDb; });
        tones.resize (200);
    }
    std::sort (tones.begin(), tones.end(), [] (const Tone& a, const Tone& b) { return a.hz < b.hz; });
    auto promOf = [&] (int ch) -> const std::vector<float>& { return proms[(size_t) juce::jlimit (0, (int) proms.size() - 1, ch)]; };
    auto lowOf = [&] (int ch) -> const std::vector<float>& { return lows[(size_t) juce::jlimit (0, (int) lows.size() - 1, ch)]; };
    auto wanderOf = [&] (const std::vector<int>& mem) { std::vector<double> w; for (int k : mem) w.push_back (tones[(size_t) k].wanderHz); return median (w); };
    std::vector<int> free; for (int i = 0; i < (int) tones.size(); ++i) free.push_back (i);
    auto take = [&] (const std::vector<int>& mem) { std::vector<int> rest; for (int k : free) if (std::find (mem.begin(), mem.end(), k) == mem.end()) rest.push_back (k); free = rest; };
    // series first
    for (;;)
    {
        double fund = 0.0;
        const auto mem = bestSeries (tones, free, fund);
        if (mem.empty()) break;
        Hum h; h.kind = Hum::Kind::Series; h.hz = fund;
        std::set<int> ks; int loudest = mem[0];
        for (int k : mem) { ks.insert ((int) std::lround (tones[(size_t) k].hz / fund)); if (tones[(size_t) k].aboveDb > tones[(size_t) loudest].aboveDb) loudest = k; }
        // faint members: a gap inside a run of heard harmonics, or one step beyond a run of three
        std::set<int> cands;
        for (int k : ks)
        {
            if (! ks.count (k - 1) && ks.count (k - 2)) cands.insert (k - 1);
            if (ks.count (k - 1) && ks.count (k - 2) && ! ks.count (k + 1)) cands.insert (k + 1);
            if (ks.count (k + 1) && ks.count (k + 2) && ! ks.count (k - 1) && k > 1) cands.insert (k - 1);
        }
        const auto& pr = promOf (tones[(size_t) loudest].channel); const auto& lw = lowOf (tones[(size_t) loudest].channel);
        for (int k : cands) { double above = 0.0; if (k * fund >= f0 && k * fund <= f1 && faintAt (k * fund, lw, pr, binHz, kFaintDb, above) > 0.0) ks.insert (k); }
        h.harmonics.assign (ks.begin(), ks.end());
        h.levelDb = tones[(size_t) loudest].levelDb; h.aboveDb = tones[(size_t) loudest].aboveDb; h.wanderHz = wanderOf (mem);
        out.hums.push_back (h);
        take (mem);
    }
    // then rows
    for (;;)
    {
        double sp = 0.0, base = 0.0; std::vector<int> ks;
        const auto mem = bestRow (tones, free, binHz, sp, base, ks);
        if (mem.empty() || sp <= 0.0) break;
        int loudest = mem[0]; for (int k : mem) if (tones[(size_t) k].aboveDb > tones[(size_t) loudest].aboveDb) loudest = k;
        const auto& pr = promOf (tones[(size_t) loudest].channel); const auto& lw = lowOf (tones[(size_t) loudest].channel);
        std::set<int> all (ks.begin(), ks.end());
        for (int dir : { -1, 1 })                                           // faint tones beyond the ends, one gap allowed
        {
            int k = (dir < 0 ? *all.begin() : *all.rbegin()) + dir, gaps = 0;
            while (base + k * sp >= f0 && base + k * sp <= f1)
            {
                double above = 0.0;
                if (faintAt (base + k * sp, lw, pr, binHz, kFaintDb, above) > 0.0) { all.insert (k); gaps = 0; }
                else if (++gaps > 1) break;
                k += dir;
            }
        }
        Hum h; h.kind = Hum::Kind::Row; h.spacing = sp; h.hz = base + (*all.begin()) * sp;
        for (int k : all) h.harmonics.push_back (k - *all.begin());
        h.levelDb = tones[(size_t) loudest].levelDb; h.aboveDb = tones[(size_t) loudest].aboveDb; h.wanderHz = wanderOf (mem);
        out.hums.push_back (h);
        take (mem);
    }
    for (int k : free)
    {
        Hum h; h.kind = Hum::Kind::Tone; h.hz = tones[(size_t) k].hz; h.levelDb = tones[(size_t) k].levelDb; h.aboveDb = tones[(size_t) k].aboveDb; h.wanderHz = tones[(size_t) k].wanderHz;
        out.hums.push_back (h);
    }
    std::stable_sort (out.hums.begin(), out.hums.end(), [] (const Hum& a, const Hum& b) { return a.aboveDb > b.aboveDb; });
    if (out.hums.size() > 12) out.hums.resize (12);
    for (auto& h : out.hums) h.on = h.aboveDb >= 15.0;                     // the faint ones are listed but left for the user to tick
    if (out.hums.empty()) out.note = "No steady tone stands out in the box (" + juce::String (frames) + (frames == 1 ? " frame" : " frames") + ", " + juce::String (binHz, 1) + " Hz per bin). Add one by hand if you can hear it.";
    return out;
}

/** The steady narrow peak nearest 'hz' (and strong: a dB of height counts as a hertz of distance) within +-searchHz of it in the box - for a
    pitch the user typed (Add, or a changed one): the exact pitch and how loud it is. Nothing steady there: the typed pitch comes back with
    aboveDb 0 (it is still taken out, exactly as typed). */
inline Hum measureAt (const Source& src, double sr, double hz, double searchHz, const std::function<bool()>& cancelled = {})
{
    using namespace detail;
    Hum h; h.kind = Hum::Kind::Tone; h.hz = hz; h.user = true;
    if (src.channels <= 0 || src.length < 2048 || sr <= 0.0 || hz <= 0.0) return h;
    double best = -1.0e9;
    for (int ch = 0; ch < src.channels; ++ch)
    {
        Frames fr;
        if (! analyse (src, ch, sr, fr, cancelled)) return h;
        std::vector<float> low, prom;
        auto t = tonesOf (fr, ch, juce::jmax (20.0, hz - searchHz), juce::jmin (sr * 0.5 - 10.0, hz + searchHz), 6.0, -140.0, low, prom);
        for (auto& x : t)                                                   // the nearest strong one: a dB of height is worth a hertz of distance
            if (h.aboveDb <= 0.0 || x.aboveDb - std::abs (x.hz - hz) > best) { best = x.aboveDb - std::abs (x.hz - hz); h.hz = x.hz; h.levelDb = x.levelDb; h.aboveDb = x.aboveDb; h.wanderHz = x.wanderHz; }
    }
    return h;
}

namespace detail
{
/** A fourth-order Butterworth low-pass (two biquads), run forwards then backwards (zero phase) over a real signal, in place: flat out to the
    cutoff, steep beyond it, so the notch takes what is inside it whole and nothing outside. The state starts at the steady state for the first
    sample (and, backwards, the last), so a signal that begins mid-hum does not ring. */
inline void zeroPhaseLowpass (std::vector<double>& x, double sr, double cutoffHz)
{
    if (x.size() < 3) return;
    const double K = std::tan (juce::MathConstants<double>::pi * juce::jlimit (1e-6, 0.45 * sr, cutoffHz) / sr);
    struct Biquad { double b0, b1, b2, a1, a2; };
    auto make = [K] (double q) { const double norm = 1.0 / (1.0 + K / q + K * K); return Biquad { K * K * norm, 2.0 * K * K * norm, K * K * norm, 2.0 * (K * K - 1.0) * norm, (1.0 - K / q + K * K) * norm }; };
    const Biquad stages[2] = { make (0.5411961), make (1.3065630) };          // (the two Q's of a 4th-order Butterworth)
    auto pass = [&] (bool forwards)
    {
        const size_t n = x.size();
        for (const auto& s : stages)
        {
            const double x0 = forwards ? x[0] : x[n - 1];
            double z1 = (1.0 - s.b0) * x0, z2 = (s.b2 - s.a2) * x0;          // transposed direct form II at steady state (unity gain at DC)
            for (size_t i = 0; i < n; ++i)
            {
                double& v = x[forwards ? i : n - 1 - i];
                const double y = s.b0 * v + z1;
                z1 = s.b1 * v - s.a1 * y + z2;
                z2 = s.b2 * v - s.a2 * y;
                v = y;
            }
        }
    };
    pass (true); pass (false);
}
} // namespace detail

/** Takes the ticked hums out of x over [a, b) (samples), fading in and out over 'fadeSeconds' at the edges; outside [a, b) nothing changes.
    The work goes in stretches of a minute, each with two seconds of sound either side to settle the filters (so an hour's edit needs a minute's
    memory, and the join between stretches is exact to well under a thousandth of a dB). */
inline void remove (std::vector<float>& x, double sr, juce::int64 a, juce::int64 b, const Params& p, double fadeSeconds = 0.05)
{
    using namespace detail;
    const juce::int64 L = (juce::int64) x.size();
    a = juce::jlimit ((juce::int64) 0, L, a); b = juce::jlimit ((juce::int64) 0, L, b);
    if (b - a < 2) return;
    const auto notches = p.notches();
    if (notches.empty()) return;
    const juce::int64 ctx = (juce::int64) (2.0 * sr), chunk = (juce::int64) (60.0 * sr);
    const double fraction = 1.0 - std::pow (10.0, -p.reductionDb() / 20.0);
    const double fadeN = juce::jmax (1.0, juce::jmin (fadeSeconds * sr, (double) (b - a) * 0.5));
    std::vector<double> re, im, total;
    std::vector<float> keep;                                                     // the ORIGINAL sound of the two seconds before this stretch (the last stretch wrote over it in x)
    for (juce::int64 c0 = a; c0 < b; c0 += chunk)
    {
        const juce::int64 c1 = juce::jmin (b, c0 + chunk);                        // the stretch written this time
        const juce::int64 lo = juce::jmax ((juce::int64) 0, c0 - ctx), hi = juce::jmin (L, c1 + ctx);
        const size_t n = (size_t) (hi - lo);
        re.assign (n, 0.0); im.assign (n, 0.0); total.assign (n, 0.0);
        auto original = [&] (size_t i) { const juce::int64 j = lo + (juce::int64) i; return j < c0 && ! keep.empty() ? keep[(size_t) (j - lo)] : x[(size_t) j]; };
        for (auto& nt : notches)
        {
            const double hz = nt.first, width = juce::jmax (0.5, nt.second);
            // shift the tone to 0 Hz: multiply by e^(-i w t), with a rotating phasor (renormalised every 1024 samples)
            const std::complex<double> step = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz / sr);
            std::complex<double> ph = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz * (double) lo / sr);
            for (size_t i = 0; i < n; ++i)
            {
                const double v = original (i);
                re[i] = v * ph.real(); im[i] = v * ph.imag();
                ph *= step;
                if ((i & 1023) == 1023) ph /= std::abs (ph);
            }
            zeroPhaseLowpass (re, sr, width * 0.5); zeroPhaseLowpass (im, sr, width * 0.5);
            // back up: 2 Re (z e^(+i w t)) is the tone as it is in the sound
            ph = std::polar (1.0, 2.0 * juce::MathConstants<double>::pi * hz * (double) lo / sr);
            const std::complex<double> up = std::conj (step);
            for (size_t i = 0; i < n; ++i)
            {
                total[i] += 2.0 * (re[i] * ph.real() - im[i] * ph.imag());
                ph *= up;
                if ((i & 1023) == 1023) ph /= std::abs (ph);
            }
        }
        keep.assign (x.begin() + (std::ptrdiff_t) juce::jmax ((juce::int64) 0, c1 - ctx), x.begin() + (std::ptrdiff_t) c1);     // (before this stretch is written: the next one's context)
        for (juce::int64 i = c0; i < c1; ++i)
        {
            const double w = juce::jlimit (0.0, 1.0, juce::jmin ((double) (i - a) / fadeN, (double) (b - 1 - i) / fadeN));
            x[(size_t) i] = (float) ((double) x[(size_t) i] - fraction * w * total[(size_t) (i - lo)]);
        }
    }
}
} // namespace td::dehum
