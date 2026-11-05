#pragma once
/** The picture of the sound for the Spectral Repair window: a time-frequency "reassigned" spectrogram made for the part that is in view, at the size it is shown.
    A Gaussian window is used (very low side lobes), and the energy of every analysis point is moved to where the sound really is (the instantaneous time and
    frequency), which makes tones thin sharp lines and clicks thin vertical lines instead of blurs. Plain C++17 + JUCE core types only. */
#include "Dsp.h"
#include "Workers.h"
#include <atomic>
#include <functional>
#include <thread>
#include <juce_core/juce_core.h>

namespace td
{
/** How pitch is spread up the picture: a blend of the straight line (even Hz) and the logarithm (even octaves), or the Mel scale. */
struct PictureAxis
{
    double fLo = 40.0, fHi = 20000.0, warp = 1.0; bool mel = false;
    static double melOf (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); }
    static double hzOfMel (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }
    double posOf (double hz) const
    {
        hz = juce::jlimit (fLo, fHi, hz);
        if (mel) return (melOf (hz) - melOf (fLo)) / juce::jmax (1.0e-9, melOf (fHi) - melOf (fLo));
        const double lin = (hz - fLo) / (fHi - fLo), lg = std::log (hz / fLo) / std::log (fHi / fLo);
        return (1.0 - warp) * lin + warp * lg;
    }
    double hzOf (double pos) const
    {
        pos = juce::jlimit (0.0, 1.0, pos);
        if (mel) return hzOfMel (melOf (fLo) + pos * (melOf (fHi) - melOf (fLo)));
        if (warp > 0.999) return fLo * std::pow (fHi / fLo, pos);
        if (warp < 0.001) return fLo + pos * (fHi - fLo);
        double lo = fLo, hi = fHi;
        for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); if (posOf (m) < pos) lo = m; else hi = m; }
        return 0.5 * (lo + hi);
    }
    bool same (const PictureAxis& o) const { return std::abs (fLo - o.fLo) < 1.0e-6 && std::abs (fHi - o.fHi) < 1.0e-6 && std::abs (warp - o.warp) < 1.0e-9 && mel == o.mel; }
};

struct PictureOptions
{
    bool   reassign = true;        // move the energy to where the sound really is (sharp lines)
    double detail = 0.5;           // 0 = finer pitch detail (a longer window), 1 = finer time detail (a shorter one); 0.5 = chosen from the zoom
    double reduceAboveSec = 240.0; // a view longer than this is drawn with fewer analysis points
    // 3.37.0, for the ReHarmoniSer's picture (its own adds its channels up, each at the level of its See mixer):
    bool   sumTracks = false;      // the tracks' energy added up at every point, instead of the loudest track's
    std::vector<float> trackGain;  // each track's level (linear; empty = all 1; 0 = left out)
};

/** A finished picture: the level in dB (0 = a full-scale tone) of every pixel, row 0 at the top. */
struct DetailPicture
{
    int w = 0, h = 0; double t0 = 0.0, t1 = 0.0; PictureAxis axis; juce::int64 id = 0;
    int c0 = 0, c1 = -1;           // 3.38.0: the columns it holds, [c0, c1) (-1 = to the end); a part made again (see RenderPart) holds only those
    std::vector<float> db;
    bool whole() const { return c0 <= 0 && (c1 < 0 || c1 >= w); }
    bool valid() const { return w > 1 && h > 1 && db.size() == (size_t) w * (size_t) h; }
};

namespace picture
{
/** The analysis window: Gaussian, its width (sigma, in samples) chosen from the zoom (about 6 pixels wide) unless the 'detail' control moves it; and the
    FFT for it. (3.37.0: on their own, so the ReHarmoniSer knows how far apart the analysis points are in pitch - render uses them as before.) */
inline double windowSigma (double sr, double span, int W, const PictureOptions& opt)
{
    const double secPerPx = span / (double) juce::jmax (1, W);
    const double fwhm = juce::jlimit (0.008, 0.040, secPerPx * 6.0) * std::pow (2.0, -(juce::jlimit (0.0, 1.0, opt.detail) - 0.5) * 2.0);
    return juce::jmax (8.0, fwhm / 2.3548 * sr);
}
inline int fftSize (double sigma) { int N = 512; while ((double) N < 13.6 * sigma && N < 65536) N <<= 1; return N; }

/** 3.38.0: the timeline's column (whole pixels from time 0) that a picture of [t0, t1] at W columns starts at - its column x is this + x. The analysis points
    sit on one grid from time 0 too, so a view moved by whole pixels has every column it shares with the picture before exactly as it was: only the strip
    it uncovers is made. (The picture is then up to half a pixel from t0, which nothing can see.) */
inline juce::int64 firstColumn (double t0, double t1, int W) { return (juce::int64) std::llround (t0 * (double) W / (t1 - t0)); }
/** How far (seconds) a change in the sound reaches in the picture: a frame half a window either side of it hears it, and moves its points up to half a
    window further - a whole window each way (and two pixels more). */
inline double reach (double sr, double t0, double t1, int W, const PictureOptions& opt)
{
    return (double) fftSize (windowSigma (sr, t1 - t0, W, opt)) / sr + 2.0 * (t1 - t0) / (double) juce::jmax (1, W);
}
/** 3.38.0: a part of the picture: where the tracks start on the timeline t0 and t1 are on (they may be a stretch read for this picture), and the columns
    to make. The columns made are exactly what the whole picture has there (the same analysis points, added in the same order), so a part made again -
    round a fix, or the strip a drag uncovered - joins the rest without a seam. */
struct RenderPart
{
    juce::int64 firstSample = 0;   // the timeline sample of the tracks' first sample
    int c0 = 0, c1 = -1;           // [c0, c1) of the W columns (-1 = to the end); the others are left at -200 dB
};

/** The picture of [t0,t1] seconds (of the timeline: the tracks' first sample is part.firstSample on it) of every track, the loudest track at every point
    (or all of them added, PictureOptions::sumTracks). Returns an invalid picture if cancelled. */
inline DetailPicture render (const std::vector<std::vector<float>>& tracks, double sr, double t0, double t1, int W, int H, const PictureAxis& axis, const PictureOptions& opt,
                             const std::function<bool()>& cancelled = {}, const std::function<void (float)>& progress = {}, const RenderPart& part = {})
{
    DetailPicture pic; pic.w = W; pic.h = H; pic.t0 = t0; pic.t1 = t1; pic.axis = axis;
    if (tracks.empty() || W < 2 || H < 2 || t1 <= t0 || sr < 8000.0) return pic;
    const int cFrom = juce::jlimit (0, W, part.c0), cTo = juce::jlimit (cFrom, W, part.c1 < 0 ? W : part.c1), Wp = cTo - cFrom;
    pic.c0 = cFrom; pic.c1 = cTo;
    if (Wp <= 0) return pic;
    const double span = t1 - t0;
    const double sigma = windowSigma (sr, span, W, opt);                         // samples
    const int N = fftSize (sigma);
    const double Nd = (double) N;
    const bool reduced = span > opt.reduceAboveSec;
    const double framesWanted = juce::jmin (6000.0, (reduced ? 1.0 : 1.5) * (double) W);
    const double hop = juce::jmax (1.0, span * sr / framesWanted);
    // 3.38.0: the columns are whole pixels of the timeline from time 0 and the analysis points every 'hop' samples from time 0: the frames are those whose
    // points can land in the columns asked for (a point is moved at most half a window), so the columns of a part are exactly the whole picture's
    const juce::int64 A0 = firstColumn (t0, t1, W);
    const double colsPerSample = (double) W / (span * sr);
    const double sLo = (double) (A0 + cFrom) / colsPerSample, sHi = (double) (A0 + cTo) / colsPerSample;
    const juce::int64 mFrom = juce::jmax ((juce::int64) 0, (juce::int64) std::floor ((sLo - 0.5 * Nd - 2.0) / hop - 0.5) - 1);
    const juce::int64 mTo = (juce::int64) std::ceil ((sHi + 0.5 * Nd + 2.0) / hop) + 1;
    const int kLo = juce::jmax (1, (int) std::floor (axis.fLo * Nd / sr) - 1), kHi = juce::jmin (N / 2 - 1, (int) std::ceil (axis.fHi * Nd / sr) + 1);
    if (kHi <= kLo) return pic;
    // the window
    std::vector<double> win ((size_t) N / 2 + 1); double sumH = 0.0;
    for (int t = 0; t <= N / 2; ++t) { win[(size_t) t] = std::exp (-0.5 * (double) t * (double) t / (sigma * sigma)); sumH += win[(size_t) t] * (t == 0 ? 1.0 : 2.0); }
    // where each pitch row sits, and how much a pixel is worth (so the level does not depend on the size of the picture)
    std::vector<double> rowHz ((size_t) H + 1);
    for (int r = 0; r <= H; ++r) rowHz[(size_t) r] = axis.hzOf (1.0 - ((double) r - 0.5) / (double) (H - 1));
    std::vector<float> rowScale ((size_t) H);
    const double cellArea = hop * (sr / Nd);                                    // samples x Hz of one analysis point
    const double pxSamples = span * sr / (double) W;
    const double norm2 = (2.0 / sumH) * (2.0 / sumH);
    for (int r = 0; r < H; ++r)
    {
        const double pxHz = juce::jmax (1.0e-3, std::abs (rowHz[(size_t) r] - rowHz[(size_t) r + 1]));
        rowScale[(size_t) r] = (float) (norm2 / juce::jmax (1.0e-9, pxSamples * pxHz / cellArea));
    }
    // row of each bin and how fast the row moves with the pitch (the reassigned pitch is within a bin of the bin's own)
    std::vector<float> rowOfBin ((size_t) N / 2 + 1, -1.0f), rowSlope ((size_t) N / 2 + 1, 0.0f);
    for (int k = kLo; k <= kHi; ++k)
    {
        const double hz = (double) k * sr / Nd, d = 0.25 * sr / Nd;
        const double p0 = axis.posOf (juce::jmax (axis.fLo, hz - d)), p1 = axis.posOf (juce::jmin (axis.fHi, hz + d));
        const double dh = juce::jmin (axis.fHi, hz + d) - juce::jmax (axis.fLo, hz - d);
        rowOfBin[(size_t) k] = (float) ((1.0 - axis.posOf (hz)) * (double) (H - 1));
        rowSlope[(size_t) k] = dh > 0.0 ? (float) (-(p1 - p0) / dh * (double) (H - 1)) : 0.0f;
    }
    const dsp::FastFFT fft (N);                                                 // (3.37.0: pocketfft - the same picture, several times sooner)
    const size_t npx = (size_t) Wp * (size_t) H;
    std::atomic<int> nextTrack { 0 }, doneTracks { 0 }; std::atomic<bool> stop { false };
    const int nT = workers::threadsFor ((int) tracks.size());                  // (3.37.0: every core it may use - it stopped at 8)
    std::vector<std::vector<float>> best ((size_t) nT);
    auto worker = [&] (int who)
    {
        auto& mx = best[(size_t) who]; mx.assign (npx, 0.0f);
        std::vector<float> acc (npx);
        std::vector<std::complex<double>> z ((size_t) N);
        for (;;)
        {
            const int ti = nextTrack.fetch_add (1);
            if (ti >= (int) tracks.size() || stop.load()) break;
            const float g = opt.trackGain.empty() ? 1.0f : ((size_t) ti < opt.trackGain.size() ? opt.trackGain[(size_t) ti] : 1.0f), g2 = g * g;
            if (g2 <= 0.0f) { if (progress) progress ((float) (doneTracks.fetch_add (1) + 1) / (float) tracks.size()); continue; }
            const auto& x = tracks[(size_t) ti]; const long L = (long) x.size();
            std::fill (acc.begin(), acc.end(), 0.0f);
            for (juce::int64 m = mFrom; m < mTo; ++m)
            {
                if ((m & 15) == 0 && cancelled && cancelled()) { stop = true; break; }
                const long c = (long) ((juce::int64) std::llround (((double) m + 0.5) * hop) - part.firstSample);       // (in the tracks)
                if (c + N / 2 < 0 || c - N / 2 >= L) continue;
                double energy = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const int tau = i < N / 2 ? i : i - N;
                    const long n = c + tau; const double s = (n >= 0 && n < L) ? (double) x[(size_t) n] : 0.0;
                    const double h = win[(size_t) std::abs (tau)];
                    z[(size_t) i] = std::complex<double> (s * h, s * h * (double) tau);
                    energy += s * s;
                }
                if (energy < 1.0e-18) continue;
                fft.forward (z);
                for (int k = kLo; k <= kHi; ++k)
                {
                    const auto zk = z[(size_t) k], zn = std::conj (z[(size_t) (N - k)]);
                    const auto A = 0.5 * (zk + zn);                                     // spectrum with the window
                    const auto B = (zk - zn) / std::complex<double> (0.0, 2.0);         // spectrum with the window times time
                    const double P = std::norm (A);
                    if (P < 1.0e-22) continue;
                    double tHat = (double) c, rowF = (double) rowOfBin[(size_t) k];
                    if (opt.reassign)
                    {
                        const auto q = B * std::conj (A) / P;
                        const double dt = q.real();                                      // samples
                        const double dw = q.imag() / (sigma * sigma);                   // rad / sample
                        if (std::abs (dt) > 0.5 * Nd) continue;
                        const double dHz = dw * sr / (2.0 * dsp::kPi);
                        if (std::abs (dHz) > 2.0 * sr / Nd) continue;
                        tHat += dt; rowF += (double) rowSlope[(size_t) k] * dHz;
                    }
                    const juce::int64 col = (juce::int64) std::floor ((tHat + (double) part.firstSample) * colsPerSample) - A0 - cFrom;
                    const int row = (int) std::floor (rowF + 0.5);
                    if (col < 0 || col >= Wp || row < 0 || row >= H) continue;
                    acc[(size_t) row * (size_t) Wp + (size_t) col] += (float) P;
                }
            }
            if (stop.load()) break;
            for (int r = 0; r < H; ++r)
            {
                const float sc = rowScale[(size_t) r] * g2;
                float* a = acc.data() + (size_t) r * (size_t) Wp; float* d = mx.data() + (size_t) r * (size_t) Wp;
                if (opt.sumTracks) for (int cx = 0; cx < Wp; ++cx) d[cx] += a[cx] * sc;
                else               for (int cx = 0; cx < Wp; ++cx) { const float v = a[cx] * sc; if (v > d[cx]) d[cx] = v; }
            }
            if (progress) progress ((float) (doneTracks.fetch_add (1) + 1) / (float) tracks.size());
        }
    };
    std::vector<std::thread> pool;
    for (int i = 1; i < nT; ++i) pool.emplace_back (worker, i);
    worker (0);
    for (auto& th : pool) th.join();
    if (stop.load()) return DetailPicture();
    pic.db.assign ((size_t) W * (size_t) H, -200.0f);
    for (int r = 0; r < H; ++r)
        for (int cx = 0; cx < Wp; ++cx)
        {
            const size_t i = (size_t) r * (size_t) Wp + (size_t) cx;
            float v = 0.0f;
            if (opt.sumTracks) { for (auto& b : best) if (! b.empty()) v += b[i]; }
            else for (auto& b : best) if (! b.empty() && b[i] > v) v = b[i];
            if (v > 1.0e-20f) pic.db[(size_t) r * (size_t) W + (size_t) (cFrom + cx)] = 10.0f * std::log10 (v);
        }
    return pic;
}
} // namespace picture
} // namespace td
