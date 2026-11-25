#pragma once
/** 3.63.0 (asked for on 23 November 2026): the tuning line. "as fermata can work out how the pitch of a take can get sharper or flatter over
    time (I think in the ReHarmoniser module), could we also get a tool that is used both during the take window use (to show quickly after a
    take how the pitch of the take is and where it goes up or down). This would be a sort of automatic process that would process while the take
    is being recorded so the user can see it live and then it gets stored for viewing after and during editing." Then: "make sure there is the
    option to show/hide the tuning line. The user should be able to set a fixed pitch called 'Absolute' (i.e. A=440 or any other Hz for A) or
    select 'Relative' mode, where the tuning is relative to the starting pitch".

    WHAT IS MEASURED: the ensemble's own tuning, as the ReHarmoniser's Ensemble ReCentre measures it (rehar::refPoints / refStats): the steady
    spectral peaks (60 - 1100 Hz) of the four loudest channels, frame by frame (0.1 s), kept where a peak carries on in the two frames either side,
    each as cents from the nearest semitone of A = 440 Hz; their weighted mode / mean is where the ensemble sits. One reading for every 4 s
    window, every 2 s (refSeries' windows), so a line of readings across the take. Readings fold into -50 .. +50 c; the line is kept continuous
    (each reading is taken within 50 c of the one before), so a drift of more than a quarter-tone still reads as a drift. Held notes give
    readings; fast passages, percussion, silence and noise give gaps. A reading needs at least 12 steady peaks; it is about +-2 c.

    WHERE: the audio is first brought down to about 8 kHz (Decimator: everything the measurement looks at is below 1.1 kHz) and measured on a
    single low-priority thread of its own - the ReHarmoniser's refPoints itself starts threads for its channels (parallelFor), which must not
    happen beside a recording, so its measurement is done here again, step for step, on one thread (measure). While a take records (Live), the
    engine's feed of each recording track (AudioEngine::LiveFeed - the one the live waveform colours use; the feeds run while WaveColour OR the
    tuning line is on) is measured as it comes in, so the line grows during the take; when the take is finished its line is stored (Store::put)
    without reading the files again. Takes recorded without it, imported, or measured from other tracks are measured from their files (Store),
    one at a time, never while a take records. Each take's line is kept in the project's Analysis folder (<take id>.ftune), with what it was
    measured from, so it is measured once.

    SHOWN: Absolute: cents from A = the Hz set (440 by default; the first reading is put within 50 c of it, the rest follow it). Relative: cents
    from the take's starting pitch (the middle of its first three readings) - in an edit window, from the edit's starting pitch, so a join from
    a flat take to a sharp one shows. Green on pitch, red going sharp, blue going flat; +-25 c across the lane. fermata only.

    For the Fable pass (asked for: "we will do a pass with Fable later to stabilise it all"): the Live thread beside a recording (it only reads
    the feeds' rings, as LiveWaveColours does), the Store thread (one take at a time, held while a take records), and the empty-job bug the
    trial found (DEVELOPMENT-NOTES 3.63.0). */
#include "../core/AudioEngine.h"
#include "../core/Project.h"
#include "../core/AudioFormats.h"
#include "../reharmoniser/Engine.h"
#include "Uikit.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>

namespace td::tuning
{
struct Point { float t = 0.0f, cents = 0.0f, spread = 0.0f; int n = 0; };     // t: seconds into the take (the middle of its 4 s); cents: see appendPoint
struct Line
{
    std::vector<Point> pts;
    bool whole = true;                 // every window was measured (false: the live reader fell behind somewhere - the files are measured instead)
};

enum class Mode { relative = 0, absolute = 1 };
struct Settings
{
    Mode mode = Mode::relative;
    double aHz = 440.0;
    std::vector<juce::Uuid> from;      // the tracks it is measured from (empty: the loudest channels of each moment)
};

constexpr double kWindow = 4.0, kStep = 2.0, kScale = 25.0;     // seconds; seconds; cents from the middle of the lane to its edge
constexpr int kLaneH = 60;                                     // (46 at first: a 12 c drift was 10 px - now about 14)

/** Adds a reading, kept within 50 c of the one before (the readings are folded into -50 .. +50 c of the A = 440 semitones). */
inline void appendPoint (Line& l, Point p)
{
    if (! l.pts.empty())
    {
        const float prev = l.pts.back().cents;
        while (p.cents - prev > 50.0f) p.cents -= 100.0f;
        while (p.cents - prev < -50.0f) p.cents += 100.0f;
    }
    l.pts.push_back (p);
}

/** The take's starting pitch (Relative): the middle of its first three readings. */
inline float startOf (const Line& l)
{
    if (l.pts.empty()) return 0.0f;
    std::vector<float> v; for (size_t i = 0; i < l.pts.size() && i < 3; ++i) v.push_back (l.pts[i].cents);
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}
/** What the line shows for a reading of this line: cents from the take's start (Relative) or from A = aHz (Absolute). */
inline float shown (const Line& l, float cents, const Settings& s)
{
    if (l.pts.empty()) return 0.0f;
    if (s.mode == Mode::relative) return cents - startOf (l);
    const double off = 1200.0 * std::log2 (juce::jlimit (300.0, 600.0, s.aHz) / 440.0);
    const double k = std::round (((double) l.pts.front().cents - off) / 100.0);
    return (float) ((double) cents - off - 100.0 * k);
}
/** Green on pitch, towards red sharp and blue flat (full at 15 c). */
inline juce::Colour colourFor (float c)
{
    const juce::Colour green (0xff4cc463), red (0xffff5a5a), blue (0xff58a8ff);
    const float a = juce::jlimit (0.0f, 1.0f, (std::abs (c) - 3.0f) / 12.0f);
    return c >= 0.0f ? green.interpolatedWith (red, a) : green.interpolatedWith (blue, a);
}
inline juce::String centsText (float c)
{
    const int r = (int) std::lround (c);
    return (r > 0 ? "+" : r < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (r)) + " c";
}
inline juce::String modeText (const Settings& s)
{
    return s.mode == Mode::relative ? juce::String ("Relative") : "A = " + juce::String (s.aHz, s.aHz == std::floor (s.aHz) ? 0 : 1) + " Hz";
}

// ======================================================================================================== bringing the audio down to ~8 kHz
/** A low-pass and decimation by a whole number to about 8 kHz, a sample at a time (it keeps its state between calls). Only 60 - 1100 Hz is
    measured, so the pass band ends at 1.5 kHz and the stop band starts where a fold-back would reach 1.1 kHz (the new rate less 1.15 kHz):
    a Blackman-windowed sinc, 50 - 200 taps. */
class Decimator
{
public:
    void prepare (double sr)
    {
        factor = juce::jmax (1, (int) std::lround (sr / 8000.0));
        outRate = sr / (double) factor;
        phase = 0; pos = 0;
        if (factor == 1) { taps.assign (1, 1.0f); hist.assign (2, 0.0f); return; }
        const double pass = 1500.0, stop = juce::jmax (pass + 500.0, outRate - 1150.0);
        int L = (int) std::ceil (5.5 / ((stop - pass) / sr)); L |= 1; L = juce::jlimit (15, 801, L);
        const double fc = 0.5 * (pass + stop) / sr, M = 0.5 * (double) (L - 1), pi = juce::MathConstants<double>::pi;
        std::vector<double> h ((size_t) L); double sum = 0.0;
        for (int i = 0; i < L; ++i)
        {
            const double x = (double) i - M;
            const double sinc = x == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * x) / (pi * x);
            const double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (L - 1)) + 0.08 * std::cos (4.0 * pi * i / (L - 1));
            h[(size_t) i] = sinc * w; sum += h[(size_t) i];
        }
        taps.resize ((size_t) L); for (int i = 0; i < L; ++i) taps[(size_t) i] = (float) (h[(size_t) i] / sum);
        hist.assign ((size_t) (2 * L), 0.0f);
    }
    double rate() const { return outRate; }
    /** Takes n samples; 'out' is called with each new sample at the lower rate. */
    template <typename Out> void push (const float* x, int n, Out&& out)
    {
        const int L = (int) taps.size();
        for (int i = 0; i < n; ++i)
        {
            hist[(size_t) pos] = hist[(size_t) (pos + L)] = x[i];          // (twice: the last L samples are always in one piece, hist[pos + 1 .. pos + L])
            if (++pos == L) pos = 0;
            if (++phase < factor) continue;
            phase = 0;
            const float* w = hist.data() + pos;
            float acc = 0.0f;
            for (int k = 0; k < L; ++k) acc += taps[(size_t) k] * w[k];
            out (acc);
        }
    }
private:
    int factor = 1, phase = 0, pos = 0; double outRate = 8000.0;
    std::vector<float> taps, hist;
};

// ======================================================================================================== one reading from 4 s
/** The ensemble's tuning over one window of each channel (all the same length, at sr2): rehar::refPoints step for step on one thread (the four
    loudest channels; Hann windows of ~0.34 s every 0.1 s, padded twice; peaks 10 dB proud within 45 dB of the loudest, 60 - 1100 Hz, kept
    where a peak within 25 c is in the TWO frames either side - refPoints asks for one: see below), then rehar::refStats (at least 12 peaks). False: nothing steady enough, or near
    silence (the loudest channel under -60 dBFS). */
inline bool measure (const std::vector<std::vector<float>>& ch, double sr2, Point& out)
{
    if (ch.empty() || ch[0].empty()) return false;
    const size_t n = ch[0].size();
    std::vector<std::pair<double, size_t>> en;
    for (size_t c = 0; c < ch.size(); ++c)
    {
        double s = 0.0; for (float v : ch[c]) s += (double) v * (double) v;
        en.push_back ({ s / (double) n, c });
    }
    std::stable_sort (en.begin(), en.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    if (en.front().first < 1.0e-6) return false;                                       // (-60 dBFS)
    std::vector<size_t> top; for (size_t i = 0; i < en.size() && i < 4; ++i) if (en[i].first > 0.0) top.push_back (en[i].second);
    const size_t N = (size_t) std::pow (2.0, std::nearbyint (std::log2 (0.34 * sr2))), pad = 2;
    const size_t hop = (size_t) (0.1 * sr2);
    if (N < 64 || hop < 1 || n < N + 3 * hop) return false;
    const rehar::Vec win = rehar::hanning (N);
    const double df = sr2 / (double) (N * pad);
    const size_t nb = N * pad / 2 + 1;
    std::vector<size_t> band; for (size_t q = 0; q < nb; ++q) { const double f = (double) q * df; if (f >= 60.0 && f <= 1100.0) band.push_back (q); }
    if (band.size() < 8) return false;
    struct Frame { std::vector<double> cs, ws; };
    std::vector<Frame> frames;
    rehar::Vec buf (N), mag, P (nb), db (band.size());
    for (size_t i = 0; i + N <= n; i += hop)
    {
        std::fill (P.begin(), P.end(), 0.0);
        for (size_t c : top)
        {
            for (size_t q = 0; q < N; ++q) buf[q] = (double) ch[c][i + q] * win[q];
            rehar::rfftMagnitude (buf.data(), N, N * pad, mag);
            for (size_t q = 0; q < nb && q < mag.size(); ++q) P[q] += mag[q] * mag[q];
        }
        double mx = -1e300;
        for (size_t q = 0; q < band.size(); ++q) { db[q] = 10.0 * std::log10 (P[band[q]] + 1e-18); mx = juce::jmax (mx, db[q]); }
        Frame fr;
        for (int j : rehar::findPeaks (db, 10.0, mx - 45.0))
            if (j > 0 && j < (int) db.size() - 1)
            {
                const double a = db[(size_t) j - 1], b = db[(size_t) j], c2 = db[(size_t) j + 1];
                const double dd = 0.5 * (a - c2) / (a - 2.0 * b + c2 + 1e-12);
                const double f = (double) band[(size_t) j] * df + dd * df;
                if (f <= 0.0) continue;
                fr.cs.push_back (1200.0 * std::log2 (f / 440.0));
                fr.ws.push_back (std::pow (10.0, (b - mx) / 20.0));
            }
        frames.push_back (std::move (fr));
    }
    // kept: a peak with one within 25 c in each of the TWO frames either side (0.2 s each way). Ensemble ReCentre asks for one frame either
    // side, which is enough on music chosen by hand; on a whole take it let room noise, applause and talk through (white noise gave a reading
    // every 2 s, 12 - 36 peaks each - an ensemble gives 80 - 200). Two frames make a chance match some fifty times rarer; a held note, vibrato
    // and all, keeps every peak.
    constexpr int reach = 2;
    rehar::Vec D, W;
    for (size_t k = reach; k + reach < frames.size(); ++k)
    {
        const auto& f = frames[k];
        bool any = ! f.cs.empty();
        for (int d = -reach; d <= reach && any; ++d) any = ! frames[(size_t) ((int) k + d)].cs.empty();
        if (! any) continue;
        for (size_t q = 0; q < f.cs.size(); ++q)
        {
            bool steady = true;
            for (int d = -reach; d <= reach && steady; ++d)
            {
                if (d == 0) continue;
                double m = 1e300;
                for (double v : frames[(size_t) ((int) k + d)].cs) m = std::min (m, std::abs (v - f.cs[q]));
                steady = m < 25.0;
            }
            if (steady)
            {
                double d = std::fmod (f.cs[q] + 50.0, 100.0); if (d < 0.0) d += 100.0;
                D.push_back (d - 50.0); W.push_back (f.ws[q]);
            }
        }
    }
    const auto st = rehar::refStats (D, W, 12);
    if (! st.ok) return false;
    out.cents = (float) st.cents; out.spread = (float) st.spread; out.n = st.n;
    return true;
}

// ======================================================================================================== channels in, a reading every 2 s
/** Channels fed side by side (each at the take's rate); a reading for every 4 s window all of them have reached, every 2 s. */
class Meter
{
public:
    Meter (int channels, double sr) : dec ((size_t) juce::jmax (0, channels))
    {
        for (auto& d : dec) d.prepare (sr);
        sr2 = dec.empty() ? 8000.0 : dec.front().rate();
        W = (int) std::lround (kWindow * sr2); S = (int) std::lround (kStep * sr2);
        R = W + S + 4096;
        rings.assign (dec.size(), std::vector<float> ((size_t) R, 0.0f));
        got.assign (dec.size(), 0);
        win.assign (dec.size(), std::vector<float> ((size_t) W, 0.0f));
    }
    void push (int c, const float* x, int n)
    {
        if (! juce::isPositiveAndBelow (c, (int) dec.size())) return;
        auto& ring = rings[(size_t) c]; auto& g = got[(size_t) c];
        dec[(size_t) c].push (x, n, [&] (float v) { ring[(size_t) (g % R)] = v; ++g; });
    }
    /** The readings of the windows that every channel now covers ('lost': a window a channel had already written over - it ran too far ahead). */
    void step (std::vector<Point>& out, bool& lost)
    {
        if (dec.empty()) return;
        const auto have = *std::min_element (got.begin(), got.end()), most = *std::max_element (got.begin(), got.end());
        while (next * (juce::int64) S + W <= have)
        {
            const auto a = next * (juce::int64) S;
            ++next;
            if (most - a > R) { lost = true; continue; }
            for (size_t c = 0; c < dec.size(); ++c)
                for (int i = 0; i < W; ++i) win[c][(size_t) i] = rings[c][(size_t) ((a + i) % R)];
            Point p;
            if (measure (win, sr2, p)) { p.t = (float) (((double) a + 0.5 * (double) W) / sr2); out.push_back (p); }
        }
    }
private:
    std::vector<Decimator> dec;
    double sr2 = 8000.0; int W = 0, S = 0, R = 0;
    std::vector<std::vector<float>> rings, win;
    std::vector<juce::int64> got;
    juce::int64 next = 0;
};

// ======================================================================================================== a take's line on disk
inline bool saveLine (const juce::File& f, const Line& l, const juce::String& key)
{
    juce::String t;
    t << "fermata tuning 1\n" << "key " << key << "\n" << "whole " << (l.whole ? 1 : 0) << "\n";
    for (auto& p : l.pts) t << juce::String (p.t, 2) << " " << juce::String (p.cents, 2) << " " << juce::String (p.spread, 2) << " " << p.n << "\n";
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (t);
}
inline std::shared_ptr<Line> loadLine (const juce::File& f, juce::String& key)
{
    juce::StringArray lines; lines.addLines (f.loadFileAsString());
    if (lines.size() < 3 || lines[0] != "fermata tuning 1" || ! lines[1].startsWith ("key ")) return nullptr;
    key = lines[1].substring (4);
    auto l = std::make_shared<Line>();
    l->whole = lines[2].trim() == "whole 1";
    for (int i = 3; i < lines.size(); ++i)
    {
        juce::StringArray w; w.addTokens (lines[i], " ", {});
        if (w.size() < 4) continue;
        Point p; p.t = w[0].getFloatValue(); p.cents = w[1].getFloatValue(); p.spread = w[2].getFloatValue(); p.n = w[3].getIntValue();
        l->pts.push_back (p);
    }
    return l;
}

/** A take's files to measure, and what that is called (the key a stored line must match): which tracks, and the files' names. */
struct Source
{
    std::vector<std::pair<juce::File, int>> files;     // file, channels
    juce::String key;
};
inline Source sourceOf (const TakeGroup& g, const Settings& s)
{
    Source src;
    bool any = false;
    for (auto& tf : g.files) any = any || std::find (s.from.begin(), s.from.end(), tf.trackId) != s.from.end();
    juce::StringArray names;
    for (auto& tf : g.files)
    {
        if (any && std::find (s.from.begin(), s.from.end(), tf.trackId) == s.from.end()) continue;
        src.files.push_back ({ tf.file, tf.numChannels });
        names.add (tf.file.getFullPathName());
    }
    names.sort (true);
    src.key = juce::String (any ? "tracks" : "loudest") + ":" + juce::String::toHexString (names.joinIntoString ("|").hashCode64());
    return src;
}

// ======================================================================================================== the takes' lines: kept, measured, stored
/** Every take's line, once known: read from the Analysis folder, or measured from its files on a thread of its own (low priority, one take at
    a time, never while a take records - 'holdOff'). Message thread except run(). */
class Store final : private juce::Thread
{
public:
    Store() : juce::Thread ("Tuning lines") { td::registerAudioFormats (formats); }
    ~Store() override { stop(); }

    std::function<bool()> holdOff;                       // (read on the thread) true while it must not read take files (a take records)
    std::atomic<int> generation { 0 };                   // goes up whenever a line is new: the windows repaint

    /** The project changed (opened, new, closed): its Analysis folder; everything known is forgotten. */
    void setFolder (const juce::File& analysisFolder)
    {
        const std::lock_guard<std::mutex> g (lock);
        folder = analysisFolder; known.clear(); queue.clear(); ++epoch;
        notify();
    }
    /** A take's line, or nothing while it is not known yet (it is then read or measured; generation goes up when it is). */
    std::shared_ptr<const Line> get (const TakeGroup& take, const Source& src)
    {
        if (src.files.empty()) return nullptr;
        const std::lock_guard<std::mutex> g (lock);
        auto it = known.find (take.id);
        if (it != known.end() && it->second.key == src.key) return it->second.line;
        if (it != known.end() && it->second.pending && it->second.key == src.key) return nullptr;
        auto& k = known[take.id]; k.key = src.key; k.pending = true; k.line = nullptr;
        queue.push_back ({ take.id, src, nullptr });
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
        return nullptr;
    }
    /** A take measured while it recorded: its line (stored in the Analysis folder). A line the live reader fell behind on is measured again from
        the files instead. */
    void put (const juce::Uuid& takeId, std::shared_ptr<const Line> line, const juce::String& key)
    {
        if (line == nullptr) return;
        const std::lock_guard<std::mutex> g (lock);
        if (! line->whole) return;                                         // (asked for again from the files when it is drawn)
        auto& k = known[takeId]; k.key = key; k.pending = false; k.line = line;
        queue.push_front ({ takeId, {}, line });                           // (written by the thread)
        ++generation;
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }
    /** "Measure every take again": forgets every line (and deletes the stored ones). */
    void measureAgain()
    {
        juce::File f;
        { const std::lock_guard<std::mutex> g (lock); known.clear(); queue.clear(); ++epoch; f = folder; }
        if (f.isDirectory()) for (auto& x : f.findChildFiles (juce::File::findFiles, false, "*.ftune")) x.deleteFile();
        ++generation;
    }
    void stop() { signalThreadShouldExit(); notify(); stopThread (4000); }
    /** (for a test) The take files are measured here at once, on the calling thread. */
    static std::shared_ptr<Line> measureFiles (const Source& src, juce::AudioFormatManager& fm, const std::function<bool()>& keepGoing)
    {
        std::vector<std::unique_ptr<juce::AudioFormatReader>> rs;
        for (auto& [file, ch] : src.files) { juce::ignoreUnused (ch); if (auto* r = fm.createReaderFor (file)) rs.emplace_back (r); }
        if (rs.empty()) return std::make_shared<Line>();
        const double sr = rs.front()->sampleRate;
        juce::int64 len = rs.front()->lengthInSamples; for (auto& r : rs) len = juce::jmin (len, r->lengthInSamples);
        Meter m ((int) rs.size(), sr);
        auto line = std::make_shared<Line>();
        const int chunk = 16384;
        juce::AudioBuffer<float> b (2, chunk); std::vector<float> mono ((size_t) chunk);
        std::vector<Point> pts; bool lost = false;
        for (juce::int64 at = 0; at < len; at += chunk)
        {
            if (keepGoing && ! keepGoing()) return nullptr;
            const int n = (int) juce::jmin ((juce::int64) chunk, len - at);
            for (size_t c = 0; c < rs.size(); ++c)
            {
                auto& r = *rs[c];
                const int nch = (int) juce::jmin (2u, juce::jmax (1u, r.numChannels));
                b.clear();
                r.read (&b, 0, n, at, true, nch > 1);
                for (int i = 0; i < n; ++i) mono[(size_t) i] = nch > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
                m.push ((int) c, mono.data(), n);
            }
            pts.clear(); m.step (pts, lost);
            for (auto& p : pts) appendPoint (*line, p);
        }
        return line;
    }
private:
    struct Known { juce::String key; std::shared_ptr<const Line> line; bool pending = false; };
    struct Job { juce::Uuid takeId = juce::Uuid::null(); Source src; std::shared_ptr<const Line> save; };     // (null: juce::Uuid's own default is a NEW random id)
    void run() override
    {
        while (! threadShouldExit())
        {
            Job job; juce::File dir; int ep = 0;
            {
                const std::lock_guard<std::mutex> g (lock);
                if (queue.empty()) { /* nothing to do */ }
                else { job = queue.front(); queue.pop_front(); }
                dir = folder; ep = epoch;
            }
            if (job.takeId.isNull()) { wait (-1); continue; }
            if (job.save == nullptr && job.src.files.empty()) continue;           // (nothing to measure)
            const auto file = dir.getChildFile (job.takeId.toString() + ".ftune");
            if (job.save != nullptr)                                       // a live line: written
            {
                juce::String key; { const std::lock_guard<std::mutex> g (lock); auto it = known.find (job.takeId); if (it != known.end()) key = it->second.key; }
                if (dir != juce::File() && key.isNotEmpty()) saveLine (file, *job.save, key);
                continue;
            }
            juce::String key;
            if (file.existsAsFile())
                if (auto l = loadLine (file, key); l != nullptr && key == job.src.key && l->whole)
                {
                    finish (job, l, ep);
                    continue;
                }
            if (holdOff && holdOff())                                      // a take records: later (put back, looked at again in a second)
            {
                { const std::lock_guard<std::mutex> g (lock); if (ep == epoch) queue.push_back (job); }
                wait (1000);
                continue;
            }
            auto l = measureFiles (job.src, formats, [&] { return ! threadShouldExit() && ! (holdOff && holdOff()) && epoch == ep; });
            if (l == nullptr)                                              // stopped part way: again later
            {
                const std::lock_guard<std::mutex> g (lock);
                if (ep == epoch) queue.push_back (job);
                continue;
            }
            if (dir != juce::File()) saveLine (file, *l, job.src.key);
            finish (job, l, ep);
        }
    }
    void finish (const Job& job, std::shared_ptr<const Line> l, int ep)
    {
        const std::lock_guard<std::mutex> g (lock);
        if (ep != epoch) return;
        auto& k = known[job.takeId];
        if (k.key != job.src.key) return;                                  // (asked for from other tracks meanwhile)
        k.line = std::move (l); k.pending = false;
        ++generation;
    }
    juce::AudioFormatManager formats;
    std::mutex lock;
    juce::File folder;
    std::map<juce::Uuid, Known> known;
    std::deque<Job> queue;
    std::atomic<int> epoch { 0 };
};

// ======================================================================================================== while a take records
/** The take recording now, measured from the engine's feeds as it comes in (its own thread, low priority). When every feed has ended and been
    read, onTakeDone gets the line on the message thread (Store::put). Message thread except run(). */
class Live final : private juce::Thread
{
public:
    using Feed = AudioEngine::LiveFeed;
    Live() : juce::Thread ("Live tuning line") {}
    ~Live() override { stop(); }

    std::function<void (const juce::Uuid& takeId, std::shared_ptr<const Line>, const juce::String& key)> onTakeDone;
    std::atomic<int> generation { 0 };

    void watch (const juce::Uuid& takeId, const std::vector<std::shared_ptr<Feed>>& feeds, const juce::String& key)
    {
        if (! alive->load() || takeId.isNull() || feeds.empty()) return;
        {
            const std::lock_guard<std::mutex> g (lock);
            for (auto& j : jobs) if (j->takeId == takeId) return;
            auto j = std::make_shared<Job> ((int) feeds.size(), feeds.front()->sampleRate);
            j->takeId = takeId; j->feeds = feeds; j->key = key; j->pos.assign (feeds.size(), 0);
            j->shown = std::make_shared<const Line>();
            jobs.push_back (std::move (j));
        }
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }
    std::shared_ptr<const Line> lineFor (const juce::Uuid& takeId) const
    {
        const std::lock_guard<std::mutex> g (lock);
        for (auto& j : jobs) if (j->takeId == takeId) return j->shown;
        return nullptr;
    }
    void stop()
    {
        alive->store (false);
        signalThreadShouldExit(); notify(); stopThread (4000);
        const std::lock_guard<std::mutex> g (lock);
        jobs.clear();
    }
private:
    struct Job
    {
        Job (int channels, double sr) : meter (channels, sr) {}
        juce::Uuid takeId = juce::Uuid::null(); juce::String key;
        std::vector<std::shared_ptr<Feed>> feeds;
        std::vector<juce::int64> pos;
        Meter meter;
        Line line;
        std::shared_ptr<const Line> shown;
        bool lost = false;
    };
    void run() override
    {
        std::vector<float> buf (4096), zeros (4096, 0.0f);
        std::vector<Point> pts;
        while (! threadShouldExit())
        {
            std::vector<std::shared_ptr<Job>> now;
            { const std::lock_guard<std::mutex> g (lock); now = jobs; }
            if (now.empty()) { wait (-1); continue; }
            for (auto& j : now)
            {
                bool allDone = true;
                for (size_t c = 0; c < j->feeds.size(); ++c)
                {
                    auto& f = *j->feeds[c];
                    const bool ended = f.ended.load();                     // (read before 'written': what came before the end is all there)
                    const auto w = f.written();
                    auto& p = j->pos[c];
                    while (p < w && ! threadShouldExit())
                    {
                        const int n = (int) juce::jmin ((juce::int64) buf.size(), w - p);
                        if (! f.read (p, buf.data(), n))                    // written over: the reader fell behind - silence instead, and the files are measured later
                        {
                            j->lost = true;
                            const auto to = juce::jmax (p + 1, f.written() - (juce::int64) f.capacity() / 2);
                            while (p < to) { const int z = (int) juce::jmin ((juce::int64) zeros.size(), to - p); j->meter.push ((int) c, zeros.data(), z); p += z; }
                            continue;
                        }
                        j->meter.push ((int) c, buf.data(), n);
                        p += n;
                    }
                    if (! ended || p < f.written()) allDone = false;
                }
                pts.clear(); bool lost = false;
                j->meter.step (pts, lost);
                if (lost) j->lost = true;
                if (! pts.empty())
                {
                    for (auto& p : pts) appendPoint (j->line, p);
                    auto copy = std::make_shared<Line> (j->line);
                    copy->whole = ! j->lost;
                    { const std::lock_guard<std::mutex> g (lock); j->shown = copy; }
                    ++generation;
                }
                if (allDone)
                {
                    auto last = std::make_shared<Line> (j->line);
                    last->whole = ! j->lost;
                    { const std::lock_guard<std::mutex> g (lock); jobs.erase (std::remove (jobs.begin(), jobs.end(), j), jobs.end()); }
                    auto a = alive; auto id = j->takeId; auto key = j->key;
                    juce::MessageManager::callAsync ([this, a, id, last, key] { if (a->load() && onTakeDone) onTakeDone (id, last, key); });
                }
            }
            wait (150);
        }
    }
    mutable std::mutex lock;
    std::vector<std::shared_ptr<Job>> jobs;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

// ======================================================================================================== the button
/** The Tuning button (take and edit windows): a line that rises and falls across a centre mark - lit when the tuning line is shown. A click
    shows / hides it; a right-click is the menu (onMenu). */
class Button final : public juce::Button
{
public:
    Button() : juce::Button ("Tuning") { setClickingTogglesState (false); }
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { menuPress = true; if (onMenu) onMenu(); return; } juce::Button::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (menuPress) { menuPress = false; return; } juce::Button::mouseUp (e); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        g.setColour (on ? theme::button.brighter (0.35f) : down ? theme::button.brighter (0.2f) : over ? theme::button.brighter (0.1f) : theme::button); g.fillRoundedRectangle (b, 4.0f);
        g.setColour (on ? theme::accent : theme::border); g.drawRoundedRectangle (b, 4.0f, on ? 1.6f : 1.0f);
        const float cx = b.getCentreX(), cy = b.getCentreY(), w = juce::jmin (22.0f, b.getWidth() - 8.0f), h = juce::jmin (7.0f, b.getHeight() * 0.3f);
        g.setColour (theme::text.withAlpha (on ? 0.55f : 0.35f));
        g.drawHorizontalLine ((int) cy, cx - w * 0.5f, cx + w * 0.5f);
        juce::Path p;
        const float xs[] = { -0.5f, -0.3f, -0.1f, 0.1f, 0.3f, 0.5f }, ys[] = { 0.1f, -0.6f, -0.9f, -0.2f, 0.5f, 1.0f };
        for (int i = 0; i < 6; ++i) { const float x = cx + xs[i] * w, y = cy + ys[i] * h; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        for (int i = 0; i < 5; ++i)
        {
            juce::Path s; s.startNewSubPath (cx + xs[i] * w, cy + ys[i] * h); s.lineTo (cx + xs[i + 1] * w, cy + ys[i + 1] * h);
            g.setColour (colourFor (-ys[i] * 15.0f).withAlpha (on ? 1.0f : 0.45f));
            g.strokePath (s, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
private:
    bool menuPress = false;
};

// ======================================================================================================== drawing
/** The lane's background: a band with a line at 0 and faint ones at +-10 and +-20 c. */
inline void paintLaneBack (juce::Graphics& g, juce::Rectangle<int> lane)
{
    g.setColour (juce::Colours::black.withAlpha (0.20f)); g.fillRect (lane);
    const float mid = (float) lane.getCentreY(), per = ((float) lane.getHeight() * 0.5f - 3.0f) / (float) kScale;
    g.setColour (juce::Colours::white.withAlpha (0.16f)); g.drawHorizontalLine ((int) mid, (float) lane.getX(), (float) lane.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float c : { -20.0f, -10.0f, 10.0f, 20.0f }) g.drawHorizontalLine ((int) (mid - c * per), (float) lane.getX(), (float) lane.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (lane.getX(), lane.getBottom() - 1, lane.getWidth(), 1);
}
inline float yOf (juce::Rectangle<int> lane, float cents)
{
    const float mid = (float) lane.getCentreY(), per = ((float) lane.getHeight() * 0.5f - 3.0f) / (float) kScale;
    return mid - juce::jlimit (-(float) kScale, (float) kScale, cents) * per;
}
/** One line in the lane: 'xOf' places a reading's time (seconds into the take) on the screen; 'value' is what is shown for a reading's cents;
    readings with 'from' <= t < 'to' only. A gap of more than 4.5 s between readings breaks the line. */
inline void paintLine (juce::Graphics& g, juce::Rectangle<int> lane, const Line& l, double from, double to,
                       const std::function<float (double)>& xOf, const std::function<float (float)>& value)
{
    const Point* prev = nullptr;
    for (auto& p : l.pts)
    {
        if ((double) p.t < from || (double) p.t >= to) { prev = nullptr; continue; }
        const float x = xOf ((double) p.t), v = value (p.cents), y = yOf (lane, v);
        if (prev != nullptr && p.t - prev->t <= 4.5f)
        {
            const float x0 = xOf ((double) prev->t), v0 = value (prev->cents);
            g.setColour (colourFor (0.5f * (v + v0)));
            g.drawLine (x0, yOf (lane, v0), x, y, 1.8f);
        }
        g.setColour (colourFor (v)); g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
        prev = &p;
    }
}
/** The name box at the lane's left (Tuning, the mode, the scale). */
inline void paintNameBox (juce::Graphics& g, juce::Rectangle<int> box, const Settings& s, bool editWindow)
{
    g.setColour (theme::panel.withAlpha (0.96f)); g.fillRect (box);
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (box.getX(), box.getBottom() - 1, box.getWidth(), 1);
    g.setColour (theme::text); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("TUNING", box.getX() + 8, box.getY() + 3, box.getWidth() - 12, 15, juce::Justification::centredLeft, true);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (theme::text.withAlpha (0.85f));
    const juce::String m = s.mode == Mode::relative ? (editWindow ? "Relative to the edit's start" : "Relative to each take's start") : modeText (s);
    g.drawText (m, box.getX() + 8, box.getY() + 17, box.getWidth() - 12, 13, juce::Justification::centredLeft, true);
    g.setColour (theme::text.withAlpha (0.55f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String ((int) kScale) + " c   (right-click: settings)", box.getX() + 8, box.getY() + 30, box.getWidth() - 12, 13, juce::Justification::centredLeft, true);
}
/** A small label on a dark strip (a take's start and end values). */
inline void paintTag (juce::Graphics& g, int x, int y, const juce::String& text, juce::Colour ink)
{
    const juce::Font f (juce::FontOptions (10.0f, juce::Font::bold));
    const int w = juce::GlyphArrangement::getStringWidthInt (f, text) + 8;
    g.setColour (juce::Colour (0xc0000000)); g.fillRoundedRectangle ((float) x, (float) y, (float) w, 13.0f, 3.0f);
    g.setColour (ink); g.setFont (f); g.drawText (text, x + 4, y, w - 6, 13, juce::Justification::centredLeft, false);
}
} // namespace td::tuning
