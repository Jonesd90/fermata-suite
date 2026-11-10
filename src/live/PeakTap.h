#pragma once
/** fermata:Live 0.3.0: the waveform of a session as it is recorded, for the stations' take windows (the Primary's fills live, the Fixer's as the
    audio reaches the disk). A thread of its own reads the memory buffer (Ring) exactly as a drive does - at its own pace, never holding the audio
    thread or a drive up - and keeps, for every recorded channel, the lowest and highest sample of every point (about 10.7 ms: 512 samples at
    48 kHz, the same time at any rate), companded into 8 bits each (the square root of the level, so a quiet reverb tail still has a shape: about
    -84 dB at the bottom). A coarse level (one point per kCoarse) is kept for the whole session; the fine level for as much of it as fits in
    kMaxFineBytes (the oldest goes first). The recorder sends the new points to the stations as they come, and a station that starts later asks for
    the history in pieces.

    If the thread ever falls behind the ring (the machine stalled for longer than the buffer), the points it missed are flat: the recording
    itself is not touched (the drives read the ring for themselves).

    Threads: start / stop on the message thread; takeNew / get / ranges on any thread (under a lock held only for copying). */
#include "LiveRecorder.h"
#include <mutex>

namespace td::live
{
class PeakTap final : private juce::Thread
{
public:
    static constexpr int kCoarse = 16;                                     // fine points per coarse point
    static constexpr juce::int64 kMaxFineBytes = (juce::int64) 160 * 1024 * 1024;
    static int samplesPerPointFor (double rate) noexcept { return juce::jmax (64, (int) std::lround (512.0 * rate / 48000.0)); }
    static juce::int8 encode (float v) noexcept
    {
        const float a = juce::jmin (1.0f, std::abs (v));
        const int q = (int) std::lround (std::sqrt (a) * 127.0f);
        return (juce::int8) (v < 0.0f ? -q : q);
    }
    static float decode (juce::int8 q) noexcept { const float s = (float) q / 127.0f; return q < 0 ? -s * s : s * s; }

    PeakTap() : juce::Thread ("fermata:Live waveform peaks") {}
    ~PeakTap() override { stop(); }

    /** Starts reading the session's ring from where it is now (message thread). */
    void start (std::shared_ptr<LiveRecorder> s)
    {
        stop();
        if (s == nullptr || ! s->isPrepared()) return;
        session = std::move (s);
        const auto& ring = session->ringForReaders();
        const std::lock_guard<std::mutex> l (lock);
        numCh = ring.channels();
        spp = samplesPerPointFor (session->sampleRate());
        reader = {}; reader.pos = ring.position();
        firstPoint = (reader.pos + spp - 1) / spp;                       // (the first whole point)
        fineFirst = fineEnd = firstPoint; sentUpTo = firstPoint;
        coarseFirst = firstPoint / kCoarse; coarseEnd = coarseFirst;
        fine.assign ((size_t) numCh, {}); coarse.assign ((size_t) numCh, {});
        mn.assign ((size_t) numCh, 0.0f); mx.assign ((size_t) numCh, 0.0f);
        curPoint = -1; curHas = false;
        scratch.assign ((size_t) numCh, std::vector<float> ((size_t) kRead, 0.0f));
        ptrs.resize ((size_t) numCh);
        for (int c = 0; c < numCh; ++c) ptrs[(size_t) c] = scratch[(size_t) c].data();
        startThread (juce::Thread::Priority::low);
    }
    /** Stops the thread; the points stay (until the next start). */
    void stop()
    {
        signalThreadShouldExit();
        stopThread (5000);
        session.reset();
    }
    bool isRunning() const                 { return isThreadRunning(); }

    int channels() const                   { const std::lock_guard<std::mutex> l (lock); return numCh; }
    int samplesPerPoint() const            { const std::lock_guard<std::mutex> l (lock); return spp; }
    struct Range { juce::int64 first = 0, end = 0; };
    Range fineRange() const                { const std::lock_guard<std::mutex> l (lock); return { fineFirst, fineEnd }; }
    Range coarseRange() const              { const std::lock_guard<std::mutex> l (lock); return { coarseFirst, coarseEnd }; }

    /** The fine points made since the last call: [first, first + n) of every channel, data[ch] = 2n bytes (min, max of each point). False: none. */
    bool takeNew (juce::int64& first, int& n, std::vector<std::vector<juce::int8>>& data)
    {
        const std::lock_guard<std::mutex> l (lock);
        if (sentUpTo < fineFirst) sentUpTo = fineFirst;
        if (sentUpTo >= fineEnd || numCh == 0) return false;
        first = sentUpTo; n = (int) juce::jmin<juce::int64> (fineEnd - sentUpTo, 4096);
        data.assign ((size_t) numCh, {});
        const size_t at = (size_t) (first - fineFirst) * 2;
        for (int c = 0; c < numCh; ++c) data[(size_t) c].assign (fine[(size_t) c].begin() + (std::ptrdiff_t) at, fine[(size_t) c].begin() + (std::ptrdiff_t) (at + (size_t) n * 2));
        sentUpTo += n;
        return true;
    }
    /** History: 'count' points from 'from' (fine or coarse), every channel; what is outside what is kept comes back flat (zeros). */
    void get (bool coarseLevel, juce::int64 from, int count, std::vector<std::vector<juce::int8>>& data) const
    {
        const std::lock_guard<std::mutex> l (lock);
        data.assign ((size_t) numCh, std::vector<juce::int8> ((size_t) juce::jmax (0, count) * 2, 0));
        const juce::int64 f0 = coarseLevel ? coarseFirst : fineFirst, f1 = coarseLevel ? coarseEnd : fineEnd;
        const auto& src = coarseLevel ? coarse : fine;
        for (juce::int64 k = juce::jmax (from, f0); k < juce::jmin (from + count, f1); ++k)
            for (int c = 0; c < numCh; ++c)
            {
                const size_t s = (size_t) (k - f0) * 2, d = (size_t) (k - from) * 2;
                data[(size_t) c][d] = src[(size_t) c][s]; data[(size_t) c][d + 1] = src[(size_t) c][s + 1];
            }
    }

private:
    static constexpr int kRead = 8192;

    void run() override
    {
        crashreport::nameThisThread ("fermata:Live waveform peaks");
        auto keep = session;                                               // (the ring lives as long as the session object)
        if (keep == nullptr) return;
        const auto& ring = keep->ringForReaders();
        // 0.7.1: the waveform must never be what ends the recorder. Its memory grows with the session (the fine level up to kMaxFineBytes, the coarse
        // one without end); if the machine cannot give it more, the waveform stops here - the stations draw what they have - and the recording,
        // in the same process, goes on. (An exception that leaves a thread ends the whole program.)
        try
        {
            while (! threadShouldExit())
            {
                if (ring.available (reader) <= 0) { wait (15); continue; }
                const auto before = reader.pos;
                const int got = ring.take (reader, ptrs.data(), kRead);
                if (got <= 0)
                {
                    if (reader.gapAt >= 0) consumeGap (reader.pos);          // (fell behind the ring: the points it missed are flat)
                    continue;
                }
                consume (before, got);
            }
        }
        catch (...) { juce::Logger::writeToLog ("[recorder] The waveform for the stations has stopped (no memory for more of it); the recording is not affected"); }
    }

    /** Samples [at, at + n) of every channel (in ptrs). */
    void consume (juce::int64 at, int n)
    {
        int i = 0;
        while (i < n)
        {
            const juce::int64 p = at + i;
            const juce::int64 k = p / spp;
            if (k != curPoint)
            {
                if (curHas) emit (curPoint);
                curPoint = k; curHas = false;
                std::fill (mn.begin(), mn.end(), 0.0f); std::fill (mx.begin(), mx.end(), 0.0f);
            }
            const int seg = (int) juce::jmin<juce::int64> (n - i, (k + 1) * spp - p);
            for (int c = 0; c < numCh; ++c)
            {
                const auto r = juce::FloatVectorOperations::findMinAndMax (ptrs[(size_t) c] + i, seg);
                if (! curHas) { mn[(size_t) c] = r.getStart(); mx[(size_t) c] = r.getEnd(); }
                else { mn[(size_t) c] = juce::jmin (mn[(size_t) c], r.getStart()); mx[(size_t) c] = juce::jmax (mx[(size_t) c], r.getEnd()); }
            }
            curHas = true;
            i += seg;
            if ((at + i) % spp == 0) { emit (curPoint); curHas = false; curPoint = k + 1; std::fill (mn.begin(), mn.end(), 0.0f); std::fill (mx.begin(), mx.end(), 0.0f); }
        }
    }
    void consumeGap (juce::int64 newPos)
    {
        if (curHas) emit (curPoint);
        curHas = false; curPoint = newPos / spp;
        std::fill (mn.begin(), mn.end(), 0.0f); std::fill (mx.begin(), mx.end(), 0.0f);
    }
    /** Point k is complete (mn / mx): it goes on the end of the fine level (flat points first if some were missed), and the coarse level follows. */
    void emit (juce::int64 k)
    {
        if (k < firstPoint) return;                                        // (the part-point before the first whole one)
        const std::lock_guard<std::mutex> l (lock);
        if (k < fineEnd) return;
        while (fineEnd < k) appendPoint (true);
        appendPoint (false);
        // the fine level within its memory: the oldest quarter goes when it is over
        const juce::int64 bytes = (fineEnd - fineFirst) * 2 * (juce::int64) juce::jmax (1, numCh);
        if (bytes > kMaxFineBytes)
        {
            const juce::int64 drop = (fineEnd - fineFirst) / 4;
            for (auto& f : fine) f.erase (f.begin(), f.begin() + (std::ptrdiff_t) (drop * 2));
            fineFirst += drop;
        }
    }
    void appendPoint (bool flat)                                           // (lock held)
    {
        for (int c = 0; c < numCh; ++c)
        {
            auto& f = fine[(size_t) c];
            f.push_back (flat ? (juce::int8) 0 : encode (mn[(size_t) c]));
            f.push_back (flat ? (juce::int8) 0 : encode (mx[(size_t) c]));
        }
        ++fineEnd;
        if (fineEnd % kCoarse == 0)                                        // a coarse point is complete: the lowest and highest of its fine points
        {
            const juce::int64 c0 = juce::jmax (fineFirst, fineEnd - kCoarse);
            const juce::int64 ck = (fineEnd - 1) / kCoarse;
            while (coarseEnd < ck) { for (auto& cs : coarse) { cs.push_back (0); cs.push_back (0); } ++coarseEnd; }
            for (int c = 0; c < numCh; ++c)
            {
                juce::int8 lo = 127, hi = -127;
                const auto& f = fine[(size_t) c];
                for (juce::int64 k = c0; k < fineEnd; ++k) { const size_t s = (size_t) (k - fineFirst) * 2; lo = juce::jmin (lo, f[s]); hi = juce::jmax (hi, f[s + 1]); }
                if (lo > hi) lo = hi = 0;
                coarse[(size_t) c].push_back (lo); coarse[(size_t) c].push_back (hi);
            }
            ++coarseEnd;
        }
    }

    std::shared_ptr<LiveRecorder> session;
    mutable std::mutex lock;
    int numCh = 0, spp = 512;
    Ring::Reader reader;                                                   // (the thread's)
    juce::int64 firstPoint = 0, fineFirst = 0, fineEnd = 0, sentUpTo = 0, coarseFirst = 0, coarseEnd = 0;
    std::vector<std::vector<juce::int8>> fine, coarse;                     // per channel: min, max, min, max ...
    std::vector<float> mn, mx;                                             // (the thread's: the point being made)
    juce::int64 curPoint = -1; bool curHas = false;
    std::vector<std::vector<float>> scratch;
    std::vector<float*> ptrs;
    JUCE_DECLARE_NON_COPYABLE (PeakTap)
};
} // namespace td::live
