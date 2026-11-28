#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

/** fermata 3.74.0 (asked for on 27 November 2026; written with Claude Fable): THE MEASUREMENTS OF A FINISHED FILE - the figures a mastering
    engineer reads off iZotope RX's Waveform Statistics, for the whole file and for any part of it:

      true peak (dBTP)             the highest level between the samples too: 4 x oversampled below 80 kHz, 2 x below 160 kHz (ITU-R BS.1770 Annex 2)
      sample peak (dBFS)           the highest sample
      max / min / total RMS        the RMS level in windows of a chosen length (50 ms unless asked), their highest and lowest, and of the whole part
      possibly clipped samples     how many samples have a true peak over 0 dBTP
      DC offset                    the mean, in per cent of full scale
      max momentary loudness       the loudest 400 ms (K-weighted, BS.1770)
      max short-term loudness      the loudest 3 s
      integrated loudness          BS.1770 gated loudness (blocks of 400 ms every 100 ms; gates at -70 LUFS and at 10 LU under the rest)
      loudness range (LRA)         EBU Tech 3342 (short-term values every 100 ms; gates at -70 LUFS and 20 LU under; 10th to 95th percentile)

    HOW: the file is read ONCE (Analyzer) into a Profile: for every 10 ms ("tick") and every channel its lowest and highest sample, its sample
    peak, its true peak and where it is, how many of its samples are over 0 dBTP, the sum of its samples (DC), of their squares (RMS) and of their
    K-weighted squares (loudness), and for the first two channels the sum of their products (the correlation). Every figure of any part of the
    file is then put together from the ticks (measure) in no time, so a selection's figures follow the mouse; the two ends of a part that do not
    fall on a tick are read from the file again and measured sample by sample (the Reader), so the peaks, the clipped samples, the DC and the total
    RMS of a selection are exact. The loudness figures and the RMS windows of a part start at its first whole tick (within 10 ms of its start).

    The live meters of the window read the same Profile at the playhead: what they show while it plays is what the figures were made from.

    No JUCE here: plain C++, so it is tested on its own (tests/wavestats_test.cpp). */
namespace td::wavestats
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kNone = -200.0;                           // "no reading" for a level in dB / LUFS (silence, or a part too short to measure)

inline double ampToDb (double a)   { return a > 1.0e-10 ? 20.0 * std::log10 (a) : kNone; }
inline double powerToDb (double p) { return p > 1.0e-20 ? 10.0 * std::log10 (p) : kNone; }
inline double powerToLufs (double p) { return p > 1.0e-20 ? -0.691 + 10.0 * std::log10 (p) : kNone; }
inline double lufsToPower (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }

/** How many times a file of this sample rate is oversampled for its true peak (BS.1770 Annex 2: 4 x at 44.1 / 48 kHz, 2 x at 88.2 / 96 kHz; at
    176.4 / 192 kHz the samples themselves). */
inline int oversampleFor (double rate) { return rate < 80000.0 ? 4 : rate < 160000.0 ? 2 : 1; }

/** A sample counts as "possibly clipped" when its true peak is over 0 dBTP by more than this (a flat top AT full scale is not over it: the
    interpolator reads 1.0 there, give or take the last digit of a float). */
constexpr float kClipOver = 1.0f + 2.0e-6f;

//==================================================================================================================================
/** The true-peak interpolator: the points between two samples, from the 12 samples either side (a Kaiser-windowed sinc, each phase at unity
    gain). Flat within 0.01 dB to 0.45 of the sample rate: what is left is only how finely the points are spaced (4 x: a tone at a quarter of
    the sample rate can read 0.17 dB low, as with every BS.1770 meter). */
class TruePeak
{
public:
    static constexpr int kHalf = 12;                       // samples either side
    static constexpr int kTaps = 2 * kHalf;

    explicit TruePeak (int oversample = 4) { prepare (oversample); }
    void prepare (int oversample)
    {
        os = oversample >= 4 ? 4 : oversample >= 2 ? 2 : 1;
        phases = os - 1;
        const double beta = 6.0, i0b = bessel0 (beta);
        for (int p = 0; p < phases; ++p)
        {
            const double frac = (double) (p + 1) / (double) os;            // the point is this far after sample i
            double sum = 0.0, h[kTaps];
            for (int t = 0; t < kTaps; ++t)
            {
                const double m = (double) (t - kHalf + 1) - frac;           // the tap's sample, as a distance from the point
                const double sinc = std::abs (m) < 1.0e-12 ? 1.0 : std::sin (kPi * m) / (kPi * m);
                const double r = m / (double) kHalf;
                const double w = r * r < 1.0 ? bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b : 0.0;
                h[t] = sinc * w; sum += h[t];
            }
            for (int t = 0; t < kTaps; ++t) taps[p][t] = (float) (h[t] / sum);
        }
    }
    int oversample() const noexcept { return os; }

    /** The peak (magnitude) of the cell of sample i: the sample itself and the points between it and the next one. xi points at sample i, with
        kHalf - 1 samples before it and kHalf after it there to read. */
    float cell (const float* xi) const noexcept
    {
        float pk = std::abs (xi[0]);
        const float* x = xi - (kHalf - 1);
        for (int p = 0; p < phases; ++p)
        {
            const float* h = taps[p];
            float y = 0.0f;
            for (int t = 0; t < kTaps; ++t) y += h[t] * x[t];
            y = std::abs (y);
            if (y > pk) pk = y;
        }
        return pk;
    }

private:
    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1.0e-16 * sum) break; }
        return sum;
    }
    int os = 4, phases = 3;
    float taps[3][kTaps] = {};
};

//==================================================================================================================================
/** The K-weighting of BS.1770: a high shelf (+4 dB above about 1.5 kHz), then a high-pass at 38 Hz. The standard gives the numbers for
    48 kHz; these come out of the same analogue filters for any sample rate. */
struct KFilter
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    Biquad shelf, hp;
    explicit KFilter (double rate = 48000.0) { design (rate); }
    void design (double rate)
    {
        const double sr = rate > 0.0 ? rate : 48000.0;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;                      // (as the standard writes them)
            hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        shelf.z1 = shelf.z2 = hp.z1 = hp.z2 = 0.0;
    }
    double run (double x) noexcept { return hp.run (shelf.run (x)); }
};

/** A channel's weight in the loudness (BS.1770): 1 for the front channels, 1.41 for the surrounds, none for the LFE (5.1 order: L R C LFE Ls Rs). */
inline double channelWeight (int channel, int numChannels)
{
    if (numChannels < 4) return 1.0;
    if (channel == 3) return 0.0;
    return channel >= 4 ? 1.41 : 1.0;
}

//==================================================================================================================================
/** One channel's 10 ms. */
struct Tick
{
    float lo = 0.0f, hi = 0.0f;                 // its lowest and highest sample (the waveform)
    float samplePeak = 0.0f;                    // the highest magnitude of its samples
    float truePeak = 0.0f;                      // ... and between them
    std::int32_t truePeakAt = 0;                // which of its samples has the true peak (counted from the tick's first)
    std::uint32_t clipped = 0;                  // how many of its samples have a true peak over 0 dBTP
    double sum = 0.0, sumSq = 0.0, kSumSq = 0.0;
};

/** A whole file, tick by tick (made by the Analyzer). */
struct Profile
{
    double rate = 0.0;
    int channels = 0, tickLen = 480, oversample = 4;
    std::int64_t frames = 0;
    std::vector<std::vector<Tick>> ch;          // [channel][tick]; the last tick may be short
    std::vector<double> cross;                  // per tick: the sum of left x right (channels 0 and 1; empty for a mono file)
    // running sums over the ticks (entry t: everything before tick t), made once by the Analyzer
    std::vector<std::vector<double>> sqUpTo;    // [channel][tick]: sumSq
    std::vector<double> kUpTo;                  // the K-weighted squares, the channels added with their weights

    std::int64_t ticks() const noexcept { return ch.empty() ? 0 : (std::int64_t) ch[0].size(); }
    /** The whole ticks there are (a short last one is not one). */
    std::int64_t wholeTicks() const noexcept { return tickLen > 0 ? frames / tickLen : 0; }
    bool empty() const noexcept { return frames <= 0 || channels <= 0; }
    double seconds() const noexcept { return rate > 0.0 ? (double) frames / rate : 0.0; }

    /** The K-weighted mean square (the channels added, BS.1770) of the whole ticks [t0, t1). */
    double kPower (std::int64_t t0, std::int64_t t1) const noexcept
    {
        t0 = std::max<std::int64_t> (0, t0); t1 = std::min (t1, wholeTicks());
        if (t1 <= t0 || kUpTo.size() <= (size_t) t1) return 0.0;
        return (kUpTo[(size_t) t1] - kUpTo[(size_t) t0]) / ((double) (t1 - t0) * (double) tickLen);
    }
    /** The loudness of the 400 ms that end where tick 'end' begins (momentary), and of the 3 s (short-term); kNone before there is that much. */
    double momentaryAt (std::int64_t end) const noexcept { return end >= 40 && end <= wholeTicks() ? powerToLufs (kPower (end - 40, end)) : kNone; }
    double shortTermAt (std::int64_t end) const noexcept { return end >= 300 && end <= wholeTicks() ? powerToLufs (kPower (end - 300, end)) : kNone; }
    /** One channel's mean square over the whole ticks [t0, t1). */
    double power (int c, std::int64_t t0, std::int64_t t1) const noexcept
    {
        t0 = std::max<std::int64_t> (0, t0); t1 = std::min (t1, wholeTicks());
        if (t1 <= t0 || c < 0 || c >= channels) return 0.0;
        const auto& s = sqUpTo[(size_t) c];
        return (s[(size_t) t1] - s[(size_t) t0]) / ((double) (t1 - t0) * (double) tickLen);
    }
    /** The correlation of the first two channels over the ticks [t0, t1): +1 the same in both, 0 nothing in common, -1 out of phase. */
    double correlation (std::int64_t t0, std::int64_t t1) const noexcept
    {
        t0 = std::max<std::int64_t> (0, t0); t1 = std::min (t1, ticks());
        if (t1 <= t0 || channels < 2 || cross.empty()) return 0.0;
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (auto t = t0; t < t1; ++t) { lr += cross[(size_t) t]; ll += ch[0][(size_t) t].sumSq; rr += ch[1][(size_t) t].sumSq; }
        const double d = std::sqrt (ll * rr);
        return d > 1.0e-18 ? std::max (-1.0, std::min (1.0, lr / d)) : 0.0;
    }
};

//==================================================================================================================================
/** Reads a file into its Profile: prepare, process block after block (any sizes), finish. */
class Analyzer
{
public:
    Analyzer (double sampleRate, int numChannels) { prepare (sampleRate, numChannels); }

    void prepare (double sampleRate, int numChannels)
    {
        p = Profile();
        p.rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        p.channels = std::max (1, std::min (64, numChannels));
        p.tickLen = std::max (1, (int) std::llround (p.rate / 100.0));
        p.oversample = oversampleFor (p.rate);
        p.ch.assign ((size_t) p.channels, {});
        tp.prepare (p.oversample);
        kf.assign ((size_t) p.channels, KFilter (p.rate));
        tail.assign ((size_t) p.channels, std::vector<float> ((size_t) kTail, 0.0f));
        pos = 0; done = false;
    }

    /** The next n frames (chans: one pointer per channel). */
    void process (const float* const* chans, int n)
    {
        if (n <= 0 || done) return;
        const std::int64_t ticksNow = (pos + n + p.tickLen - 1) / p.tickLen;
        for (int c = 0; c < p.channels; ++c)
        {
            auto& ticks = p.ch[(size_t) c];
            if ((std::int64_t) ticks.size() < ticksNow) ticks.resize ((size_t) ticksNow);
            const float* x = chans[c];
            // the samples themselves
            auto& k = kf[(size_t) c];
            std::int64_t i = pos;
            int done_ = 0;
            while (done_ < n)
            {
                const std::int64_t t = i / p.tickLen;
                const int room = (int) std::min<std::int64_t> ((t + 1) * p.tickLen - i, n - done_);
                auto& tk = ticks[(size_t) t];
                const bool fresh = i == t * p.tickLen;
                float lo = fresh ? x[done_] : tk.lo, hi = fresh ? x[done_] : tk.hi;
                double sum = tk.sum, sq = tk.sumSq, ksq = tk.kSumSq;
                for (int j = 0; j < room; ++j)
                {
                    const float v = x[done_ + j];
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                    const double d = (double) v;
                    sum += d; sq += d * d;
                    const double y = k.run (d); ksq += y * y;
                }
                tk.lo = lo; tk.hi = hi; tk.sum = sum; tk.sumSq = sq; tk.kSumSq = ksq;
                tk.samplePeak = std::max (std::abs (lo), std::abs (hi));
                done_ += room; i += room;
            }
            truePeaks (c, x, n, pos);
        }
        if (p.channels >= 2)
        {
            if ((std::int64_t) p.cross.size() < ticksNow) p.cross.resize ((size_t) ticksNow, 0.0);
            const float* l = chans[0]; const float* r = chans[1];
            for (int j = 0; j < n; ++j) p.cross[(size_t) ((pos + j) / p.tickLen)] += (double) l[j] * (double) r[j];
        }
        pos += n;
    }

    /** The end of the file: the Profile (the Analyzer is then spent; prepare it again for another file). */
    Profile finish()
    {
        if (! done)
        {
            // the true peaks of the last samples: silence follows the file
            std::vector<float> zeros ((size_t) TruePeak::kHalf, 0.0f);
            for (int c = 0; c < p.channels; ++c) truePeaks (c, zeros.data(), TruePeak::kHalf, pos);
            p.frames = pos;
            const auto nt = (size_t) p.ticks();
            p.sqUpTo.assign ((size_t) p.channels, std::vector<double> (nt + 1, 0.0));
            p.kUpTo.assign (nt + 1, 0.0);
            for (int c = 0; c < p.channels; ++c)
            {
                const double w = channelWeight (c, p.channels);
                auto& s = p.sqUpTo[(size_t) c];
                for (size_t t = 0; t < nt; ++t)
                {
                    s[t + 1] = s[t] + p.ch[(size_t) c][t].sumSq;
                    p.kUpTo[t + 1] += w * p.ch[(size_t) c][t].kSumSq;      // (this tick's share, made a running sum below)
                }
            }
            for (size_t t = 0; t < nt; ++t) p.kUpTo[t + 1] += p.kUpTo[t];
            done = true;
        }
        return std::move (p);
    }

private:
    static constexpr int kTail = TruePeak::kTaps - 1;        // the samples kept from the block before

    /** The cells that these n samples complete: sample i's cell needs kHalf samples after it, so they are the cells of (at - kHalf) onwards. */
    void truePeaks (int c, const float* x, int n, std::int64_t at)
    {
        auto& tl = tail[(size_t) c];
        work.resize ((size_t) (kTail + n));
        std::copy (tl.begin(), tl.end(), work.begin());
        std::copy (x, x + n, work.begin() + kTail);
        auto& ticks = p.ch[(size_t) c];
        for (int j = 0; j < n; ++j)
        {
            const std::int64_t i = at - TruePeak::kHalf + j;                // the cell's sample
            if (i < 0) continue;
            const float pk = tp.cell (work.data() + (kTail - TruePeak::kHalf) + j);
            const std::int64_t t = i / p.tickLen;
            if ((size_t) t >= ticks.size()) continue;                       // (cannot happen: the sample itself made its tick)
            auto& tk = ticks[(size_t) t];
            if (pk > tk.truePeak) { tk.truePeak = pk; tk.truePeakAt = (std::int32_t) (i - t * p.tickLen); }
            if (pk > kClipOver) ++tk.clipped;
        }
        if (n >= kTail) std::copy (x + n - kTail, x + n, tl.begin());
        else { std::copy (tl.begin() + n, tl.end(), tl.begin()); std::copy (x, x + n, tl.end() - n); }
    }

    Profile p;
    TruePeak tp;
    std::vector<KFilter> kf;
    std::vector<std::vector<float>> tail;
    std::vector<float> work;
    std::int64_t pos = 0;
    bool done = false;
};

//==================================================================================================================================
struct Options
{
    int rmsWindowMs = 50;                       // the length of the windows of max / min RMS (a multiple of 10 ms)
    bool aes17 = true;                          // RMS levels as AES17: a full-scale sine reads 0 dB (3.01 dB more than the plain RMS)
};

struct ChannelStats
{
    double truePeakDb = kNone, samplePeakDb = kNone, maxRmsDb = kNone, minRmsDb = kNone, totalRmsDb = kNone;
    double dcPercent = 0.0;                     // of full scale, signed
    std::int64_t clipped = 0;
    std::int64_t truePeakAt = -1, samplePeakAt = -1;       // the frame (-1: the part is empty). samplePeakAt is exact to the tick only
};

struct Stats
{
    std::int64_t from = 0, to = 0;              // the frames measured: [from, to)
    std::vector<ChannelStats> ch;
    double truePeakDb = kNone, samplePeakDb = kNone;         // the highest of the channels
    double maxMomentary = kNone, maxShortTerm = kNone, integrated = kNone, lra = 0.0;
    bool lraValid = false;
    std::int64_t clipped = 0;                   // all the channels
    bool exactEnds = true;                      // false: the ends were rounded to whole ticks (no Reader was given, or it failed)
    double dcDb (int c) const { return c >= 0 && c < (int) ch.size() ? ampToDb (std::abs (ch[(size_t) c].dcPercent) / 100.0) : kNone; }
};

/** Reads frames [start, start + n) of the file into dest (one pointer per channel); frames before the file or after it are silence. False if it cannot. */
using Reader = std::function<bool (std::int64_t start, int n, float* const* dest)>;

namespace detail
{
/** A stretch of samples measured one by one (an end of a part that is not on a tick): [a, b). */
struct Piece { double sum = 0.0, sumSq = 0.0; float samplePeak = 0.0f, truePeak = 0.0f; std::int64_t truePeakAt = -1, samplePeakAt = -1, clipped = 0; };

inline bool measurePiece (const Profile& p, const Reader& read, std::int64_t a, std::int64_t b, std::vector<Piece>& out)
{
    out.assign ((size_t) p.channels, {});
    if (b <= a) return true;
    const int pad = TruePeak::kHalf, n = (int) (b - a) + 2 * pad;
    std::vector<std::vector<float>> buf ((size_t) p.channels, std::vector<float> ((size_t) n, 0.0f));
    std::vector<float*> ptr; for (auto& v : buf) ptr.push_back (v.data());
    if (! read (a - pad, n, ptr.data())) return false;
    const TruePeak tp (p.oversample);
    for (int c = 0; c < p.channels; ++c)
    {
        auto& o = out[(size_t) c];
        const float* x = buf[(size_t) c].data() + pad;                 // x[0] is frame a
        for (std::int64_t i = 0; i < b - a; ++i)
        {
            const float v = x[i]; const double d = (double) v;
            o.sum += d; o.sumSq += d * d;
            if (std::abs (v) > o.samplePeak || o.samplePeakAt < 0) { o.samplePeak = std::abs (v); o.samplePeakAt = a + i; }
            const float pk = tp.cell (x + i);
            if (pk > o.truePeak || o.truePeakAt < 0) { o.truePeak = pk; o.truePeakAt = a + i; }
            if (pk > kClipOver) ++o.clipped;
        }
    }
    return true;
}
}

/** The figures of frames [from, to) of the file. With a Reader the two ends are exact to the sample (see the top of this file). */
inline Stats measure (const Profile& p, std::int64_t from, std::int64_t to, const Options& opt = {}, const Reader* reader = nullptr)
{
    Stats s;
    from = std::max<std::int64_t> (0, std::min (from, p.frames)); to = std::max (from, std::min (to, p.frames));
    s.ch.assign ((size_t) std::max (0, p.channels), {});
    if (p.empty() || to <= from) { s.from = s.to = from; return s; }

    const std::int64_t L = p.tickLen;
    // the ticks that lie wholly inside the part: [t0, t1). The file's short last tick counts as one when the part runs to the file's end.
    std::int64_t t0 = (from + L - 1) / L, t1 = to / L;
    if (to == p.frames && p.frames % L != 0 && t0 <= p.ticks() - 1) t1 = p.ticks();
    std::vector<detail::Piece> head ((size_t) p.channels), tailPiece ((size_t) p.channels);
    // what is left at the two ends, measured sample by sample: [from, headEnd) and [tailStart, to)
    std::int64_t headEnd, tailStart;
    if (t1 < t0) { headEnd = to; tailStart = to; t0 = t1 = 0; }                // (inside one tick: all of it is the head)
    else         { headEnd = t0 * L; tailStart = std::min (to, t1 * L); }
    bool exact = true;
    if (headEnd > from || to > tailStart)
    {
        exact = reader != nullptr && *reader != nullptr
                && detail::measurePiece (p, *reader, from, headEnd, head) && detail::measurePiece (p, *reader, tailStart, to, tailPiece);
        if (! exact)                                                            // no samples to read: the ticks the part touches, whole
        {
            t0 = from / L; t1 = std::min<std::int64_t> (p.ticks(), (to + L - 1) / L);
            head.assign ((size_t) p.channels, {}); tailPiece.assign ((size_t) p.channels, {});
        }
    }
    s.exactEnds = exact;
    s.from = exact ? from : t0 * L; s.to = exact ? to : std::min (p.frames, t1 * L);
    const double frames = (double) std::max<std::int64_t> (1, s.to - s.from);
    const double rmsRef = opt.aes17 ? 2.0 : 1.0;                            // (AES17: the power of a full-scale sine is the 0 dB)

    const std::int64_t whole1 = std::min (t1, p.wholeTicks());              // the ticks of full length among them (for the windows)
    const int W = std::max (1, opt.rmsWindowMs / 10);
    for (int c = 0; c < p.channels; ++c)
    {
        auto& o = s.ch[(size_t) c];
        const auto& ticks = p.ch[(size_t) c];
        double sum = head[(size_t) c].sum + tailPiece[(size_t) c].sum, sq = head[(size_t) c].sumSq + tailPiece[(size_t) c].sumSq;
        float sp = 0.0f, tpk = 0.0f; std::int64_t spAt = -1, tpAt = -1, clipped = 0;
        auto piece = [&] (const detail::Piece& d)
        {
            if (d.samplePeakAt >= 0 && (d.samplePeak > sp || spAt < 0)) { sp = d.samplePeak; spAt = d.samplePeakAt; }
            if (d.truePeakAt >= 0 && (d.truePeak > tpk || tpAt < 0)) { tpk = d.truePeak; tpAt = d.truePeakAt; }
            clipped += d.clipped;
        };
        piece (head[(size_t) c]);
        for (auto t = t0; t < t1; ++t)
        {
            const auto& tk = ticks[(size_t) t];
            sum += tk.sum; sq += tk.sumSq; clipped += tk.clipped;
            if (tk.samplePeak > sp || spAt < 0) { sp = tk.samplePeak; spAt = t * L; }
            if (tk.truePeak > tpk || tpAt < 0) { tpk = tk.truePeak; tpAt = t * L + tk.truePeakAt; }
        }
        piece (tailPiece[(size_t) c]);
        o.samplePeakDb = ampToDb (sp); o.truePeakDb = ampToDb (std::max (tpk, sp));
        o.samplePeakAt = spAt; o.truePeakAt = tpAt; o.clipped = clipped;
        o.dcPercent = 100.0 * sum / frames;
        o.totalRmsDb = powerToDb (rmsRef * sq / frames);
        // the windows: W ticks long, a tick apart, all of them inside the part
        if (whole1 - t0 >= W)
        {
            const auto& up = p.sqUpTo[(size_t) c];
            double hi = -1.0, lo = std::numeric_limits<double>::max();
            for (auto t = t0; t + W <= whole1; ++t)
            {
                const double e = up[(size_t) (t + W)] - up[(size_t) t];
                if (e > hi) hi = e;
                if (e < lo) lo = e;
            }
            const double div = (double) W * (double) L;
            o.maxRmsDb = powerToDb (rmsRef * std::max (0.0, hi) / div); o.minRmsDb = powerToDb (rmsRef * std::max (0.0, lo) / div);
        }
        else o.maxRmsDb = o.minRmsDb = o.totalRmsDb;                         // shorter than one window: the part itself is the window
        s.samplePeakDb = std::max (s.samplePeakDb, o.samplePeakDb); s.truePeakDb = std::max (s.truePeakDb, o.truePeakDb);
        s.clipped += clipped;
    }

    // ---- loudness: from the first whole tick, in steps of 100 ms
    {
        std::vector<double> blocks;                                         // the 400 ms blocks' powers
        double maxM = -1.0, maxS = -1.0;
        for (auto t = t0; t + 40 <= whole1; t += 10) { const double pw = p.kPower (t, t + 40); blocks.push_back (pw); if (pw > maxM) maxM = pw; }
        std::vector<double> shortTerm;                                      // the 3 s windows' powers
        for (auto t = t0; t + 300 <= whole1; t += 10) { const double pw = p.kPower (t, t + 300); shortTerm.push_back (pw); if (pw > maxS) maxS = pw; }
        if (maxM >= 0.0) s.maxMomentary = powerToLufs (maxM);
        if (maxS >= 0.0) s.maxShortTerm = powerToLufs (maxS);
        // integrated (BS.1770): the blocks over -70 LUFS, then those within 10 LU of their mean
        {
            const double absGate = lufsToPower (-70.0);
            double sum = 0.0; std::int64_t n = 0;
            for (double b : blocks) if (b > absGate) { sum += b; ++n; }
            if (n > 0)
            {
                const double rel = (sum / (double) n) * std::pow (10.0, -10.0 / 10.0);
                double sum2 = 0.0; std::int64_t n2 = 0;
                for (double b : blocks) if (b > absGate && b > rel) { sum2 += b; ++n2; }
                if (n2 > 0) s.integrated = powerToLufs (sum2 / (double) n2);
            }
        }
        // the loudness range (EBU Tech 3342): the 3 s values over -70 LUFS, then those within 20 LU of their mean; from the 10th to the 95th percentile
        {
            const double absGate = lufsToPower (-70.0);
            double sum = 0.0; std::int64_t n = 0;
            for (double b : shortTerm) if (b > absGate) { sum += b; ++n; }
            if (n > 0)
            {
                const double rel = (sum / (double) n) * std::pow (10.0, -20.0 / 10.0);
                std::vector<double> kept;
                for (double b : shortTerm) if (b > absGate && b > rel) kept.push_back (b);
                if (! kept.empty())
                {
                    std::sort (kept.begin(), kept.end());
                    const auto at = [&] (double q) { return kept[(size_t) std::min<double> ((double) kept.size() - 1.0, std::floor ((double) (kept.size() - 1) * q + 0.5))]; };
                    s.lra = powerToLufs (at (0.95)) - powerToLufs (at (0.10));
                    s.lraValid = true;
                }
            }
        }
    }
    return s;
}

/** The whole file. */
inline Stats measure (const Profile& p, const Options& opt = {}) { return measure (p, 0, p.frames, opt, nullptr); }

} // namespace td::wavestats
