// fermata 3.74.0: the tests of src/core/WaveStats.h (the Spectrogram Editing window's figures). Plain C++ - no JUCE:
//     g++ -std=c++17 -O2 tests/wavestats_test.cpp -o wavestats_test && ./wavestats_test
// tools/sandbox-check.sh runs it with the shared core's tests.
#include "../src/core/WaveStats.h"
#include <cstdio>
#include <random>

using namespace td::wavestats;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAILED line %d: %s\n", __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { ++checks; const double va_ = (a), vb_ = (b); if (! (std::abs (va_ - vb_) <= (tol))) { ++failures; std::printf ("FAILED line %d: %s = %.6f, wanted %.6f (+/- %g)\n", __LINE__, #a, va_, vb_, (double) (tol)); } } while (0)

using Signal = std::vector<std::vector<float>>;           // [channel][frame]

static Signal tone (double rate, double seconds, double dbfs, double hz, int chans = 2, double phase = 0.0)
{
    const auto n = (size_t) std::llround (rate * seconds); const double a = std::pow (10.0, dbfs / 20.0);
    Signal s ((size_t) chans, std::vector<float> (n));
    for (size_t i = 0; i < n; ++i) { const float v = (float) (a * std::sin (2.0 * kPi * hz * (double) i / rate + phase)); for (auto& c : s) c[i] = v; }
    return s;
}
static void append (Signal& a, const Signal& b) { for (size_t c = 0; c < a.size(); ++c) a[c].insert (a[c].end(), b[c].begin(), b[c].end()); }

/** The file's Profile, fed in blocks of the sizes 'next' gives. */
static Profile analyse (const Signal& s, double rate, const std::function<int()>& next = [] { return 4096; })
{
    Analyzer a (rate, (int) s.size());
    const auto n = s[0].size();
    for (size_t pos = 0; pos < n;)
    {
        const int k = (int) std::min<size_t> ((size_t) std::max (1, next()), n - pos);
        std::vector<const float*> ptr; for (auto& c : s) ptr.push_back (c.data() + pos);
        a.process (ptr.data(), k);
        pos += (size_t) k;
    }
    return a.finish();
}
static Reader readerOf (const Signal& s)
{
    return [&s] (std::int64_t start, int n, float* const* dest)
    {
        for (size_t c = 0; c < s.size(); ++c)
            for (int i = 0; i < n; ++i) { const auto f = start + i; dest[c][i] = f >= 0 && f < (std::int64_t) s[c].size() ? s[c][(size_t) f] : 0.0f; }
        return true;
    };
}

/** The figures of the middle half of a signal (a tone that starts or stops with a jump has real peaks between the samples there: not what
    these tests are about). */
static Stats middle (const Signal& s, double rate)
{
    const auto p = analyse (s, rate);
    const auto read = readerOf (s);
    return measure (p, p.frames / 4, p.frames * 3 / 4, {}, &read);
}

/** The same figures worked out the slow, obvious way from the samples of [from, to). */
struct Brute { double sum = 0, sumSq = 0; float samplePeak = 0, truePeak = 0; std::int64_t clipped = 0, truePeakAt = -1; };
static Brute brute (const std::vector<float>& x, std::int64_t from, std::int64_t to, int oversample)
{
    Brute b; const TruePeak tp (oversample); const int pad = TruePeak::kHalf;
    std::vector<float> padded ((size_t) (to - from) + 2 * (size_t) pad, 0.0f);
    for (std::int64_t i = from - pad; i < to + pad; ++i) if (i >= 0 && i < (std::int64_t) x.size()) padded[(size_t) (i - from + pad)] = x[(size_t) i];
    for (std::int64_t i = from; i < to; ++i)
    {
        const float v = x[(size_t) i];
        b.sum += v; b.sumSq += (double) v * v; b.samplePeak = std::max (b.samplePeak, std::abs (v));
        const float pk = tp.cell (padded.data() + (i - from + pad));
        if (pk > b.truePeak || b.truePeakAt < 0) { b.truePeak = pk; b.truePeakAt = i; }
        if (pk > kClipOver) ++b.clipped;
    }
    return b;
}

int main()
{
    // ---- the true peak: tones whose samples miss their crests
    for (double rate : { 44100.0, 48000.0 })
    {
        // a full-scale tone at a quarter of the sample rate, 45 degrees on: every sample is 3 dB down, the true peak is 0 dB
        const auto s = middle (tone (rate, 1.0, 0.0, rate / 4.0, 1, kPi / 4.0), rate);
        NEAR (s.ch[0].samplePeakDb, -3.0103, 0.01);
        NEAR (s.ch[0].truePeakDb, 0.0, 0.02);
        // a tone well below the top: the true peak is the tone's level, whatever the phase
        for (double ph : { 0.0, 0.3, 1.1, 2.0 })
            NEAR (middle (tone (rate, 0.5, -6.0, 997.0, 1, ph), rate).ch[0].truePeakDb, -6.0, 0.01);
        // up to 0.45 of the sample rate: never over, and never under by more than the spacing of the points allows (4 x: cos (pi f / 4 fs))
        for (double f : { 0.05, 0.11, 0.2, 0.3, 0.37, 0.42, 0.45 })
            for (double ph : { 0.0, 0.4, 0.8, 1.3 })
            {
                const double tpDb = middle (tone (rate, 0.25, 0.0, f * rate, 1, ph), rate).ch[0].truePeakDb;
                const double floorDb = 20.0 * std::log10 (std::cos (kPi * f / 4.0));
                CHECK (tpDb <= 0.03 && tpDb >= floorDb - 0.03);
            }
    }
    {   // at 96 kHz the points are 2 x, at 192 kHz the samples themselves
        CHECK (oversampleFor (44100.0) == 4 && oversampleFor (48000.0) == 4 && oversampleFor (88200.0) == 2 && oversampleFor (96000.0) == 2 && oversampleFor (176400.0) == 1 && oversampleFor (192000.0) == 1);
        NEAR (middle (tone (96000.0, 0.5, 0.0, 24000.0, 1, kPi / 4.0), 96000.0).ch[0].truePeakDb, 0.0, 0.02);
        NEAR (middle (tone (192000.0, 0.25, -2.0, 997.0, 1), 192000.0).ch[0].truePeakDb, -2.0, 0.01);
        {   // a tone that starts with a jump (at its crest) does peak higher between the first samples: a true peak, and it is found
            const auto jump = measure (analyse (tone (48000.0, 0.25, -6.0, 997.0, 1, kPi / 2.0), 48000.0));
            CHECK (jump.ch[0].truePeakDb > -5.9 && jump.ch[0].truePeakAt < 4);
        }
    }

    // ---- every block size gives the same Profile, and any part's figures are the samples' own
    {
        std::mt19937 rng (12345);
        std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
        const double rate = 48000.0;
        const size_t n = 48000 * 3 + 137;                                   // (a short last tick)
        Signal s (2, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
        {
            const float env = 0.2f + 0.8f * (float) std::abs (std::sin (0.00013 * (double) i));
            s[0][i] = env * (0.6f * (float) std::sin (0.21 * (double) i) + 0.5f * uni (rng)) + 0.01f;
            s[1][i] = env * (0.6f * (float) std::sin (0.21 * (double) i + 0.7) + 0.5f * uni (rng)) - 0.02f;
        }
        const auto whole = analyse (s, rate, [] { return 1 << 20; });
        std::uniform_int_distribution<int> sizes (1, 3000);
        const auto bits = analyse (s, rate, [&] { return sizes (rng); });
        CHECK (whole.frames == (std::int64_t) n && bits.frames == whole.frames && whole.ticks() == bits.ticks() && whole.ticks() == 301 && whole.wholeTicks() == 300);
        bool same = true;
        for (int c = 0; c < 2; ++c)
            for (size_t t = 0; t < (size_t) whole.ticks(); ++t)
            {
                const auto& a = whole.ch[(size_t) c][t]; const auto& b = bits.ch[(size_t) c][t];
                same = same && a.lo == b.lo && a.hi == b.hi && a.samplePeak == b.samplePeak && a.truePeak == b.truePeak && a.truePeakAt == b.truePeakAt
                            && a.clipped == b.clipped && a.sum == b.sum && a.sumSq == b.sumSq && a.kSumSq == b.kSumSq;
            }
        CHECK (same);
        const auto read = readerOf (s);
        Options plain; plain.aes17 = false;
        std::uniform_int_distribution<std::int64_t> where (0, (std::int64_t) n);
        std::vector<std::pair<std::int64_t, std::int64_t>> parts { { 0, (std::int64_t) n }, { 0, 480 }, { 5, 9 }, { 475, 485 }, { 479, 481 }, { 480, 960 }, { 100, 1000 },
                                                                    { (std::int64_t) n - 100, (std::int64_t) n }, { (std::int64_t) n - 137, (std::int64_t) n }, { (std::int64_t) n - 600, (std::int64_t) n - 3 }, { 7, 8 } };
        for (int k = 0; k < 60; ++k) { auto a = where (rng), b = where (rng); if (a > b) std::swap (a, b); if (b > a) parts.push_back ({ a, b }); }
        for (auto& pr : parts)
        {
            const auto st = measure (bits, pr.first, pr.second, plain, &read);
            CHECK (st.exactEnds && st.from == pr.first && st.to == pr.second);
            for (int c = 0; c < 2; ++c)
            {
                const auto b = brute (s[(size_t) c], pr.first, pr.second, 4);
                const double frames = (double) (pr.second - pr.first);
                NEAR (st.ch[(size_t) c].samplePeakDb, ampToDb (b.samplePeak), 1.0e-6);
                NEAR (st.ch[(size_t) c].truePeakDb, ampToDb (b.truePeak), 1.0e-6);
                CHECK (st.ch[(size_t) c].clipped == b.clipped);
                CHECK (st.ch[(size_t) c].truePeakAt == b.truePeakAt);
                NEAR (st.ch[(size_t) c].dcPercent, 100.0 * b.sum / frames, 1.0e-7);
                NEAR (st.ch[(size_t) c].totalRmsDb, powerToDb (b.sumSq / frames), 1.0e-6);
            }
        }
        // without a reader the ends go out to whole ticks, and say so
        const auto rough = measure (bits, 100, 1000, plain, nullptr);
        CHECK (! rough.exactEnds && rough.from == 0 && rough.to == 1440);
        CHECK (measure (bits, 480, 960, plain, nullptr).exactEnds);         // (on the ticks already: nothing to read)
        // DC: 1 % and -2 % were added
        NEAR (measure (bits).ch[0].dcPercent, 1.0, 0.25); NEAR (measure (bits).ch[1].dcPercent, -2.0, 0.25);
    }

    // ---- RMS: a tone's level (AES17: a full-scale sine reads 0 dB), and the windows
    {
        const double rate = 48000.0;
        auto s = tone (rate, 2.0, -20.0, 1000.0); append (s, tone (rate, 2.0, -40.0, 1000.0));
        const auto p = analyse (s, rate);
        Options aes; Options plain; plain.aes17 = false;
        const auto a = measure (p, 0, 96000, aes), b = measure (p, 0, 96000, plain);
        NEAR (a.ch[0].totalRmsDb, -20.0, 0.01); NEAR (b.ch[0].totalRmsDb, -23.0103, 0.01);
        const auto all = measure (p, aes);
        NEAR (all.ch[0].maxRmsDb, -20.0, 0.02); NEAR (all.ch[0].minRmsDb, -40.0, 0.02);
        NEAR (all.ch[1].maxRmsDb, -20.0, 0.02);
        Options longer; longer.rmsWindowMs = 400;
        NEAR (measure (p, longer).ch[0].maxRmsDb, -20.0, 0.02);
        // a part shorter than one window is its own window
        const auto tiny = measure (p, 4800, 4800 + 960, aes);
        NEAR (tiny.ch[0].maxRmsDb, tiny.ch[0].totalRmsDb, 1.0e-9); NEAR (tiny.ch[0].minRmsDb, tiny.ch[0].totalRmsDb, 1.0e-9);
    }

    // ---- possibly clipped samples
    {
        const double rate = 48000.0;
        auto clean = tone (rate, 1.0, -1.0, 997.0, 1);
        CHECK (measure (analyse (clean, rate)).clipped == 0);
        auto hot = tone (rate, 1.0, 3.0, 997.0, 1);                         // 3 dB over, cut off flat at full scale
        std::int64_t flat = 0;
        for (auto& v : hot[0]) { if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f; if (std::abs (v) == 1.0f) ++flat; }
        const auto st = measure (analyse (hot, rate));
        NEAR (st.ch[0].samplePeakDb, 0.0, 1.0e-9);
        CHECK (st.truePeakDb > 0.05);                                       // (the corners of the flat tops overshoot between the samples)
        CHECK (st.clipped > 0 && st.clipped < flat);                        // the overshoots are counted; a flat top AT full scale is not over it
        Signal dc (1, std::vector<float> (48000, 1.0f));                    // a steady full-scale level: its middle is not over 0 dBTP
        const auto d = measure (analyse (dc, rate), 4800, 43200);
        CHECK (d.clipped == 0); NEAR (d.ch[0].dcPercent, 100.0, 1.0e-6);
        auto over = tone (rate, 0.5, 2.0, 997.0, 1);                        // a file that really goes over full scale (32-bit float): most of every crest is counted
        const auto o = measure (analyse (over, rate));
        NEAR (o.truePeakDb, 2.0, 0.01); CHECK (o.clipped > 5000);
    }

    // ---- loudness (EBU Tech 3341): a 997 Hz stereo tone at -23 dBFS reads -23.0 LUFS, at any sample rate
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        const auto st = measure (analyse (tone (rate, 20.0, -23.0, 997.0), rate));
        NEAR (st.integrated, -23.0, 0.1); NEAR (st.maxMomentary, -23.0, 0.1); NEAR (st.maxShortTerm, -23.0, 0.1);
        NEAR (st.truePeakDb, -23.0, 0.02);
        NEAR (measure (analyse (tone (rate, 20.0, -33.0, 997.0), rate)).integrated, -33.0, 0.1);
    }
    {
        const double rate = 48000.0;
        NEAR (measure (analyse (tone (rate, 10.0, -23.0, 997.0, 1), rate)).integrated, -26.0, 0.1);       // one channel: 3 dB less
        {   // case 3: -36, -23, -36 LUFS (10 s, 60 s, 10 s): the quiet parts are gated out
            auto s = tone (rate, 10.0, -36.0, 997.0); append (s, tone (rate, 60.0, -23.0, 997.0)); append (s, tone (rate, 10.0, -36.0, 997.0));
            NEAR (measure (analyse (s, rate)).integrated, -23.0, 0.1);
        }
        {   // case 4: -72, -36, -23, -36, -72
            auto s = tone (rate, 10.0, -72.0, 997.0); append (s, tone (rate, 10.0, -36.0, 997.0)); append (s, tone (rate, 60.0, -23.0, 997.0));
            append (s, tone (rate, 10.0, -36.0, 997.0)); append (s, tone (rate, 10.0, -72.0, 997.0));
            NEAR (measure (analyse (s, rate)).integrated, -23.0, 0.1);
        }
        {   // case 5: -26, -20, -26 (20 s, 20.1 s, 20 s)
            auto s = tone (rate, 20.0, -26.0, 997.0); append (s, tone (rate, 20.1, -20.0, 997.0)); append (s, tone (rate, 20.0, -26.0, 997.0));
            const auto st = measure (analyse (s, rate));
            NEAR (st.integrated, -23.0, 0.1); NEAR (st.maxMomentary, -20.0, 0.1); NEAR (st.maxShortTerm, -20.0, 0.1);
        }
        {   // silence: nothing to read; less than 400 ms: no loudness yet; less than 3 s: no short-term
            Signal z (2, std::vector<float> (48000 * 5, 0.0f));
            const auto st = measure (analyse (z, rate));
            CHECK (st.integrated == kNone && st.truePeakDb == kNone && ! st.lraValid);
            const auto sh = measure (analyse (tone (rate, 0.3, -23.0, 997.0), rate));
            CHECK (sh.integrated == kNone && sh.maxMomentary == kNone);
            const auto two = measure (analyse (tone (rate, 2.0, -23.0, 997.0), rate));
            NEAR (two.maxMomentary, -23.0, 0.1); CHECK (two.maxShortTerm == kNone && ! two.lraValid);
        }
        {   // a part's loudness is the part's: the loud second half of a file read on its own
            auto s = tone (rate, 20.0, -30.0, 997.0); append (s, tone (rate, 20.0, -18.0, 997.0));
            const auto p = analyse (s, rate);
            NEAR (measure (p, (std::int64_t) (rate * 21.0), (std::int64_t) (rate * 39.0)).integrated, -18.0, 0.1);
            NEAR (measure (p, (std::int64_t) (rate * 1.0), (std::int64_t) (rate * 19.0)).integrated, -30.0, 0.1);
            NEAR (p.momentaryAt (3900), -18.0, 0.1); NEAR (p.shortTermAt (1900), -30.0, 0.1); CHECK (p.momentaryAt (39) == kNone && p.shortTermAt (299) == kNone);
        }
    }

    // ---- the loudness range (EBU Tech 3342)
    {
        const double rate = 48000.0;
        auto two = [&] (double a, double b) { auto s = tone (rate, 20.0, a, 997.0); append (s, tone (rate, 20.0, b, 997.0)); return measure (analyse (s, rate)); };
        NEAR (two (-20.0, -30.0).lra, 10.0, 1.0);
        NEAR (two (-20.0, -15.0).lra, 5.0, 1.0);
        NEAR (two (-40.0, -20.0).lra, 20.0, 1.0);
        auto s = tone (rate, 20.0, -50.0, 997.0);
        for (double l : { -35.0, -20.0, -35.0, -50.0 }) append (s, tone (rate, 20.0, l, 997.0));
        NEAR (measure (analyse (s, rate)).lra, 15.0, 1.0);
        NEAR (measure (analyse (tone (rate, 30.0, -23.0, 997.0), rate)).lra, 0.0, 0.1);
    }

    // ---- the correlation of left and right
    {
        const double rate = 48000.0;
        auto s = tone (rate, 1.0, -10.0, 440.0);
        NEAR (analyse (s, rate).correlation (0, 100), 1.0, 1.0e-6);
        for (auto& v : s[1]) v = -v;
        NEAR (analyse (s, rate).correlation (0, 100), -1.0, 1.0e-6);
        auto q = tone (rate, 1.0, -10.0, 440.0); const auto r = tone (rate, 1.0, -10.0, 440.0, 2, kPi / 2.0); q[1] = r[1];
        NEAR (analyse (q, rate).correlation (0, 100), 0.0, 0.01);
    }

    // ---- 3.75.0: the highest true peaks, as separate events
    {
        const double rate = 48000.0;
        auto s = tone (rate, 20.0, -30.0, 220.0);
        // five short bursts of a louder tone, each a different height (and two more close to the highest, which belong to it)
        struct Burst { double at, db; int ch; };
        const Burst bursts[] = { { 2.0, -6.0, 0 }, { 5.0, -3.0, 1 }, { 5.2, -4.0, 0 }, { 4.9, -5.0, 1 }, { 9.0, -9.0, 0 }, { 13.0, -12.0, 1 }, { 17.5, -15.0, 0 } };
        for (auto& b : bursts)
        {
            const auto n0 = (size_t) std::llround (b.at * rate); const double a = std::pow (10.0, b.db / 20.0);
            for (size_t i = 0; i < 960; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / 959.0);
                s[(size_t) b.ch][n0 + i] = (float) (a * w * std::sin (2.0 * kPi * 1000.0 * (double) i / rate + kPi / 2.0)) + s[(size_t) b.ch][n0 + i] * (float) (1.0 - w);
            }
        }
        const auto p = analyse (s, rate);
        const auto whole = measure (p);
        const auto top = topPeaks (p, 5);
        CHECK (top.size() == 5);
        if (top.size() == 5)
        {
            const double wantAt[] = { 5.0, 2.0, 9.0, 13.0, 17.5 }, wantDb[] = { -3.0, -6.0, -9.0, -12.0, -15.0 }; const int wantCh[] = { 1, 0, 0, 1, 0 };
            for (int i = 0; i < 5; ++i)
            {
                NEAR ((double) top[(size_t) i].frame / rate, wantAt[i] + 0.01, 0.006);             // (the middle of the burst)
                NEAR (top[(size_t) i].db, wantDb[i], 0.05);
                CHECK (top[(size_t) i].channel == wantCh[i]);
                if (i > 0) CHECK (top[(size_t) i].db <= top[(size_t) i - 1].db);
            }
            NEAR (top[0].db, whole.truePeakDb, 1.0e-9);                                              // the first IS the file's true peak
        }
        CHECK (topPeaks (p, 3).size() == 3);
        CHECK (topPeaks (p, 0).empty());
        // nearer than the distance: one event; with no distance asked for, the bursts beside the highest count on their own
        const auto close = topPeaks (p, 4, 0.05);
        CHECK (close.size() == 4);
        if (close.size() == 4) { NEAR (close[1].db, -4.0, 0.05); NEAR ((double) close[1].frame / rate, 5.21, 0.006); NEAR (close[2].db, -5.0, 0.05); NEAR (close[3].db, -6.0, 0.05); }
        // a range: only the peaks inside it
        const auto part = topPeaks (p, 5, 0.5, (std::int64_t) (8.0 * rate), (std::int64_t) (14.0 * rate));
        CHECK (part.size() >= 2);
        if (part.size() >= 2) { NEAR (part[0].db, -9.0, 0.05); NEAR (part[1].db, -12.0, 0.05); for (auto& e : part) CHECK (e.frame >= (std::int64_t) (8.0 * rate) && e.frame < (std::int64_t) (14.0 * rate)); }
        // more asked for than there are: every event once, none nearer to another than the distance
        const auto many = topPeaks (p, 1000, 0.5);
        for (size_t i = 0; i < many.size(); ++i) for (size_t j = 0; j < i; ++j) CHECK (std::llabs (many[i].frame - many[j].frame) >= (std::int64_t) (0.5 * rate));
        CHECK (many.size() <= 41 && many.size() >= 20);
        // silence has none
        Signal z (2, std::vector<float> ((size_t) rate, 0.0f));
        CHECK (topPeaks (analyse (z, rate), 5).empty());
    }

    std::printf (failures == 0 ? "WAVE STATISTICS TESTS PASSED (%d checks)\n" : "WAVE STATISTICS TESTS FAILED: %d of %d checks\n", failures == 0 ? checks : failures, checks);
    return failures == 0 ? 0 : 1;
}
